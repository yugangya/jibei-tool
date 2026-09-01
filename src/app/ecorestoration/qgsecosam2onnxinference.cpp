/***************************************************************************
  qgsecosam2onnxinference.cpp
 ***************************************************************************/

#include "qgsecosam2onnxinference.h"

#include "qgsmaprendererparalleljob.h"
#include "qgsmapsettings.h"
#include "qgsrasterlayer.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QVector>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace
{
  constexpr std::array<float, 3> sSamPixelMean = { 123.675f, 116.28f, 103.53f };
  constexpr std::array<float, 3> sSamPixelStd = { 58.395f, 57.12f, 57.375f };

  struct MaskPoint
  {
    int x = 0;
    int y = 0;

    bool operator==( const MaskPoint &other ) const
    {
      return x == other.x && y == other.y;
    }
  };

  struct MaskEdge
  {
    MaskPoint start;
    MaskPoint end;
  };

  qint64 maskPointKey( const MaskPoint &point )
  {
    return ( static_cast<qint64>( point.x ) << 32 ) ^ static_cast<quint32>( point.y );
  }

  double signedArea( const QVector<MaskPoint> &ring )
  {
    if ( ring.size() < 4 )
      return 0.0;

    double area = 0.0;
    for ( int index = 0; index + 1 < ring.size(); ++index )
      area += static_cast<double>( ring.at( index ).x ) * ring.at( index + 1 ).y
              - static_cast<double>( ring.at( index + 1 ).x ) * ring.at( index ).y;
    return area / 2.0;
  }

  float bilinearSample( const float *data, int width, int height, double x, double y )
  {
    if ( !data || width <= 0 || height <= 0 )
      return 0.0f;

    x = std::clamp( x, 0.0, static_cast<double>( width - 1 ) );
    y = std::clamp( y, 0.0, static_cast<double>( height - 1 ) );

    const int x0 = static_cast<int>( std::floor( x ) );
    const int y0 = static_cast<int>( std::floor( y ) );
    const int x1 = std::min( x0 + 1, width - 1 );
    const int y1 = std::min( y0 + 1, height - 1 );
    const double tx = x - x0;
    const double ty = y - y0;

    const double top = static_cast<double>( data[static_cast<size_t>( y0 * width + x0 )] ) * ( 1.0 - tx )
                       + static_cast<double>( data[static_cast<size_t>( y0 * width + x1 )] ) * tx;
    const double bottom = static_cast<double>( data[static_cast<size_t>( y1 * width + x0 )] ) * ( 1.0 - tx )
                          + static_cast<double>( data[static_cast<size_t>( y1 * width + x1 )] ) * tx;
    return static_cast<float>( top * ( 1.0 - ty ) + bottom * ty );
  }

  QVector<QVector<MaskPoint>> boundaryLoops( const std::vector<unsigned char> &data, int width, int height )
  {
    QVector<MaskEdge> edges;
    edges.reserve( static_cast<int>( data.size() ) / 2 );
    const auto isPositive = [&data, width, height]( int x, int y ) {
      return x >= 0 && x < width && y >= 0 && y < height && data[static_cast<size_t>( y * width + x )] != 0;
    };

    for ( int y = 0; y < height; ++y )
    {
      for ( int x = 0; x < width; ++x )
      {
        if ( !isPositive( x, y ) )
          continue;
        if ( !isPositive( x, y - 1 ) ) edges.push_back( { { x, y }, { x + 1, y } } );
        if ( !isPositive( x + 1, y ) ) edges.push_back( { { x + 1, y }, { x + 1, y + 1 } } );
        if ( !isPositive( x, y + 1 ) ) edges.push_back( { { x + 1, y + 1 }, { x, y + 1 } } );
        if ( !isPositive( x - 1, y ) ) edges.push_back( { { x, y + 1 }, { x, y } } );
      }
    }

    std::unordered_map<qint64, std::vector<MaskEdge>> byStart;
    byStart.reserve( edges.size() );
    for ( const MaskEdge &edge : std::as_const( edges ) )
      byStart[maskPointKey( edge.start )].push_back( edge );

    QVector<QVector<MaskPoint>> loops;
    while ( !byStart.empty() )
    {
      auto first = byStart.begin();
      if ( first == byStart.end() || first->second.empty() )
        break;

      MaskEdge edge = first->second.back();
      first->second.pop_back();
      if ( first->second.empty() )
        byStart.erase( first );

      const MaskPoint start = edge.start;
      MaskPoint end = edge.end;
      QVector<MaskPoint> ring;
      ring.reserve( 512 );
      ring.append( start );
      ring.append( end );

      int guard = 0;
      const int maximumSteps = static_cast<int>( edges.size() ) + 4;
      while ( !( end == start ) && guard++ < maximumSteps )
      {
        auto next = byStart.find( maskPointKey( end ) );
        if ( next == byStart.end() || next->second.empty() )
          break;
        const MaskEdge nextEdge = next->second.back();
        next->second.pop_back();
        if ( next->second.empty() )
          byStart.erase( next );
        end = nextEdge.end;
        ring.append( end );
      }

      if ( ring.size() >= 4 && ring.constFirst() == ring.constLast() )
        loops.append( ring );
    }
    return loops;
  }

  QgsGeometry maskToGeometry(
    const float *maskData,
    int maskCount,
    int maskHeight,
    int maskWidth,
    int maskIndex,
    bool imageCoordinates,
    const QgsRectangle &extent
  )
  {
    if ( !maskData || maskCount <= 0 || maskHeight <= 0 || maskWidth <= 0 )
      return QgsGeometry();

    const int selectedMask = std::clamp( maskIndex, 0, maskCount - 1 );
    const size_t planeSize = static_cast<size_t>( maskHeight ) * maskWidth;
    const float *plane = maskData + static_cast<size_t>( selectedMask ) * planeSize;
    float minimum = plane[0];
    float maximum = plane[0];
    for ( size_t index = 1; index < planeSize; ++index )
    {
      minimum = std::min( minimum, plane[index] );
      maximum = std::max( maximum, plane[index] );
    }
    const float threshold = minimum >= 0.0f && maximum <= 1.0f ? 0.5f : 0.0f;

    const int longestSide = std::max( maskWidth, maskHeight );
    const int contourSide = std::max( longestSide, std::min( 1024, longestSide * 4 ) );
    const double contourScale = static_cast<double>( contourSide ) / std::max( 1, longestSide );
    const int width = std::max( 1, static_cast<int>( std::lround( static_cast<double>( maskWidth ) * contourScale ) ) );
    const int height = std::max( 1, static_cast<int>( std::lround( static_cast<double>( maskHeight ) * contourScale ) ) );
    std::vector<unsigned char> binary( static_cast<size_t>( width ) * height, 0 );

    for ( int y = 0; y < height; ++y )
    {
      const double sourceY = ( ( static_cast<double>( y ) + 0.5 ) / static_cast<double>( height ) ) * maskHeight - 0.5;
      for ( int x = 0; x < width; ++x )
      {
        const double sourceX = ( ( static_cast<double>( x ) + 0.5 ) / static_cast<double>( width ) ) * maskWidth - 0.5;
        binary[static_cast<size_t>( y * width + x )] = bilinearSample( plane, maskWidth, maskHeight, sourceX, sourceY ) > threshold ? 1 : 0;
      }
    }

    QVector<QVector<MaskPoint>> loops = boundaryLoops( binary, width, height );
    if ( loops.isEmpty() )
      return QgsGeometry();

    std::sort( loops.begin(), loops.end(), []( const QVector<MaskPoint> &left, const QVector<MaskPoint> &right ) {
      return std::abs( signedArea( left ) ) > std::abs( signedArea( right ) );
    } );
    const QVector<MaskPoint> &loop = loops.constFirst();
    QgsPolylineXY ring;
    ring.reserve( loop.size() );
    for ( const MaskPoint &point : loop )
    {
      ring.append( QgsPointXY(
        extent.xMinimum() + static_cast<double>( point.x ) / width * extent.width(),
        imageCoordinates
          ? extent.yMinimum() + static_cast<double>( point.y ) / height * extent.height()
          : extent.yMaximum() - static_cast<double>( point.y ) / height * extent.height()
      ) );
    }
    QgsPolygonXY polygon;
    polygon << ring;
    QgsGeometry geometry = QgsGeometry::fromPolygonXY( polygon );
    if ( geometry.isNull() || geometry.isEmpty() )
      return QgsGeometry();

    // Supersample the decoder mask first, then apply a gentle smoothing pass
    // so the exported contour keeps the model boundary but no longer looks
    // like a raw raster staircase.
    const QgsGeometry smoothed = geometry.smooth( 2, 0.24, -1.0, 180.0 );
    if ( !smoothed.isNull() && !smoothed.isEmpty() )
      geometry = smoothed;
    return geometry;
  }

#ifdef HAVE_ONNXRUNTIME
  struct Sam2SessionCache
  {
    QString encoderPath;
    QString decoderPath;
    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> decoder;
    std::string encoderInputName;
    std::vector<std::string> encoderOutputNames;
    std::vector<std::string> decoderInputNames;
    std::vector<std::string> decoderOutputNames;
    int inputWidth = 1024;
    int inputHeight = 1024;
  };

  Ort::Env &sam2Environment()
  {
    static Ort::Env environment( ORT_LOGGING_LEVEL_WARNING, "qgis-eco-sam2" );
    return environment;
  }

  Sam2SessionCache &sam2Sessions()
  {
    static Sam2SessionCache cache;
    return cache;
  }

  QString loadSam2Sessions( const QString &encoderPath, const QString &decoderPath )
  {
    Sam2SessionCache &cache = sam2Sessions();
    if ( cache.encoder && cache.decoder && cache.encoderPath == encoderPath && cache.decoderPath == decoderPath )
      return QString();

    try
    {
      Ort::SessionOptions options;
      options.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
      options.SetIntraOpNumThreads( std::max( 1u, std::thread::hardware_concurrency() / 2 ) );

#ifdef _WIN32
      const std::wstring encoderNativePath = encoderPath.toStdWString();
      const std::wstring decoderNativePath = decoderPath.toStdWString();
      std::unique_ptr<Ort::Session> encoder = std::make_unique<Ort::Session>( sam2Environment(), encoderNativePath.c_str(), options );
      std::unique_ptr<Ort::Session> decoder = std::make_unique<Ort::Session>( sam2Environment(), decoderNativePath.c_str(), options );
#else
      const std::string encoderNativePath = encoderPath.toStdString();
      const std::string decoderNativePath = decoderPath.toStdString();
      std::unique_ptr<Ort::Session> encoder = std::make_unique<Ort::Session>( sam2Environment(), encoderNativePath.c_str(), options );
      std::unique_ptr<Ort::Session> decoder = std::make_unique<Ort::Session>( sam2Environment(), decoderNativePath.c_str(), options );
#endif

      if ( encoder->GetInputCount() != 1 || decoder->GetInputCount() < 5 )
        return QStringLiteral( "智能分割模型结构不完整。" );

      Ort::AllocatorWithDefaultOptions allocator;
      const std::vector<int64_t> inputShape = encoder->GetInputTypeInfo( 0 ).GetTensorTypeAndShapeInfo().GetShape();
      if ( inputShape.size() != 4 || inputShape[1] != 3 || inputShape[2] <= 0 || inputShape[3] <= 0 )
        return QStringLiteral( "智能分割模型必须是 RGB 图像编码器。" );

      Sam2SessionCache loaded;
      loaded.encoderPath = encoderPath;
      loaded.decoderPath = decoderPath;
      loaded.encoder = std::move( encoder );
      loaded.decoder = std::move( decoder );
      loaded.inputHeight = static_cast<int>( inputShape[2] );
      loaded.inputWidth = static_cast<int>( inputShape[3] );
      loaded.encoderInputName = loaded.encoder->GetInputNameAllocated( 0, allocator ).get();
      for ( size_t index = 0; index < loaded.encoder->GetOutputCount(); ++index )
        loaded.encoderOutputNames.emplace_back( loaded.encoder->GetOutputNameAllocated( index, allocator ).get() );
      for ( size_t index = 0; index < loaded.decoder->GetInputCount(); ++index )
        loaded.decoderInputNames.emplace_back( loaded.decoder->GetInputNameAllocated( index, allocator ).get() );
      for ( size_t index = 0; index < loaded.decoder->GetOutputCount(); ++index )
        loaded.decoderOutputNames.emplace_back( loaded.decoder->GetOutputNameAllocated( index, allocator ).get() );

      cache = std::move( loaded );
      return QString();
    }
    catch ( const Ort::Exception &exception )
    {
      return QStringLiteral( "智能分割模型加载失败：%1" ).arg( QString::fromUtf8( exception.what() ) );
    }
  }

  template <typename Function>
  auto runWithUiEvents( Function &&function )
  {
    auto task = std::async( std::launch::async, std::forward<Function>( function ) );
    while ( task.wait_for( std::chrono::milliseconds( 16 ) ) != std::future_status::ready )
    {
      if ( QCoreApplication::instance() )
        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents, 5 );
    }
    return task.get();
  }

  struct TensorStorage
  {
    std::vector<float> floats;
    std::vector<int64_t> integers;
    std::vector<int64_t> shape;
  };

  double normalizedX( const QgsPointXY &point, const QgsRectangle &extent )
  {
    return std::clamp( ( point.x() - extent.xMinimum() ) / std::max( extent.width(), 1e-20 ), 0.0, 1.0 );
  }

  double normalizedY( const QgsPointXY &point, const QgsRectangle &extent, bool imageCoordinates )
  {
    return imageCoordinates
           ? std::clamp( ( point.y() - extent.yMinimum() ) / std::max( extent.height(), 1e-20 ), 0.0, 1.0 )
           : std::clamp( ( extent.yMaximum() - point.y() ) / std::max( extent.height(), 1e-20 ), 0.0, 1.0 );
  }
#endif
}

bool QgsEcoSam2OnnxInference::isRuntimeAvailable()
{
#ifdef HAVE_ONNXRUNTIME
  return true;
#else
  return false;
#endif
}

QgsEcoSam2OnnxInference::Result QgsEcoSam2OnnxInference::run( QgsRasterLayer *rasterLayer, const Parameters &parameters )
{
  Result result;
#ifndef HAVE_ONNXRUNTIME
  Q_UNUSED( rasterLayer )
  Q_UNUSED( parameters )
  result.error = QStringLiteral( "当前构建未链接智能分割组件。" );
  return result;
#else
  const bool useImageInput = !parameters.inputImage.isNull();
  if ( !useImageInput && ( !rasterLayer || !rasterLayer->isValid() ) )
  {
    result.error = QStringLiteral( "请先选择有效的影像图层。" );
    return result;
  }
  if ( useImageInput && parameters.inputImage.isNull() )
  {
    result.error = QStringLiteral( "照片输入无效。" );
    return result;
  }
  if ( !QFileInfo::exists( parameters.encoderModelPath ) || !QFileInfo::exists( parameters.decoderModelPath ) )
  {
    result.error = QStringLiteral( "智能分割组件尚未准备完成。" );
    return result;
  }

  const QgsRectangle inputExtent = useImageInput ? ( parameters.imageExtent.isEmpty() ? QgsRectangle( 0, 0, parameters.inputImage.width(), parameters.inputImage.height() ) : parameters.imageExtent ) : rasterLayer->extent();
  const QgsRectangle cropExtent = useImageInput ? inputExtent : parameters.cropExtent.intersect( inputExtent );
  if ( cropExtent.isEmpty() )
  {
    result.error = QStringLiteral( "所选范围不在影像覆盖范围内。" );
    return result;
  }
  QVector<QgsPointXY> pointPrompts = parameters.promptPoints;
  QVector<int> pointLabels = parameters.promptLabels;
  if ( parameters.promptMode == PromptMode::Point && pointPrompts.isEmpty() )
    pointPrompts.append( parameters.promptPoint );
  while ( pointLabels.size() < pointPrompts.size() )
    pointLabels.append( 1 );

  if ( parameters.promptMode == PromptMode::Point && pointPrompts.isEmpty() )
  {
    result.error = QStringLiteral( "缺少智能分割提示点。" );
    return result;
  }
  if ( parameters.promptMode == PromptMode::Point && std::any_of( pointPrompts.cbegin(), pointPrompts.cend(), [&cropExtent]( const QgsPointXY &point ) { return !cropExtent.contains( point ); } ) )
  {
    result.error = QStringLiteral( "提示点不在当前影像范围内。" );
    return result;
  }

  const QString sessionError = loadSam2Sessions( parameters.encoderModelPath, parameters.decoderModelPath );
  if ( !sessionError.isEmpty() )
  {
    result.error = sessionError;
    return result;
  }

  try
  {
    Sam2SessionCache &cache = sam2Sessions();
    QImage image;
    if ( useImageInput )
    {
      image = parameters.inputImage.convertToFormat( QImage::Format_RGB888 );
      if ( image.size() != QSize( cache.inputWidth, cache.inputHeight ) )
        image = image.scaled( cache.inputWidth, cache.inputHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
    }
    else
    {
      QgsMapSettings mapSettings;
      mapSettings.setLayers( { rasterLayer } );
      mapSettings.setDestinationCrs( rasterLayer->crs() );
      mapSettings.setExtent( cropExtent );
      mapSettings.setOutputSize( QSize( cache.inputWidth, cache.inputHeight ) );
      mapSettings.setBackgroundColor( Qt::black );
      QgsMapRendererParallelJob renderJob( mapSettings );
      renderJob.start();
      renderJob.waitForFinished();
      image = renderJob.renderedImage().convertToFormat( QImage::Format_RGB888 );
    }
    if ( image.isNull() )
    {
      result.error = QStringLiteral( "无法读取当前影像范围。" );
      return result;
    }

    const size_t channelSize = static_cast<size_t>( cache.inputWidth ) * cache.inputHeight;
    std::vector<float> encoderInput( channelSize * 3 );
    for ( int y = 0; y < cache.inputHeight; ++y )
    {
      const uchar *line = image.constScanLine( y );
      for ( int x = 0; x < cache.inputWidth; ++x )
      {
        const size_t pixel = static_cast<size_t>( y * cache.inputWidth + x );
        encoderInput[pixel] = ( line[x * 3] - sSamPixelMean[0] ) / sSamPixelStd[0];
        encoderInput[channelSize + pixel] = ( line[x * 3 + 1] - sSamPixelMean[1] ) / sSamPixelStd[1];
        encoderInput[channelSize * 2 + pixel] = ( line[x * 3 + 2] - sSamPixelMean[2] ) / sSamPixelStd[2];
      }
    }

    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault );
    const std::array<int64_t, 4> encoderShape = { 1, 3, cache.inputHeight, cache.inputWidth };
    Ort::Value encoderInputValue = Ort::Value::CreateTensor<float>(
      memoryInfo, encoderInput.data(), encoderInput.size(), encoderShape.data(), encoderShape.size()
    );
    std::vector<const char *> encoderOutputNames;
    encoderOutputNames.reserve( cache.encoderOutputNames.size() );
    for ( const std::string &name : std::as_const( cache.encoderOutputNames ) )
      encoderOutputNames.push_back( name.c_str() );
    const char *encoderInputName = cache.encoderInputName.c_str();
    std::vector<Ort::Value> encoderOutputs = runWithUiEvents( [&cache, encoderInputName, &encoderInputValue, &encoderOutputNames] {
      return cache.encoder->Run( Ort::RunOptions { nullptr }, &encoderInputName, &encoderInputValue, 1, encoderOutputNames.data(), encoderOutputNames.size() );
    } );

    if ( encoderOutputs.size() != cache.encoderOutputNames.size() )
    {
      result.error = QStringLiteral( "智能分割编码结果异常。" );
      return result;
    }

    const int pointCount = parameters.promptMode == PromptMode::Point ? pointPrompts.size() : 2;
    std::vector<float> promptCoordinates;
    std::vector<int64_t> promptLabels;
    if ( parameters.promptMode == PromptMode::Point )
    {
      promptCoordinates.reserve( static_cast<size_t>( pointCount ) * 2 );
      promptLabels.reserve( static_cast<size_t>( pointCount ) );
      for ( int index = 0; index < pointPrompts.size(); ++index )
      {
        const QgsPointXY point = pointPrompts.at( index );
        promptCoordinates.push_back( static_cast<float>( normalizedX( point, cropExtent ) * cache.inputWidth ) );
        promptCoordinates.push_back( static_cast<float>( normalizedY( point, cropExtent, useImageInput ) * cache.inputHeight ) );
        promptLabels.push_back( pointLabels.value( index, 1 ) == 0 ? 0 : 1 );
      }
    }
    else
    {
      const QgsRectangle promptBox = parameters.promptBox.intersect( cropExtent );
      if ( promptBox.isEmpty() )
      {
        result.error = QStringLiteral( "框选范围不在当前影像范围内。" );
        return result;
      }
      const QgsPointXY topLeft( promptBox.xMinimum(), promptBox.yMaximum() );
      const QgsPointXY bottomRight( promptBox.xMaximum(), promptBox.yMinimum() );
      promptCoordinates = {
        static_cast<float>( normalizedX( topLeft, cropExtent ) * cache.inputWidth ),
        static_cast<float>( normalizedY( topLeft, cropExtent, useImageInput ) * cache.inputHeight ),
        static_cast<float>( normalizedX( bottomRight, cropExtent ) * cache.inputWidth ),
        static_cast<float>( normalizedY( bottomRight, cropExtent, useImageInput ) * cache.inputHeight )
      };
      promptLabels = { 2, 3 };
    }

    std::unordered_map<std::string, size_t> encoderOutputIndexes;
    for ( size_t index = 0; index < cache.encoderOutputNames.size(); ++index )
      encoderOutputIndexes.emplace( cache.encoderOutputNames[index], index );

    std::vector<const char *> decoderInputNames;
    std::vector<Ort::Value> decoderInputValues;
    std::vector<TensorStorage> storage;
    decoderInputNames.reserve( cache.decoderInputNames.size() );
    decoderInputValues.reserve( cache.decoderInputNames.size() );
    storage.reserve( cache.decoderInputNames.size() );

    const auto appendFloatTensor = [&memoryInfo, &storage, &decoderInputValues]( std::vector<float> values, std::vector<int64_t> shape ) {
      TensorStorage &tensor = storage.emplace_back();
      tensor.floats = std::move( values );
      tensor.shape = std::move( shape );
      decoderInputValues.emplace_back( Ort::Value::CreateTensor<float>(
        memoryInfo, tensor.floats.data(), tensor.floats.size(), tensor.shape.data(), tensor.shape.size()
      ) );
    };
    const auto appendInt64Tensor = [&memoryInfo, &storage, &decoderInputValues]( std::vector<int64_t> values, std::vector<int64_t> shape ) {
      TensorStorage &tensor = storage.emplace_back();
      tensor.integers = std::move( values );
      tensor.shape = std::move( shape );
      decoderInputValues.emplace_back( Ort::Value::CreateTensor<int64_t>(
        memoryInfo, tensor.integers.data(), tensor.integers.size(), tensor.shape.data(), tensor.shape.size()
      ) );
    };

    for ( const std::string &name : std::as_const( cache.decoderInputNames ) )
    {
      decoderInputNames.push_back( name.c_str() );
      if ( name == "input_points" )
      {
        appendFloatTensor( promptCoordinates, { 1, 1, pointCount, 2 } );
      }
      else if ( name == "input_labels" )
      {
        appendInt64Tensor( promptLabels, { 1, 1, pointCount } );
      }
      else if ( name == "input_masks" )
      {
        appendFloatTensor( std::vector<float>( 256 * 256, 0.0f ), { 1, 1, 256, 256 } );
      }
      else if ( name == "has_mask_input" )
      {
        appendFloatTensor( { 0.0f }, { 1 } );
      }
      else
      {
        const auto encoderOutput = encoderOutputIndexes.find( name );
        if ( encoderOutput == encoderOutputIndexes.end() )
        {
          result.error = QStringLiteral( "智能分割模型输入不匹配。" );
          return result;
        }
        decoderInputValues.emplace_back( std::move( encoderOutputs[encoderOutput->second] ) );
      }
    }

    std::vector<const char *> decoderOutputNames;
    decoderOutputNames.reserve( cache.decoderOutputNames.size() );
    for ( const std::string &name : std::as_const( cache.decoderOutputNames ) )
      decoderOutputNames.push_back( name.c_str() );
    std::vector<Ort::Value> decoderOutputs = runWithUiEvents( [&cache, &decoderInputNames, &decoderInputValues, &decoderOutputNames] {
      return cache.decoder->Run(
        Ort::RunOptions { nullptr },
        decoderInputNames.data(), decoderInputValues.data(), decoderInputValues.size(),
        decoderOutputNames.data(), decoderOutputNames.size()
      );
    } );

    int maskOutputIndex = -1;
    int scoreOutputIndex = -1;
    for ( int index = 0; index < static_cast<int>( cache.decoderOutputNames.size() ); ++index )
    {
      const QString name = QString::fromStdString( cache.decoderOutputNames[static_cast<size_t>( index )] ).toLower();
      if ( name.contains( QStringLiteral( "mask" ) ) && !name.contains( QStringLiteral( "low" ) ) )
        maskOutputIndex = index;
      if ( name.contains( QStringLiteral( "iou" ) ) || name.contains( QStringLiteral( "score" ) ) || name.contains( QStringLiteral( "quality" ) ) )
        scoreOutputIndex = index;
    }
    if ( maskOutputIndex < 0 || maskOutputIndex >= static_cast<int>( decoderOutputs.size() ) )
    {
      result.error = QStringLiteral( "智能分割没有返回有效图斑。" );
      return result;
    }

    int bestMask = 0;
    double bestScore = 0.0;
    if ( scoreOutputIndex >= 0 && scoreOutputIndex < static_cast<int>( decoderOutputs.size() ) && decoderOutputs[scoreOutputIndex].IsTensor() )
    {
      const auto scoreInfo = decoderOutputs[scoreOutputIndex].GetTensorTypeAndShapeInfo();
      if ( scoreInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT )
      {
        const size_t scoreCount = scoreInfo.GetElementCount();
        const float *scores = decoderOutputs[scoreOutputIndex].GetTensorData<float>();
        if ( scores && scoreCount > 0 )
        {
          bestScore = scores[0];
          for ( size_t index = 1; index < scoreCount; ++index )
          {
            if ( scores[index] > bestScore )
            {
              bestScore = scores[index];
              bestMask = static_cast<int>( index );
            }
          }
        }
      }
    }

    const Ort::Value &maskOutput = decoderOutputs[maskOutputIndex];
    if ( !maskOutput.IsTensor() )
    {
      result.error = QStringLiteral( "智能分割图斑格式异常。" );
      return result;
    }
    const auto maskInfo = maskOutput.GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> maskShape = maskInfo.GetShape();
    if ( maskInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || maskShape.size() != 4
         || maskShape[1] <= 0 || maskShape[2] <= 0 || maskShape[3] <= 0 )
    {
      result.error = QStringLiteral( "智能分割图斑尺寸异常。" );
      return result;
    }

    result.geometry = maskToGeometry(
      maskOutput.GetTensorData<float>(),
      static_cast<int>( maskShape[1] ),
      static_cast<int>( maskShape[2] ),
      static_cast<int>( maskShape[3] ),
      bestMask,
      useImageInput,
      cropExtent
    );
    if ( result.geometry.isNull() || result.geometry.isEmpty() )
    {
      result.error = QStringLiteral( "没有生成有效边界，请改用框选或调整点位。" );
      return result;
    }
    result.success = true;
    result.score = std::clamp( bestScore, 0.0, 1.0 );
    result.cropExtent = cropExtent;
    return result;
  }
  catch ( const Ort::Exception &exception )
  {
    result.error = QStringLiteral( "智能分割暂不可用。" );
    Q_UNUSED( exception )
    return result;
  }
  catch ( const std::exception &exception )
  {
    result.error = QStringLiteral( "智能分割暂不可用。" );
    Q_UNUSED( exception )
    return result;
  }
#endif
}
