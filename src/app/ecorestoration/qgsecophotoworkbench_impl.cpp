/***************************************************************************
  qgsecophotoworkbench_impl.cpp
 ***************************************************************************/

#include "qgsecophotoworkbench.h"

#include "qgsapplication.h"
#include "qgsecosam2onnxinference.h"
#include "qgsecoonnxinference.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsObject>
#include <QStyleOptionGraphicsItem>
#include <QPointer>
#include <QPromise>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QEasingCurve>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeySequence>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineF>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMainWindow>
#include <QPainter>
#include <QVariantAnimation>
#include <QRadialGradient>
#include <QAbstractAnimation>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <memory>

namespace
{
  constexpr int sPhotoThumbnailExtent = 88;
  constexpr int sPhotoPreviewMaxPixels = 4096;

  QString photoIndexFilePath( const QString &workspacePath )
  {
    return QDir( workspacePath ).filePath( QStringLiteral( "photos/photo_index.json" ) );
  }

  QString photoDerivedDirectory( const QString &workspacePath )
  {
    return QDir( workspacePath ).filePath( QStringLiteral( "photos/fused" ) );
  }

  QString photoRecognitionModelPath()
  {
    return QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "models/disturbanceX_best_new.onnx" ) );
  }
  QString photoSourcePath( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    return QFileInfo( record.sourcePath ).exists() ? record.sourcePath : record.fusedPath;
  }

  QString photoSelectionKey( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    const QFileInfo info( record.sourcePath );
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath( info.absoluteFilePath() ) : canonical;
  }
  QImage readPreviewImage( const QString &path, const QSize &targetSize = QSize() )
  {
    if ( path.isEmpty() || !QFileInfo::exists( path ) )
      return QImage();

    QImageReader reader( path );
    reader.setAutoTransform( true );
    const QSize sourceSize = reader.size();
    if ( sourceSize.isValid() )
    {
      QSize desired = targetSize;
      if ( desired.isEmpty() )
      {
        desired = sourceSize;
        desired.scale( sPhotoPreviewMaxPixels, sPhotoPreviewMaxPixels, Qt::KeepAspectRatio );
      }
      if ( desired.isValid() && desired != sourceSize )
        reader.setScaledSize( desired );
    }
    return reader.read();
  }


  QString preferredPhotoSam2ModelDirectory()
  {
    const QDir modelsRoot( QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "models/sam2-onnx" ) ) );
    return modelsRoot.filePath( QStringLiteral( "sam2.1-hiera-large" ) );
  }

  QColor annotationColor( int classId )
  {
    return classId == 1 ? QColor( QStringLiteral( "#f59e0b" ) ) : QColor( QStringLiteral( "#ef4444" ) );
  }

  QString yoloClassName( int classId )
  {
    return classId == 1 ? QString::fromUtf8( "\xE9\xA1\xBA\xE5\x9D\xA1\xE6\xBA\x9C\xE6\xB8\xA3" ) : QString::fromUtf8( "\xE6\x96\xBD\xE5\xB7\xA5\xE6\x89\xB0\xE5\x8A\xA8" );
  }

  QString photoClassName( int classId )
  {
    return classId == 1 ? QString::fromUtf8( "\xE9\xA1\xBA\xE5\x9D\xA1\xE6\xBA\x9C\xE6\xB8\xA3" ) : QString::fromUtf8( "\xE6\x96\xBD\xE5\xB7\xA5\xE6\x89\xB0\xE5\x8A\xA8" );
  }

  QString safeOutputBaseName( const QString &sourcePath, int index )
  {
    QString name = QFileInfo( sourcePath ).completeBaseName().trimmed();
    if ( name.isEmpty() )
      name = QStringLiteral( "photo_%1" ).arg( index + 1, 5, 10, QLatin1Char( '0' ) );
    return name;
  }

  struct PhotoAnnotationStats
  {
    int total = 0;
    int disturbanceCount = 0;
    int slopeCount = 0;
    int autoCount = 0;
    int manualCount = 0;
  };

  PhotoAnnotationStats photoAnnotationStats( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    PhotoAnnotationStats stats;
    for ( const QgsEcoPhotoWorkbench::Annotation &annotation : record.annotations )
    {
      ++stats.total;
      if ( annotation.classId == 1 )
        ++stats.slopeCount;
      else
        ++stats.disturbanceCount;
      if ( annotation.source == QStringLiteral( "auto" ) )
        ++stats.autoCount;
      else
        ++stats.manualCount;
    }
    return stats;
  }

  QString photoChipHtml( const QString &label, const QString &value, const QColor &accent, const QColor &background )
  {
    return QStringLiteral( "<span style=\"display:inline-block; margin:0 6px 6px 0; padding:2px 9px; border-radius:999px; background:%1; border:1px solid %2; color:#dce8f5; white-space:nowrap; font-size:11px; letter-spacing:0.2px; box-shadow:inset 0 0 0 1px rgba(255,255,255,0.03);\">%3 <span style=\"color:%4; font-weight:700;\">%5</span></span>" )
      .arg( background.name(), accent.darker( 115 ).name(), label.toHtmlEscaped(), accent.name(), value.toHtmlEscaped() );
  }

  QString photoStatusHtml( const QString &text, const QColor &accent, const QColor &background )
  {
    return QStringLiteral( "<span style=\"display:inline-block; margin:0 6px 6px 0; padding:2px 9px; border-radius:999px; background:%1; border:1px solid %2; color:%3; white-space:nowrap; font-size:11px; font-weight:600; letter-spacing:0.2px; box-shadow:inset 0 0 0 1px rgba(255,255,255,0.03);\">%4</span>" )
      .arg( background.name(), accent.darker( 115 ).name(), accent.name(), text.toHtmlEscaped() );
  }

  QString photoSummaryHtml( const QString &phaseName, int photoCount, int annotationCount, int autoCount, int manualCount, int fusionCount )
  {
    const QString phaseChip = phaseName.isEmpty()
                                ? photoStatusHtml( QStringLiteral( "未选择期次" ), QColor( QStringLiteral( "#94a3b8" ) ), QColor( QStringLiteral( "#1a2330" ) ) )
                                : photoStatusHtml( QStringLiteral( "期次 %1" ).arg( phaseName ), QColor( QStringLiteral( "#7dd3fc" ) ), QColor( QStringLiteral( "#122636" ) ) );
    const QString countLine = photoChipHtml( QStringLiteral( "照片" ), QString::number( photoCount ), QColor( QStringLiteral( "#22d3ee" ) ), QColor( QStringLiteral( "#122636" ) ) )
                              + photoChipHtml( QStringLiteral( "标绘" ), QString::number( annotationCount ), QColor( QStringLiteral( "#a78bfa" ) ), QColor( QStringLiteral( "#221933" ) ) )
                              + photoChipHtml( QStringLiteral( "自动" ), QString::number( autoCount ), autoCount > 0 ? QColor( QStringLiteral( "#38bdf8" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), autoCount > 0 ? QColor( QStringLiteral( "#122636" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
                              + photoChipHtml( QStringLiteral( "人工" ), QString::number( manualCount ), manualCount > 0 ? QColor( QStringLiteral( "#c084fc" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), manualCount > 0 ? QColor( QStringLiteral( "#221933" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
                              + photoChipHtml( QStringLiteral( "融合" ), QString::number( fusionCount ), fusionCount > 0 ? QColor( QStringLiteral( "#4ade80" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), fusionCount > 0 ? QColor( QStringLiteral( "#132617" ) ) : QColor( QStringLiteral( "#1a2330" ) ) );
    return QStringLiteral( "<div style=\"line-height:1.45;\">"
                          "<div style=\"color:#94a3b8; margin-bottom:5px;\">照片数据按期次独立存放，原始照片保持不变。</div>"
                          "<div style=\"margin-bottom:2px;\">%1</div>"
                          "<div>%2</div>"
                          "</div>" )
      .arg( phaseChip, countLine );
  }

  QColor photoCardAccentColor( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    const PhotoAnnotationStats stats = photoAnnotationStats( record );
    if ( !record.fusedPath.isEmpty() && QFileInfo::exists( record.fusedPath ) )
      return QColor( QStringLiteral( "#4ade80" ) );
    if ( stats.disturbanceCount > stats.slopeCount && stats.disturbanceCount > 0 )
      return QColor( QStringLiteral( "#f87171" ) );
    if ( stats.slopeCount > 0 )
      return QColor( QStringLiteral( "#f59e0b" ) );
    if ( stats.autoCount > 0 )
      return QColor( QStringLiteral( "#38bdf8" ) );
    return QColor( QStringLiteral( "#64748b" ) );
  }

  QString photoCompactStatsHtml( const PhotoAnnotationStats &stats )
  {
    return photoChipHtml( QStringLiteral( "总标绘" ), QString::number( stats.total ), QColor( QStringLiteral( "#60a5fa" ) ), QColor( QStringLiteral( "#122033" ) ) )
           + photoChipHtml( QStringLiteral( "自动" ), QString::number( stats.autoCount ), stats.autoCount > 0 ? QColor( QStringLiteral( "#38bdf8" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.autoCount > 0 ? QColor( QStringLiteral( "#122636" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
           + photoChipHtml( QStringLiteral( "人工" ), QString::number( stats.manualCount ), stats.manualCount > 0 ? QColor( QStringLiteral( "#c084fc" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.manualCount > 0 ? QColor( QStringLiteral( "#221933" ) ) : QColor( QStringLiteral( "#1a2330" ) ) );
  }

  QString photoFusionStateHtml( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    const bool fused = !record.fusedPath.isEmpty() && QFileInfo::exists( record.fusedPath );
    return photoStatusHtml( fused ? QStringLiteral( "已融合" ) : QStringLiteral( "未融合" ), fused ? QColor( QStringLiteral( "#22c55e" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), fused ? QColor( QStringLiteral( "#132617" ) ) : QColor( QStringLiteral( "#1a2330" ) ) );
  }

  QString photoItemInfoHtml( const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    const PhotoAnnotationStats stats = photoAnnotationStats( record );
    const QString disturbanceColor = stats.disturbanceCount > 0 ? QStringLiteral( "#f87171" ) : QStringLiteral( "#94a3b8" );
    const QString slopeColor = stats.slopeCount > 0 ? QStringLiteral( "#f87171" ) : QStringLiteral( "#94a3b8" );
    const bool fused = !record.fusedPath.isEmpty() && QFileInfo::exists( record.fusedPath );
    const QString fusionText = fused ? QStringLiteral( "已融合" ) : QStringLiteral( "未融合" );
    const QString fusionColor = fused ? QStringLiteral( "#4ade80" ) : QStringLiteral( "#94a3b8" );
    return QStringLiteral(
             "<div style=\"padding:4px 0 1px 0; line-height:18px; font-size:11px;\">"
             "<div style=\"white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:#cbd5e1;\">施工扰动 <span style=\"color:%1; font-weight:800;\">%2</span></div>"
             "<div style=\"white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:#cbd5e1;\">顺坡溜渣 <span style=\"color:%3; font-weight:800;\">%4</span></div>"
             "<div style=\"white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:%5; font-weight:700;\">%6</div>"
             "</div>" )
      .arg( disturbanceColor,
            QString::number( stats.disturbanceCount ),
            slopeColor,
            QString::number( stats.slopeCount ),
            fusionColor,
            fusionText );
  }

  QString photoPreviewInfoHtml( const QString &path, const QImage &image, const QgsEcoPhotoWorkbench::PhotoRecord &record )
  {
    const QFileInfo info( path );
    const PhotoAnnotationStats stats = photoAnnotationStats( record );
    const QString title = info.fileName().isEmpty() ? QFileInfo( record.sourcePath ).fileName() : info.fileName();
    const QString sizeLine = image.isNull() ? QStringLiteral( "预览失败：图像不可读。" ) : QStringLiteral( "尺寸：%1 × %2" ).arg( image.width() ).arg( image.height() );
    const QString totalLine = photoChipHtml( QStringLiteral( "总标绘" ), QString::number( stats.total ), QColor( QStringLiteral( "#60a5fa" ) ), QColor( QStringLiteral( "#122033" ) ) )
                              + photoChipHtml( QStringLiteral( "自动" ), QString::number( stats.autoCount ), stats.autoCount > 0 ? QColor( QStringLiteral( "#38bdf8" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.autoCount > 0 ? QColor( QStringLiteral( "#122636" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
                              + photoChipHtml( QStringLiteral( "人工" ), QString::number( stats.manualCount ), stats.manualCount > 0 ? QColor( QStringLiteral( "#a78bfa" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.manualCount > 0 ? QColor( QStringLiteral( "#221933" ) ) : QColor( QStringLiteral( "#1a2330" ) ) );
    const QString classLine = photoChipHtml( QStringLiteral( "施工扰动" ), QString::number( stats.disturbanceCount ), stats.disturbanceCount > 0 ? QColor( QStringLiteral( "#f87171" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.disturbanceCount > 0 ? QColor( QStringLiteral( "#2a1b22" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
                              + photoChipHtml( QStringLiteral( "顺坡溜渣" ), QString::number( stats.slopeCount ), stats.slopeCount > 0 ? QColor( QStringLiteral( "#f59e0b" ) ) : QColor( QStringLiteral( "#94a3b8" ) ), stats.slopeCount > 0 ? QColor( QStringLiteral( "#2b2214" ) ) : QColor( QStringLiteral( "#1a2330" ) ) )
                              + photoFusionStateHtml( record );
    return QStringLiteral( "<div style=\"line-height:1.5; text-align:left;\">"
                          "<div style=\"font-size:13px; font-weight:700; color:#f8fbff;\">%1</div>"
                          "<div style=\"color:#94a3b8; margin:2px 0 6px 0;\">%2</div>"
                          "<div>%3</div>"
                          "<div>%4</div>"
                          "</div>" )
      .arg( title.toHtmlEscaped(), sizeLine.toHtmlEscaped(), totalLine, classLine );
  }

  QIcon classColorIcon( const QColor &color )
  {
    QPixmap pixmap( 14, 14 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setPen( Qt::NoPen );
    painter.setBrush( color );
    painter.drawRoundedRect( QRectF( 1, 1, 12, 12 ), 3.0, 3.0 );
    return QIcon( pixmap );
  }

  QCursor photoEditCursor()
  {
    static const QCursor cursor = []() -> QCursor {
      QPixmap pixmap = QgsApplication::getThemeIcon( QStringLiteral( "/mActionToggleEditing.svg" ) ).pixmap( 24, 24 );
      if ( pixmap.isNull() )
        pixmap = QgsApplication::getThemeIcon( QStringLiteral( "/mActionVertexTool.svg" ) ).pixmap( 24, 24 );
      if ( pixmap.isNull() )
        return QCursor( Qt::CrossCursor );
      return QCursor( pixmap, std::max( 1, pixmap.width() - 4 ), std::max( 1, pixmap.height() - 4 ) );
    }();
    return cursor;
  }

  QPolygonF normalizedRectanglePolygon( const QRectF &rect )
  {
    const QRectF normalized = rect.normalized();
    return QPolygonF( { normalized.topLeft(), normalized.topRight(), normalized.bottomRight(), normalized.bottomLeft() } );
  }

  QPolygonF clampNormalizedPolygon( const QPolygonF &polygon )
  {
    QPolygonF clamped;
    clamped.reserve( polygon.size() );
    for ( const QPointF &point : polygon )
      clamped.append( QPointF( std::clamp( point.x(), 0.0, 1.0 ), std::clamp( point.y(), 0.0, 1.0 ) ) );
    return clamped;
  }

  QPolygonF scenePolygonFromNormalized( const QPolygonF &normalizedPolygon, const QRectF &imageRect )
  {
    QPolygonF scenePolygon;
    scenePolygon.reserve( normalizedPolygon.size() );
    for ( const QPointF &point : normalizedPolygon )
    {
      scenePolygon.append( QPointF( imageRect.left() + point.x() * imageRect.width(),
                                    imageRect.top() + point.y() * imageRect.height() ) );
    }
    return scenePolygon;
  }

  QPolygonF normalizedPolygonFromScene( const QPolygonF &scenePolygon, const QRectF &imageRect )
  {
    QPolygonF normalizedPolygon;
    if ( imageRect.width() <= 0.0 || imageRect.height() <= 0.0 )
      return normalizedPolygon;
    normalizedPolygon.reserve( scenePolygon.size() );
    for ( const QPointF &point : scenePolygon )
    {
      normalizedPolygon.append( QPointF( ( point.x() - imageRect.left() ) / imageRect.width(),
                                         ( point.y() - imageRect.top() ) / imageRect.height() ) );
    }
    return clampNormalizedPolygon( normalizedPolygon );
  }

  QRectF normalizedBoundingRect( const QPolygonF &polygon )
  {
    if ( polygon.isEmpty() )
      return QRectF();
    return polygon.boundingRect().intersected( QRectF( 0.0, 0.0, 1.0, 1.0 ) );
  }

  bool ensureDirectory( const QString &path )
  {
    return !path.isEmpty() && QDir().mkpath( path );
  }

  QString indexedOutputPath( const QString &directory, const QString &sourcePath, int index, const QString &suffix = QString() )
  {
    const QFileInfo sourceInfo( sourcePath );
    const QString extension = sourceInfo.suffix().isEmpty() ? QStringLiteral( "png" ) : sourceInfo.suffix().toLower();
    return QDir( directory ).filePath( QStringLiteral( "%1_%2%3.%4" )
                                         .arg( safeOutputBaseName( sourcePath, index ) )
                                         .arg( index + 1, 5, 10, QLatin1Char( '0' ) )
                                         .arg( suffix )
                                         .arg( extension ) );
  }

  bool copyFileReplacingTarget( const QString &sourcePath, const QString &targetPath )
  {
    if ( sourcePath.isEmpty() || targetPath.isEmpty() || !QFileInfo( sourcePath ).isFile() )
      return false;
    ensureDirectory( QFileInfo( targetPath ).absolutePath() );
    if ( QFileInfo::exists( targetPath ) && !QFile::remove( targetPath ) )
      return false;
    return QFile::copy( sourcePath, targetPath );
  }

  QWidget *dialogParentFor( QWidget *widget )
  {
    auto findMainWindow = []( QWidget *start ) -> QWidget *
    {
      for ( QWidget *current = start; current; current = current->parentWidget() )
      {
        if ( qobject_cast<QMainWindow *>( current ) )
          return current;
      }
      return nullptr;
    };

    if ( QWidget *mainWindow = findMainWindow( widget ) )
      return mainWindow;
    if ( widget )
    {
      if ( QWidget *window = widget->window() )
        return window;
    }
    return QApplication::activeWindow();
  }

  struct PhotoBatchResult
  {
    int written = 0;
    int skipped = 0;
    int processed = 0;
    QStringList outputPaths;
    QVector<QgsEcoPhotoWorkbench::PhotoRecord> records;
  };
}

class PhotoSelectionCheckBox final : public QCheckBox
{
  public:
    explicit PhotoSelectionCheckBox( QWidget *parent = nullptr ) : QCheckBox( parent ) { setFixedSize( 18, 18 ); setMouseTracking( true ); setCursor( Qt::PointingHandCursor ); }
  protected:
    void paintEvent( QPaintEvent *event ) override
    {
      Q_UNUSED( event )
      QPainter painter( this ); painter.setRenderHint( QPainter::Antialiasing, true );
      const QRectF box( 2.0, 2.0, 14.0, 14.0 ); const bool enabled = isEnabled(); const bool hovered = underMouse();
      QColor border = !enabled ? QColor( QStringLiteral( "#454545" ) ) : hovered ? QColor( QStringLiteral( "#c5c5c5" ) ) : QColor( QStringLiteral( "#6b6b6b" ) );
      QColor fill = isChecked() ? QColor( QStringLiteral( "#007acc" ) ) : QColor( QStringLiteral( "#1e1e1e" ) ); if ( !enabled ) fill = QColor( QStringLiteral( "#252526" ) );
      painter.setPen( QPen( border, 1.0 ) ); painter.setBrush( fill ); painter.drawRoundedRect( box, 2.0, 2.0 );
      if ( isChecked() && enabled ) { QPainterPath check; check.moveTo( 5.0, 9.0 ); check.lineTo( 7.4, 11.4 ); check.lineTo( 12.8, 5.8 ); painter.setPen( QPen( Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) ); painter.setBrush( Qt::NoBrush ); painter.drawPath( check ); }
      if ( hasFocus() ) { painter.setPen( QPen( QColor( 0, 122, 204, 180 ), 1.0, Qt::DashLine ) ); painter.setBrush( Qt::NoBrush ); painter.drawRoundedRect( box.adjusted( -1.0, -1.0, 1.0, 1.0 ), 3.0, 3.0 ); }
    }
    void mouseMoveEvent( QMouseEvent *event ) override { update(); QCheckBox::mouseMoveEvent( event ); }
    void leaveEvent( QEvent *event ) override { update(); QCheckBox::leaveEvent( event ); }
};

class PhotoListEntryWidget final : public QWidget
{
  public:
    explicit PhotoListEntryWidget( QWidget *parent = nullptr, bool checkable = true ) : QWidget( parent )
    {
      setAttribute( Qt::WA_TranslucentBackground, true );
      setMouseTracking( true );
      setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );

      auto *root = new QHBoxLayout( this );
      root->setContentsMargins( 10, 10, 10, 10 );
      root->setSpacing( 10 );

      mThumbnail = new QLabel( this );
      mThumbnail->setFixedSize( 74, 74 );
      mThumbnail->setAlignment( Qt::AlignCenter );
      mThumbnail->setAttribute( Qt::WA_TransparentForMouseEvents, true );
      mThumbnail->setStyleSheet( QStringLiteral( "background:#223041; border:1px solid #344255; border-radius:10px;" ) );
      root->addWidget( mThumbnail, 0, Qt::AlignTop );

      auto *details = new QVBoxLayout;
      details->setContentsMargins( 0, 0, 0, 0 );
      details->setSpacing( 5 );

      auto *top = new QHBoxLayout;
      top->setContentsMargins( 0, 0, 0, 0 );
      top->setSpacing( 8 );

      auto *titleStack = new QVBoxLayout;
      titleStack->setContentsMargins( 0, 0, 0, 0 );
      titleStack->setSpacing( 2 );

      mNameLabel = new QLabel( this );
      mNameLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
      mNameLabel->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
      mNameLabel->setStyleSheet( QStringLiteral( "color:#f6fbff; font-weight:700; font-size:12px;" ) );
      titleStack->addWidget( mNameLabel );

      mSubTitleLabel = new QLabel( this );
      mSubTitleLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
      mSubTitleLabel->setTextFormat( Qt::RichText );
      mSubTitleLabel->setWordWrap( false );
      mSubTitleLabel->setStyleSheet( QStringLiteral( "color:#9aa8b8; font-size:10px;" ) );
      titleStack->addWidget( mSubTitleLabel );

      top->addLayout( titleStack, 1 );

      mStateLabel = new QLabel( this );
      mStateLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
      mStateLabel->setTextFormat( Qt::RichText );
      mStateLabel->setAlignment( Qt::AlignRight | Qt::AlignTop );
      mStateLabel->setStyleSheet( QStringLiteral( "font-size:10px;" ) );
      top->addWidget( mStateLabel, 0, Qt::AlignTop );

      mCheckBox = new PhotoSelectionCheckBox( this );
      mCheckBox->setToolTip( tr( "勾选后参与批量智能识别。" ) );
      mCheckBox->setVisible( checkable );
      top->addWidget( mCheckBox, 0, Qt::AlignTop );
      details->addLayout( top );

      mInfoLabel = new QLabel( this );
      mInfoLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
      mInfoLabel->setTextFormat( Qt::RichText );
      mInfoLabel->setWordWrap( false );
      mInfoLabel->setMinimumHeight( 54 );
      mInfoLabel->setMaximumHeight( 54 );
      mInfoLabel->setStyleSheet( QStringLiteral( "color:#9aa8b8; font-size:11px;" ) );
      details->addWidget( mInfoLabel, 1 );
      root->addLayout( details, 1 );
      updateChrome();
    }

    void setThumbnail( const QPixmap &pixmap )
    {
      mThumbnail->setPixmap( pixmap.scaled( mThumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
    }
    void setName( const QString &name ) { mNameLabel->setText( name ); mNameLabel->setToolTip( name ); }
    void setSubtitle( const QString &text ) { mSubTitleLabel->setText( text ); mSubTitleLabel->setVisible( !text.trimmed().isEmpty() ); }
    void setInfo( const QString &info ) { mInfoLabel->setText( info ); }
    void setStateHtml( const QString &html ) { mStateLabel->setText( html ); mStateLabel->setVisible( !html.trimmed().isEmpty() ); }
    void setSelectedState( bool selected )
    {
      if ( mSelected == selected )
        return;
      mSelected = selected;
      updateChrome();
      update();
    }
    void setAccentColor( const QColor &color ) { if ( color.isValid() ) mAccentColor = color; updateChrome(); update(); }
    void setCheckable( bool checkable ) { mCheckBox->setVisible( checkable ); }
    bool isChecked() const { return mCheckBox->isChecked(); }
    void setChecked( bool checked ) { const QSignalBlocker blocker( mCheckBox ); mCheckBox->setChecked( checked ); }
    void onCheckedChanged( std::function<void( bool )> callback ) { connect( mCheckBox, &QCheckBox::toggled, this, [callback = std::move( callback )]( bool checked ) { callback( checked ); } ); }
    void onClicked( std::function<void()> callback ) { mClickCallback = std::move( callback ); }

  protected:
    void paintEvent( QPaintEvent *event ) override
    {
      Q_UNUSED( event )
      QPainter painter( this );
      painter.setRenderHint( QPainter::Antialiasing, true );

      const QRectF cardRect = QRectF( rect() ).adjusted( 6.0, 4.0, -6.0, -4.0 );
      const QColor background = mSelected ? QColor( QStringLiteral( "#1d2d3d" ) ) : mHovered ? QColor( QStringLiteral( "#1b2431" ) ) : QColor( QStringLiteral( "#171d27" ) );
      const QColor border = mSelected ? mAccentColor.lighter( 165 ) : mHovered ? mAccentColor.lighter( 138 ) : QColor( QStringLiteral( "#2d3948" ) );
      painter.setPen( QPen( border, 1.0 ) );
      painter.setBrush( background );
      painter.drawRoundedRect( cardRect, 12.0, 12.0 );

      QColor stripe = mAccentColor;
      stripe.setAlpha( mSelected ? 255 : 220 );
      painter.setPen( Qt::NoPen );
      painter.setBrush( stripe );
      painter.drawRoundedRect( QRectF( cardRect.left() + 1.0, cardRect.top() + 2.0, mSelected ? 5.0 : 4.0, cardRect.height() - 4.0 ), 2.0, 2.0 );

      if ( mSelected )
      {
        painter.setPen( QPen( QColor( 129, 216, 255, 180 ), 1.1 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawRoundedRect( cardRect.adjusted( 0.8, 0.8, -0.8, -0.8 ), 11.0, 11.0 );
      }

    }
    void mousePressEvent( QMouseEvent *event ) override { if ( event && event->button() == Qt::LeftButton && mClickCallback ) { mClickCallback(); event->accept(); return; } QWidget::mousePressEvent( event ); }
    void enterEvent( QEnterEvent *event ) override { Q_UNUSED( event ); mHovered = true; updateChrome(); update(); QWidget::enterEvent( event ); }
    void leaveEvent( QEvent *event ) override { Q_UNUSED( event ); mHovered = false; updateChrome(); update(); QWidget::leaveEvent( event ); }

  private:
    void updateChrome()
    {
      if ( mThumbnail )
      {
        const QString thumbBg = mSelected ? QStringLiteral( "#22364b" ) : mHovered ? QStringLiteral( "#223041" ) : QStringLiteral( "#1c2734" );
        const QString thumbBorder = mSelected ? mAccentColor.lighter( 150 ).name() : mHovered ? mAccentColor.lighter( 124 ).name() : QStringLiteral( "#344255" );
        mThumbnail->setStyleSheet( QStringLiteral( "background:%1; border:1px solid %2; border-radius:10px;" ).arg( thumbBg, thumbBorder ) );
      }
      if ( mNameLabel )
        mNameLabel->setStyleSheet( QStringLiteral( "color:%1; font-weight:%2; font-size:12px;" ).arg( mSelected ? QStringLiteral( "#ffffff" ) : mHovered ? QStringLiteral( "#f8fcff" ) : QStringLiteral( "#f6fbff" ), mSelected ? QStringLiteral( "800" ) : QStringLiteral( "700" ) ) );
      if ( mSubTitleLabel )
        mSubTitleLabel->setStyleSheet( QStringLiteral( "color:%1; font-size:10px;" ).arg( mSelected ? QStringLiteral( "#d7e8f7" ) : QStringLiteral( "#9aa8b8" ) ) );
      if ( mStateLabel )
        mStateLabel->setStyleSheet( QStringLiteral( "color:%1; font-size:10px; font-weight:700;" ).arg( mSelected ? QStringLiteral( "#edf7ff" ) : QStringLiteral( "#d7e2ee" ) ) );
      if ( mInfoLabel )
        mInfoLabel->setStyleSheet( QStringLiteral( "color:%1; font-size:10px;" ).arg( mSelected ? QStringLiteral( "#dce8f5" ) : QStringLiteral( "#9aa8b8" ) ) );
    }

    QLabel *mThumbnail = nullptr;
    QLabel *mNameLabel = nullptr;
    QLabel *mSubTitleLabel = nullptr;
    QLabel *mStateLabel = nullptr;
    QLabel *mInfoLabel = nullptr;
    QCheckBox *mCheckBox = nullptr;
    std::function<void()> mClickCallback;
    QColor mAccentColor = QColor( QStringLiteral( "#38bdf8" ) );
    bool mHovered = false;
    bool mSelected = false;
};

void syncListEntrySelectionState( QListWidget *list )
{
  if ( !list )
    return;
  const QListWidgetItem *currentItem = list->currentItem();
  for ( int row = 0; row < list->count(); ++row )
  {
    QListWidgetItem *item = list->item( row );
    if ( !item )
      continue;
    auto *entry = dynamic_cast<PhotoListEntryWidget *>( list->itemWidget( item ) );
    if ( entry )
      entry->setSelectedState( item == currentItem || item->isSelected() );
  }
}

class PhotoPolygonItem final : public QGraphicsPolygonItem
{
  public:
    explicit PhotoPolygonItem( const QPolygonF &polygon, const QColor &color, QGraphicsItem *parent = nullptr ) : QGraphicsPolygonItem( polygon, parent ), mBaseColor( color ) { setFlag( QGraphicsItem::ItemIsSelectable, true ); setAcceptHoverEvents( true ); setAcceptedMouseButtons( Qt::NoButton ); setCacheMode( QGraphicsItem::DeviceCoordinateCache ); setZValue( 10.0 ); }
  protected:
    QRectF boundingRect() const override { return QGraphicsPolygonItem::boundingRect().adjusted( -14.0, -14.0, 14.0, 14.0 ); }
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr ) override
    {
      Q_UNUSED( widget ) painter->setRenderHint( QPainter::Antialiasing, true ); const bool selected = option && ( option->state & QStyle::State_Selected ); const bool hovered = option && ( option->state & QStyle::State_MouseOver ); const QPolygonF poly = polygon();
      QColor glow = mBaseColor.lighter( selected ? 160 : hovered ? 145 : 126 ); glow.setAlpha( selected ? 110 : hovered ? 68 : 30 ); QPen glowPen( glow, selected ? 8.0 : hovered ? 6.0 : 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); glowPen.setCosmetic( true ); painter->setPen( glowPen ); painter->setBrush( Qt::NoBrush ); painter->drawPolygon( poly );
      QColor fill = mBaseColor; fill.setAlpha( selected ? 126 : hovered ? 96 : 74 ); QPen mainPen( selected ? mBaseColor.lighter( 160 ) : hovered ? mBaseColor.lighter( 136 ) : mBaseColor, selected ? 2.6 : hovered ? 2.1 : 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); mainPen.setCosmetic( true ); painter->setPen( mainPen ); painter->setBrush( fill ); painter->drawPolygon( poly );
      if ( selected || hovered ) { QPen highlightPen( QColor( 255, 255, 255, selected ? 120 : 84 ), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); highlightPen.setCosmetic( true ); painter->setPen( highlightPen ); painter->setBrush( Qt::NoBrush ); painter->drawPolygon( poly ); }
    }
  private: QColor mBaseColor;
};

class PolygonEditHandleItem final : public QGraphicsEllipseItem
{
  public:
    PolygonEditHandleItem( int annotationIndex, int vertexIndex, const QPointF &center, const QColor &baseColor, QGraphicsItem *parent = nullptr )
      : QGraphicsEllipseItem( QRectF( -4.5, -4.5, 9.0, 9.0 ), parent )
      , mAnnotationIndex( annotationIndex )
      , mVertexIndex( vertexIndex )
      , mBaseColor( baseColor )
    {
      setPos( center );
      setFlag( QGraphicsItem::ItemIsMovable, true );
      setFlag( QGraphicsItem::ItemIsSelectable, true );
      setFlag( QGraphicsItem::ItemSendsGeometryChanges, true );
      setFlag( QGraphicsItem::ItemIgnoresTransformations, true );
      setAcceptHoverEvents( true );
      setAcceptedMouseButtons( Qt::LeftButton | Qt::RightButton );
      setCacheMode( QGraphicsItem::DeviceCoordinateCache );
      setZValue( 26.0 );
    }
    int annotationIndex() const { return mAnnotationIndex; }
    int vertexIndex() const { return mVertexIndex; }
    void setVertexIndex( int vertexIndex ) { mVertexIndex = vertexIndex; }
    void setMoveCallback( std::function<void( int, int, const QPointF & )> callback ) { mMoveCallback = std::move( callback ); }
    void setDeleteCallback( std::function<void( int, int )> callback ) { mDeleteCallback = std::move( callback ); }
    void setScenePositionSilently( const QPointF &position ) { mBlockCallback = true; setPos( position ); mBlockCallback = false; }
  protected:
    QVariant itemChange( GraphicsItemChange change, const QVariant &value ) override
    {
      if ( change == QGraphicsItem::ItemPositionHasChanged && !mBlockCallback && mMoveCallback )
        mMoveCallback( mAnnotationIndex, mVertexIndex, pos() );
      return QGraphicsEllipseItem::itemChange( change, value );
    }
    void mousePressEvent( QGraphicsSceneMouseEvent *event ) override
    {
      if ( event && event->button() == Qt::RightButton && mDeleteCallback )
      {
        mDeleteCallback( mAnnotationIndex, mVertexIndex );
        event->accept();
        return;
      }
      QGraphicsEllipseItem::mousePressEvent( event );
    }
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr ) override
    {
      Q_UNUSED( widget )
      painter->setRenderHint( QPainter::Antialiasing, true );
      const bool hovered = option && ( option->state & QStyle::State_MouseOver );
      const bool selected = option && ( option->state & QStyle::State_Selected );
      const QRectF r = rect();
      QColor ring = selected ? QColor( QStringLiteral( "#ffffff" ) ) : hovered ? QColor( QStringLiteral( "#d8f3ff" ) ) : QColor( QStringLiteral( "#f0f8ff" ) );
      ring.setAlpha( selected ? 230 : hovered ? 190 : 155 );
      QPen outerPen( ring, selected ? 1.6 : hovered ? 1.35 : 1.1 );
      outerPen.setCosmetic( true );
      painter->setPen( outerPen );
      QColor fill = mBaseColor.lighter( selected ? 122 : hovered ? 114 : 100 );
      fill.setAlpha( selected ? 255 : hovered ? 245 : 232 );
      painter->setBrush( fill );
      painter->drawEllipse( r );
      painter->setPen( Qt::NoPen );
      painter->setBrush( QColor( 255, 255, 255, selected ? 240 : 220 ) );
      painter->drawEllipse( r.adjusted( r.width() * 0.34, r.height() * 0.34, -r.width() * 0.34, -r.height() * 0.34 ) );
    }
  private:
    int mAnnotationIndex = -1;
    int mVertexIndex = -1;
    QColor mBaseColor;
    bool mBlockCallback = false;
    std::function<void( int, int, const QPointF & )> mMoveCallback;
    std::function<void( int, int )> mDeleteCallback;
};
class DrawingPointItem final : public QGraphicsEllipseItem
{
  public:
    DrawingPointItem( const QPointF &center, qreal radius, const QColor &baseColor, bool active, QGraphicsItem *parent = nullptr ) : QGraphicsEllipseItem( QRectF( -radius, -radius, radius * 2.0, radius * 2.0 ), parent ), mBaseColor( baseColor ), mActive( active ) { setPos( center ); setAcceptedMouseButtons( Qt::NoButton ); setFlag( QGraphicsItem::ItemIgnoresTransformations, true ); setCacheMode( QGraphicsItem::DeviceCoordinateCache ); setZValue( 24.0 ); }
  protected:
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr ) override { Q_UNUSED( option ) Q_UNUSED( widget ) painter->setRenderHint( QPainter::Antialiasing, true ); const QRectF r = rect(); QPen outerPen( QColor( 255, 255, 255, mActive ? 150 : 90 ), mActive ? 1.4 : 1.0 ); outerPen.setCosmetic( true ); painter->setPen( outerPen ); painter->setBrush( mBaseColor ); painter->drawEllipse( r ); painter->setPen( Qt::NoPen ); painter->setBrush( QColor( 255, 255, 255, mActive ? 245 : 220 ) ); painter->drawEllipse( r.adjusted( r.width() * 0.32, r.height() * 0.32, -r.width() * 0.32, -r.height() * 0.32 ) ); }
  private: QColor mBaseColor; bool mActive = false;
};

class DrawingPulseItem final : public QGraphicsObject
{
  public:
    DrawingPulseItem( const QPolygonF &polygon, const QColor &color, QGraphicsItem *parent = nullptr ) : QGraphicsObject( parent ), mPolygon( polygon ), mBaseColor( color ), mBounds( polygon.boundingRect() ) { setZValue( 30.0 ); }
    QRectF boundingRect() const override { return mBounds.adjusted( -18.0, -18.0, 18.0, 18.0 ); }
    void setProgress( qreal progress ) { mProgress = progress; update(); }
  protected:
    void paint( QPainter *painter, const QStyleOptionGraphicsItem *, QWidget * = nullptr ) override { if ( mPolygon.size() < 3 ) return; QPainterPath path; path.addPolygon( mPolygon ); path.closeSubpath(); const qreal fade = 1.0 - mProgress; const qreal scale = 1.0 + 0.055 * fade; painter->save(); painter->translate( mBounds.center() ); painter->scale( scale, scale ); painter->translate( -mBounds.center() ); QColor glow = mBaseColor.lighter( 145 ); glow.setAlphaF( 0.28 * fade ); painter->setPen( QPen( glow, 9.0 + 6.0 * fade, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) ); painter->setBrush( Qt::NoBrush ); painter->drawPath( path ); QColor core = mBaseColor.lighter( 160 ); core.setAlphaF( 0.96 * fade ); painter->setPen( QPen( core, 2.6 + 0.4 * fade, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) ); painter->drawPath( path ); QColor fill = mBaseColor; fill.setAlphaF( 0.12 * fade ); painter->fillPath( path, fill ); painter->restore(); }
  private: QPolygonF mPolygon; QColor mBaseColor; qreal mProgress = 0.0; QRectF mBounds;
};

class EcoPhotoAnnotationView final : public QGraphicsView
{
  public:
    explicit EcoPhotoAnnotationView( QWidget *parent = nullptr ) : QGraphicsView( parent ) { mScene = new QGraphicsScene( this ); setScene( mScene ); setRenderHint( QPainter::Antialiasing, true ); setRenderHint( QPainter::SmoothPixmapTransform, true ); setBackgroundBrush( QColor( QStringLiteral( "#111827" ) ) ); setFrameShape( QFrame::NoFrame ); setAlignment( Qt::AlignCenter ); setFocusPolicy( Qt::StrongFocus ); setMouseTracking( true ); viewport()->setMouseTracking( true ); setViewportUpdateMode( QGraphicsView::BoundingRectViewportUpdate ); setDragMode( QGraphicsView::ScrollHandDrag ); setTransformationAnchor( QGraphicsView::AnchorUnderMouse ); setResizeAnchor( QGraphicsView::AnchorViewCenter ); }
    void setImage( const QImage &image, const QVector<QgsEcoPhotoWorkbench::Annotation> &annotations ) { clearDrawingPreview(); clearSmartPromptPoints(); clearEditHandles(); mScene->clear(); mImage = image; mAnnotations = annotations; mPixmapItem = nullptr; mAnnotationItems.clear(); mUserAdjustedView = false; if ( mImage.isNull() ) { mScene->setSceneRect( QRectF() ); return; } mPixmapItem = mScene->addPixmap( QPixmap::fromImage( mImage ) ); mPixmapItem->setZValue( 0.0 ); mScene->setSceneRect( mPixmapItem->boundingRect() ); rebuildAnnotationItems(); fitToImage(); }
    void setAnnotations( const QVector<QgsEcoPhotoWorkbench::Annotation> &annotations ) { clearEditHandles(); mAnnotations = annotations; rebuildAnnotationItems(); viewport()->update(); }
    void setDrawingEnabled( bool enabled, int classId, const QString &className ) { mDrawingEnabled = enabled; if ( enabled ) { mDeleteEnabled = false; mEditEnabled = false; clearEditHandles(); mDrawingClassId = classId; mDrawingClassName = className; } else clearDrawingPreview(); setDragMode( enabled ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag ); restoreInteractionCursor(); if ( enabled ) setFocus(); }
    void setDeleteEnabled( bool enabled ) { mDeleteEnabled = enabled; if ( enabled ) { mDrawingEnabled = false; mEditEnabled = false; clearDrawingPreview(); clearEditHandles(); setDragMode( QGraphicsView::NoDrag ); } else setDragMode( QGraphicsView::ScrollHandDrag ); restoreInteractionCursor(); if ( enabled ) setFocus(); }
    void setEditEnabled( bool enabled ) { mEditEnabled = enabled; if ( enabled ) { mDrawingEnabled = false; mDeleteEnabled = false; mSmartSegmentationEnabled = false; clearDrawingPreview(); clearSmartPromptPoints(); setDragMode( QGraphicsView::NoDrag ); } else { clearEditHandles(); setDragMode( QGraphicsView::ScrollHandDrag ); } restoreInteractionCursor(); if ( enabled ) setFocus(); }
    void setAnnotationEditCallback( std::function<void( int, const QPolygonF & )> callback ) { mAnnotationEditCallback = std::move( callback ); }
    void setAnnotationCallback( std::function<void( int, const QString &, const QPolygonF & )> callback ) { mAnnotationCallback = std::move( callback ); }
    void setDeleteCallback( std::function<void( int )> callback ) { mDeleteCallback = std::move( callback ); }
    void setDrawingFinishedCallback( std::function<void()> callback ) { mDrawingFinishedCallback = std::move( callback ); }
    void setSmartSegmentationCallback( std::function<void( const QVector<QPointF> &, const QVector<int> & )> callback ) { mSmartSegmentationCallback = std::move( callback ); }
    void setSmartSegmentationEnabled( bool enabled ) { mSmartSegmentationEnabled = enabled; mSpacePanning = false; mSpaceDragging = false; if ( !enabled ) clearSmartPromptPoints(); if ( enabled ) { mDrawingEnabled = false; mDeleteEnabled = false; mEditEnabled = false; clearDrawingPreview(); clearEditHandles(); setDragMode( QGraphicsView::NoDrag ); } else setDragMode( QGraphicsView::ScrollHandDrag ); restoreInteractionCursor(); if ( enabled ) setFocus(); }
    bool hasUnfinishedDrawing() const { return !mDrawingPoints.isEmpty(); }
    void discardCurrentDrawing() { clearDrawingPreview(); }
    bool finishCurrentDrawing() { if ( !mPixmapItem || mDrawingPoints.size() < 3 || !mAnnotationCallback ) return false; const QPolygonF normalized = normalizedPolygonFromScene( mDrawingPoints, mPixmapItem->boundingRect() ); const QRectF bounds = normalizedBoundingRect( normalized ); if ( normalized.size() < 3 || bounds.width() < 0.002 || bounds.height() < 0.002 ) return false; mAnnotationCallback( mDrawingClassId, mDrawingClassName, normalized ); clearDrawingPreview(); if ( mDrawingFinishedCallback ) mDrawingFinishedCallback(); return true; }
    void playCompletionPulse( int classId, const QPolygonF &normalizedPolygon ) { if ( !mScene || !mPixmapItem || normalizedPolygon.size() < 3 ) return; auto *pulse = new DrawingPulseItem( scenePolygonFromNormalized( normalizedPolygon, mPixmapItem->boundingRect() ), annotationColor( classId ) ); mScene->addItem( pulse ); auto *animation = new QVariantAnimation( mScene ); animation->setDuration( 560 ); animation->setStartValue( 0.0 ); animation->setEndValue( 1.0 ); animation->setEasingCurve( QEasingCurve::InOutCubic ); QPointer<DrawingPulseItem> guard = pulse; connect( animation, &QVariantAnimation::valueChanged, this, [guard]( const QVariant &value ) { if ( guard ) guard->setProgress( value.toReal() ); } ); connect( animation, &QVariantAnimation::finished, this, [guard, animation] { if ( guard ) { DrawingPulseItem *item = guard.data(); if ( item->scene() ) item->scene()->removeItem( item ); delete item; } animation->deleteLater(); } ); animation->start(); }
  protected:
    void resizeEvent( QResizeEvent *event ) override { QGraphicsView::resizeEvent( event ); if ( !mImage.isNull() && !mUserAdjustedView ) fitToImage(); }
    void wheelEvent( QWheelEvent *event ) override { if ( mImage.isNull() ) { event->ignore(); return; } scale( event->angleDelta().y() >= 0 ? 1.16 : 1.0 / 1.16, event->angleDelta().y() >= 0 ? 1.16 : 1.0 / 1.16 ); mUserAdjustedView = true; event->accept(); }
    void mousePressEvent( QMouseEvent *event ) override
    {
      if ( mSmartSegmentationEnabled && mSpacePanning && event->button() == Qt::LeftButton ) { mSpaceDragging = true; mSpacePanStartPos = event->pos(); mSpacePanStartH = horizontalScrollBar()->value(); mSpacePanStartV = verticalScrollBar()->value(); setCursor( Qt::ClosedHandCursor ); viewport()->setCursor( Qt::ClosedHandCursor ); event->accept(); return; }
      if ( mEditEnabled && mPixmapItem )
      {
        if ( event->button() == Qt::LeftButton )
        {
          const QPointF scenePosition = clampedImagePoint( mapToScene( event->pos() ) );
          const int handleIndex = nearestEditHandleIndex( scenePosition );
          if ( handleIndex >= 0 ) { mDraggingEditHandle = true; mDraggedEditHandleIndex = handleIndex; setCursor( Qt::SizeAllCursor ); viewport()->setCursor( Qt::SizeAllCursor ); event->accept(); return; }
          const int annotationIndex = annotationIndexAt( event->pos() );
          if ( annotationIndex >= 0 ) beginEditSelection( annotationIndex ); else clearEditHandles();
          event->accept();
          return;
        }
        if ( event->button() == Qt::RightButton )
        {
          const int handleIndex = nearestEditHandleIndex( mapToScene( event->pos() ) );
          if ( handleIndex >= 0 && removeEditVertex( handleIndex ) ) { event->accept(); return; }
        }
      }
      if ( mDeleteEnabled && event->button() == Qt::LeftButton ) { QGraphicsItem *rawItem = itemAt( event->pos() ); while ( rawItem && !dynamic_cast<PhotoPolygonItem *>( rawItem ) ) rawItem = rawItem->parentItem(); if ( auto *polygonItem = dynamic_cast<PhotoPolygonItem *>( rawItem ) ) { if ( mDeleteCallback ) mDeleteCallback( polygonItem->data( 0 ).toInt() ); event->accept(); return; } }
      if ( mDrawingEnabled && event->button() == Qt::RightButton && mPixmapItem ) { mRightPanning = true; mRightPanStartPos = event->pos(); mRightPanStartH = horizontalScrollBar()->value(); mRightPanStartV = verticalScrollBar()->value(); setCursor( Qt::ClosedHandCursor ); viewport()->setCursor( Qt::ClosedHandCursor ); event->accept(); return; }
      if ( mSmartSegmentationEnabled && ( event->button() == Qt::LeftButton || event->button() == Qt::RightButton ) && mPixmapItem ) { const QPointF scenePosition = clampedImagePoint( mapToScene( event->pos() ) ); if ( mPixmapItem->contains( scenePosition ) && mSmartSegmentationCallback ) { const QRectF rect = mPixmapItem->boundingRect(); mSmartPromptPoints.append( QPointF( ( scenePosition.x() - rect.left() ) / rect.width(), ( scenePosition.y() - rect.top() ) / rect.height() ) ); const int label = event->button() == Qt::RightButton ? 0 : 1; mSmartPromptLabels.append( label ); auto *marker = new DrawingPointItem( scenePosition, 4.6, label == 1 ? QColor( QStringLiteral( "#ef4444" ) ) : QColor( QStringLiteral( "#38bdf8" ) ), true ); mScene->addItem( marker ); mSmartPromptItems.append( marker ); mSmartSegmentationCallback( mSmartPromptPoints, mSmartPromptLabels ); event->accept(); return; } }
      if ( mDrawingEnabled && event->button() == Qt::LeftButton && mPixmapItem ) { const QPointF scenePosition = clampedImagePoint( mapToScene( event->pos() ) ); if ( mPixmapItem->contains( scenePosition ) ) { mDrawingPoints.append( scenePosition ); mCurrentMousePoint = scenePosition; updateDrawingPreview( false ); event->accept(); return; } }
      QGraphicsView::mousePressEvent( event );
    }
    void mouseMoveEvent( QMouseEvent *event ) override { if ( mSpaceDragging ) { const QPoint delta = event->pos() - mSpacePanStartPos; horizontalScrollBar()->setValue( mSpacePanStartH - delta.x() ); verticalScrollBar()->setValue( mSpacePanStartV - delta.y() ); event->accept(); return; } if ( mRightPanning ) { const QPoint delta = event->pos() - mRightPanStartPos; horizontalScrollBar()->setValue( mRightPanStartH - delta.x() ); verticalScrollBar()->setValue( mRightPanStartV - delta.y() ); event->accept(); return; } if ( mEditEnabled && mDraggingEditHandle && mEditingAnnotationIndex >= 0 && mPixmapItem ) { updateDraggedVertex( clampedImagePoint( mapToScene( event->pos() ) ) ); event->accept(); return; } if ( mDrawingEnabled && !mDrawingPoints.isEmpty() && mPixmapItem ) { mCurrentMousePoint = clampedImagePoint( mapToScene( event->pos() ) ); updateDrawingPreview( false ); event->accept(); return; } QGraphicsView::mouseMoveEvent( event ); }
    void mouseReleaseEvent( QMouseEvent *event ) override { if ( mSpaceDragging && event->button() == Qt::LeftButton ) { mSpaceDragging = false; restoreInteractionCursor(); event->accept(); return; } if ( mRightPanning && event->button() == Qt::RightButton ) { mRightPanning = false; restoreInteractionCursor(); event->accept(); return; } if ( mEditEnabled && mDraggingEditHandle && event->button() == Qt::LeftButton ) { mDraggingEditHandle = false; commitEditGeometry(); event->accept(); return; } QGraphicsView::mouseReleaseEvent( event ); }
    void mouseDoubleClickEvent( QMouseEvent *event ) override { if ( mEditEnabled && event->button() == Qt::LeftButton && mPixmapItem && mEditingAnnotationIndex >= 0 && insertEditVertex( clampedImagePoint( mapToScene( event->pos() ) ) ) ) { commitEditGeometry(); event->accept(); return; } if ( mDrawingEnabled && event->button() == Qt::LeftButton && mPixmapItem ) { if ( mDrawingPoints.size() >= 3 && mAnnotationCallback ) { const QPolygonF normalized = normalizedPolygonFromScene( mDrawingPoints, mPixmapItem->boundingRect() ); const QRectF bounds = normalizedBoundingRect( normalized ); if ( normalized.size() >= 3 && bounds.width() >= 0.002 && bounds.height() >= 0.002 ) { mAnnotationCallback( mDrawingClassId, mDrawingClassName, normalized ); if ( mDrawingFinishedCallback ) mDrawingFinishedCallback(); } } clearDrawingPreview(); event->accept(); return; } QGraphicsView::mouseDoubleClickEvent( event ); }    void keyPressEvent( QKeyEvent *event ) override { if ( mSmartSegmentationEnabled && event->key() == Qt::Key_Space && !event->isAutoRepeat() ) { mSpacePanning = true; setCursor( Qt::OpenHandCursor ); viewport()->setCursor( Qt::OpenHandCursor ); event->accept(); return; } if ( mDrawingEnabled && event->matches( QKeySequence::Undo ) ) { if ( !mDrawingPoints.isEmpty() ) { mDrawingPoints.removeLast(); updateDrawingPreview( false ); } event->accept(); return; } if ( mDrawingEnabled && event->key() == Qt::Key_Escape ) { clearDrawingPreview(); event->accept(); return; } QGraphicsView::keyPressEvent( event ); }
    void keyReleaseEvent( QKeyEvent *event ) override { if ( mSmartSegmentationEnabled && event->key() == Qt::Key_Space && !event->isAutoRepeat() ) { mSpacePanning = false; if ( !mSpaceDragging ) restoreInteractionCursor(); event->accept(); return; } QGraphicsView::keyReleaseEvent( event ); }
  private:
    qreal editHitTolerance() const { return std::max<qreal>( 6.0, QLineF( mapToScene( QPoint( 0, 0 ) ), mapToScene( QPoint( 10, 0 ) ) ).length() ); }
    int annotationIndexAt( const QPoint &viewPosition ) const
    {
      const QList<QGraphicsItem *> candidates = mScene ? mScene->items( mapToScene( viewPosition ) ) : QList<QGraphicsItem *>();
      for ( QGraphicsItem *candidate : candidates )
        if ( auto *polygonItem = dynamic_cast<PhotoPolygonItem *>( candidate ) )
          return polygonItem->data( 0 ).toInt();
      return -1;
    }
    int nearestEditHandleIndex( const QPointF &scenePosition ) const
    {
      if ( mEditingAnnotationIndex < 0 ) return -1;
      const qreal tolerance = editHitTolerance();
      for ( int i = 0; i < mEditPolygon.size(); ++i )
        if ( QLineF( scenePosition, mEditPolygon.at( i ) ).length() <= tolerance ) return i;
      return -1;
    }
    void clearEditHandles()
    {
      for ( QGraphicsEllipseItem *handle : std::as_const( mEditHandles ) ) { if ( handle && handle->scene() ) handle->scene()->removeItem( handle ); delete handle; }
      mEditHandles.clear();
      if ( mEditingAnnotationIndex >= 0 && mAnnotationItems.contains( mEditingAnnotationIndex ) ) mAnnotationItems.value( mEditingAnnotationIndex )->setSelected( false );
      mEditingAnnotationIndex = -1;
      mDraggedEditHandleIndex = -1;
      mDraggingEditHandle = false;
      mEditPolygon.clear();
    }
    void beginEditSelection( int annotationIndex )
    {
      if ( annotationIndex < 0 || annotationIndex >= mAnnotations.size() || !mPixmapItem ) return;
      clearEditHandles();
      mEditingAnnotationIndex = annotationIndex;
      mEditPolygon = scenePolygonFromNormalized( mAnnotations.at( annotationIndex ).normalizedPolygon, mPixmapItem->boundingRect() );
      if ( mEditPolygon.size() < 3 ) { clearEditHandles(); return; }
      if ( mAnnotationItems.contains( annotationIndex ) ) mAnnotationItems.value( annotationIndex )->setSelected( true );
      const QColor color = annotationColor( mAnnotations.at( annotationIndex ).classId );
      for ( int i = 0; i < mEditPolygon.size(); ++i ) { auto *handle = new PolygonEditHandleItem( annotationIndex, i, mEditPolygon.at( i ), color ); handle->setAcceptedMouseButtons( Qt::NoButton ); mScene->addItem( handle ); mEditHandles.append( handle ); }
      viewport()->update();
    }
    void refreshEditGeometry()
    {
      if ( mEditingAnnotationIndex < 0 || !mAnnotationItems.contains( mEditingAnnotationIndex ) ) return;
      mAnnotationItems.value( mEditingAnnotationIndex )->setPolygon( mEditPolygon );
      while ( mEditHandles.size() > mEditPolygon.size() ) { QGraphicsEllipseItem *handle = mEditHandles.takeLast(); if ( handle->scene() ) handle->scene()->removeItem( handle ); delete handle; }
      while ( mEditHandles.size() < mEditPolygon.size() ) { const int index = mEditHandles.size(); auto *handle = new PolygonEditHandleItem( mEditingAnnotationIndex, index, mEditPolygon.at( index ), annotationColor( mAnnotations.at( mEditingAnnotationIndex ).classId ) ); handle->setAcceptedMouseButtons( Qt::NoButton ); mScene->addItem( handle ); mEditHandles.append( handle ); }
      for ( int i = 0; i < mEditHandles.size(); ++i ) { if ( auto *handle = dynamic_cast<PolygonEditHandleItem *>( mEditHandles.at( i ) ) ) { handle->setVertexIndex( i ); handle->setScenePositionSilently( mEditPolygon.at( i ) ); } }
      viewport()->update();
    }
    void updateDraggedVertex( const QPointF &scenePosition ) { if ( mDraggedEditHandleIndex >= 0 && mDraggedEditHandleIndex < mEditPolygon.size() ) { mEditPolygon[ mDraggedEditHandleIndex ] = scenePosition; refreshEditGeometry(); } }
    void commitEditGeometry()
    {
      if ( mEditingAnnotationIndex < 0 || !mPixmapItem || mEditPolygon.size() < 3 || !mAnnotationEditCallback ) return;
      const QPolygonF normalized = normalizedPolygonFromScene( mEditPolygon, mPixmapItem->boundingRect() );
      const QRectF bounds = normalizedBoundingRect( normalized );
      if ( normalized.size() >= 3 && bounds.width() >= 0.002 && bounds.height() >= 0.002 ) { mAnnotations[ mEditingAnnotationIndex ].normalizedPolygon = normalized; mAnnotationEditCallback( mEditingAnnotationIndex, normalized ); }
    }
    bool insertEditVertex( const QPointF &scenePosition )
    {
      if ( mEditingAnnotationIndex < 0 || mEditPolygon.size() < 3 ) return false;
      qreal bestDistance = std::max<qreal>( 14.0, editHitTolerance() * 2.6 ); int insertIndex = -1;
      for ( int i = 0; i < mEditPolygon.size(); ++i ) { const QPointF a = mEditPolygon.at( i ); const QPointF b = mEditPolygon.at( ( i + 1 ) % mEditPolygon.size() ); const double dx = b.x() - a.x(); const double dy = b.y() - a.y(); const double lengthSquared = dx * dx + dy * dy; if ( lengthSquared <= 1e-12 ) continue; const double t = std::clamp( ( ( scenePosition.x() - a.x() ) * dx + ( scenePosition.y() - a.y() ) * dy ) / lengthSquared, 0.0, 1.0 ); const QPointF projection( a.x() + t * dx, a.y() + t * dy ); const qreal distance = QLineF( scenePosition, projection ).length(); if ( distance < bestDistance ) { bestDistance = distance; insertIndex = i + 1; } }
      if ( insertIndex < 0 ) return false;
      mEditPolygon.insert( insertIndex, scenePosition ); refreshEditGeometry(); return true;
    }
    bool removeEditVertex( int vertexIndex ) { if ( mEditingAnnotationIndex < 0 || mEditPolygon.size() <= 3 || vertexIndex < 0 || vertexIndex >= mEditPolygon.size() ) return false; mEditPolygon.removeAt( vertexIndex ); refreshEditGeometry(); commitEditGeometry(); return true; }  private:
    void fitToImage() { if ( !mPixmapItem ) return; resetTransform(); fitInView( mPixmapItem, Qt::KeepAspectRatio ); centerOn( mPixmapItem ); mUserAdjustedView = false; }
    void clearAnnotationItems() { for ( QGraphicsPolygonItem *item : std::as_const( mAnnotationItems ) ) { if ( item && item->scene() ) item->scene()->removeItem( item ); delete item; } mAnnotationItems.clear(); }
    void rebuildAnnotationItems() { if ( !mPixmapItem ) return; clearAnnotationItems(); const QRectF imageRect = mPixmapItem->boundingRect(); for ( int i = 0; i < mAnnotations.size(); ++i ) { const auto &annotation = mAnnotations.at( i ); const QPolygonF polygon = scenePolygonFromNormalized( annotation.normalizedPolygon, imageRect ); if ( polygon.size() < 3 ) continue; auto *item = new PhotoPolygonItem( polygon, annotationColor( annotation.classId ) ); item->setData( 0, i ); item->setToolTip( annotation.className ); mScene->addItem( item ); mAnnotationItems.insert( i, item ); } }
    void clearSmartPromptPoints() { for ( QGraphicsItem *item : std::as_const( mSmartPromptItems ) ) { if ( item && item->scene() ) item->scene()->removeItem( item ); delete item; } mSmartPromptItems.clear(); mSmartPromptPoints.clear(); mSmartPromptLabels.clear(); }
    void restoreInteractionCursor()
    {
      if ( mSmartSegmentationEnabled )
      {
        const Qt::CursorShape cursor = mSpacePanning ? Qt::OpenHandCursor : Qt::CrossCursor;
        setCursor( cursor );
        viewport()->setCursor( cursor );
        return;
      }
      if ( mDrawingEnabled )
      {
        setCursor( Qt::CrossCursor );
        viewport()->setCursor( Qt::CrossCursor );
        return;
      }
      if ( mDeleteEnabled )
      {
        setCursor( Qt::PointingHandCursor );
        viewport()->setCursor( Qt::PointingHandCursor );
        return;
      }
      if ( mEditEnabled )
      {
        const Qt::CursorShape cursor = mEditingAnnotationIndex >= 0 ? Qt::CrossCursor : Qt::PointingHandCursor;
        setCursor( cursor );
        viewport()->setCursor( cursor );
        return;
      }
      setCursor( Qt::ArrowCursor );
      viewport()->setCursor( Qt::ArrowCursor );
    }    QPointF clampedImagePoint( const QPointF &scenePoint ) const { if ( !mPixmapItem ) return scenePoint; const QRectF rect = mPixmapItem->boundingRect(); return QPointF( std::clamp( scenePoint.x(), rect.left(), rect.right() ), std::clamp( scenePoint.y(), rect.top(), rect.bottom() ) ); }
    void clearDrawingPreview() { mDrawingPoints.clear(); mCurrentMousePoint = QPointF(); if ( mDrawGlowItem ) { mScene->removeItem( mDrawGlowItem ); delete mDrawGlowItem; mDrawGlowItem = nullptr; } if ( mDrawItem ) { mScene->removeItem( mDrawItem ); delete mDrawItem; mDrawItem = nullptr; } }
    void updateDrawingPreview( bool closePolygon ) { if ( !mScene ) return; if ( mDrawGlowItem ) { mScene->removeItem( mDrawGlowItem ); delete mDrawGlowItem; mDrawGlowItem = nullptr; } if ( mDrawItem ) { mScene->removeItem( mDrawItem ); delete mDrawItem; mDrawItem = nullptr; } if ( mDrawingPoints.isEmpty() ) return; QPolygonF points = mDrawingPoints; if ( !closePolygon && mCurrentMousePoint != mDrawingPoints.constLast() ) points.append( mCurrentMousePoint ); QPainterPath path; path.moveTo( points.first() ); for ( int i = 1; i < points.size(); ++i ) path.lineTo( points.at( i ) ); if ( points.size() >= 3 ) path.closeSubpath(); QColor fill = annotationColor( mDrawingClassId ); fill.setAlpha( points.size() >= 3 ? 78 : 24 ); QPen glowPen( annotationColor( mDrawingClassId ), points.size() >= 3 ? 7.0 : 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); glowPen.setCosmetic( true ); mDrawGlowItem = mScene->addPath( path, glowPen, Qt::NoBrush ); mDrawGlowItem->setZValue( 19.5 ); QPen pen( annotationColor( mDrawingClassId ).lighter( 145 ), points.size() >= 3 ? 2.2 : 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); pen.setCosmetic( true ); mDrawItem = mScene->addPath( path, pen, QBrush( fill ) ); mDrawItem->setZValue( 20.0 ); for ( int i = 0; i < mDrawingPoints.size(); ++i ) new DrawingPointItem( mDrawingPoints.at( i ), 3.7, annotationColor( mDrawingClassId ), i == mDrawingPoints.size() - 1, mDrawItem ); if ( !closePolygon && mCurrentMousePoint != mDrawingPoints.constLast() ) new DrawingPointItem( mCurrentMousePoint, 4.2, annotationColor( mDrawingClassId ), true, mDrawItem ); }
    QGraphicsScene *mScene = nullptr; QGraphicsPixmapItem *mPixmapItem = nullptr; QHash<int, QGraphicsPolygonItem *> mAnnotationItems; QVector<QGraphicsEllipseItem *> mEditHandles; QPolygonF mEditPolygon; QVector<QGraphicsItem *> mSmartPromptItems; QVector<QPointF> mSmartPromptPoints; QVector<int> mSmartPromptLabels; QImage mImage; QVector<QgsEcoPhotoWorkbench::Annotation> mAnnotations; bool mDrawingEnabled = false; bool mEditEnabled = false; bool mUserAdjustedView = false; int mDrawingClassId = 0; QString mDrawingClassName; QPolygonF mDrawingPoints; QPointF mCurrentMousePoint; QGraphicsPathItem *mDrawGlowItem = nullptr; QGraphicsPathItem *mDrawItem = nullptr; int mEditingAnnotationIndex = -1; int mDraggedEditHandleIndex = -1; bool mDraggingEditHandle = false; bool mDeleteEnabled = false; bool mSmartSegmentationEnabled = false; bool mRightPanning = false; QPoint mRightPanStartPos; int mRightPanStartH = 0; int mRightPanStartV = 0; bool mSpacePanning = false; bool mSpaceDragging = false; QPoint mSpacePanStartPos; int mSpacePanStartH = 0; int mSpacePanStartV = 0; std::function<void( int, const QString &, const QPolygonF & )> mAnnotationCallback; std::function<void( int )> mDeleteCallback; std::function<void()> mDrawingFinishedCallback; std::function<void( const QVector<QPointF> &, const QVector<int> & )> mSmartSegmentationCallback; std::function<void( int, const QPolygonF & )> mAnnotationEditCallback;
};QgsEcoPhotoWorkbench::QgsEcoPhotoWorkbench( MessageCallback messageCallback, QWidget *parent )
  : QWidget( parent )
  , mMessageCallback( std::move( messageCallback ) )
{
  setObjectName( QStringLiteral( "EcoPhotoWorkbench" ) );
  setStyleSheet( QStringLiteral( R"(
    QWidget#EcoPhotoWorkbench { background:#181818; color:#d4d4d4; }
    QWidget#EcoPhotoWorkbench QLabel { color:#d4d4d4; }
    QWidget#EcoPhotoWorkbench QLabel[muted="true"] { color:#8b96a5; }
    QWidget#EcoPhotoWorkbench QLabel[previewCard="true"] { background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #111b2c, stop:1 #0c1420); border:1px solid #2d3d4f; border-left:4px solid #38bdf8; border-radius:12px; padding:10px 12px; color:#e7f2ff; }
    QWidget#EcoPhotoWorkbench QLabel[previewCard="true"][muted="true"] { color:#aab7c7; }
    QWidget#EcoPhotoWorkbench QFrame[photoPanel="true"] { background:#20242d; border:1px solid #303946; border-radius:6px; }
    QWidget#EcoPhotoWorkbench QListWidget { background:#151a22; color:#d4d4d4; border:1px solid #303946; outline:0; }
    QWidget#EcoPhotoWorkbench QListWidget::item { min-height:110px; border:0; padding:6px; background:transparent; }
    QWidget#EcoPhotoWorkbench QListWidget::item:hover { background:transparent; }
    QWidget#EcoPhotoWorkbench QListWidget::item:selected { background:transparent; border:0; }
    QWidget#EcoPhotoWorkbench QComboBox {
      min-height:28px;
      background:#1e1e1e;
      color:#d4d4d4;
      border:1px solid #3c3c3c;
      border-radius:4px;
      padding:2px 8px;
      selection-background-color:#094771;
    }
    QWidget#EcoPhotoWorkbench QComboBox:hover { border-color:#5a5a5a; }
    QWidget#EcoPhotoWorkbench QComboBox::drop-down { border:0; width:20px; }
    QWidget#EcoPhotoWorkbench QComboBox QAbstractItemView {
      background:#252526;
      color:#d4d4d4;
      selection-background-color:#094771;
      selection-color:#ffffff;
      border:1px solid #3c3c3c;
      outline:0;
    }
    QWidget#EcoPhotoWorkbench QPushButton { min-height:27px; color:#dce8f5; background:#26313d; border:1px solid #405061; border-radius:4px; padding:4px 12px; }
    QWidget#EcoPhotoWorkbench QPushButton:hover { background:#304253; border-color:#5b7288; }
    QWidget#EcoPhotoWorkbench QPushButton[primary="true"] { color:#f8fbff; background:#066c9e; border-color:#169bce; font-weight:600; }
    QWidget#EcoPhotoWorkbench QPushButton[primary="true"]:hover { background:#0b7fb5; }
    QWidget#EcoPhotoWorkbench QPushButton:checked { background:#0e639c; border-color:#38bdf8; color:white; }
    QWidget#EcoPhotoWorkbench QPushButton[photoAi="segment"] {
      min-height:27px;

      max-width:none;
      padding:4px 12px;
      color:#effcff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f766e, stop:0.55 #0891b2, stop:1 #2563eb);
      border:1px solid #22d3ee;
      border-radius:4px;
            font-weight:700;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAi="segment"]:hover {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #11998d, stop:0.55 #0ea5e9, stop:1 #3b82f6);
      border-color:#67e8f9;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAi="segment"]:checked {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #155e75, stop:0.5 #0f766e, stop:1 #1d4ed8);
      border-color:#99f6e4;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAi="recognize"] {
      min-height:27px;

      max-width:none;
      padding:4px 12px;
      color:#fdf7ff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6d28d9, stop:0.55 #7c3aed, stop:1 #0ea5e9);
      border:1px solid #a855f7;
      border-radius:4px;
            font-weight:700;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAi="recognize"]:hover {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #7c3aed, stop:0.55 #8b5cf6, stop:1 #38bdf8);
      border-color:#c084fc;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction] {
      min-height:27px;

      max-width:none;
      padding:4px 12px;
      border-radius:4px;
      font-weight:700;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="import"] {
            color:#effcff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0e639c, stop:1 #2563eb);
      border:1px solid #38bdf8;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="import"]:hover {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1177bb, stop:1 #3b82f6);
      border-color:#67e8f9;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="segment"] {
            color:#effcff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0f766e, stop:0.55 #0891b2, stop:1 #2563eb);
      border:1px solid #22d3ee;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="segment"]:hover {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #11998d, stop:0.55 #0ea5e9, stop:1 #3b82f6);
      border-color:#67e8f9;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="recognize"] {
            color:#fdf7ff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6d28d9, stop:0.55 #7c3aed, stop:1 #0ea5e9);
      border:1px solid #a855f7;
    }
    QWidget#EcoPhotoWorkbench QPushButton[photoAction="recognize"]:hover {
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #7c3aed, stop:0.55 #8b5cf6, stop:1 #38bdf8);
      border-color:#c084fc;
    }    QWidget#EcoPhotoWorkbench QProgressBar { min-height:9px; max-height:9px; padding:0; color:#e5eef7; background:#0b1220; border:1px solid #273244; border-radius:999px; text-align:center; font-size:9px; }
    QWidget#EcoPhotoWorkbench QProgressBar::chunk { margin:0; background:qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #22d3ee, stop:0.55 #3b82f6, stop:1 #8b5cf6); border-radius:999px; }
  )" ) );

  QVBoxLayout *layout = new QVBoxLayout( this );
  layout->setContentsMargins( 10, 10, 10, 10 );
  layout->setSpacing( 8 );

  QFrame *contextPanel = new QFrame( this );
  contextPanel->setProperty( "photoPanel", true );
  QVBoxLayout *contextLayout = new QVBoxLayout( contextPanel );
  contextLayout->setContentsMargins( 10, 8, 10, 8 );
  contextLayout->setSpacing( 3 );
      mPhaseLabel = new QLabel( tr( "未选择期次" ), contextPanel );
  mPhaseLabel->setStyleSheet( QStringLiteral( "font-weight:650; color:#e5f6ff;" ) );
      mSummaryLabel = new QLabel( tr( "照片数据按期次独立存放，原始照片保持不变。" ), contextPanel );
  mSummaryLabel->setProperty( "muted", true );
  mSummaryLabel->setWordWrap( true );
  mSummaryLabel->setTextFormat( Qt::RichText );
  mSummaryLabel->setAlignment( Qt::AlignLeft | Qt::AlignVCenter );
  mSummaryLabel->setStyleSheet( QStringLiteral( "background:#0f172a; border:1px solid #263244; border-radius:8px; padding:8px 10px; color:#dce8f5;" ) );
  contextLayout->addWidget( mPhaseLabel );
  contextLayout->addWidget( mSummaryLabel );
  layout->addWidget( contextPanel );

  QHBoxLayout *actions = new QHBoxLayout;
  actions->setSpacing( 6 );
      mImportButton = new QPushButton( QgsApplication::getThemeIcon( QStringLiteral( "/mActionAdd.svg" ) ), tr( "导入照片" ), this );
    mImportButton->setProperty( "primary", true );
  mImportButton->setProperty( "photoAction", "import" );
      mRemoveButton = new QPushButton( tr( "移除所选" ), this );
      mCreateFusionButton = new QPushButton( tr( "生成融合照片" ), this );
      mExportOriginalButton = new QPushButton( tr( "导出原图" ), this );
      mExportFusedButton = new QPushButton( tr( "导出融合图" ), this );
      mExportYoloButton = new QPushButton( tr( "导出 YOLO 样本" ), this );
  actions->addWidget( mImportButton );
  actions->addWidget( mRemoveButton );
  actions->addSpacing( 4 );
  actions->addWidget( mCreateFusionButton );
  actions->addStretch( 1 );
  actions->addWidget( mExportOriginalButton );
  actions->addWidget( mExportFusedButton );
  actions->addWidget( mExportYoloButton );
  layout->addLayout( actions );
  QSplitter *splitter = new QSplitter( Qt::Horizontal );
  splitter->setChildrenCollapsible( false );
  mPhotoList = new QListWidget( splitter );
  mPhotoList->setIconSize( QSize( sPhotoThumbnailExtent, 64 ) );
  mPhotoList->setSelectionMode( QAbstractItemView::ExtendedSelection );
  mPhotoList->setMinimumWidth( 300 );

  QWidget *previewPanel = new QWidget( splitter );
  QVBoxLayout *previewLayout = new QVBoxLayout( previewPanel );
  previewLayout->setContentsMargins( 0, 0, 0, 0 );
  previewLayout->setSpacing( 7 );
  mPreview = new EcoPhotoAnnotationView( previewPanel );
  mPreview->setMinimumHeight( 270 );
  previewLayout->addWidget( mPreview, 1 );

    mMetadataLabel = new QLabel( tr( "未选择照片" ), previewPanel );
  mMetadataLabel->setProperty( "muted", true );
  mMetadataLabel->setProperty( "previewCard", true );
  mMetadataLabel->setWordWrap( true );
  mMetadataLabel->setTextFormat( Qt::RichText );
  mMetadataLabel->setAlignment( Qt::AlignLeft | Qt::AlignTop );
  mMetadataLabel->hide();

  QFrame *annotationPanel = new QFrame( previewPanel );
  annotationPanel->setProperty( "photoPanel", true );
  QHBoxLayout *annotationLayout = new QHBoxLayout( annotationPanel );
  annotationLayout->setContentsMargins( 8, 6, 8, 6 );
  annotationLayout->setSpacing( 6 );
     annotationLayout->addWidget( new QLabel( tr( "图斑类型" ), annotationPanel ) );
  mAnnotationClassCombo = new QComboBox( annotationPanel );
  mAnnotationClassCombo->addItem( classColorIcon( annotationColor( 0 ) ), photoClassName( 0 ), 0 );
  mAnnotationClassCombo->addItem( classColorIcon( annotationColor( 1 ) ), photoClassName( 1 ), 1 );
  mAnnotationClassCombo->setIconSize( QSize( 14, 14 ) );
  annotationLayout->addWidget( mAnnotationClassCombo );
     mManualDrawButton = new QPushButton( tr( "标绘图斑" ), annotationPanel );
            mPhotoSmartSegmentationButton = new QPushButton( QgsApplication::getThemeIcon( QStringLiteral( "/mActionVertexTool.svg" ) ), tr( "智能分割" ), annotationPanel );
    mPhotoSmartSegmentationButton->setProperty( "photoAi", "segment" );
  mPhotoSmartSegmentationButton->setProperty( "photoAction", "segment" );
  mPhotoSmartSegmentationButton->setIconSize( QSize( 16, 16 ) );
  mPhotoSmartSegmentationButton->setCheckable( true );
     mPhotoSmartSegmentationButton->setToolTip( tr( "左键添加红点，右键添加蓝点，按空格可拖动图片。" ) );
  mManualDrawButton->setCheckable( true );
   mManualDrawButton->setToolTip( tr( "左键添加点，Ctrl+Z 撤销上一个点，双击完成。" ) );
        mEditAnnotationButton = new QPushButton( tr( "编辑结果" ), annotationPanel );
  mEditAnnotationButton->setCheckable( true );
     mEditAnnotationButton->setToolTip( tr( "点击图斑后可拖动顶点编辑，双击边可新增点，右键顶点可删除点。" ) );
        mDeleteAnnotationButton = new QPushButton( tr( "删除标绘" ), annotationPanel );
  mDeleteAnnotationButton->setCheckable( true );
        mDeleteAnnotationButton->setToolTip( tr( "点击后在图片上单击图斑即可删除。" ) );
  annotationLayout->addWidget( mManualDrawButton );
  annotationLayout->addWidget( mDeleteAnnotationButton );
  annotationLayout->addWidget( mEditAnnotationButton );
  annotationLayout->addStretch( 1 );
        mPhotoRecognitionButton = new QPushButton( QgsApplication::getThemeIcon( QStringLiteral( "/mActionIdentify.svg" ) ), tr( "照片智能识别" ), annotationPanel );
    mPhotoRecognitionButton->setProperty( "primary", true );
  mPhotoRecognitionButton->setProperty( "photoAction", "recognize" );
    mPhotoRecognitionButton->setProperty( "photoAi", "recognize" );
  mPhotoRecognitionButton->setIconSize( QSize( 16, 16 ) );

    mPhotoRecognitionButton->setToolTip( tr( "批量识别当前期次勾选的照片。" ) );
  const auto widenPhotoActionButton = []( QPushButton *button ) {
    if ( !button )
      return;
    const int textWidth = button->fontMetrics().horizontalAdvance( button->text() );
    const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
    const int horizontalPadding = 38;
    button->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Fixed );
    button->setMinimumWidth( textWidth + iconWidth + horizontalPadding );
    button->setMaximumWidth( QWIDGETSIZE_MAX );
  };
  widenPhotoActionButton( mImportButton );
  widenPhotoActionButton( mPhotoSmartSegmentationButton );
  widenPhotoActionButton( mPhotoRecognitionButton );
  annotationLayout->addWidget( mPhotoSmartSegmentationButton );
  annotationLayout->addWidget( mPhotoRecognitionButton );
  previewLayout->addWidget( annotationPanel );

  splitter->addWidget( mPhotoList );
  splitter->addWidget( previewPanel );
  splitter->setStretchFactor( 0, 0 );
  splitter->setStretchFactor( 1, 1 );
  splitter->setSizes( { 300, 540 } );

  QWidget *originalTab = new QWidget( this );
  QVBoxLayout *originalLayout = new QVBoxLayout( originalTab );
  originalLayout->setContentsMargins( 0, 0, 0, 0 );
  originalLayout->addWidget( splitter, 1 );

  QWidget *fusedTab = new QWidget( this );
  QVBoxLayout *fusedTabLayout = new QVBoxLayout( fusedTab );
  fusedTabLayout->setContentsMargins( 0, 0, 0, 0 );
  fusedTabLayout->setSpacing( 7 );
  QSplitter *fusedSplitter = new QSplitter( Qt::Horizontal, fusedTab );
  fusedSplitter->setChildrenCollapsible( false );
  mFusedList = new QListWidget( fusedSplitter );
  mFusedList->setIconSize( QSize( sPhotoThumbnailExtent, 64 ) );
  mFusedList->setSelectionMode( QAbstractItemView::SingleSelection );
  mFusedList->setMinimumWidth( 300 );

  QWidget *fusedPreviewPanel = new QWidget( fusedSplitter );
  QVBoxLayout *fusedPreviewLayout = new QVBoxLayout( fusedPreviewPanel );
  fusedPreviewLayout->setContentsMargins( 0, 0, 0, 0 );
  fusedPreviewLayout->setSpacing( 7 );
  mFusedPreview = new EcoPhotoAnnotationView( fusedPreviewPanel );
  mFusedPreview->setMinimumHeight( 270 );
  fusedPreviewLayout->addWidget( mFusedPreview, 1 );
  mFusedMetadataLabel = new QLabel( tr( "融合结果预览" ), fusedPreviewPanel );
  mFusedMetadataLabel->setProperty( "muted", true );
  mFusedMetadataLabel->setProperty( "previewCard", true );
  mFusedMetadataLabel->setWordWrap( true );
  mFusedMetadataLabel->setTextFormat( Qt::RichText );
  mFusedMetadataLabel->setAlignment( Qt::AlignLeft | Qt::AlignTop );
  fusedPreviewLayout->addWidget( mFusedMetadataLabel );
  fusedSplitter->addWidget( mFusedList );
  fusedSplitter->addWidget( fusedPreviewPanel );
  fusedSplitter->setStretchFactor( 0, 0 );
  fusedSplitter->setStretchFactor( 1, 1 );
  fusedSplitter->setSizes( { 300, 540 } );
  fusedTabLayout->addWidget( fusedSplitter, 1 );

  mPhotoTabs = new QTabWidget( this );
  mPhotoTabs->setDocumentMode( true );
  mPhotoTabs->setElideMode( Qt::ElideNone );
  mPhotoTabs->setUsesScrollButtons( false );
  mPhotoTabs->tabBar()->setExpanding( true );
  mPhotoTabs->setStyleSheet( QStringLiteral( R"(
    QTabWidget::pane {
      background:#0f1622;
      border:1px solid #284055;
      border-radius:12px;
      top:-1px;
    }
    QTabBar {
      qproperty-drawBase:0;
    }
    QTabBar::tab {

      padding:6px 10px;
      margin-right:6px;
      color:#9fb0c1;
      background:#141b28;
      border:1px solid #284055;
      border-bottom:none;
      border-top-left-radius:10px;
      border-top-right-radius:10px;
      font-weight:600;
    }
    QTabBar::tab:hover {
      color:#ffffff;
      background:#1f2b3b;
    }
    QTabBar::tab:selected {
      color:#ffffff;
      background:qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0e639c, stop:1 #1d4ed8);
      border-color:#38bdf8;
    }
  )" ) );
            mPhotoTabs->addTab( originalTab, tr( "原图列表" ) );
            mPhotoTabs->addTab( fusedTab, tr( "融合结果" ) );
  layout->addWidget( mPhotoTabs, 1 );

  const auto syncPhotoTabBarWidth = [this, splitter, fusedSplitter]() {
    if ( !mPhotoTabs )
      return;
    QSplitter *activeSplitter = mPhotoTabs->currentIndex() == 1 ? fusedSplitter : splitter;
    const int listWidth = activeSplitter ? activeSplitter->sizes().value( 0, 0 ) : 0;
    if ( listWidth > 0 )
      mPhotoTabs->tabBar()->setFixedWidth( listWidth );
  };
  connect( splitter, &QSplitter::splitterMoved, this, [syncPhotoTabBarWidth]( int, int ) { syncPhotoTabBarWidth(); } );
  connect( fusedSplitter, &QSplitter::splitterMoved, this, [syncPhotoTabBarWidth]( int, int ) { syncPhotoTabBarWidth(); } );
  connect( mPhotoTabs, &QTabWidget::currentChanged, this, [syncPhotoTabBarWidth]( int ) { syncPhotoTabBarWidth(); } );
  QTimer::singleShot( 0, this, syncPhotoTabBarWidth );
  mProgressBar = new QProgressBar( this );
  mProgressBar->setVisible( false );
  mProgressBar->setTextVisible( true );
    mStatusLabel = new QLabel( tr( "就绪" ), this );
  mStatusLabel->setProperty( "muted", true );
  layout->addWidget( mProgressBar );
  layout->addWidget( mStatusLabel );

  connect( mImportButton, &QPushButton::clicked, this, [this] { importPhotos(); } );
  connect( mRemoveButton, &QPushButton::clicked, this, [this] { removeSelectedPhotos(); } );
  connect( mCreateFusionButton, &QPushButton::clicked, this, [this] { createFusedPhotos(); } );
  connect( mPhotoRecognitionButton, &QPushButton::clicked, this, [this] { batchRecognizePhotos(); } );
  connect( mExportOriginalButton, &QPushButton::clicked, this, [this] { exportOriginalPhotos(); } );
  connect( mExportFusedButton, &QPushButton::clicked, this, [this] { exportFusedPhotos(); } );
  connect( mExportYoloButton, &QPushButton::clicked, this, [this] { exportYoloSamples(); } );
  connect( mFusedList, &QListWidget::currentRowChanged, this, [this]( int ) { showSelectedFusedPhoto(); } );
  connect( mPhotoList, &QListWidget::itemSelectionChanged, this, [this] { syncListEntrySelectionState( mPhotoList ); } );
  connect( mFusedList, &QListWidget::itemSelectionChanged, this, [this] { syncListEntrySelectionState( mFusedList ); } );
  connect( mPhotoList, &QListWidget::currentItemChanged, this, [this]( QListWidgetItem *current, QListWidgetItem *previous ) {
    if ( previous && mPreview && mPreview->hasUnfinishedDrawing() )
    {
      const int targetIndex = current ? current->data( Qt::UserRole ).toInt() : -1;
      {
        const QSignalBlocker blocker( mPhotoList );
        mPhotoList->setCurrentItem( previous );
      }

        QMessageBox prompt( QMessageBox::Warning, tr( "提示" ), tr( "当前标绘尚未完成，是否保存后切换照片？" ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this );




      prompt.setDefaultButton( QMessageBox::Save );
      const int choice = prompt.exec();
      if ( choice == QMessageBox::Save )
      {
        if ( !mPreview->finishCurrentDrawing() )
        {
            QMessageBox::warning( this, tr( "提示" ), tr( "当前标绘未完成，请先保存或放弃当前标绘。" ) );


          return;
        }
      }
      else if ( choice == QMessageBox::Discard )
      {
        mPreview->discardCurrentDrawing();
      }
      else
      {
        return;
      }

      if ( targetIndex >= 0 && targetIndex < mPhotoList->count() )
        mPhotoList->setCurrentRow( targetIndex );
      return;
    }
    showPhotoAt( current ? current->data( Qt::UserRole ).toInt() : -1 );
    syncListEntrySelectionState( mPhotoList );
  } );
  connect( mManualDrawButton, &QPushButton::toggled, this, [this]( bool enabled ) { setManualDrawingEnabled( enabled ); } );
  connect( mPhotoSmartSegmentationButton, &QPushButton::toggled, this, [this]( bool enabled ) { setPhotoSmartSegmentationEnabled( enabled ); } );
  connect( mDeleteAnnotationButton, &QPushButton::toggled, this, [this]( bool enabled ) { setDeleteAnnotationEnabled( enabled ); } );
  connect( mEditAnnotationButton, &QPushButton::toggled, this, [this]( bool enabled ) { setAnnotationEditingEnabled( enabled ); } );
  connect( mAnnotationClassCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this] {
    if ( mManualDrawButton && mManualDrawButton->isChecked() )
      setManualDrawingEnabled( true );
  } );
  mPreview->setAnnotationCallback( [this]( int classId, const QString &className, const QPolygonF &polygon ) { addAnnotation( classId, className, polygon ); } );
  mPreview->setDeleteCallback( [this]( int annotationIndex ) { deleteAnnotationAt( annotationIndex ); } );
  mPreview->setAnnotationEditCallback( [this]( int annotationIndex, const QPolygonF &polygon ) { updateAnnotation( annotationIndex, polygon ); } );
  mPreview->setSmartSegmentationCallback( [this]( const QVector<QPointF> &points, const QVector<int> &labels ) { runPhotoSmartSegmentation( points, labels ); } );
  mPreview->setDrawingFinishedCallback( [this] {
    if ( mManualDrawButton )
    {
      const QSignalBlocker blocker( mManualDrawButton );
      mManualDrawButton->setChecked( false );
    }
    setManualDrawingEnabled( false );
  } );
  setBusy( false );
}
void QgsEcoPhotoWorkbench::setPhaseContext( const QString &phaseId, const QString &phaseName, const QString &workspacePath )
{
  const bool changed = mPhaseId != phaseId || mWorkspacePath != workspacePath;
  if ( mBusy && changed )
  {
    mPendingPhaseContext = true;
    mPendingPhaseId = phaseId;
    mPendingPhaseName = phaseName;
    mPendingWorkspacePath = workspacePath;
      notify( tr( "期次切换" ), tr( "当前期次已切换，正在重新加载照片索引。" ), true );
    return;
  }

  mPendingPhaseContext = false;
  mPendingPhaseId.clear();
  mPendingPhaseName.clear();
  mPendingWorkspacePath.clear();
  mPhaseId = phaseId;
  mPhaseName = phaseName;
  mWorkspacePath = workspacePath;
  if ( changed )
    ++mContextGeneration;

  if ( mPhaseLabel )
     mPhaseLabel->setText( phaseId.isEmpty() ? tr( "未选择期次" ) : tr( "期次：%1" ).arg( phaseName ) );

  if ( changed )
    reloadIndex();
  else
    updateSummary();

  if ( !mBusy )
    setBusy( false );
}

void QgsEcoPhotoWorkbench::reloadIndex()
{
  mPhotos.clear();
  mCheckedPhotoPaths.clear();
  ++mThumbnailGeneration;
  if ( mPhotoList )
    mPhotoList->clear();
  if ( mFusedList )
    mFusedList->clear();
  mDeleteMode = false;
  if ( mPreview )
  {
    mPreview->setDrawingEnabled( false, 0, QString() );
    mPreview->setDeleteEnabled( false );
    mPreview->setEditEnabled( false );
  }
  if ( mManualDrawButton )
  {
    const QSignalBlocker blocker( mManualDrawButton );
    mManualDrawButton->setChecked( false );
  }
  if ( mDeleteAnnotationButton )
  {
    const QSignalBlocker blocker( mDeleteAnnotationButton );
    mDeleteAnnotationButton->setChecked( false );
  }
  if ( mEditAnnotationButton )
  {
    const QSignalBlocker blocker( mEditAnnotationButton );
    mEditAnnotationButton->setChecked( false );
  }
  if ( mPhotoSmartSegmentationButton )
  {
    const QSignalBlocker blocker( mPhotoSmartSegmentationButton );
    mPhotoSmartSegmentationButton->setChecked( false );
  }
  if ( mPreview )
  {
    mPreview->setSmartSegmentationEnabled( false );
    mPreview->setImage( QImage(), {} );
  }
  if ( mMetadataLabel )
      mMetadataLabel->setText( tr( "当前期次暂无照片信息。" ) );
  if ( mFusedPreview )
    mFusedPreview->setImage( QImage(), {} );
  if ( mFusedMetadataLabel )
      mFusedMetadataLabel->setText( tr( "请选择一张融合照片查看详情。" ) );

  if ( mWorkspacePath.isEmpty() )
  {
    updateSummary();
    return;
  }

  QFile indexFile( photoIndexFilePath( mWorkspacePath ) );
  if ( indexFile.exists() && indexFile.open( QIODevice::ReadOnly ) )
  {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson( indexFile.readAll(), &parseError );
    if ( parseError.error == QJsonParseError::NoError && document.isObject() )
    {
      const QJsonObject root = document.object();
      const QString indexedPhaseId = root.value( QStringLiteral( "phaseId" ) ).toString();
      if ( !indexedPhaseId.isEmpty() && indexedPhaseId != mPhaseId )
      {
         notify( tr( "索引已过期" ), tr( "检测到照片索引的期次已变化，已忽略旧结果。" ), true );
      }
      else
      {
        const QJsonArray photos = root.value( QStringLiteral( "photos" ) ).toArray();
        for ( const QJsonValue &value : photos )
        {
          const QJsonObject object = value.toObject();
          PhotoRecord record;
          record.sourcePath = object.value( QStringLiteral( "sourcePath" ) ).toString();
          record.fusedPath = object.value( QStringLiteral( "fusedPath" ) ).toString();
          const QJsonArray annotations = object.value( QStringLiteral( "annotations" ) ).toArray();
          for ( const QJsonValue &annotationValue : annotations )
          {
            const QJsonObject annotationObject = annotationValue.toObject();
            Annotation annotation;
            annotation.classId = annotationObject.value( QStringLiteral( "classId" ) ).toInt();
            annotation.className = annotationObject.value( QStringLiteral( "className" ) ).toString( photoClassName( annotation.classId ) );
            annotation.source = annotationObject.value( QStringLiteral( "source" ) ).toString( QStringLiteral( "manual" ) );
            const QJsonArray polygonArray = annotationObject.value( QStringLiteral( "polygon" ) ).toArray();
            for ( const QJsonValue &pointValue : polygonArray )
            {
              const QJsonObject pointObject = pointValue.toObject();
              if ( pointObject.contains( QStringLiteral( "x" ) ) && pointObject.contains( QStringLiteral( "y" ) ) )
              {
                annotation.normalizedPolygon.append( QPointF( pointObject.value( QStringLiteral( "x" ) ).toDouble(),
                                                              pointObject.value( QStringLiteral( "y" ) ).toDouble() ) );
              }
            }
            if ( annotation.normalizedPolygon.size() < 3 )
            {
              annotation.normalizedPolygon = normalizedRectanglePolygon( QRectF( annotationObject.value( QStringLiteral( "x" ) ).toDouble(),
                                                                                 annotationObject.value( QStringLiteral( "y" ) ).toDouble(),
                                                                                 annotationObject.value( QStringLiteral( "width" ) ).toDouble(),
                                                                                 annotationObject.value( QStringLiteral( "height" ) ).toDouble() ) );
            }
            annotation.normalizedPolygon = clampNormalizedPolygon( annotation.normalizedPolygon );
            const QRectF bounds = normalizedBoundingRect( annotation.normalizedPolygon );
            if ( annotation.normalizedPolygon.size() >= 3 && bounds.width() > 0.0 && bounds.height() > 0.0 )
              record.annotations.append( annotation );
          }
          if ( !record.sourcePath.isEmpty() || !record.fusedPath.isEmpty() )
            mPhotos.append( record );
        }
      }
    }
  }
  rebuildPhotoList();
  rebuildFusedList();
  updateSummary();
}

void QgsEcoPhotoWorkbench::saveIndex() const
{
  if ( mWorkspacePath.isEmpty() )
    return;

  const QString photosDirectory = QDir( mWorkspacePath ).filePath( QStringLiteral( "photos" ) );
  if ( !ensureDirectory( photosDirectory ) )
    return;

  QJsonArray photosArray;
  for ( const PhotoRecord &record : mPhotos )
  {
    QJsonObject photoObject;
    photoObject.insert( QStringLiteral( "sourcePath" ), record.sourcePath );
    photoObject.insert( QStringLiteral( "fusedPath" ), record.fusedPath );
    QJsonArray annotationsArray;
    for ( const Annotation &annotation : record.annotations )
    {
      QJsonObject annotationObject;
      annotationObject.insert( QStringLiteral( "classId" ), annotation.classId );
      annotationObject.insert( QStringLiteral( "className" ), annotation.className );
      annotationObject.insert( QStringLiteral( "source" ), annotation.source.isEmpty() ? QStringLiteral( "manual" ) : annotation.source );
      QJsonArray polygonArray;
      const QPolygonF polygon = clampNormalizedPolygon( annotation.normalizedPolygon );
      for ( const QPointF &point : polygon )
      {
        QJsonObject pointObject;
        pointObject.insert( QStringLiteral( "x" ), point.x() );
        pointObject.insert( QStringLiteral( "y" ), point.y() );
        polygonArray.append( pointObject );
      }
      const QRectF bounds = normalizedBoundingRect( polygon );
      annotationObject.insert( QStringLiteral( "polygon" ), polygonArray );
      annotationObject.insert( QStringLiteral( "x" ), bounds.x() );
      annotationObject.insert( QStringLiteral( "y" ), bounds.y() );
      annotationObject.insert( QStringLiteral( "width" ), bounds.width() );
      annotationObject.insert( QStringLiteral( "height" ), bounds.height() );
      annotationsArray.append( annotationObject );
    }
    photoObject.insert( QStringLiteral( "annotations" ), annotationsArray );
    photosArray.append( photoObject );
  }

  QJsonObject root;
  root.insert( QStringLiteral( "schemaVersion" ), 2 );
  root.insert( QStringLiteral( "phaseId" ), mPhaseId );
  root.insert( QStringLiteral( "updatedAt" ), QDateTime::currentDateTime().toString( Qt::ISODate ) );
  root.insert( QStringLiteral( "photos" ), photosArray );

  QSaveFile file( photoIndexFilePath( mWorkspacePath ) );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return;
  const QByteArray serialized = QJsonDocument( root ).toJson( QJsonDocument::Indented );
  if ( file.write( serialized ) != serialized.size() )
    return;
  file.commit();
}

void QgsEcoPhotoWorkbench::scheduleIndexSave()
{
  if ( mIndexSavePending )
    return;
  mIndexSavePending = true;
  QTimer::singleShot( 0, this, [this] {
    mIndexSavePending = false;
    saveIndex();
  } );
}

void QgsEcoPhotoWorkbench::rebuildPhotoList()
{
  if ( !mPhotoList )
    return;

  ++mThumbnailGeneration;
  const int previousIndex = mPhotoList->currentRow();
  const QSignalBlocker blocker( mPhotoList );
  mPhotoList->clear();
  for ( int index = 0; index < mPhotos.size(); ++index )
  {
    QListWidgetItem *item = new QListWidgetItem();
    item->setData( Qt::UserRole, index );
    item->setSizeHint( QSize( 300, 132 ) );
    mPhotoList->addItem( item );
    auto *entry = new PhotoListEntryWidget( mPhotoList, true );
    entry->onClicked( [this, index] {
      if ( mPhotoList && index >= 0 && index < mPhotoList->count() )
        mPhotoList->setCurrentRow( index );
    } );
    entry->onCheckedChanged( [this, index]( bool checked ) {
      if ( index < 0 || index >= mPhotos.size() )
        return;
      const QString key = photoSelectionKey( mPhotos.at( index ) );
      if ( checked )
        mCheckedPhotoPaths.insert( key );
      else
        mCheckedPhotoPaths.remove( key );
    } );
    entry->setChecked( mCheckedPhotoPaths.contains( photoSelectionKey( mPhotos.at( index ) ) ) );
    mPhotoList->setItemWidget( item, entry );
    updatePhotoListItem( index, mPhotos.size() <= 60 );
  }
  if ( !mPhotos.isEmpty() )
    mPhotoList->setCurrentRow( std::clamp( previousIndex, 0, static_cast<int>( mPhotos.size() ) - 1 ) );
  syncListEntrySelectionState( mPhotoList );

  if ( mPhotos.size() > 60 )
    scheduleThumbnailLoading();
  showSelectedPhoto();
  rebuildFusedList();
}
void QgsEcoPhotoWorkbench::scheduleThumbnailLoading()
{
  if ( !mPhotoList || mPhotos.isEmpty() )
    return;

  const quint64 generation = mThumbnailGeneration;
  auto loadBatch = std::make_shared<std::function<void( int )>>();
  *loadBatch = [this, generation, loadBatch]( int startIndex ) {
    if ( !mPhotoList || generation != mThumbnailGeneration )
      return;

    const int endIndex = std::min( startIndex + 12, static_cast<int>( mPhotos.size() ) );
    for ( int index = startIndex; index < endIndex; ++index )
      updatePhotoListItem( index, true );

    if ( endIndex < mPhotos.size() )
      QTimer::singleShot( 0, this, [loadBatch, endIndex] { ( *loadBatch )( endIndex ); } );
  };
  QTimer::singleShot( 0, this, [loadBatch] { ( *loadBatch )( 0 ); } );
}

void QgsEcoPhotoWorkbench::showSelectedPhoto()
{
  if ( !mPhotoList )
    return;
  showPhotoAt( mPhotoList->currentRow() );
}

void QgsEcoPhotoWorkbench::showSelectedFusedPhoto()
{
  if ( !mFusedList || !mFusedPreview )
    return;
  QListWidgetItem *item = mFusedList->currentItem();
  const int index = item ? item->data( Qt::UserRole ).toInt() : -1;
  if ( index < 0 || index >= mPhotos.size() || mPhotos.at( index ).fusedPath.isEmpty() )
  {
    mFusedPreview->setImage( QImage(), {} );
    if ( mFusedMetadataLabel )
      mFusedMetadataLabel->setText( tr( "当前没有可显示的融合照片。" ) );
    return;
  }

  const PhotoRecord &record = mPhotos.at( index );
  const QImage image = readPreviewImage( record.fusedPath );
  mFusedPreview->setImage( image, {} );
  if ( mFusedMetadataLabel )
    mFusedMetadataLabel->setText( photoPreviewInfoHtml( record.fusedPath, image, record ) );
}

void QgsEcoPhotoWorkbench::showPhotoAt( int index )
{
  if ( mEditAnnotationButton && mEditAnnotationButton->isChecked() )
  {
    const QSignalBlocker blocker( mEditAnnotationButton );
    mEditAnnotationButton->setChecked( false );
    setAnnotationEditingEnabled( false );
  }

  if ( !mPreview || index < 0 || index >= mPhotos.size() )
  {
    if ( mPreview )
      mPreview->setImage( QImage(), {} );
    if ( mMetadataLabel )
      mMetadataLabel->setText( tr( "未选择照片" ) );
    return;
  }

  const PhotoRecord &record = mPhotos.at( index );
  const QString source = photoSourcePath( record );
  const QImage image = readPreviewImage( source );
  mPreview->setImage( image, record.annotations );
  if ( mMetadataLabel )
    mMetadataLabel->setText( photoPreviewInfoHtml( source, image, record ) );
}
void QgsEcoPhotoWorkbench::updatePhotoListItem( int index, bool loadThumbnail )
{
  if ( !mPhotoList || index < 0 || index >= mPhotos.size() )
    return;
  QListWidgetItem *item = mPhotoList->item( index );
  if ( !item )
    return;

  PhotoListEntryWidget *entry = dynamic_cast<PhotoListEntryWidget *>( mPhotoList->itemWidget( item ) );
  if ( !entry )
  {
    entry = new PhotoListEntryWidget( mPhotoList, true );
    entry->onCheckedChanged( [this, index]( bool checked ) {
      if ( index < 0 || index >= mPhotos.size() )
        return;
      const QString key = photoSelectionKey( mPhotos.at( index ) );
      if ( checked )
        mCheckedPhotoPaths.insert( key );
      else
        mCheckedPhotoPaths.remove( key );
    } );
    entry->onClicked( [this, index] {
      if ( mPhotoList && index >= 0 && index < mPhotoList->count() )
        mPhotoList->setCurrentRow( index );
    } );
    mPhotoList->setItemWidget( item, entry );
  }

  const PhotoRecord &record = mPhotos.at( index );
  const PhotoAnnotationStats stats = photoAnnotationStats( record );
  const QString source = photoSourcePath( record );
  const QFileInfo info( source );
  QPixmap thumbnail( sPhotoThumbnailExtent, 64 );
  thumbnail.fill( QColor( QStringLiteral( "#253142" ) ) );
  if ( loadThumbnail )
  {
    const QImage thumbnailImage = readPreviewImage( source, QSize( sPhotoThumbnailExtent, 64 ) );
    if ( !thumbnailImage.isNull() )
      thumbnail = QPixmap::fromImage( thumbnailImage );
  }
  entry->setThumbnail( thumbnail );
  entry->setName( info.fileName() );
  entry->setSubtitle( QString() );
  entry->setStateHtml( QString() );
  entry->setAccentColor( photoCardAccentColor( record ) );
  entry->setInfo( photoItemInfoHtml( record ) );
  entry->setChecked( mCheckedPhotoPaths.contains( photoSelectionKey( record ) ) );
  entry->setSelectedState( item->isSelected() || item == mPhotoList->currentItem() );
  item->setToolTip( record.sourcePath );
}
void QgsEcoPhotoWorkbench::rebuildFusedList()
{
  if ( !mFusedList )
    return;

  const QSignalBlocker blocker( mFusedList );
  const int previousIndex = mFusedList->currentRow();
  mFusedList->clear();
  int firstRow = -1;
  int row = 0;
  for ( int index = 0; index < mPhotos.size(); ++index )
  {
    if ( mPhotos.at( index ).fusedPath.isEmpty() || !QFileInfo::exists( mPhotos.at( index ).fusedPath ) )
      continue;
    if ( firstRow < 0 )
      firstRow = row;
    QListWidgetItem *item = new QListWidgetItem();
    item->setData( Qt::UserRole, index );
    item->setSizeHint( QSize( 300, 132 ) );
    mFusedList->addItem( item );
    auto *entry = new PhotoListEntryWidget( mFusedList, false );
    entry->setCheckable( false );
    entry->onClicked( [this, index] {
      if ( !mFusedList )
        return;
      for ( int candidateRow = 0; candidateRow < mFusedList->count(); ++candidateRow )
      {
        QListWidgetItem *candidate = mFusedList->item( candidateRow );
        if ( candidate && candidate->data( Qt::UserRole ).toInt() == index )
        {
          mFusedList->setCurrentRow( candidateRow );
          break;
        }
      }
    } );
    mFusedList->setItemWidget( item, entry );
    updateFusedListItem( index, mPhotos.size() <= 60 );
    ++row;
  }
  if ( mFusedList->count() > 0 )
    mFusedList->setCurrentRow( previousIndex >= 0 && previousIndex < mFusedList->count() ? previousIndex : firstRow );
  else if ( mFusedPreview )
    mFusedPreview->setImage( QImage(), {} );
  syncListEntrySelectionState( mFusedList );
  showSelectedFusedPhoto();
}
void QgsEcoPhotoWorkbench::updateFusedListItem( int index, bool loadThumbnail )
{
  if ( !mFusedList || index < 0 || index >= mPhotos.size() )
    return;
  QListWidgetItem *item = nullptr;
  for ( int row = 0; row < mFusedList->count(); ++row )
  {
    QListWidgetItem *candidate = mFusedList->item( row );
    if ( candidate && candidate->data( Qt::UserRole ).toInt() == index )
    {
      item = candidate;
      break;
    }
  }
  if ( !item )
    return;

  auto *entry = dynamic_cast<PhotoListEntryWidget *>( mFusedList->itemWidget( item ) );
  if ( !entry )
    return;
  const PhotoRecord &record = mPhotos.at( index );
  const PhotoAnnotationStats stats = photoAnnotationStats( record );
  const QFileInfo info( record.fusedPath );
  QPixmap thumbnail( sPhotoThumbnailExtent, 64 );
  thumbnail.fill( QColor( QStringLiteral( "#253142" ) ) );
  if ( loadThumbnail )
  {
    const QImage thumbnailImage = readPreviewImage( record.fusedPath, QSize( sPhotoThumbnailExtent, 64 ) );
    if ( !thumbnailImage.isNull() )
      thumbnail = QPixmap::fromImage( thumbnailImage );
  }
  entry->setThumbnail( thumbnail );
  entry->setName( info.fileName() );
  entry->setSubtitle( QString() );
  entry->setStateHtml( QString() );
  entry->setAccentColor( photoCardAccentColor( record ) );
  entry->setInfo( photoItemInfoHtml( record ) );
  entry->setSelectedState( item->isSelected() || item == mFusedList->currentItem() );
}
void QgsEcoPhotoWorkbench::updateSummary()
{
  if ( !mSummaryLabel )
    return;

  int annotationCount = 0;
  int autoCount = 0;
  int manualCount = 0;
  int fusionCount = 0;
  for ( const PhotoRecord &record : std::as_const( mPhotos ) )
  {
    const PhotoAnnotationStats stats = photoAnnotationStats( record );
    annotationCount += stats.total;
    autoCount += stats.autoCount;
    manualCount += stats.manualCount;
    if ( !record.fusedPath.isEmpty() && QFileInfo::exists( record.fusedPath ) )
      ++fusionCount;
  }

  mSummaryLabel->setText( photoSummaryHtml( mPhaseName, mPhotos.size(), annotationCount, autoCount, manualCount, fusionCount ) );
}
void QgsEcoPhotoWorkbench::setBusy( bool busy, const QString &status )
{
  mBusy = busy;
  const bool controlsEnabled = !busy && !mPhaseId.isEmpty();
  const QList<QPushButton *> buttons = { mImportButton, mRemoveButton, mManualDrawButton, mPhotoSmartSegmentationButton, mPhotoRecognitionButton, mDeleteAnnotationButton,
                                         mCreateFusionButton, mExportOriginalButton, mExportFusedButton, mExportYoloButton };
  for ( QPushButton *button : buttons )
  {
    if ( button )
      button->setEnabled( controlsEnabled );
  }
  if ( mAnnotationClassCombo )
    mAnnotationClassCombo->setEnabled( controlsEnabled );
  if ( mPhotoList )
    mPhotoList->setEnabled( !busy );
  if ( mFusedList )
    mFusedList->setEnabled( !busy );
  if ( mStatusLabel && !status.isEmpty() )
    mStatusLabel->setText( status );
  if ( mProgressBar )
  {
    if ( busy )
    {
      mProgressBar->setVisible( true );
      mProgressBar->setRange( 0, 0 );
      mProgressBar->setFormat( tr( "后台处理中..." ) );
    }
    else
    {
      mProgressBar->setVisible( false );
      mProgressBar->setRange( 0, 100 );
      mProgressBar->setValue( 0 );
      mProgressBar->setFormat( QStringLiteral( "%p%" ) );
    }
  }

  if ( !busy && mProgressAnimation )
  {
    mProgressAnimation->stop();
    mProgressAnimation->deleteLater();
    mProgressAnimation.clear();
  }
  if ( !busy && mPendingPhaseContext )
  {
    const QString phaseId = mPendingPhaseId;
    const QString phaseName = mPendingPhaseName;
    const QString workspacePath = mPendingWorkspacePath;
    mPendingPhaseContext = false;
    mPendingPhaseId.clear();
    mPendingPhaseName.clear();
    mPendingWorkspacePath.clear();
    QTimer::singleShot( 0, this, [this, phaseId, phaseName, workspacePath] {
      if ( !mBusy )
        setPhaseContext( phaseId, phaseName, workspacePath );
    } );
  }
}

void QgsEcoPhotoWorkbench::setTaskProgress( int current, int total, const QString &status )
{
  if ( !mProgressBar )
    return;

  const int maximum = std::max( 1, total );
  const int target = std::clamp( current, 0, maximum );
  mProgressBar->setVisible( true );
  mProgressBar->setRange( 0, maximum );
  mProgressBar->setFormat( maximum <= 100 ? QStringLiteral( "%p%" ) : tr( "%1 / %2" ).arg( target ).arg( maximum ) );

  if ( mProgressAnimation )
  {
    mProgressAnimation->stop();
    mProgressAnimation->deleteLater();
    mProgressAnimation.clear();
  }

  const int startValue = std::clamp( mProgressBar->value(), 0, maximum );
  auto *animation = new QVariantAnimation( this );
  animation->setStartValue( startValue );
  animation->setEndValue( target );
  animation->setDuration( std::clamp( 60 + std::abs( target - startValue ) * 6, 70, 180 ) );
  animation->setEasingCurve( QEasingCurve::OutCubic );
  connect( animation, &QVariantAnimation::valueChanged, this, [this, maximum]( const QVariant &value ) {
    if ( mProgressBar )
      mProgressBar->setValue( std::clamp( value.toInt(), 0, maximum ) );
  } );
  connect( animation, &QVariantAnimation::finished, this, [this, animation, target, maximum] {
    if ( mProgressBar )
      mProgressBar->setValue( std::clamp( target, 0, maximum ) );
    if ( mProgressAnimation == animation )
      mProgressAnimation.clear();
    animation->deleteLater();
  } );
  mProgressAnimation = animation;
  animation->start();

  if ( mStatusLabel && !status.isEmpty() )
    mStatusLabel->setText( status );
}
void QgsEcoPhotoWorkbench::notify( const QString &title, const QString &message, bool warning ) const
{
  if ( mMessageCallback )
    mMessageCallback( title, message, warning );
}

void QgsEcoPhotoWorkbench::importPhotos()
{
  if ( mBusy || mPhaseId.isEmpty() || mWorkspacePath.isEmpty() )
  {
    if ( mStatusLabel )
       mStatusLabel->setText( tr( "请先选择期次并打开工作区。" ) );
    notify( tr( "导入照片" ), tr( "当前状态不可导入照片，请先选择期次并打开工作区。" ), true );
    return;
  }

  const QString defaultDirectory = QDir( mWorkspacePath ).filePath( QStringLiteral( "photos" ) );
  QDir().mkpath( defaultDirectory );
  QFileDialog dialog( dialogParentFor( this ), tr( "导入照片" ), defaultDirectory, tr( "Photos (*.jpg *.jpeg *.png *.bmp *.tif *.tiff);;All files (*.*)" ) );
  dialog.setWindowModality( Qt::WindowModal );
  dialog.setFileMode( QFileDialog::ExistingFiles );
  dialog.setOption( QFileDialog::DontUseNativeDialog, true );
  dialog.setViewMode( QFileDialog::Detail );
  dialog.setWindowFlag( Qt::WindowStaysOnTopHint, true );
  if ( mStatusLabel )
    mStatusLabel->setText( tr( "导入照片失败：请稍后重试。" ) );
  if ( dialog.exec() != QDialog::Accepted )
    return;
  const QStringList paths = dialog.selectedFiles();
  if ( paths.isEmpty() )
    return;

  QSet<QString> existingPaths;
  for ( const PhotoRecord &record : std::as_const( mPhotos ) )
  {
    const QFileInfo info( record.sourcePath );
    existingPaths.insert( info.canonicalFilePath().isEmpty() ? QDir::cleanPath( record.sourcePath ) : info.canonicalFilePath() );
  }

  int added = 0;
  int ignored = 0;
  for ( const QString &path : paths )
  {
    const QFileInfo info( path );
    const QString normalizedPath = info.canonicalFilePath().isEmpty() ? QDir::cleanPath( info.absoluteFilePath() ) : info.canonicalFilePath();
    if ( !info.isFile() || existingPaths.contains( normalizedPath ) )
    {
      ++ignored;
      continue;
    }

    static const QSet<QString> supportedExtensions = { QStringLiteral( "jpg" ), QStringLiteral( "jpeg" ), QStringLiteral( "png" ),
                                                        QStringLiteral( "bmp" ), QStringLiteral( "tif" ), QStringLiteral( "tiff" ) };
    if ( !supportedExtensions.contains( info.suffix().toLower() ) )
    {
      ++ignored;
      continue;
    }
    PhotoRecord record;
    record.sourcePath = normalizedPath;
    mPhotos.append( record );
    existingPaths.insert( normalizedPath );
    ++added;
  }

  if ( added > 0 )
  {
    rebuildPhotoList();
    scheduleIndexSave();
  }
  updateSummary();
  if ( mStatusLabel )
     mStatusLabel->setText( tr( "已导入 %1 张照片，忽略 %2 张。" ).arg( added ).arg( ignored ) );
  if ( added > 0 )
     notify( tr( "导入完成" ), tr( "已导入 %1 张照片，忽略 %2 张。" ).arg( added ).arg( ignored ) );
}

void QgsEcoPhotoWorkbench::removeSelectedPhotos()
{
  if ( mBusy || !mPhotoList )
    return;

  QList<int> indexes;
  for ( int row = 0; row < mPhotoList->count(); ++row )
  {
    QListWidgetItem *item = mPhotoList->item( row );
    auto *entry = item ? dynamic_cast<PhotoListEntryWidget *>( mPhotoList->itemWidget( item ) ) : nullptr;
    if ( entry && entry->isChecked() )
      indexes.append( item->data( Qt::UserRole ).toInt() );
  }
  if ( indexes.isEmpty() )
  {
    const QList<QListWidgetItem *> selectedItems = mPhotoList->selectedItems();
    for ( QListWidgetItem *item : selectedItems )
    {
      if ( item )
        indexes.append( item->data( Qt::UserRole ).toInt() );
    }
  }
  if ( indexes.isEmpty() )
  {
    if ( mStatusLabel )
       mStatusLabel->setText( tr( "请先选择要移除的照片。" ) );
    return;
  }

  std::sort( indexes.begin(), indexes.end(), std::greater<int>() );
  indexes.erase( std::unique( indexes.begin(), indexes.end() ), indexes.end() );
  for ( int index : std::as_const( indexes ) )
  {
    if ( index >= 0 && index < mPhotos.size() )
    {
      mCheckedPhotoPaths.remove( photoSelectionKey( mPhotos.at( index ) ) );
      mPhotos.removeAt( index );
    }
  }

  rebuildPhotoList();
  scheduleIndexSave();
  updateSummary();
  if ( mStatusLabel )
     mStatusLabel->setText( tr( "已移除 %1 张照片。" ).arg( indexes.size() ) );
}
void QgsEcoPhotoWorkbench::setPhotoSmartSegmentationEnabled( bool enabled )
{
  if ( !mPreview || !mPhotoSmartSegmentationButton )
    return;
  if ( enabled && ( !mPhotoList || mPhotoList->currentRow() < 0 ) )
  {
    const QSignalBlocker blocker( mPhotoSmartSegmentationButton );
    mPhotoSmartSegmentationButton->setChecked( false );
    notify( tr( "智能分割" ), tr( "请先选择一张照片后再开启智能分割。" ), true );
    return;
  }
  if ( enabled && mManualDrawButton && mManualDrawButton->isChecked() )
  {
    const QSignalBlocker blocker( mManualDrawButton );
    mManualDrawButton->setChecked( false );
    setManualDrawingEnabled( false );
  }
  if ( enabled && mDeleteAnnotationButton && mDeleteAnnotationButton->isChecked() )
  {
    const QSignalBlocker blocker( mDeleteAnnotationButton );
    mDeleteAnnotationButton->setChecked( false );
    setDeleteAnnotationEnabled( false );
  }
  if ( enabled && mEditAnnotationButton && mEditAnnotationButton->isChecked() ) { const QSignalBlocker blocker( mEditAnnotationButton ); mEditAnnotationButton->setChecked( false ); setAnnotationEditingEnabled( false ); }
  mPreview->setSmartSegmentationEnabled( enabled );
  if ( mStatusLabel )
    mStatusLabel->setText( enabled ? tr( "智能分割已启用：左键红点，右键蓝点，按空格拖动图片。" ) : tr( "智能分割已关闭。" ) );
}

void QgsEcoPhotoWorkbench::runPhotoSmartSegmentation( const QVector<QPointF> &normalizedPoints, const QVector<int> &promptLabels )
{
  if ( mBusy || !mPhotoList || !mPhotoSmartSegmentationButton || !mPhotoSmartSegmentationButton->isChecked() )
    return;
  const int photoIndex = mPhotoList->currentRow();
  if ( photoIndex < 0 || photoIndex >= mPhotos.size() )
    return;
  const QImage image = readPreviewImage( photoSourcePath( mPhotos.at( photoIndex ) ), QSize() );
  if ( image.isNull() )
  {
      notify( tr( "智能分割" ), tr( "当前照片无法读取。" ), true );
    return;
  }
  const QString modelDirectory = preferredPhotoSam2ModelDirectory();
  QgsEcoSam2OnnxInference::Parameters parameters;
  parameters.encoderModelPath = QDir( modelDirectory ).filePath( QStringLiteral( "vision_encoder.onnx" ) );
  parameters.decoderModelPath = QDir( modelDirectory ).filePath( QStringLiteral( "prompt_encoder_mask_decoder.onnx" ) );
  parameters.inputImage = image;
  parameters.imageExtent = QgsRectangle( 0, 0, image.width(), image.height() );
  parameters.cropExtent = parameters.imageExtent;
  parameters.promptPoints.reserve( normalizedPoints.size() );
  for ( const QPointF &point : normalizedPoints )
    parameters.promptPoints.append( QgsPointXY( point.x() * image.width(), point.y() * image.height() ) );
  parameters.promptPoint = parameters.promptPoints.value( 0 );
  parameters.promptLabels = promptLabels;
  parameters.promptMode = QgsEcoSam2OnnxInference::PromptMode::Point;
  const quint64 generation = mContextGeneration;
  setBusy( true, tr( "正在后台执行智能分割..." ) );
  auto *watcher = new QFutureWatcher<QgsEcoSam2OnnxInference::Result>( this );
  connect( watcher, &QFutureWatcher<QgsEcoSam2OnnxInference::Result>::finished, this, [this, watcher, generation, photoIndex, image] {
    const QgsEcoSam2OnnxInference::Result result = watcher->result();
    watcher->deleteLater();
    if ( generation != mContextGeneration )
    {
       setBusy( false, tr( "智能分割结果已过期，已忽略。" ) );
      return;
    }
    if ( !result.success )
    {
       setBusy( false, result.error.isEmpty() ? tr( "智能分割失败。" ) : result.error );
       notify( tr( "智能分割失败" ), result.error.isEmpty() ? tr( "智能分割失败，请重试。" ) : result.error, true );
      return;
    }
    const QgsPolygonXY polygon = result.geometry.asPolygon();
    const QgsPolylineXY ring = polygon.value( 0 );
    QPolygonF normalized;
    normalized.reserve( ring.size() );
    for ( const QgsPointXY &point : ring )
      normalized << QPointF( point.x() / std::max( 1, image.width() ), point.y() / std::max( 1, image.height() ) );
    setBusy( false );
    if ( normalized.size() >= 3 )
    {
      const int classId = mAnnotationClassCombo ? mAnnotationClassCombo->currentData().toInt() : 0;
      addAnnotation( classId, photoClassName( classId ), normalized, QStringLiteral( "smart" ) );
      if ( mStatusLabel )
        mStatusLabel->setText( tr( "智能分割完成。" ) );
    }
    else
      notify( tr( "智能分割失败" ), tr( "智能分割失败，请重试。" ), true );
  } );
  watcher->setFuture( QtConcurrent::run( [parameters] { return QgsEcoSam2OnnxInference::run( nullptr, parameters ); } ) );
}
void QgsEcoPhotoWorkbench::batchRecognizePhotos()
{
  if ( mBusy || mPhotos.isEmpty() )
  {
    notify( QStringLiteral( "智能识别" ), QStringLiteral( "当前期次没有可用的照片。" ), true );
    return;
  }

  if ( !QgsEcoOnnxInference::isRuntimeAvailable() )
  {
    notify( QStringLiteral( "智能识别" ), QStringLiteral( "当前构建未启用 ONNX Runtime，无法执行智能识别。" ), true );
    return;
  }

  const QString modelPath = photoRecognitionModelPath();
  if ( modelPath.isEmpty() || !QFileInfo::exists( modelPath ) )
  {
    notify( QStringLiteral( "智能识别" ), QStringLiteral( "找不到照片智能识别使用的模型文件。" ), true );
    return;
  }

  QVector<int> targetIndexes;
  QSet<int> indexSet;
  auto appendIndex = [&targetIndexes, &indexSet]( int index )
  {
    if ( index >= 0 && !indexSet.contains( index ) )
    {
      indexSet.insert( index );
      targetIndexes.append( index );
    }
  };

  if ( mPhotoList )
  {
    for ( int row = 0; row < mPhotoList->count(); ++row )
    {
      QListWidgetItem *item = mPhotoList->item( row );
      auto *entry = item ? dynamic_cast<PhotoListEntryWidget *>( mPhotoList->itemWidget( item ) ) : nullptr;
      if ( entry && entry->isChecked() )
        appendIndex( item->data( Qt::UserRole ).toInt() );
    }

    if ( targetIndexes.isEmpty() )
    {
      const QList<QListWidgetItem *> selectedItems = mPhotoList->selectedItems();
      for ( QListWidgetItem *item : selectedItems )
      {
        if ( item )
          appendIndex( item->data( Qt::UserRole ).toInt() );
      }
    }

    if ( targetIndexes.isEmpty() && mPhotoList->currentRow() >= 0 )
      appendIndex( mPhotoList->currentRow() );
  }

  if ( targetIndexes.isEmpty() )
  {
    notify( QStringLiteral( "智能识别" ), QStringLiteral( "请选择一张或多张照片后再执行批量智能识别。" ), true );
    return;
  }

  std::sort( targetIndexes.begin(), targetIndexes.end() );

  const QVector<PhotoRecord> records = mPhotos;
  const quint64 generation = mContextGeneration;
  setBusy( true, QStringLiteral( "正在后台批量智能识别..." ) );
  setTaskProgress( 0, 100, tr( "智能识别进度：0%" ) );

  auto *watcher = new QFutureWatcher<PhotoBatchResult>( this );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::progressValueChanged, this, [this]( int progress ) {
    const int percent = std::clamp( progress, 0, 100 );
    setTaskProgress( percent, 100, tr( "智能识别进度：%1%" ).arg( percent ) );
  } );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::finished, this, [this, watcher, generation] {
    const PhotoBatchResult result = watcher->result();
    watcher->deleteLater();
      if ( generation != mContextGeneration )
    {
       setBusy( false, QStringLiteral( "智能识别结果已过期，已忽略。" ) );
      return;
    }

    mPhotos = result.records;
    rebuildPhotoList();
    rebuildFusedList();
    saveIndex();
    updateSummary();
    setTaskProgress( 100, 100, tr( "智能识别完成" ) );
    setBusy( false, QStringLiteral( "智能识别完成：处理 %1 张，跳过 %2 张。" ).arg( result.processed ).arg( result.skipped ) );
    notify( QStringLiteral( "智能识别完成" ), QStringLiteral( "已处理 %1 张照片，跳过 %2 张。" ).arg( result.processed ).arg( result.skipped ) );
  } );

  watcher->setFuture( QtConcurrent::run( [records, targetIndexes, modelPath]( QPromise<PhotoBatchResult> &promise ) {
    PhotoBatchResult result;
    result.records = records;
    const int total = std::max( 1, static_cast<int>( targetIndexes.size() ) );
    promise.setProgressRange( 0, 100 );
    promise.setProgressValue( 0 );

    QgsEcoOnnxInference::Parameters parameters;
    parameters.modelPath = modelPath;
    parameters.fallbackImageSize = 640;
    parameters.confidence = 0.35;
    parameters.iouThreshold = 0.35;

    for ( int pos = 0; pos < targetIndexes.size(); ++pos )
    {
      if ( promise.isCanceled() )
        break;

      const int index = targetIndexes.at( pos );
      if ( index < 0 || index >= result.records.size() )
      {
        ++result.skipped;
        promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
        continue;
      }

      PhotoRecord &record = result.records[index];
      const QImage image = readPreviewImage( photoSourcePath( record ), QSize() );
      if ( image.isNull() )
      {
        ++result.skipped;
        promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
        continue;
      }

      const QgsEcoOnnxInference::Result inference = QgsEcoOnnxInference::run( image, parameters );
      if ( !inference.success )
      {
        ++result.skipped;
        promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
        continue;
      }

      QVector<Annotation> updatedAnnotations;
      updatedAnnotations.reserve( record.annotations.size() + inference.detections.size() );
      for ( const Annotation &annotation : std::as_const( record.annotations ) )
      {
        if ( annotation.source != QStringLiteral( "auto" ) )
          updatedAnnotations.append( annotation );
      }

      for ( const QgsEcoOnnxDetection &detection : inference.detections )
      {
        const QgsPolylineXY ring = detection.geometry.asPolygon().value( 0 );
        if ( ring.size() < 3 )
          continue;

        QPolygonF polygon;
        polygon.reserve( ring.size() );
        for ( const QgsPointXY &point : ring )
          polygon << QPointF( point.x(), point.y() );

        QPolygonF normalized;
        normalized.reserve( polygon.size() );
        const double width = std::max( 1, image.width() );
        const double height = std::max( 1, image.height() );
        for ( const QPointF &point : polygon )
          normalized.append( QPointF( point.x() / width, point.y() / height ) );
        normalized = clampNormalizedPolygon( normalized );
        const QRectF bounds = normalizedBoundingRect( normalized );
        if ( normalized.size() < 3 || bounds.width() < 0.002 || bounds.height() < 0.002 )
          continue;

        Annotation annotation;
        annotation.classId = detection.classId;
        annotation.className = detection.classId == 0 ? photoClassName( 0 ) : detection.classId == 1 ? photoClassName( 1 ) : QStringLiteral( "类别_%1" ).arg( detection.classId );
        annotation.source = QStringLiteral( "auto" );
        annotation.normalizedPolygon = normalized;
        updatedAnnotations.append( annotation );
      }

      record.annotations = std::move( updatedAnnotations );
      ++result.processed;
      promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
    }

    promise.setProgressValue( 100 );
    promise.addResult( result );
    promise.finish();
  } ) );
}
void QgsEcoPhotoWorkbench::setManualDrawingEnabled( bool enabled )
{
  if ( !mPreview || !mManualDrawButton )
    return;
  if ( enabled && ( !mPhotoList || mPhotoList->currentRow() < 0 ) )
  {
    mManualDrawButton->setChecked( false );
     notify( tr( "标绘图斑" ), tr( "请先选择照片后再开始标绘。" ), true );
    return;
  }

  if ( enabled && mDeleteAnnotationButton && mDeleteAnnotationButton->isChecked() )
  {
    const QSignalBlocker blocker( mDeleteAnnotationButton );
    mDeleteAnnotationButton->setChecked( false );
    setDeleteAnnotationEnabled( false );
  }

  if ( enabled && mEditAnnotationButton && mEditAnnotationButton->isChecked() ) { const QSignalBlocker blocker( mEditAnnotationButton ); mEditAnnotationButton->setChecked( false ); setAnnotationEditingEnabled( false ); }
  const int currentClass = mAnnotationClassCombo ? mAnnotationClassCombo->currentData().toInt() : 0;
  const QString currentClassName = mAnnotationClassCombo ? mAnnotationClassCombo->currentText() : photoClassName( currentClass );
  mPreview->setDrawingEnabled( enabled, currentClass, currentClassName );
  if ( mManualDrawButton )
     mManualDrawButton->setText( enabled ? tr( "结束标绘" ) : tr( "标绘图斑" ) );
  if ( mStatusLabel )
     mStatusLabel->setText( enabled ? tr( "标绘模式：左键添加点，Ctrl+Z 撤销，双击完成。" ) : tr( "标绘模式已关闭。" ) );
}

void QgsEcoPhotoWorkbench::setDeleteAnnotationEnabled( bool enabled )
{
  if ( !mPreview || !mDeleteAnnotationButton )
    return;

  if ( enabled && mManualDrawButton && mManualDrawButton->isChecked() )
  {
    const QSignalBlocker blocker( mManualDrawButton );
    mManualDrawButton->setChecked( false );
    setManualDrawingEnabled( false );
  }

  if ( enabled && mEditAnnotationButton && mEditAnnotationButton->isChecked() ) { const QSignalBlocker blocker( mEditAnnotationButton ); mEditAnnotationButton->setChecked( false ); setAnnotationEditingEnabled( false ); }
  mDeleteMode = enabled;
  mPreview->setDeleteEnabled( enabled );
   mDeleteAnnotationButton->setText( enabled ? tr( "完成删除" ) : tr( "删除标绘" ) );
  if ( mStatusLabel )
      mStatusLabel->setText( enabled ? tr( "删除模式：悬停图斑后单击删除。" ) : tr( "删除模式已关闭。" ) );
}

void QgsEcoPhotoWorkbench::setAnnotationEditingEnabled( bool enabled )
{
  if ( !mPreview || !mEditAnnotationButton )
    return;
  if ( enabled && ( !mPhotoList || mPhotoList->currentRow() < 0 ) )
  {
    const QSignalBlocker blocker( mEditAnnotationButton );
    mEditAnnotationButton->setChecked( false );
    notify( QStringLiteral( "编辑结果" ), QStringLiteral( "请先选择照片后再开始编辑。" ), true );
    return;
  }
  if ( enabled && mManualDrawButton && mManualDrawButton->isChecked() ) { const QSignalBlocker blocker( mManualDrawButton ); mManualDrawButton->setChecked( false ); setManualDrawingEnabled( false ); }
  if ( enabled && mDeleteAnnotationButton && mDeleteAnnotationButton->isChecked() ) { const QSignalBlocker blocker( mDeleteAnnotationButton ); mDeleteAnnotationButton->setChecked( false ); setDeleteAnnotationEnabled( false ); }
  if ( enabled && mPhotoSmartSegmentationButton && mPhotoSmartSegmentationButton->isChecked() ) { const QSignalBlocker blocker( mPhotoSmartSegmentationButton ); mPhotoSmartSegmentationButton->setChecked( false ); setPhotoSmartSegmentationEnabled( false ); }
  mPreview->setEditEnabled( enabled );
  mEditAnnotationButton->setText( enabled ? QStringLiteral( "完成编辑" ) : QStringLiteral( "编辑结果" ) );
  if ( mStatusLabel ) mStatusLabel->setText( enabled ? QStringLiteral( "编辑模式：点击图斑后可拖动顶点，双击边可新增点，右键顶点可删除点。" ) : QStringLiteral( "编辑模式已关闭。" ) );
}

void QgsEcoPhotoWorkbench::updateAnnotation( int annotationIndex, const QPolygonF &normalizedPolygon )
{
  if ( mBusy || !mPhotoList )
    return;
  const int photoIndex = mPhotoList->currentRow();
  if ( photoIndex < 0 || photoIndex >= mPhotos.size() || annotationIndex < 0 || annotationIndex >= mPhotos.at( photoIndex ).annotations.size() )
    return;
  const QPolygonF polygon = clampNormalizedPolygon( normalizedPolygon );
  const QRectF bounds = normalizedBoundingRect( polygon );
  if ( polygon.size() < 3 || bounds.width() < 0.002 || bounds.height() < 0.002 )
    return;
  mPhotos[photoIndex].annotations[annotationIndex].normalizedPolygon = polygon;
  updatePhotoListItem( photoIndex );
  scheduleIndexSave();
  updateSummary();
  if ( mStatusLabel ) mStatusLabel->setText( QStringLiteral( "Polygon updated." ) );
}
void QgsEcoPhotoWorkbench::deleteAnnotationAt( int annotationIndex )
{
  if ( mBusy || !mPreview || !mPhotoList )
    return;
  const int photoIndex = mPhotoList->currentRow();
  if ( photoIndex < 0 || photoIndex >= mPhotos.size() || annotationIndex < 0 || annotationIndex >= mPhotos.at( photoIndex ).annotations.size() )
    return;

  const QString className = mPhotos.at( photoIndex ).annotations.at( annotationIndex ).className; Q_UNUSED( className );
  const int choice = QMessageBox::question( this, tr( "删除标绘" ), tr( "确定要删除当前图中的标绘图斑吗？" ), QMessageBox::Yes | QMessageBox::No, QMessageBox::No );




  if ( choice != QMessageBox::Yes )
    return;

  mPhotos[photoIndex].annotations.removeAt( annotationIndex );
  updatePhotoListItem( photoIndex );
  mPreview->setAnnotations( mPhotos[photoIndex].annotations );
  scheduleIndexSave();
  updateSummary();
  if ( mDeleteAnnotationButton )
  {
    const QSignalBlocker blocker( mDeleteAnnotationButton );
    mDeleteAnnotationButton->setChecked( false );
  }
  setDeleteAnnotationEnabled( false );
  if ( mStatusLabel )
    mStatusLabel->setText( tr( "标绘已删除。" ) );
}
void QgsEcoPhotoWorkbench::addAnnotation( int classId, const QString &className, const QPolygonF &normalizedPolygon, const QString &source )
{
  if ( mBusy || !mPhotoList )
    return;
  const int index = mPhotoList->currentRow();
  if ( index < 0 || index >= mPhotos.size() )
    return;

  Annotation annotation;
  annotation.classId = classId;
  annotation.className = className.isEmpty() ? photoClassName( classId ) : className;
  annotation.source = source.isEmpty() ? QStringLiteral( "manual" ) : source;
  annotation.normalizedPolygon = clampNormalizedPolygon( normalizedPolygon );
  const QRectF bounds = normalizedBoundingRect( annotation.normalizedPolygon );
  if ( annotation.normalizedPolygon.size() < 3 || bounds.width() < 0.002 || bounds.height() < 0.002 )
    return;

  if ( annotation.source == QStringLiteral( "smart" ) )
  {
    for ( int i = mPhotos[index].annotations.size() - 1; i >= 0; --i )
    {
      if ( mPhotos[index].annotations.at( i ).source == QStringLiteral( "smart" ) )
        mPhotos[index].annotations.removeAt( i );
    }
  }

  mPhotos[index].annotations.append( annotation );
  updatePhotoListItem( index );
  mPreview->setAnnotations( mPhotos[index].annotations );
  if ( mPreview )
    mPreview->playCompletionPulse( classId, annotation.normalizedPolygon );
  scheduleIndexSave();
  updateSummary();
  if ( mStatusLabel )
    mStatusLabel->setText( tr( "已保存“%1”标绘。" ).arg( annotation.className ) );
}

void QgsEcoPhotoWorkbench::exportOriginalPhotos()
{
  if ( mBusy || mPhotos.isEmpty() )
  {
    notify( tr( "导出原图" ), tr( "当前没有照片可导出。" ), true );
    return;
  }

  const QString defaultDirectory = QDir( mWorkspacePath ).filePath( QStringLiteral( "photos/export_original" ) );
  QDir().mkpath( defaultDirectory );
  QFileDialog dialog( dialogParentFor( this ), tr( "导出原图" ), defaultDirectory );
  dialog.setWindowModality( Qt::WindowModal );
  dialog.setFileMode( QFileDialog::Directory );
  dialog.setOption( QFileDialog::ShowDirsOnly, true );
  dialog.setOption( QFileDialog::DontUseNativeDialog, true );
  dialog.setViewMode( QFileDialog::Detail );
  if ( dialog.exec() != QDialog::Accepted )
    return;
  const QString outputDirectory = dialog.selectedFiles().value( 0 );
  if ( outputDirectory.isEmpty() )
    return;

  setBusy( true, tr( "正在导出原图..." ) );
  const quint64 generation = mContextGeneration;
  const QVector<PhotoRecord> records = mPhotos;
  auto *watcher = new QFutureWatcher<PhotoBatchResult>( this );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::finished, this, [this, watcher, generation] {
    const PhotoBatchResult result = watcher->result();
    watcher->deleteLater();
    if ( generation != mContextGeneration )
      return;
    setBusy( false, tr( "原图导出完成：%1 张，跳过 %2 张。" ).arg( result.written ).arg( result.skipped ) );
    notify( tr( "原图导出完成" ), tr( "已导出 %1 张原图。" ).arg( result.written ) );
  } );
  watcher->setFuture( QtConcurrent::run( [records, outputDirectory] {
    PhotoBatchResult result;
    ensureDirectory( outputDirectory );
    for ( int i = 0; i < records.size(); ++i )
    {
      const QString sourcePath = records.at( i ).sourcePath;
      const QString targetPath = indexedOutputPath( outputDirectory, sourcePath, i );
      if ( copyFileReplacingTarget( sourcePath, targetPath ) )
      {
        ++result.written;
        result.outputPaths.append( targetPath );
      }
      else
      {
        ++result.skipped;
      }
    }
    return result;
  } ) );
}

void QgsEcoPhotoWorkbench::createFusedPhotos()
{
  if ( mBusy || mPhotos.isEmpty() )
  {
    notify( tr( "生成融合照片" ), tr( "当前没有照片可用于生成融合照片。" ), true );
    return;
  }

  bool hasCheckedPhoto = false;
  for ( const PhotoRecord &record : std::as_const( mPhotos ) )
  {
    if ( mCheckedPhotoPaths.contains( photoSelectionKey( record ) ) )
    {
      hasCheckedPhoto = true;
      break;
    }
  }
  Q_UNUSED( hasCheckedPhoto );
  QVector<int> targetIndexes;
  QSet<int> indexSet;
  if ( mPhotoList )
  {
    for ( int row = 0; row < mPhotoList->count(); ++row )
    {
      QListWidgetItem *item = mPhotoList->item( row );
      auto *entry = item ? dynamic_cast<PhotoListEntryWidget *>( mPhotoList->itemWidget( item ) ) : nullptr;
      if ( entry && entry->isChecked() )
      {
        const int index = item->data( Qt::UserRole ).toInt();
        if ( !indexSet.contains( index ) )
        {
          indexSet.insert( index );
          targetIndexes.append( index );
        }
      }
    }
    if ( targetIndexes.isEmpty() )
    {
      const QList<QListWidgetItem *> selectedItems = mPhotoList->selectedItems();
      for ( QListWidgetItem *item : selectedItems )
      {
        if ( !item )
          continue;
        const int index = item->data( Qt::UserRole ).toInt();
        if ( !indexSet.contains( index ) )
        {
          indexSet.insert( index );
          targetIndexes.append( index );
        }
      }
    }
    if ( targetIndexes.isEmpty() && mPhotoList->currentRow() >= 0 )
    {
      const int index = mPhotoList->currentRow();
      if ( !indexSet.contains( index ) )
      {
        indexSet.insert( index );
        targetIndexes.append( index );
      }
    }
  }
  if ( targetIndexes.isEmpty() )
  {
    notify( tr( "移除照片" ), tr( "请先勾选要移除的照片。" ), true );


    return;
  }

  const QString outputDirectory = photoDerivedDirectory( mWorkspacePath );
  if ( !ensureDirectory( outputDirectory ) )
  {
    notify( tr( "移除照片" ), tr( "无法创建照片输出目录，请检查工作区权限。" ), true );
    return;
  }

  setBusy( true, tr( "正在生成融合照片..." ) );
  setTaskProgress( 0, 100, tr( "生成进度：0%" ) );
  const quint64 generation = mContextGeneration;
  const QVector<PhotoRecord> records = mPhotos;
  auto *watcher = new QFutureWatcher<PhotoBatchResult>( this );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::progressValueChanged, this, [this]( int progress ) {
    const int percent = std::clamp( progress, 0, 100 );
    setTaskProgress( percent, 100, tr( "生成进度：%1%" ).arg( percent ) );
  } );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::finished, this, [this, watcher, generation] {
    const PhotoBatchResult result = watcher->result();
    watcher->deleteLater();
      if ( generation != mContextGeneration )
    {
       setBusy( false, tr( "融合照片生成结果已过期，已忽略。" ) );
      return;
    }

    mPhotos = result.records;
    rebuildPhotoList();
    rebuildFusedList();
    saveIndex();
    updateSummary();
    if ( result.written > 0 && mPhotoTabs )
      mPhotoTabs->setCurrentIndex( 1 );
    setTaskProgress( 100, 100, tr( "融合照片生成完成" ) );
    setBusy( false, tr( "融合照片生成完成：%1 张，跳过 %2 张。" ).arg( result.written ).arg( result.skipped ) );
    notify( tr( "融合照片生成完成" ), tr( "已生成 %1 张融合照片。" ).arg( result.written ) );
  } );
  watcher->setFuture( QtConcurrent::run( [records, targetIndexes, outputDirectory]( QPromise<PhotoBatchResult> &promise ) {
    PhotoBatchResult result;
    result.records = records;
    ensureDirectory( outputDirectory );
    const int total = std::max( 1, static_cast<int>( targetIndexes.size() ) );
    promise.setProgressRange( 0, 100 );
    promise.setProgressValue( 0 );
    for ( int pos = 0; pos < targetIndexes.size(); ++pos )
    {
      const int index = targetIndexes.at( pos );
      if ( index < 0 || index >= result.records.size() )
      {
        ++result.skipped;
        promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
        continue;
      }
      PhotoRecord &record = result.records[index];
      QImage image = readPreviewImage( record.sourcePath, QSize() );
      if ( image.isNull() )
      {
        ++result.skipped;
        promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
        continue;
      }

      QPainter painter( &image );
      painter.setRenderHint( QPainter::Antialiasing, true );
      for ( const Annotation &annotation : std::as_const( record.annotations ) )
      {
        const QPolygonF polygon = scenePolygonFromNormalized( annotation.normalizedPolygon, QRectF( 0.0, 0.0, image.width(), image.height() ) );
        if ( polygon.size() < 3 )
          continue;
        QPen pen( annotationColor( annotation.classId ) );
        pen.setWidth( std::max( 2, image.width() / 900 ) );
        pen.setJoinStyle( Qt::RoundJoin );
        pen.setCapStyle( Qt::RoundCap );
        painter.setPen( pen );
        QColor fill = annotationColor( annotation.classId );
        fill.setAlpha( 72 );
        painter.setBrush( fill );
        painter.drawPolygon( polygon );
      }
      painter.end();

      const QString targetPath = QDir( outputDirectory ).filePath( QStringLiteral( "%1_%2_fused.png" )
                                                                     .arg( safeOutputBaseName( record.sourcePath, index ) )
                                                                     .arg( index + 1, 5, 10, QLatin1Char( '0' ) ) );
      if ( image.save( targetPath, "PNG" ) )
      {
        record.fusedPath = targetPath;
        result.outputPaths.append( targetPath );
        ++result.written;
      }
      else
      {
        ++result.skipped;
      }
      promise.setProgressValue( ( ( pos + 1 ) * 100 ) / total );
    }
    promise.setProgressValue( 100 );
    promise.addResult( result );
    promise.finish();
  } ) );
}
void QgsEcoPhotoWorkbench::exportFusedPhotos()
{
  if ( mBusy || mPhotos.isEmpty() )
  {
    notify( tr( "生成融合照片" ), tr( "当前没有可导出的融合照片。" ), true );
    return;
  }

  const QString defaultDirectory = QDir( mWorkspacePath ).filePath( QStringLiteral( "photos/export_fused" ) );
  QDir().mkpath( defaultDirectory );
  QFileDialog dialog( dialogParentFor( this ), tr( "导出融合照片" ), defaultDirectory );
  dialog.setWindowModality( Qt::WindowModal );
  dialog.setFileMode( QFileDialog::Directory );
  dialog.setOption( QFileDialog::ShowDirsOnly, true );
  dialog.setOption( QFileDialog::DontUseNativeDialog, true );
  dialog.setViewMode( QFileDialog::Detail );
  if ( dialog.exec() != QDialog::Accepted )
    return;
  const QString outputDirectory = dialog.selectedFiles().value( 0 );
  if ( outputDirectory.isEmpty() )
    return;

  setBusy( true, tr( "正在导出融合照片..." ) );
  const quint64 generation = mContextGeneration;
  const QVector<PhotoRecord> records = mPhotos;
  auto *watcher = new QFutureWatcher<PhotoBatchResult>( this );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::finished, this, [this, watcher, generation] {
    const PhotoBatchResult result = watcher->result();
    watcher->deleteLater();
    if ( generation != mContextGeneration )
    {
      setBusy( false, tr( "融合照片导出结果已过期，已忽略。" ) );
      return;
    }
    setBusy( false, tr( "融合照片导出完成：%1 张，跳过 %2 张。" ).arg( result.written ).arg( result.skipped ) );
    notify( tr( "融合照片导出完成" ), tr( "已导出 %1 张融合照片。" ).arg( result.written ) );
  } );
  watcher->setFuture( QtConcurrent::run( [records, outputDirectory] {
    PhotoBatchResult result;
    ensureDirectory( outputDirectory );
    for ( int i = 0; i < records.size(); ++i )
    {
      const PhotoRecord &record = records.at( i );
      if ( record.fusedPath.isEmpty() )
      {
        ++result.skipped;
        continue;
      }
      const QString targetPath = indexedOutputPath( outputDirectory, record.fusedPath, i, QStringLiteral( "_fused" ) );
      if ( copyFileReplacingTarget( record.fusedPath, targetPath ) )
      {
        ++result.written;
        result.outputPaths.append( targetPath );
      }
      else
      {
        ++result.skipped;
      }
    }
    return result;
  } ) );
}

void QgsEcoPhotoWorkbench::exportYoloSamples()
{
  if ( mBusy || mPhotos.isEmpty() )
  {
    notify( tr( "导出 YOLO 样本" ), tr( "当前没有照片可用于导出 YOLO 样本。" ), true );
    return;
  }

  const QString defaultDirectory = QDir( mWorkspacePath ).filePath( QStringLiteral( "photos/yolo_samples" ) );
  QDir().mkpath( defaultDirectory );
  QFileDialog dialog( dialogParentFor( this ), tr( "导出 YOLO 样本" ), defaultDirectory );
  dialog.setWindowModality( Qt::WindowModal );
  dialog.setFileMode( QFileDialog::Directory );
  dialog.setOption( QFileDialog::ShowDirsOnly, true );
  dialog.setOption( QFileDialog::DontUseNativeDialog, true );
  dialog.setViewMode( QFileDialog::Detail );
  if ( dialog.exec() != QDialog::Accepted )
    return;
  const QString outputDirectory = dialog.selectedFiles().value( 0 );
  if ( outputDirectory.isEmpty() )
    return;

  const QString imagesDirectory = QDir( outputDirectory ).filePath( QStringLiteral( "images" ) );
  const QString labelsDirectory = QDir( outputDirectory ).filePath( QStringLiteral( "labels" ) );
  if ( !ensureDirectory( imagesDirectory ) || !ensureDirectory( labelsDirectory ) )
  {
    notify( tr( "导出 YOLO 样本" ), tr( "无法创建 YOLO 输出目录，请检查工作区权限。" ), true );
    return;
  }

  setBusy( true, tr( "正在导出 YOLO 样本..." ) );
  const quint64 generation = mContextGeneration;
  const QVector<PhotoRecord> records = mPhotos;
  auto *watcher = new QFutureWatcher<PhotoBatchResult>( this );
  connect( watcher, &QFutureWatcher<PhotoBatchResult>::finished, this, [this, watcher, generation] {
    const PhotoBatchResult result = watcher->result();
    watcher->deleteLater();
    if ( generation != mContextGeneration )
    {
      setBusy( false, tr( "YOLO 导出结果已过期，已忽略。" ) );
      return;
    }
    setBusy( false, tr( "YOLO 样本导出完成：%1 张，跳过 %2 张。" ).arg( result.written ).arg( result.skipped ) );
    notify( tr( "YOLO 样本导出完成" ), tr( "已导出 %1 张照片的 YOLO 样本。" ).arg( result.written ) );
  } );
  watcher->setFuture( QtConcurrent::run( [records, imagesDirectory, labelsDirectory, outputDirectory] {
    PhotoBatchResult result;
    ensureDirectory( imagesDirectory );
    ensureDirectory( labelsDirectory );
    for ( int i = 0; i < records.size(); ++i )
    {
      const PhotoRecord &record = records.at( i );
      const QString sourcePath = record.sourcePath;
      if ( sourcePath.isEmpty() || !QFileInfo( sourcePath ).isFile() )
      {
        ++result.skipped;
        continue;
      }

      const QString imageTarget = indexedOutputPath( imagesDirectory, sourcePath, i );
      const QFileInfo imageInfo( imageTarget );
      const QString labelTarget = QDir( labelsDirectory ).filePath( imageInfo.completeBaseName() + QStringLiteral( ".txt" ) );

      if ( !copyFileReplacingTarget( sourcePath, imageTarget ) )
      {
        ++result.skipped;
        continue;
      }

      QFile labelFile( labelTarget );
      if ( !labelFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
      {
        ++result.skipped;
        continue;
      }

      QTextStream stream( &labelFile );
      for ( const Annotation &annotation : record.annotations )
      {
        const QPolygonF polygon = clampNormalizedPolygon( annotation.normalizedPolygon );
        const QRectF rect = normalizedBoundingRect( polygon );
        if ( polygon.size() < 3 || rect.width() <= 0.0 || rect.height() <= 0.0 )
          continue;
        stream << annotation.classId << ' '
               << QString::number( rect.center().x(), 'f', 6 ) << ' '
               << QString::number( rect.center().y(), 'f', 6 ) << ' '
               << QString::number( rect.width(), 'f', 6 ) << ' '
               << QString::number( rect.height(), 'f', 6 ) << '\n';
      }
      labelFile.close();
      ++result.written;
      result.outputPaths.append( imageTarget );
    }

    QFile classesFile( QDir( outputDirectory ).filePath( QStringLiteral( "classes.txt" ) ) );
    if ( classesFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    {
      QTextStream stream( &classesFile );
      stream << photoClassName( 0 ) << '\n' << photoClassName( 1 ) << '\n';
    }
    return result;
  } ) );
}




