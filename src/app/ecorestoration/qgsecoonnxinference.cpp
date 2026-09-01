/***************************************************************************
  qgsecoonnxinference.cpp
 ***************************************************************************/

#include "qgsecoonnxinference.h"

#include "qgsrasterblock.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasterlayer.h"

#include <QColor>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace
{
  struct RawCandidate
  {
    float centerX = 0;
    float centerY = 0;
    float width = 0;
    float height = 0;
    float confidence = 0;
    int classId = 0;
    std::vector<float> maskCoefficients;
  };

  QVector<int> tileStarts( int length, int tileSize, int step )
  {
    if ( length <= tileSize )
      return { 0 };
    QVector<int> starts;
    for ( int value = 0; value + tileSize < length; value += step )
      starts.append( value );
    const int last = length - tileSize;
    if ( starts.isEmpty() || starts.constLast() != last )
      starts.append( last );
    return starts;
  }

  float sigmoid( float value )
  {
    if ( value >= 0 )
      return 1.0f / ( 1.0f + std::exp( -value ) );
    const float exponential = std::exp( value );
    return exponential / ( 1.0f + exponential );
  }

  double rectangleIou( const QgsRectangle &left, const QgsRectangle &right )
  {
    const double intersectionWidth = std::max( 0.0, std::min( left.xMaximum(), right.xMaximum() ) - std::max( left.xMinimum(), right.xMinimum() ) );
    const double intersectionHeight = std::max( 0.0, std::min( left.yMaximum(), right.yMaximum() ) - std::max( left.yMinimum(), right.yMinimum() ) );
    const double intersection = intersectionWidth * intersectionHeight;
    const double unionArea = left.width() * left.height() + right.width() * right.height() - intersection;
    return unionArea > 0 ? intersection / unionArea : 0;
  }

  QgsGeometry rectangleGeometry( const RawCandidate &candidate, const QgsRectangle &tileExtent, int inputWidth, int inputHeight )
  {
    const double x1 = std::clamp<double>( candidate.centerX - candidate.width / 2.0, 0, inputWidth );
    const double y1 = std::clamp<double>( candidate.centerY - candidate.height / 2.0, 0, inputHeight );
    const double x2 = std::clamp<double>( candidate.centerX + candidate.width / 2.0, 0, inputWidth );
    const double y2 = std::clamp<double>( candidate.centerY + candidate.height / 2.0, 0, inputHeight );
    auto mapPoint = [&tileExtent, inputWidth, inputHeight]( double x, double y ) {
      return QgsPointXY(
        tileExtent.xMinimum() + x / inputWidth * tileExtent.width(),
        tileExtent.yMaximum() - y / inputHeight * tileExtent.height()
      );
    };
    QgsPolylineXY ring;
    ring << mapPoint( x1, y1 ) << mapPoint( x2, y1 ) << mapPoint( x2, y2 ) << mapPoint( x1, y2 ) << mapPoint( x1, y1 );
    QgsPolygonXY polygon;
    polygon << ring;
    return QgsGeometry::fromPolygonXY( polygon );
  }

  QgsGeometry maskGeometry(
    const RawCandidate &candidate,
    const float *prototypes,
    int maskChannels,
    int maskHeight,
    int maskWidth,
    const QgsRectangle &tileExtent,
    int inputWidth,
    int inputHeight
  )
  {
    if ( candidate.maskCoefficients.size() != static_cast<size_t>( maskChannels ) )
      return rectangleGeometry( candidate, tileExtent, inputWidth, inputHeight );

    const int xMin = std::clamp( static_cast<int>( std::floor( ( candidate.centerX - candidate.width / 2.0f ) / inputWidth * maskWidth ) ), 0, maskWidth - 1 );
    const int yMin = std::clamp( static_cast<int>( std::floor( ( candidate.centerY - candidate.height / 2.0f ) / inputHeight * maskHeight ) ), 0, maskHeight - 1 );
    const int xMax = std::clamp( static_cast<int>( std::ceil( ( candidate.centerX + candidate.width / 2.0f ) / inputWidth * maskWidth ) ), xMin + 1, maskWidth );
    const int yMax = std::clamp( static_cast<int>( std::ceil( ( candidate.centerY + candidate.height / 2.0f ) / inputHeight * maskHeight ) ), yMin + 1, maskHeight );

    const int planeSize = maskHeight * maskWidth;
    std::vector<unsigned char> binary( static_cast<size_t>( planeSize ), 0 );
    for ( int y = yMin; y < yMax; ++y )
    {
      for ( int x = xMin; x < xMax; ++x )
      {
        const int pixelIndex = y * maskWidth + x;
        float value = 0;
        for ( int channel = 0; channel < maskChannels; ++channel )
          value += candidate.maskCoefficients[static_cast<size_t>( channel )] * prototypes[channel * planeSize + pixelIndex];
        binary[static_cast<size_t>( pixelIndex )] = sigmoid( value ) >= 0.5f ? 1 : 0;
      }
    }

    QgsMultiPointXY boundary;
    boundary.reserve( ( xMax - xMin + yMax - yMin ) * 2 );
    for ( int y = yMin; y < yMax; ++y )
    {
      for ( int x = xMin; x < xMax; ++x )
      {
        const int index = y * maskWidth + x;
        if ( !binary[static_cast<size_t>( index )] )
          continue;
        const bool edge = x == xMin || x == xMax - 1 || y == yMin || y == yMax - 1
                          || !binary[static_cast<size_t>( index - 1 )]
                          || !binary[static_cast<size_t>( index + 1 )]
                          || !binary[static_cast<size_t>( index - maskWidth )]
                          || !binary[static_cast<size_t>( index + maskWidth )];
        if ( !edge )
          continue;
        boundary.append( QgsPointXY(
          tileExtent.xMinimum() + ( x + 0.5 ) / maskWidth * tileExtent.width(),
          tileExtent.yMaximum() - ( y + 0.5 ) / maskHeight * tileExtent.height()
        ) );
      }
    }
    if ( boundary.size() < 3 )
      return rectangleGeometry( candidate, tileExtent, inputWidth, inputHeight );
    QgsGeometry geometry = QgsGeometry::fromMultiPointXY( boundary ).convexHull();
    return geometry.isNull() || geometry.isEmpty() ? rectangleGeometry( candidate, tileExtent, inputWidth, inputHeight ) : geometry;
  }

  std::vector<int> candidateNms( const std::vector<RawCandidate> &candidates, float threshold )
  {
    std::vector<int> order( candidates.size() );
    std::iota( order.begin(), order.end(), 0 );
    std::sort( order.begin(), order.end(), [&candidates]( int left, int right ) { return candidates[left].confidence > candidates[right].confidence; } );
    std::vector<int> keep;
    for ( int index : order )
    {
      const RawCandidate &candidate = candidates[static_cast<size_t>( index )];
      const float leftX1 = candidate.centerX - candidate.width / 2;
      const float leftY1 = candidate.centerY - candidate.height / 2;
      const float leftX2 = candidate.centerX + candidate.width / 2;
      const float leftY2 = candidate.centerY + candidate.height / 2;
      bool suppressed = false;
      for ( int keptIndex : keep )
      {
        const RawCandidate &kept = candidates[static_cast<size_t>( keptIndex )];
        if ( candidate.classId != kept.classId )
          continue;
        const float rightX1 = kept.centerX - kept.width / 2;
        const float rightY1 = kept.centerY - kept.height / 2;
        const float rightX2 = kept.centerX + kept.width / 2;
        const float rightY2 = kept.centerY + kept.height / 2;
        const float intersection = std::max( 0.0f, std::min( leftX2, rightX2 ) - std::max( leftX1, rightX1 ) )
                                   * std::max( 0.0f, std::min( leftY2, rightY2 ) - std::max( leftY1, rightY1 ) );
        const float unionArea = candidate.width * candidate.height + kept.width * kept.height - intersection;
        if ( unionArea > 0 && intersection / unionArea > threshold )
        {
          suppressed = true;
          break;
        }
      }
      if ( !suppressed )
        keep.push_back( index );
    }
    return keep;
  }

  QVector<QgsEcoOnnxDetection> globalNms( QVector<QgsEcoOnnxDetection> detections, double threshold )
  {
    std::sort( detections.begin(), detections.end(), []( const QgsEcoOnnxDetection &left, const QgsEcoOnnxDetection &right ) { return left.confidence > right.confidence; } );
    QVector<QgsEcoOnnxDetection> keep;
    for ( QgsEcoOnnxDetection &detection : detections )
    {
      const QgsRectangle bounds = detection.geometry.boundingBox();
      bool suppressed = false;
      for ( const QgsEcoOnnxDetection &kept : std::as_const( keep ) )
      {
        if ( detection.classId == kept.classId && rectangleIou( bounds, kept.geometry.boundingBox() ) > threshold )
        {
          suppressed = true;
          break;
        }
      }
      if ( !suppressed )
        keep.append( std::move( detection ) );
    }
    return keep;
  }

  bool usableRasterBlock( const std::unique_ptr<QgsRasterBlock> &block, int width, int height )
  {
    return block && block->isValid() && !block->isEmpty() && block->width() == width && block->height() == height;
  }

  float normalizeNumericSample( double value, Qgis::DataType dataType )
  {
    if ( !std::isfinite( value ) )
      return 0.0f;

    switch ( dataType )
    {
      case Qgis::DataType::Byte:
        return static_cast<float>( std::clamp( value / 255.0, 0.0, 1.0 ) );
      case Qgis::DataType::Int8:
        return static_cast<float>( std::clamp( ( value + 128.0 ) / 255.0, 0.0, 1.0 ) );
      case Qgis::DataType::UInt16:
        return static_cast<float>( std::clamp( value / 65535.0, 0.0, 1.0 ) );
      case Qgis::DataType::Int16:
        return static_cast<float>( std::clamp( ( value + 32768.0 ) / 65535.0, 0.0, 1.0 ) );
      case Qgis::DataType::UInt32:
        return static_cast<float>( std::clamp( value / 4294967295.0, 0.0, 1.0 ) );
      case Qgis::DataType::Int32:
        return static_cast<float>( std::clamp( ( value + 2147483648.0 ) / 4294967295.0, 0.0, 1.0 ) );
      case Qgis::DataType::Float32:
      case Qgis::DataType::Float64:
        if ( value >= 0.0 && value <= 1.0 )
          return static_cast<float>( value );
        if ( value >= 0.0 && value <= 255.0 )
          return static_cast<float>( value / 255.0 );
        if ( value >= 0.0 && value <= 65535.0 )
          return static_cast<float>( value / 65535.0 );
        return static_cast<float>( std::clamp( value / 255.0, 0.0, 1.0 ) );
      default:
        return static_cast<float>( std::clamp( value / 255.0, 0.0, 1.0 ) );
    }
  }

  bool rasterTileToImageAndTensor(
    QgsRasterLayer *rasterLayer,
    const QgsRectangle &tileExtent,
    int inputWidth,
    int inputHeight,
    QImage &image,
    std::vector<float> &inputTensor
  )
  {
    if ( !rasterLayer || !rasterLayer->dataProvider() || tileExtent.isEmpty() || !tileExtent.isFinite() || inputWidth <= 0 || inputHeight <= 0 )
      return false;

    QgsRasterDataProvider *provider = rasterLayer->dataProvider();
    const int bandCount = rasterLayer->bandCount();
    if ( bandCount <= 0 )
      return false;

    image = QImage( inputWidth, inputHeight, QImage::Format_RGB888 );
    if ( image.isNull() )
      return false;
    image.fill( Qt::black );

    const size_t channelSize = static_cast<size_t>( inputWidth ) * static_cast<size_t>( inputHeight );
    inputTensor.assign( channelSize * 3, 0.0f );

    const Qgis::DataType firstBandType = provider->dataType( 1 );
    if ( QgsRasterBlock::typeIsColor( firstBandType ) )
    {
      std::unique_ptr<QgsRasterBlock> colorBlock( provider->block( 1, tileExtent, inputWidth, inputHeight ) );
      if ( !usableRasterBlock( colorBlock, inputWidth, inputHeight ) )
        return false;

      for ( int y = 0; y < inputHeight; ++y )
      {
        uchar *line = image.scanLine( y );
        for ( int x = 0; x < inputWidth; ++x )
        {
          const QRgb color = colorBlock->color( y, x );
          const float red = qAlpha( color ) == 0 ? 0.0f : qRed( color ) / 255.0f;
          const float green = qAlpha( color ) == 0 ? 0.0f : qGreen( color ) / 255.0f;
          const float blue = qAlpha( color ) == 0 ? 0.0f : qBlue( color ) / 255.0f;
          const size_t pixel = static_cast<size_t>( y ) * static_cast<size_t>( inputWidth ) + static_cast<size_t>( x );
          inputTensor[pixel] = red;
          inputTensor[channelSize + pixel] = green;
          inputTensor[channelSize * 2 + pixel] = blue;
          line[x * 3] = static_cast<uchar>( std::clamp( red * 255.0f, 0.0f, 255.0f ) );
          line[x * 3 + 1] = static_cast<uchar>( std::clamp( green * 255.0f, 0.0f, 255.0f ) );
          line[x * 3 + 2] = static_cast<uchar>( std::clamp( blue * 255.0f, 0.0f, 255.0f ) );
        }
      }
      return true;
    }

    if ( !QgsRasterBlock::typeIsNumeric( firstBandType ) )
      return false;

    const int redBand = 1;
    const int greenBand = bandCount >= 2 ? 2 : redBand;
    const int blueBand = bandCount >= 3 ? 3 : redBand;
    std::unique_ptr<QgsRasterBlock> redBlock( provider->block( redBand, tileExtent, inputWidth, inputHeight ) );
    std::unique_ptr<QgsRasterBlock> greenBlock( provider->block( greenBand, tileExtent, inputWidth, inputHeight ) );
    std::unique_ptr<QgsRasterBlock> blueBlock( provider->block( blueBand, tileExtent, inputWidth, inputHeight ) );
    if ( !usableRasterBlock( redBlock, inputWidth, inputHeight )
         || !usableRasterBlock( greenBlock, inputWidth, inputHeight )
         || !usableRasterBlock( blueBlock, inputWidth, inputHeight ) )
    {
      return false;
    }

    const Qgis::DataType redType = redBlock->dataType();
    const Qgis::DataType greenType = greenBlock->dataType();
    const Qgis::DataType blueType = blueBlock->dataType();
    const bool byteFastPath = redType == Qgis::DataType::Byte && greenType == Qgis::DataType::Byte && blueType == Qgis::DataType::Byte;

    if ( byteFastPath )
    {
      const quint8 *redData = redBlock->byteData();
      const quint8 *greenData = greenBlock->byteData();
      const quint8 *blueData = blueBlock->byteData();
      if ( !redData || !greenData || !blueData )
        return false;
      for ( int y = 0; y < inputHeight; ++y )
      {
        uchar *line = image.scanLine( y );
        const quint8 *redLine = redData + static_cast<size_t>( y ) * static_cast<size_t>( inputWidth );
        const quint8 *greenLine = greenData + static_cast<size_t>( y ) * static_cast<size_t>( inputWidth );
        const quint8 *blueLine = blueData + static_cast<size_t>( y ) * static_cast<size_t>( inputWidth );
        for ( int x = 0; x < inputWidth; ++x )
        {
          const size_t pixel = static_cast<size_t>( y ) * static_cast<size_t>( inputWidth ) + static_cast<size_t>( x );
          const float red = redLine[x] / 255.0f;
          const float green = greenLine[x] / 255.0f;
          const float blue = blueLine[x] / 255.0f;
          inputTensor[pixel] = red;
          inputTensor[channelSize + pixel] = green;
          inputTensor[channelSize * 2 + pixel] = blue;
          line[x * 3] = redLine[x];
          line[x * 3 + 1] = greenLine[x];
          line[x * 3 + 2] = blueLine[x];
        }
      }
    }
    else
    {
      for ( int y = 0; y < inputHeight; ++y )
      {
        uchar *line = image.scanLine( y );
        for ( int x = 0; x < inputWidth; ++x )
        {
          bool redNoData = false;
          bool greenNoData = false;
          bool blueNoData = false;
          const double rawRed = redBlock->valueAndNoData( y, x, redNoData );
          const double rawGreen = greenBlock->valueAndNoData( y, x, greenNoData );
          const double rawBlue = blueBlock->valueAndNoData( y, x, blueNoData );
          const float red = redNoData ? 0.0f : normalizeNumericSample( rawRed, redType );
          const float green = greenNoData ? 0.0f : normalizeNumericSample( rawGreen, greenType );
          const float blue = blueNoData ? 0.0f : normalizeNumericSample( rawBlue, blueType );
          const size_t pixel = static_cast<size_t>( y ) * static_cast<size_t>( inputWidth ) + static_cast<size_t>( x );
          inputTensor[pixel] = red;
          inputTensor[channelSize + pixel] = green;
          inputTensor[channelSize * 2 + pixel] = blue;
          line[x * 3] = static_cast<uchar>( std::clamp( red * 255.0f, 0.0f, 255.0f ) );
          line[x * 3 + 1] = static_cast<uchar>( std::clamp( green * 255.0f, 0.0f, 255.0f ) );
          line[x * 3 + 2] = static_cast<uchar>( std::clamp( blue * 255.0f, 0.0f, 255.0f ) );
        }
      }
    }

    return true;
  }
}

  bool imageToTensor(
    const QImage &sourceImage,
    int inputWidth,
    int inputHeight,
    QImage &image,
    std::vector<float> &inputTensor
  )
  {
    if ( sourceImage.isNull() || inputWidth <= 0 || inputHeight <= 0 )
      return false;

    const QImage scaledImage = sourceImage.scaled( inputWidth, inputHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
    if ( scaledImage.isNull() )
      return false;

    image = scaledImage.convertToFormat( QImage::Format_RGB888 );
    if ( image.isNull() )
      return false;

    const size_t channelSize = static_cast<size_t>( inputWidth ) * static_cast<size_t>( inputHeight );
    inputTensor.assign( channelSize * 3, 0.0f );
    for ( int y = 0; y < inputHeight; ++y )
    {
      const uchar *line = image.constScanLine( y );
      for ( int x = 0; x < inputWidth; ++x )
      {
        const size_t pixel = static_cast<size_t>( y ) * static_cast<size_t>( inputWidth ) + static_cast<size_t>( x );
        const float red = line[x * 3] / 255.0f;
        const float green = line[x * 3 + 1] / 255.0f;
        const float blue = line[x * 3 + 2] / 255.0f;
        inputTensor[pixel] = red;
        inputTensor[channelSize + pixel] = green;
        inputTensor[channelSize * 2 + pixel] = blue;
      }
    }

    return true;
  }
bool QgsEcoOnnxInference::isRuntimeAvailable()
{
#ifdef HAVE_ONNXRUNTIME
  return true;
#else
  return false;
#endif
}

QgsEcoOnnxInference::Result QgsEcoOnnxInference::run( QgsRasterLayer *rasterLayer, const Parameters &parameters, const ProgressCallback &progress )
{
  Result result;
#ifndef HAVE_ONNXRUNTIME
  Q_UNUSED( rasterLayer )
  Q_UNUSED( parameters )
  Q_UNUSED( progress )
    result.error = QStringLiteral( "当前未启用 ONNX Runtime。" );
  return result;
#else
  if ( !rasterLayer || !rasterLayer->isValid() )
  {
    result.error = QStringLiteral( "请输入有效的栅格图层。" );
    return result;
  }
  if ( parameters.modelPath.isEmpty() || !QFileInfo::exists( parameters.modelPath ) )
  {
    result.error = QStringLiteral( "ONNX 模型文件不存在。" );
    return result;
  }

  try
  {
    Ort::Env environment( ORT_LOGGING_LEVEL_WARNING, "qgis-eco-restoration" );
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
    // Whole-line recognition can process thousands of tiles.  Letting ONNX
    // Runtime create half of all logical CPU cores for every session causes
    // severe thread/memory pressure on high-core machines and has resulted
    // in native failures inside the provider. Keep a bounded pool instead.
    const unsigned int hardwareThreads = std::max( 1u, std::thread::hardware_concurrency() );
    sessionOptions.SetIntraOpNumThreads( static_cast<int>( std::clamp( hardwareThreads / 2, 1u, 8u ) ) );
    sessionOptions.SetInterOpNumThreads( 1 );
#ifdef _WIN32
    const std::wstring modelPath = parameters.modelPath.toStdWString();
    Ort::Session session( environment, modelPath.c_str(), sessionOptions );
#else
    const std::string modelPath = parameters.modelPath.toStdString();
    Ort::Session session( environment, modelPath.c_str(), sessionOptions );
#endif

    if ( session.GetInputCount() != 1 || session.GetOutputCount() < 2 )
    {
      result.error = QStringLiteral( "模型不符合单输入 YOLO 分割格式。" );
      return result;
    }
    Ort::AllocatorWithDefaultOptions allocator;
    const auto inputNameAllocated = session.GetInputNameAllocated( 0, allocator );
    const std::string inputName = inputNameAllocated.get();
    const std::vector<int64_t> declaredInputShape = session.GetInputTypeInfo( 0 ).GetTensorTypeAndShapeInfo().GetShape();
    if ( declaredInputShape.size() != 4 )
    {
      result.error = QStringLiteral( "模型输入必须是 NCHW 四维张量。" );
      return result;
    }
    const int inputChannels = declaredInputShape[1] > 0 ? static_cast<int>( declaredInputShape[1] ) : 3;
    const int inputHeight = declaredInputShape[2] > 0 ? static_cast<int>( declaredInputShape[2] ) : parameters.fallbackImageSize;
    const int inputWidth = declaredInputShape[3] > 0 ? static_cast<int>( declaredInputShape[3] ) : parameters.fallbackImageSize;
    if ( inputChannels != 3 || inputWidth < 32 || inputHeight < 32 )
    {
      result.error = QStringLiteral( "目前仅支持三通道 RGB 图像模型。" );
      return result;
    }

    std::vector<std::string> outputNames;
    std::vector<const char *> outputNamePointers;
    outputNames.reserve( session.GetOutputCount() );
    for ( size_t index = 0; index < session.GetOutputCount(); ++index )
    {
      auto name = session.GetOutputNameAllocated( index, allocator );
      outputNames.emplace_back( name.get() );
    }
    outputNamePointers.reserve( outputNames.size() );
    for ( const std::string &name : outputNames )
      outputNamePointers.push_back( name.c_str() );
    result.modelDescription = QStringLiteral( "RGB %1 x %2 / %3 个输出" ).arg( inputWidth ).arg( inputHeight ).arg( outputNames.size() );

    const int rasterWidth = rasterLayer->width();
    const int rasterHeight = rasterLayer->height();
    if ( rasterWidth <= 0 || rasterHeight <= 0 )
    {
      result.error = QStringLiteral( "栅格尺寸无效。" );
      return result;
    }
    const QgsRectangle rasterExtent = rasterLayer->extent();
    QVector<QgsRectangle> tileExtents;
    QStringList tileLabels;
    if ( !parameters.targetExtents.isEmpty() )
    {
      for ( int index = 0; index < parameters.targetExtents.size(); ++index )
      {
        const QgsRectangle extent = parameters.targetExtents.at( index ).intersect( rasterExtent );
        if ( extent.isEmpty() )
          continue;
        tileExtents.append( extent );
        tileLabels.append(
          index < parameters.targetLabels.size() && !parameters.targetLabels.at( index ).isEmpty()
            ? parameters.targetLabels.at( index )
            : QStringLiteral( "切片_%1" ).arg( index + 1 )
        );
      }
    }
    else
    {
      const int stepX = std::max( 1, inputWidth * ( 100 - std::clamp( parameters.overlapPercent, 0, 80 ) ) / 100 );
      const int stepY = std::max( 1, inputHeight * ( 100 - std::clamp( parameters.overlapPercent, 0, 80 ) ) / 100 );
      const QVector<int> xStarts = tileStarts( rasterWidth, inputWidth, stepX );
      const QVector<int> yStarts = tileStarts( rasterHeight, inputHeight, stepY );
      const double pixelWidth = rasterExtent.width() / rasterWidth;
      const double pixelHeight = rasterExtent.height() / rasterHeight;
      for ( int yStart : yStarts )
      {
        for ( int xStart : xStarts )
        {
          const int sourceTileWidth = std::min( inputWidth, rasterWidth - xStart );
          const int sourceTileHeight = std::min( inputHeight, rasterHeight - yStart );
          tileExtents.append( QgsRectangle(
            rasterExtent.xMinimum() + xStart * pixelWidth,
            rasterExtent.yMaximum() - ( yStart + sourceTileHeight ) * pixelHeight,
            rasterExtent.xMinimum() + ( xStart + sourceTileWidth ) * pixelWidth,
            rasterExtent.yMaximum() - yStart * pixelHeight
          ) );
          tileLabels.append( QStringLiteral( "切片组_%1" ).arg( tileExtents.size() ) );
        }
      }
    }

    const int totalTiles = tileExtents.size();
    if ( totalTiles <= 0 )
    {
      result.error = QStringLiteral( "未生成可处理的切片。" );
      return result;
    }
    if ( totalTiles > parameters.maximumTiles )
    {
      result.error = QStringLiteral( "切片数量 %1 超过最大允许值 %2。" ).arg( totalTiles ).arg( parameters.maximumTiles );
      return result;
    }

    QVector<QgsEcoOnnxDetection> allDetections;
    int tileIndex = 0;
    for ( const QgsRectangle &tileExtent : std::as_const( tileExtents ) )
    {
      ++tileIndex;
      const QString sourceName = tileLabels.value( tileIndex - 1, QStringLiteral( "切片_%1" ).arg( tileIndex ) );

        if ( tileExtent.isEmpty() || !tileExtent.isFinite() )
          continue;

        QImage image;
        std::vector<float> inputTensor;
        if ( !rasterTileToImageAndTensor( rasterLayer, tileExtent, inputWidth, inputHeight, image, inputTensor ) )
          continue;
        if ( image.isNull() || image.width() != inputWidth || image.height() != inputHeight )
          continue;
        const QString progressMessage = QStringLiteral( "正在识别 %1：%2 / %3" ).arg( sourceName ).arg( tileIndex ).arg( totalTiles );
        if ( progress && !progress( tileIndex, totalTiles, progressMessage, sourceName, image ) )
        {
          result.error = QStringLiteral( "识别已取消。" );
          result.processedTiles = tileIndex - 1;
          return result;
        }

        const std::array<int64_t, 4> inputShape = { 1, 3, inputHeight, inputWidth };
        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault );
        Ort::Value inputValue = Ort::Value::CreateTensor<float>( memoryInfo, inputTensor.data(), inputTensor.size(), inputShape.data(), inputShape.size() );
        const char *inputNamePointer = inputName.c_str();
        // Keep the ONNX session, its inputs and its outputs on the GUI thread.
        // Moving an individual Session::Run call to a temporary worker while
        // manually pumping Qt events re-enters QGIS code during inference and
        // can leave the ONNX session/input lifetime in an unsafe state.
        std::vector<Ort::Value> outputs = session.Run(
          Ort::RunOptions { nullptr },
          &inputNamePointer,
          &inputValue,
          1,
          outputNamePointers.data(),
          outputNamePointers.size()
        );
        if ( outputs.empty() )
        {
          result.error = QStringLiteral( "模型未返回识别结果。" );
          return result;
        }

        int predictionsIndex = -1;
        int prototypesIndex = -1;
        std::vector<int64_t> predictionsShape;
        std::vector<int64_t> prototypesShape;
        for ( int index = 0; index < static_cast<int>( outputs.size() ); ++index )
        {
          if ( !outputs[static_cast<size_t>( index )].IsTensor() )
            continue;
          const auto tensorInfo = outputs[static_cast<size_t>( index )].GetTensorTypeAndShapeInfo();
          if ( tensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || tensorInfo.GetElementCount() == 0 )
            continue;
          const std::vector<int64_t> shape = tensorInfo.GetShape();
          if ( shape.size() == 3 )
          {
            predictionsIndex = index;
            predictionsShape = shape;
          }
          else if ( shape.size() == 4 )
          {
            prototypesIndex = index;
            prototypesShape = shape;
          }
        }
        if ( predictionsIndex < 0 || prototypesIndex < 0 )
        {
          result.error = QStringLiteral( "模型输出维度异常。" );
          return result;
        }

        if ( predictionsShape.size() != 3 || prototypesShape.size() != 4
             || predictionsShape[0] <= 0 || predictionsShape[1] <= 0 || predictionsShape[2] <= 0
             || prototypesShape[0] <= 0 || prototypesShape[1] <= 0 || prototypesShape[2] <= 0 || prototypesShape[3] <= 0
             || predictionsShape[0] > std::numeric_limits<int>::max() || predictionsShape[1] > std::numeric_limits<int>::max() || predictionsShape[2] > std::numeric_limits<int>::max()
             || prototypesShape[0] > std::numeric_limits<int>::max()
             || prototypesShape[1] > std::numeric_limits<int>::max() || prototypesShape[2] > std::numeric_limits<int>::max() || prototypesShape[3] > std::numeric_limits<int>::max() )
        {
          result.error = QStringLiteral( "掩膜原型尺寸过大，无法安全处理。" );
          return result;
        }

        const int maskChannels = static_cast<int>( prototypesShape[1] );
        const int maskHeight = static_cast<int>( prototypesShape[2] );
        const int maskWidth = static_cast<int>( prototypesShape[3] );
        if ( maskHeight > std::numeric_limits<int>::max() / std::max( 1, maskWidth ) )
        {
          result.error = QStringLiteral( "掩膜原型尺寸过大，无法安全处理。" );
          return result;
        }
        const int maskPlaneSize = maskHeight * maskWidth;
        if ( maskChannels > std::numeric_limits<int>::max() / std::max( 1, maskPlaneSize ) )
        {
          result.error = QStringLiteral( "掩膜原型通道过多，无法安全处理。" );
          return result;
        }
        const int dimensionOne = static_cast<int>( predictionsShape[1] );
        const int dimensionTwo = static_cast<int>( predictionsShape[2] );
        const bool channelsFirst = dimensionOne < dimensionTwo;
        const int predictionChannels = channelsFirst ? dimensionOne : dimensionTwo;
        const int predictionCount = channelsFirst ? dimensionTwo : dimensionOne;
        const int classCount = predictionChannels - 4 - maskChannels;
        if ( maskChannels <= 0 || maskHeight <= 0 || maskWidth <= 0 || classCount <= 0 )
        {
          result.error = QStringLiteral( "无法解析模型输出维度：检测通道 %1，掩膜通道 %2。" ).arg( predictionChannels ).arg( maskChannels );
          return result;
        }

        const auto predictionInfo = outputs[static_cast<size_t>( predictionsIndex )].GetTensorTypeAndShapeInfo();
        const auto prototypeInfo = outputs[static_cast<size_t>( prototypesIndex )].GetTensorTypeAndShapeInfo();
        const size_t expectedPredictionValues = static_cast<size_t>( predictionsShape[0] ) * static_cast<size_t>( predictionChannels ) * static_cast<size_t>( predictionCount );
        const size_t expectedPrototypeValues = static_cast<size_t>( prototypesShape[0] ) * static_cast<size_t>( maskChannels ) * static_cast<size_t>( maskHeight ) * static_cast<size_t>( maskWidth );
        if ( predictionInfo.GetElementCount() < expectedPredictionValues || prototypeInfo.GetElementCount() < expectedPrototypeValues )
        {
          result.error = QStringLiteral( "模型输出数据不完整。" );
          return result;
        }
        const float *predictionData = outputs[static_cast<size_t>( predictionsIndex )].GetTensorData<float>();
        const float *prototypeData = outputs[static_cast<size_t>( prototypesIndex )].GetTensorData<float>();
        if ( !predictionData || !prototypeData )
        {
          result.error = QStringLiteral( "模型输出数据为空。" );
          return result;
        }
        auto predictionValue = [predictionData, channelsFirst, predictionChannels, predictionCount]( int row, int channel ) {
          return channelsFirst ? predictionData[channel * predictionCount + row] : predictionData[row * predictionChannels + channel];
        };

        std::vector<RawCandidate> rawCandidates;
        for ( int row = 0; row < predictionCount; ++row )
        {
          int classId = 0;
          float score = -std::numeric_limits<float>::infinity();
          for ( int classIndex = 0; classIndex < classCount; ++classIndex )
          {
            const float classScore = predictionValue( row, 4 + classIndex );
            if ( classScore > score )
            {
              score = classScore;
              classId = classIndex;
            }
          }
          if ( score < parameters.confidence )
            continue;
          RawCandidate candidate;
          candidate.centerX = predictionValue( row, 0 );
          candidate.centerY = predictionValue( row, 1 );
          candidate.width = predictionValue( row, 2 );
          candidate.height = predictionValue( row, 3 );
          candidate.confidence = score;
          candidate.classId = classId;
          if ( !std::isfinite( candidate.centerX ) || !std::isfinite( candidate.centerY )
               || !std::isfinite( candidate.width ) || !std::isfinite( candidate.height )
               || candidate.width <= 0 || candidate.height <= 0 )
            continue;
          candidate.maskCoefficients.resize( static_cast<size_t>( maskChannels ) );
          for ( int channel = 0; channel < maskChannels; ++channel )
            candidate.maskCoefficients[static_cast<size_t>( channel )] = predictionValue( row, 4 + classCount + channel );
          rawCandidates.emplace_back( std::move( candidate ) );
        }

        const std::vector<int> keep = candidateNms( rawCandidates, static_cast<float>( parameters.iouThreshold ) );
        QgsEcoOnnxDetection bestDetection;
        bool hasDetection = false;
        for ( int keptIndex : keep )
        {
          const RawCandidate &candidate = rawCandidates[static_cast<size_t>( keptIndex )];
          QgsEcoOnnxDetection detection;
          detection.geometry = maskGeometry( candidate, prototypeData, maskChannels, maskHeight, maskWidth, tileExtent, inputWidth, inputHeight );
          detection.confidence = candidate.confidence;
          detection.classId = candidate.classId;
          detection.className = candidate.classId == 0 ? QStringLiteral( "施工扰动" ) : QStringLiteral( "类别_%1" ).arg( candidate.classId );
          detection.sourceName = sourceName;
          detection.tileIndex = tileIndex;
          if ( !detection.geometry.isNull() && !detection.geometry.isEmpty()
               && ( !hasDetection || detection.confidence > bestDetection.confidence ) )
          {
            bestDetection = std::move( detection );
            hasDetection = true;
          }
        }
        // A recognition target is a single tower range (or a single manual
        // extent), so only return its highest-confidence disturbance polygon.
        if ( hasDetection )
          allDetections.append( std::move( bestDetection ) );
        result.processedTiles = tileIndex;
    }

    result.detections = globalNms( std::move( allDetections ), parameters.iouThreshold );
    result.success = true;
    return result;
  }
  catch ( const Ort::Exception &exception )
  {
    result.error = QStringLiteral( "ONNX Runtime 推理失败：%1" ).arg( QString::fromUtf8( exception.what() ) );
    return result;
  }
  catch ( const std::exception &exception )
  {
    result.error = QStringLiteral( "智能识别失败：%1" ).arg( QString::fromUtf8( exception.what() ) );
    return result;
  }
#endif
}

QgsEcoOnnxInference::Result QgsEcoOnnxInference::run( const QImage &inputImage, const Parameters &parameters, const ProgressCallback &progress )
{
  Result result;
#ifndef HAVE_ONNXRUNTIME
  Q_UNUSED( inputImage )
  Q_UNUSED( parameters )
  Q_UNUSED( progress )
  result.error = QStringLiteral( "当前未启用 ONNX Runtime。" );
  return result;
#else
  Q_UNUSED( progress )
  if ( inputImage.isNull() )
  {
    result.error = QStringLiteral( "请输入有效的照片图像。" );
    return result;
  }
  if ( parameters.modelPath.isEmpty() || !QFileInfo::exists( parameters.modelPath ) )
  {
    result.error = QStringLiteral( "ONNX 模型文件不存在。" );
    return result;
  }

  try
  {
    struct CachedSession
    {
      QString modelPath;
      int fallbackImageSize = 640;
      std::unique_ptr<Ort::Env> env;
      std::unique_ptr<Ort::Session> session;
      std::string inputName;
      std::vector<std::string> outputNames;
      std::vector<const char *> outputNamePointers;
      int inputChannels = 0;
      int inputWidth = 0;
      int inputHeight = 0;
    };

    thread_local std::unique_ptr<CachedSession> cache;
    const bool rebuildSession = !cache || cache->modelPath != parameters.modelPath || cache->fallbackImageSize != parameters.fallbackImageSize;
    if ( rebuildSession )
    {
      cache = std::make_unique<CachedSession>();
      cache->modelPath = parameters.modelPath;
      cache->fallbackImageSize = parameters.fallbackImageSize;
      cache->env = std::make_unique<Ort::Env>( ORT_LOGGING_LEVEL_WARNING, "qgis-eco-restoration" );

      Ort::SessionOptions sessionOptions;
      sessionOptions.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
      const unsigned int hardwareThreads = std::max( 1u, std::thread::hardware_concurrency() );
      sessionOptions.SetIntraOpNumThreads( static_cast<int>( std::clamp( hardwareThreads / 2, 1u, 8u ) ) );
      sessionOptions.SetInterOpNumThreads( 1 );

#ifdef _WIN32
      const std::wstring modelPath = parameters.modelPath.toStdWString();
      cache->session = std::make_unique<Ort::Session>( *cache->env, modelPath.c_str(), sessionOptions );
#else
      const std::string modelPath = parameters.modelPath.toStdString();
      cache->session = std::make_unique<Ort::Session>( *cache->env, modelPath.c_str(), sessionOptions );
#endif
      if ( !cache->session || cache->session->GetInputCount() != 1 || cache->session->GetOutputCount() < 2 )
      {
        result.error = QStringLiteral( "模型不是受支持的单输入 YOLO 分割模型。" );
        cache.reset();
        return result;
      }

      Ort::AllocatorWithDefaultOptions allocator;
      const auto inputNameAllocated = cache->session->GetInputNameAllocated( 0, allocator );
      cache->inputName = inputNameAllocated.get();
      const std::vector<int64_t> declaredInputShape = cache->session->GetInputTypeInfo( 0 ).GetTensorTypeAndShapeInfo().GetShape();
      if ( declaredInputShape.size() != 4 )
      {
        result.error = QStringLiteral( "模型输入必须是 NCHW 四维张量。" );
        cache.reset();
        return result;
      }

      cache->inputChannels = declaredInputShape[1] > 0 ? static_cast<int>( declaredInputShape[1] ) : 3;
      cache->inputHeight = declaredInputShape[2] > 0 ? static_cast<int>( declaredInputShape[2] ) : parameters.fallbackImageSize;
      cache->inputWidth = declaredInputShape[3] > 0 ? static_cast<int>( declaredInputShape[3] ) : parameters.fallbackImageSize;
      if ( cache->inputChannels != 3 || cache->inputWidth < 32 || cache->inputHeight < 32 )
      {
        result.error = QStringLiteral( "目前仅支持三通道 RGB 图像模型。" );
        cache.reset();
        return result;
      }

      cache->outputNames.clear();
      cache->outputNamePointers.clear();
      cache->outputNames.reserve( cache->session->GetOutputCount() );
      for ( size_t index = 0; index < cache->session->GetOutputCount(); ++index )
      {
        auto name = cache->session->GetOutputNameAllocated( index, allocator );
        cache->outputNames.emplace_back( name.get() );
      }
      cache->outputNamePointers.reserve( cache->outputNames.size() );
      for ( const std::string &name : cache->outputNames )
        cache->outputNamePointers.push_back( name.c_str() );
    }

    if ( !cache || !cache->session )
    {
      result.error = QStringLiteral( "无法初始化照片智能识别模型。" );
      return result;
    }

    result.modelDescription = QStringLiteral( "RGB %1 x %2 / %3 个输出" ).arg( cache->inputWidth ).arg( cache->inputHeight ).arg( cache->outputNames.size() );

    QImage modelImage;
    std::vector<float> inputTensor;
    if ( !imageToTensor( inputImage, cache->inputWidth, cache->inputHeight, modelImage, inputTensor ) )
    {
      result.error = QStringLiteral( "照片缩放或预处理失败。" );
      return result;
    }

    const std::array<int64_t, 4> inputShape = { 1, 3, cache->inputHeight, cache->inputWidth };
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu( OrtArenaAllocator, OrtMemTypeDefault );
    Ort::Value inputValue = Ort::Value::CreateTensor<float>( memoryInfo, inputTensor.data(), inputTensor.size(), inputShape.data(), inputShape.size() );
    const char *inputNamePointer = cache->inputName.c_str();
    std::vector<Ort::Value> outputs = cache->session->Run(
      Ort::RunOptions { nullptr },
      &inputNamePointer,
      &inputValue,
      1,
      cache->outputNamePointers.data(),
      cache->outputNamePointers.size()
    );
    if ( outputs.empty() )
    {
      result.error = QStringLiteral( "模型未返回识别结果。" );
      return result;
    }

    int predictionsIndex = -1;
    int prototypesIndex = -1;
    std::vector<int64_t> predictionsShape;
    std::vector<int64_t> prototypesShape;
    for ( int index = 0; index < static_cast<int>( outputs.size() ); ++index )
    {
      if ( !outputs[static_cast<size_t>( index )].IsTensor() )
        continue;
      const auto tensorInfo = outputs[static_cast<size_t>( index )].GetTensorTypeAndShapeInfo();
      if ( tensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || tensorInfo.GetElementCount() == 0 )
        continue;
      const std::vector<int64_t> shape = tensorInfo.GetShape();
      if ( shape.size() == 3 )
      {
        predictionsIndex = index;
        predictionsShape = shape;
      }
      else if ( shape.size() == 4 )
      {
        prototypesIndex = index;
        prototypesShape = shape;
      }
    }
    if ( predictionsIndex < 0 || prototypesIndex < 0 )
    {
      result.error = QStringLiteral( "模型输出中未找到检测张量和掩膜原型张量。" );
      return result;
    }

    if ( predictionsShape.size() != 3 || prototypesShape.size() != 4
         || predictionsShape[0] <= 0 || predictionsShape[1] <= 0 || predictionsShape[2] <= 0
         || prototypesShape[0] <= 0 || prototypesShape[1] <= 0 || prototypesShape[2] <= 0 || prototypesShape[3] <= 0
         || predictionsShape[0] > std::numeric_limits<int>::max() || predictionsShape[1] > std::numeric_limits<int>::max() || predictionsShape[2] > std::numeric_limits<int>::max()
         || prototypesShape[0] > std::numeric_limits<int>::max()
         || prototypesShape[1] > std::numeric_limits<int>::max() || prototypesShape[2] > std::numeric_limits<int>::max() || prototypesShape[3] > std::numeric_limits<int>::max() )
    {
      result.error = QStringLiteral( "模型输出尺寸异常。" );
      return result;
    }

    const int maskChannels = static_cast<int>( prototypesShape[1] );
    const int maskHeight = static_cast<int>( prototypesShape[2] );
    const int maskWidth = static_cast<int>( prototypesShape[3] );
    if ( maskHeight > std::numeric_limits<int>::max() / std::max( 1, maskWidth ) )
    {
      result.error = QStringLiteral( "掩膜原型尺寸过大，无法安全处理。" );
      return result;
    }
    const int maskPlaneSize = maskHeight * maskWidth;
    if ( maskChannels > std::numeric_limits<int>::max() / std::max( 1, maskPlaneSize ) )
    {
      result.error = QStringLiteral( "掩膜原型通道过多，无法安全处理。" );
      return result;
    }
    const int dimensionOne = static_cast<int>( predictionsShape[1] );
    const int dimensionTwo = static_cast<int>( predictionsShape[2] );
    const bool channelsFirst = dimensionOne < dimensionTwo;
    const int predictionChannels = channelsFirst ? dimensionOne : dimensionTwo;
    const int predictionCount = channelsFirst ? dimensionTwo : dimensionOne;
    const int classCount = predictionChannels - 4 - maskChannels;
    if ( maskChannels <= 0 || maskHeight <= 0 || maskWidth <= 0 || classCount <= 0 )
    {
      result.error = QStringLiteral( "无法解析模型输出维度：检测通道 %1，掩膜通道 %2。" ).arg( predictionChannels ).arg( maskChannels );
      return result;
    }

    const auto predictionInfo = outputs[static_cast<size_t>( predictionsIndex )].GetTensorTypeAndShapeInfo();
    const auto prototypeInfo = outputs[static_cast<size_t>( prototypesIndex )].GetTensorTypeAndShapeInfo();
    const size_t expectedPredictionValues = static_cast<size_t>( predictionsShape[0] ) * static_cast<size_t>( predictionChannels ) * static_cast<size_t>( predictionCount );
    const size_t expectedPrototypeValues = static_cast<size_t>( prototypesShape[0] ) * static_cast<size_t>( maskChannels ) * static_cast<size_t>( maskHeight ) * static_cast<size_t>( maskWidth );
    if ( predictionInfo.GetElementCount() < expectedPredictionValues || prototypeInfo.GetElementCount() < expectedPrototypeValues )
    {
      result.error = QStringLiteral( "模型输出数据不完整。" );
      return result;
    }
    const float *predictionData = outputs[static_cast<size_t>( predictionsIndex )].GetTensorData<float>();
    const float *prototypeData = outputs[static_cast<size_t>( prototypesIndex )].GetTensorData<float>();
    if ( !predictionData || !prototypeData )
    {
      result.error = QStringLiteral( "模型输出数据为空。" );
      return result;
    }
    auto predictionValue = [predictionData, channelsFirst, predictionChannels, predictionCount]( int row, int channel ) {
      return channelsFirst ? predictionData[channel * predictionCount + row] : predictionData[row * predictionChannels + channel];
    };

    std::vector<RawCandidate> rawCandidates;
    for ( int row = 0; row < predictionCount; ++row )
    {
      int classId = 0;
      float score = -std::numeric_limits<float>::infinity();
      for ( int classIndex = 0; classIndex < classCount; ++classIndex )
      {
        const float classScore = predictionValue( row, 4 + classIndex );
        if ( classScore > score )
        {
          score = classScore;
          classId = classIndex;
        }
      }
      if ( score < parameters.confidence )
        continue;
      RawCandidate candidate;
      candidate.centerX = predictionValue( row, 0 );
      candidate.centerY = predictionValue( row, 1 );
      candidate.width = predictionValue( row, 2 );
      candidate.height = predictionValue( row, 3 );
      candidate.confidence = score;
      candidate.classId = classId;
      if ( !std::isfinite( candidate.centerX ) || !std::isfinite( candidate.centerY )
           || !std::isfinite( candidate.width ) || !std::isfinite( candidate.height )
           || candidate.width <= 0 || candidate.height <= 0 )
        continue;
      candidate.maskCoefficients.resize( static_cast<size_t>( maskChannels ) );
      for ( int channel = 0; channel < maskChannels; ++channel )
        candidate.maskCoefficients[static_cast<size_t>( channel )] = predictionValue( row, 4 + classCount + channel );
      rawCandidates.emplace_back( std::move( candidate ) );
    }

    const std::vector<int> keep = candidateNms( rawCandidates, static_cast<float>( parameters.iouThreshold ) );
    QVector<QgsEcoOnnxDetection> detections;
    const QgsRectangle imageExtent( 0.0, 0.0, inputImage.width(), inputImage.height() );
    for ( int keptIndex : keep )
    {
      const RawCandidate &candidate = rawCandidates[static_cast<size_t>( keptIndex )];
      QgsEcoOnnxDetection detection;
      detection.geometry = maskGeometry( candidate, prototypeData, maskChannels, maskHeight, maskWidth, imageExtent, cache->inputWidth, cache->inputHeight );
      detection.confidence = candidate.confidence;
      detection.classId = candidate.classId;
      detection.className = candidate.classId == 0 ? QStringLiteral( "施工扰动" ) : QStringLiteral( "类别_%1" ).arg( candidate.classId );
      detection.sourceName = QStringLiteral( "photo" );
      detection.tileIndex = 1;
      if ( !detection.geometry.isNull() && !detection.geometry.isEmpty()
           && !detection.geometry.boundingBox().isEmpty() && detection.geometry.boundingBox().isFinite() )
      {
        detections.append( std::move( detection ) );
      }
    }

    result.detections = globalNms( std::move( detections ), parameters.iouThreshold );
    result.processedTiles = 1;
    result.success = true;
    return result;
  }
  catch ( const Ort::Exception &exception )
  {
    result.error = QStringLiteral( "ONNX Runtime 推理失败：%1" ).arg( QString::fromUtf8( exception.what() ) );
    return result;
  }
  catch ( const std::exception &exception )
  {
    result.error = QStringLiteral( "智能识别失败：%1" ).arg( QString::fromUtf8( exception.what() ) );
    return result;
  }
#endif
}
