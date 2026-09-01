/***************************************************************************
  qgsecorestorationcontroller.cpp
 ***************************************************************************/

#include "qgsecorestorationcontroller.h"
#include "qgsecoonnxinference.h"
#include "qgsecophotoworkbench.h"
#include "qgsecosam2onnxinference.h"

#include "qgisapp.h"

#include "qgsapplication.h"
#include "qgscoordinatetransform.h"
#include "qgsdefaultvalue.h"
#include "qgsdistancearea.h"
#include "qgsdockwidget.h"
#include "qgseditorwidgetsetup.h"
#include "qgseditformconfig.h"
#include "qgsfeature.h"
#include "qgsfeaturerequest.h"
#include "qgsfield.h"
#include "qgsfillsymbol.h"
#include "qgsfillsymbollayer.h"
#include "qgsfields.h"
#include "qgsgeometry.h"
#include "qgslayertreegroup.h"
#include "qgslayertreemodel.h"
#include "qgslayertree.h"
#include "qgslayertreelayer.h"
#include "qgslayertreemapcanvasbridge.h"
#include "qgslayertreeview.h"
#include "qgslinesymbol.h"
#include "qgsmarkersymbol.h"
#include "qgsmarkersymbollayer.h"
#include "qgsmapcanvas.h"
#include "qgsmapcanvasdockwidget.h"
#include "qgsmapmouseevent.h"
#include "qgsmaptool.h"
#include "qgsmaptoolextent.h"
#include "qgsmaptoolpan.h"
#include "qgsmaplayer.h"
#include "qgsmaprendererparalleljob.h"
#include "qgsmapsettings.h"
#include "qgspallabeling.h"
#include "qgsproject.h"
#include "qgsproperty.h"
#include "qgsrasterblock.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasterlayer.h"
#include "qgsrasterrenderer.h"
#include "qgsrastertransparency.h"
#include "qgsrubberband.h"
#include "qgssinglesymbolrenderer.h"
#include "qgssymbol.h"
#include "qgssymbollayerutils.h"
#include "qgstextformat.h"
#include "qgsvectorfilewriter.h"
#include "qgsvectordataprovider.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayerlabeling.h"
#include "qgsvectorlayerselectionproperties.h"
#include "qgswkbtypes.h"
#include "qgs3dmapcanvaswidget.h"
#include "qgsdockablewidgethelper.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLibrary>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include <QCursor>
#include <QPointF>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPolygonF>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace
{
  class EcoScopeGuard final
  {
    public:
      explicit EcoScopeGuard( std::function<void()> function )
        : mFunction( std::move( function ) )
      {
      }

      ~EcoScopeGuard()
      {
        if ( mFunction )
          mFunction();
      }

      EcoScopeGuard( const EcoScopeGuard & ) = delete;
      EcoScopeGuard &operator=( const EcoScopeGuard & ) = delete;

    private:
      std::function<void()> mFunction;
  };

  void ecoImportTrace( const QString &message )
  {
    QFile file( QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "eco_import_trace.log" ) ) );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text ) )
      return;

    QTextStream stream( &file );
    stream << QDateTime::currentDateTime().toString( Qt::ISODateWithMs ) << ' ' << message << '\n';
  }

  void showEcoToast( QgisApp *app, const QString &title, const QString &message, bool warning, int timeoutMs = 3000 )
  {
    if ( !app )
      return;

    QWidget *host = app->centralWidget() ? app->centralWidget() : static_cast<QWidget *>( app );
    if ( !host )
      return;

    if ( QWidget *previous = host->findChild<QWidget *>( QStringLiteral( "EcoToastPanel" ), Qt::FindDirectChildrenOnly ) )
      previous->deleteLater();

    const QColor accent = warning ? QColor( QStringLiteral( "#f97316" ) ) : QColor( QStringLiteral( "#38bdf8" ) );
    QFrame *panel = new QFrame( host );
    panel->setObjectName( QStringLiteral( "EcoToastPanel" ) );
    panel->setAttribute( Qt::WA_StyledBackground, true );
    panel->setStyleSheet( QStringLiteral( R"(
      QFrame#EcoToastPanel {
        background:rgba(8, 13, 28, 236);
        border:1px solid %1;
        border-radius:10px;
      }
    )" ).arg( accent.name() ) );
    QVBoxLayout *layout = new QVBoxLayout( panel );
    layout->setContentsMargins( 14, 11, 14, 12 );
    layout->setSpacing( 6 );

    QLabel *titleLabel = new QLabel( title, panel );
    titleLabel->setStyleSheet( QStringLiteral( "color:%1; font-size:13px; font-weight:700; background:transparent;" ).arg( accent.name() ) );
    layout->addWidget( titleLabel );

    QLabel *messageLabel = new QLabel( message.simplified(), panel );
    messageLabel->setWordWrap( true );
    messageLabel->setStyleSheet( QStringLiteral( "color:#e5e7eb; font-size:12px; background:transparent;" ) );
    layout->addWidget( messageLabel );

    const int panelWidth = std::clamp( host->width() / 3, 340, 430 );
    panel->setFixedWidth( std::min( panelWidth, std::max( 280, host->width() - 48 ) ) );
    panel->adjustSize();

    const int topMargin = 22;
    const int rightMargin = 22;
    const QPoint endPos( std::max( 8, host->width() - panel->width() - rightMargin ), topMargin );
    const QPoint startPos( host->width() + 8, topMargin );
    panel->move( startPos );
    panel->show();
    panel->raise();

    QPropertyAnimation *showAnimation = new QPropertyAnimation( panel, "pos", panel );
    showAnimation->setDuration( 240 );
    showAnimation->setEasingCurve( QEasingCurve::OutCubic );
    showAnimation->setStartValue( startPos );
    showAnimation->setEndValue( endPos );
    showAnimation->start( QAbstractAnimation::DeleteWhenStopped );

    QTimer::singleShot( std::max( 800, timeoutMs ), panel, [toast = QPointer<QWidget>( panel ), host] {
      if ( !toast )
        return;
      const QPoint hidePos( host ? host->width() + 8 : toast->x() + toast->width() + 8, toast->y() );
      QPropertyAnimation *hideAnimation = new QPropertyAnimation( toast, "pos", toast );
      hideAnimation->setDuration( 220 );
      hideAnimation->setEasingCurve( QEasingCurve::InCubic );
      hideAnimation->setStartValue( toast->pos() );
      hideAnimation->setEndValue( hidePos );
      QObject::connect( hideAnimation, &QPropertyAnimation::finished, toast, [toast] {
        if ( toast )
          toast->deleteLater();
      } );
      hideAnimation->start( QAbstractAnimation::DeleteWhenStopped );
    } );
  }

  enum class EcoToolbarIcon
  {
    Project,
    Raster,
    Vector,
    Pan,
    ZoomIn,
    ZoomOut,
    FullExtent,
    ThreeD,
    Disturbance,
    SmartSegment,
    Restoration,
    Screenshot,
    Panel,
    Phase,
    Compare,
    Confirm,
    Delete,
  };

  QIcon ecoToolbarIcon( EcoToolbarIcon icon )
  {
    QPixmap pixmap( 20, 20 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    QColor color( QStringLiteral( "#c5d1de" ) );
    if ( icon == EcoToolbarIcon::Disturbance )
      color = QColor( QStringLiteral( "#fbbf24" ) );
    else if ( icon == EcoToolbarIcon::Restoration )
      color = QColor( QStringLiteral( "#4ade80" ) );
    else if ( icon == EcoToolbarIcon::Confirm )
      color = QColor( QStringLiteral( "#4ec9b0" ) );
    else if ( icon == EcoToolbarIcon::Delete )
      color = QColor( QStringLiteral( "#f14c4c" ) );
    painter.setPen( QPen( color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
    painter.setBrush( Qt::NoBrush );

    const auto line = [&painter]( qreal x1, qreal y1, qreal x2, qreal y2 ) { painter.drawLine( QPointF( x1, y1 ), QPointF( x2, y2 ) ); };
    const auto circle = [&painter]( qreal x, qreal y, qreal radius ) { painter.drawEllipse( QPointF( x, y ), radius, radius ); };

    switch ( icon )
    {
      case EcoToolbarIcon::Project:
        painter.drawRoundedRect( QRectF( 4.5, 2.5, 10, 14.5 ), 1, 1 );
        line( 11, 2.5, 14.5, 6 );
        line( 11, 2.5, 11, 6 );
        line( 11, 6, 14.5, 6 );
        line( 9.5, 10, 9.5, 15 );
        line( 7, 12.5, 12, 12.5 );
        break;
      case EcoToolbarIcon::Raster:
        painter.drawRoundedRect( QRectF( 2.5, 3.5, 15, 12.5 ), 1, 1 );
        circle( 7, 7.5, 1.25 );
        painter.drawPolyline( QPolygonF { QPointF( 4.5, 14 ), QPointF( 8.5, 10 ), QPointF( 11, 12 ), QPointF( 14, 9 ), QPointF( 16, 14 ) } );
        break;
      case EcoToolbarIcon::Vector:
        line( 5, 14.5, 10, 6 );
        line( 10, 6, 15, 13.5 );
        circle( 5, 14.5, 1.6 );
        circle( 10, 6, 1.6 );
        circle( 15, 13.5, 1.6 );
        break;
      case EcoToolbarIcon::Pan:
        line( 3, 10, 17, 10 );
        line( 10, 3, 10, 17 );
        line( 3, 10, 6, 7 );
        line( 3, 10, 6, 13 );
        line( 17, 10, 14, 7 );
        line( 17, 10, 14, 13 );
        line( 10, 3, 7, 6 );
        line( 10, 3, 13, 6 );
        line( 10, 17, 7, 14 );
        line( 10, 17, 13, 14 );
        break;
      case EcoToolbarIcon::ZoomIn:
      case EcoToolbarIcon::ZoomOut:
        circle( 8.5, 8.5, 4.75 );
        line( 12, 12, 16.5, 16.5 );
        line( 6.25, 8.5, 10.75, 8.5 );
        if ( icon == EcoToolbarIcon::ZoomIn )
          line( 8.5, 6.25, 8.5, 10.75 );
        break;
      case EcoToolbarIcon::FullExtent:
        line( 3, 8, 3, 3 ); line( 3, 3, 8, 3 );
        line( 12, 3, 17, 3 ); line( 17, 3, 17, 8 );
        line( 3, 12, 3, 17 ); line( 3, 17, 8, 17 );
        line( 12, 17, 17, 17 ); line( 17, 17, 17, 12 );
        break;
      case EcoToolbarIcon::ThreeD:
        painter.drawPolygon( QPolygonF { QPointF( 10, 2.5 ), QPointF( 16, 6 ), QPointF( 10, 9.5 ), QPointF( 4, 6 ) } );
        painter.drawPolyline( QPolygonF { QPointF( 4, 6 ), QPointF( 4, 13.5 ), QPointF( 10, 17 ), QPointF( 16, 13.5 ), QPointF( 16, 6 ) } );
        line( 10, 9.5, 10, 17 );
        break;
      case EcoToolbarIcon::Disturbance:
        painter.drawPolygon( QPolygonF { QPointF( 4, 5 ), QPointF( 11.5, 3.5 ), QPointF( 16, 8.5 ), QPointF( 13, 16 ), QPointF( 5, 14 ) } );
        line( 8, 8, 12.5, 12.5 );
        line( 12.5, 8, 8, 12.5 );
        break;
      case EcoToolbarIcon::SmartSegment:
        painter.drawRoundedRect( QRectF( 3.5, 3.5, 13, 13 ), 1.5, 1.5 );
        painter.drawPolyline( QPolygonF { QPointF( 5.5, 12.5 ), QPointF( 8.5, 8.5 ), QPointF( 11, 11 ), QPointF( 14.5, 6.5 ) } );
        circle( 8.5, 8.5, 1.15 );
        circle( 11, 11, 1.15 );
        break;
      case EcoToolbarIcon::Restoration:
        painter.drawEllipse( QRectF( 5, 3, 10, 12 ) );
        line( 10, 15, 10, 18 );
        line( 10, 13, 6.5, 8 );
        line( 10, 11, 13.5, 6 );
        break;
      case EcoToolbarIcon::Screenshot:
        line( 3, 8, 3, 3 ); line( 3, 3, 8, 3 );
        line( 12, 3, 17, 3 ); line( 17, 3, 17, 8 );
        line( 3, 12, 3, 17 ); line( 3, 17, 8, 17 );
        line( 12, 17, 17, 17 ); line( 17, 17, 17, 12 );
        painter.drawRoundedRect( QRectF( 7, 7, 6, 6 ), 1, 1 );
        break;
      case EcoToolbarIcon::Panel:
        painter.drawRoundedRect( QRectF( 3, 3, 14, 14 ), 1, 1 );
        line( 7.5, 3.5, 7.5, 16.5 );
        line( 10, 7, 14.5, 7 );
        line( 10, 10, 14.5, 10 );
        line( 10, 13, 13, 13 );
        break;
      case EcoToolbarIcon::Phase:
        painter.drawRoundedRect( QRectF( 3.5, 4, 13, 12 ), 2, 2 );
        line( 6, 8, 15, 8 );
        line( 6, 11, 12, 11 );
        circle( 5.5, 8, 1.1 );
        circle( 5.5, 11, 1.1 );
        break;
      case EcoToolbarIcon::Compare:
        painter.drawRoundedRect( QRectF( 3.5, 4, 5.5, 12 ), 1.2, 1.2 );
        painter.drawRoundedRect( QRectF( 11, 4, 5.5, 12 ), 1.2, 1.2 );
        line( 8.5, 8, 10.5, 8 );
        line( 8.5, 12, 10.5, 12 );
        line( 8.5, 8, 9.5, 7 );
        line( 8.5, 12, 9.5, 13 );
        line( 10.5, 8, 9.5, 7 );
        line( 10.5, 12, 9.5, 13 );
        break;
      case EcoToolbarIcon::Confirm:
        painter.setPen( QPen( color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        painter.drawPolyline( QPolygonF { QPointF( 3.5, 10.5 ), QPointF( 8, 15 ), QPointF( 16.5, 5 ) } );
        break;
      case EcoToolbarIcon::Delete:
        painter.drawRoundedRect( QRectF( 5.5, 6, 9, 11 ), 1, 1 );
        line( 4, 6, 16, 6 );
        line( 7.5, 3.5, 12.5, 3.5 );
        line( 8, 9, 8, 14 );
        line( 12, 9, 12, 14 );
        break;
    }
    return QIcon( pixmap );
  }

  class EcoVsCodeCheckBox final : public QCheckBox
  {
    public:
      explicit EcoVsCodeCheckBox( QWidget *parent = nullptr )
        : QCheckBox( parent )
      {
        setAttribute( Qt::WA_Hover );
      }

      QSize sizeHint() const override
      {
        return QSize( 22 + fontMetrics().horizontalAdvance( text() ), std::max( 20, fontMetrics().height() + 4 ) );
      }

    protected:
      void paintEvent( QPaintEvent *event ) override
      {
        Q_UNUSED( event )
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const QRect indicatorRect( 1, std::max( 1, ( height() - 14 ) / 2 ), 14, 14 );
        const bool checked = checkState() != Qt::Unchecked;
        const QColor borderColor = checked ? QColor( QStringLiteral( "#007acc" ) )
                                           : QColor( underMouse() ? QStringLiteral( "#c5c5c5" ) : QStringLiteral( "#6b6b6b" ) );
        painter.setPen( QPen( borderColor, 1 ) );
        painter.setBrush( checked ? QColor( QStringLiteral( "#007acc" ) ) : QColor( QStringLiteral( "#1e1e1e" ) ) );
        painter.drawRoundedRect( indicatorRect, 2, 2 );

        if ( checked )
        {
          painter.setPen( QPen( Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
          painter.drawLine( indicatorRect.left() + 3, indicatorRect.center().y(), indicatorRect.center().x() - 1, indicatorRect.bottom() - 3 );
          painter.drawLine( indicatorRect.center().x() - 1, indicatorRect.bottom() - 3, indicatorRect.right() - 2, indicatorRect.top() + 3 );
        }

        painter.setPen( isEnabled() ? QColor( QStringLiteral( "#d4d4d4" ) ) : QColor( QStringLiteral( "#777777" ) ) );
        painter.drawText( QRect( 22, 0, std::max( 0, width() - 22 ), height() ), Qt::AlignVCenter | Qt::AlignLeft, text() );
      }
  };

  class EcoRecognitionScanPreview final : public QWidget
  {
    public:
      explicit EcoRecognitionScanPreview( QWidget *parent = nullptr )
        : QWidget( parent )
      {
        setObjectName( QStringLiteral( "EcoRecognitionScanPreview" ) );
        setMinimumHeight( 176 );
        setMaximumHeight( 196 );
        setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
        connect( &mAnimationTimer, &QTimer::timeout, this, [this] {
          mScanPhase += 0.010;
          if ( mScanPhase > 1.0 )
            mScanPhase -= 1.0;
          update();
        } );
        mAnimationTimer.setInterval( 16 );
        hide();
      }

      void beginTask( int total )
      {
        mImage = QImage();
        mCachedDisplayImage = QImage();
        mCachedDisplaySize = QSize();
        mLabel = tr( "准备识别" );
        mCurrent = 0;
        mTotal = std::max( 1, total );
        mScanPhase = 0.0;
        mActive = true;
        show();
        mAnimationTimer.start();
        update();
      }

      void setCurrentImage( const QImage &image, const QString &label, int current, int total )
      {
        if ( image.isNull() )
          return;
        mImage = image;
        mCachedDisplayImage = QImage();
        mCachedDisplaySize = QSize();
        mLabel = label;
        mCurrent = current;
        mTotal = std::max( 1, total );
        mActive = true;
        show();
        if ( !mAnimationTimer.isActive() )
          mAnimationTimer.start();
        update();
      }

      void finishTask( bool success )
      {
        mActive = false;
        mAnimationTimer.stop();
        mCachedDisplayImage = QImage();
        mCachedDisplaySize = QSize();
        mLabel = success ? tr( "识别完成" ) : tr( "识别中止" );
        update();
        QTimer::singleShot( 1400, this, [this] {
          if ( !mActive )
            hide();
        } );
      }

    protected:
      void paintEvent( QPaintEvent *event ) override
      {
        Q_UNUSED( event )
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );
        painter.fillRect( rect(), QColor( QStringLiteral( "#111111" ) ) );

        const QRect imageRect = rect().adjusted( 1, 1, -1, -1 );
        if ( !mImage.isNull() )
        {
          if ( mCachedDisplayImage.isNull() || mCachedDisplaySize != imageRect.size() )
          {
            const QImage scaled = mImage.scaled( imageRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation );
            const QRect sourceRect(
              std::max( 0, ( scaled.width() - imageRect.width() ) / 2 ),
              std::max( 0, ( scaled.height() - imageRect.height() ) / 2 ),
              std::min( imageRect.width(), scaled.width() ),
              std::min( imageRect.height(), scaled.height() )
            );
            mCachedDisplayImage = scaled.copy( sourceRect );
            mCachedDisplaySize = imageRect.size();
          }
          painter.drawImage( imageRect, mCachedDisplayImage );
          painter.fillRect( imageRect, QColor( 3, 12, 18, 45 ) );
        }

        painter.setPen( QPen( QColor( QStringLiteral( "#3c3c3c" ) ), 1 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawRect( rect().adjusted( 0, 0, -1, -1 ) );

        const QColor accent( QStringLiteral( "#4fc1ff" ) );
        const int bracketLength = 15;
        const QRect bracketRect = imageRect.adjusted( 8, 8, -8, -34 );
        painter.setPen( QPen( accent, 1.5, Qt::SolidLine, Qt::SquareCap ) );
        painter.drawLine( bracketRect.topLeft(), bracketRect.topLeft() + QPoint( bracketLength, 0 ) );
        painter.drawLine( bracketRect.topLeft(), bracketRect.topLeft() + QPoint( 0, bracketLength ) );
        painter.drawLine( bracketRect.topRight(), bracketRect.topRight() - QPoint( bracketLength, 0 ) );
        painter.drawLine( bracketRect.topRight(), bracketRect.topRight() + QPoint( 0, bracketLength ) );
        painter.drawLine( bracketRect.bottomLeft(), bracketRect.bottomLeft() + QPoint( bracketLength, 0 ) );
        painter.drawLine( bracketRect.bottomLeft(), bracketRect.bottomLeft() - QPoint( 0, bracketLength ) );
        painter.drawLine( bracketRect.bottomRight(), bracketRect.bottomRight() - QPoint( bracketLength, 0 ) );
        painter.drawLine( bracketRect.bottomRight(), bracketRect.bottomRight() - QPoint( 0, bracketLength ) );

        if ( mActive && !mImage.isNull() )
        {
          painter.save();
          painter.setClipRect( bracketRect );
          const qreal sweepPos = bracketRect.top() + ( bracketRect.height() + 56.0 ) * mScanPhase - 28.0;
          QLinearGradient beam( 0, sweepPos - 18.0, 0, sweepPos + 18.0 );
          beam.setColorAt( 0.0, QColor( 79, 193, 255, 0 ) );
          beam.setColorAt( 0.35, QColor( 79, 193, 255, 16 ) );
          beam.setColorAt( 0.5, QColor( 79, 193, 255, 120 ) );
          beam.setColorAt( 0.65, QColor( 79, 193, 255, 16 ) );
          beam.setColorAt( 1.0, QColor( 79, 193, 255, 0 ) );
          painter.fillRect( bracketRect, beam );
          painter.setPen( QPen( QColor( 79, 193, 255, 235 ), 1.6 ) );
          painter.drawLine( QPointF( bracketRect.left(), sweepPos ), QPointF( bracketRect.right(), sweepPos ) );
          painter.setPen( QPen( QColor( 255, 255, 255, 110 ), 1.0, Qt::DashLine ) );
          painter.drawLine( QPointF( bracketRect.left(), sweepPos + 2.5 ), QPointF( bracketRect.right(), sweepPos + 2.5 ) );
          painter.restore();
        }

        const QRect statusRect( imageRect.left(), imageRect.bottom() - 28, imageRect.width(), 29 );
        painter.fillRect( statusRect, QColor( 24, 24, 24, 225 ) );
        painter.setPen( QColor( QStringLiteral( "#d4d4d4" ) ) );
        const QString counter = QStringLiteral( "%1 / %2" ).arg( mCurrent ).arg( mTotal );
        const int counterWidth = painter.fontMetrics().horizontalAdvance( counter ) + 10;
        painter.drawText( statusRect.adjusted( 8, 0, -counterWidth, 0 ), Qt::AlignVCenter | Qt::AlignLeft,
                          painter.fontMetrics().elidedText( mLabel, Qt::ElideRight, std::max( 20, statusRect.width() - counterWidth - 14 ) ) );
        painter.setPen( accent );
        painter.drawText( statusRect.adjusted( statusRect.width() - counterWidth, 0, -8, 0 ), Qt::AlignVCenter | Qt::AlignRight, counter );
      }

    private:
      QTimer mAnimationTimer;
      QImage mImage;
      QImage mCachedDisplayImage;
      QSize mCachedDisplaySize;
      QString mLabel;
      qreal mScanPhase = 0.0;
      int mCurrent = 0;
      int mTotal = 1;
      bool mActive = false;
  };

  void applyVsCodeMenuStyle( QMenu *menu )
  {
    if ( !menu )
      return;
    menu->setStyleSheet( QStringLiteral( R"(
      QMenu { background:#252526; color:#d4d4d4; border:1px solid #454545; padding:4px; }
      QMenu::item { min-height:24px; padding:4px 28px 4px 10px; border-radius:3px; }
      QMenu::item:selected { background:#094771; color:#ffffff; }
      QMenu::item:disabled { color:#777777; }
      QMenu::separator { height:1px; margin:4px 7px; background:#3c3c3c; }
    )" ) );
  }

  enum class EcoCaptionIcon
  {
    Float,
    Restore,
    Close,
  };

  QIcon ecoCaptionIcon( EcoCaptionIcon icon )
  {
    QPixmap pixmap( 16, 16 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QPen( QColor( QStringLiteral( "#f1f5f9" ) ), 1.15, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin ) );
    painter.setBrush( Qt::NoBrush );

    if ( icon == EcoCaptionIcon::Close )
    {
      painter.drawLine( QPointF( 4.25, 4.25 ), QPointF( 11.75, 11.75 ) );
      painter.drawLine( QPointF( 11.75, 4.25 ), QPointF( 4.25, 11.75 ) );
    }
    else if ( icon == EcoCaptionIcon::Restore )
    {
      painter.drawRect( QRectF( 5.25, 3.5, 7.0, 7.0 ) );
      painter.fillRect( QRectF( 3.5, 5.25, 7.0, 7.0 ), QColor( QStringLiteral( "#181818" ) ) );
      painter.drawRect( QRectF( 3.5, 5.25, 7.0, 7.0 ) );
    }
    else
    {
      painter.drawRect( QRectF( 4.25, 4.25, 7.5, 7.5 ) );
    }
    return QIcon( pixmap );
  }

  class EcoDockTitleBar final : public QWidget
  {
    public:
      explicit EcoDockTitleBar( QDockWidget *dock )
        : QWidget( dock )
        , mDock( dock )
      {
        setObjectName( QStringLiteral( "EcoDockTitleBar" ) );
        setFixedHeight( 31 );
        setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
        setAutoFillBackground( true );
        QPalette titlePalette = palette();
        titlePalette.setColor( QPalette::Window, QColor( QStringLiteral( "#181818" ) ) );
        titlePalette.setColor( QPalette::WindowText, QColor( QStringLiteral( "#f1f5f9" ) ) );
        setPalette( titlePalette );
        setStyleSheet( QStringLiteral( R"(
          QWidget#EcoDockTitleBar { background:#181818; border:0; }
          QLabel#EcoDockTitleLabel { color:#f1f5f9; background:transparent; border:0; font-weight:600; }
          QToolButton { width:46px; height:30px; padding:0; margin:0; color:#f1f5f9; background:transparent; border:0; border-radius:0; }
          QToolButton#EcoDockFloatButton:hover { background:#3a3d41; }
          QToolButton#EcoDockFloatButton:pressed { background:#45484d; }
          QToolButton#EcoDockCloseButton:hover { background:#c42b1c; }
          QToolButton#EcoDockCloseButton:pressed { background:#8f1d14; }
        )" ) );

        QHBoxLayout *layout = new QHBoxLayout( this );
        layout->setContentsMargins( 10, 0, 0, 0 );
        layout->setSpacing( 0 );
        QLabel *title = new QLabel( dock->windowTitle(), this );
        title->setObjectName( QStringLiteral( "EcoDockTitleLabel" ) );
        title->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
        layout->addWidget( title, 1 );

        mFloatButton = new QToolButton( this );
        mFloatButton->setObjectName( QStringLiteral( "EcoDockFloatButton" ) );
        mFloatButton->setAutoRaise( false );
        mFloatButton->setFocusPolicy( Qt::NoFocus );
        mFloatButton->setIconSize( QSize( 16, 16 ) );
        layout->addWidget( mFloatButton );

        QToolButton *closeButton = new QToolButton( this );
        closeButton->setObjectName( QStringLiteral( "EcoDockCloseButton" ) );
        closeButton->setAutoRaise( false );
        closeButton->setFocusPolicy( Qt::NoFocus );
        closeButton->setIconSize( QSize( 16, 16 ) );
        closeButton->setIcon( ecoCaptionIcon( EcoCaptionIcon::Close ) );
        closeButton->setToolTip( tr( "关闭面板" ) );
        layout->addWidget( closeButton );

        connect( dock, &QDockWidget::windowTitleChanged, title, &QLabel::setText );
        connect( closeButton, &QToolButton::clicked, dock, &QDockWidget::close );
        connect( mFloatButton, &QToolButton::clicked, dock, [this] {
          if ( !mDock )
            return;
          mDock->setFloating( !mDock->isFloating() );
          mDock->show();
          mDock->raise();
        } );
        connect( dock, &QDockWidget::topLevelChanged, this, [this]( bool ) { updateFloatButton(); } );
        updateFloatButton();
      }

    protected:
      void mousePressEvent( QMouseEvent *event ) override { event->ignore(); }
      void mouseMoveEvent( QMouseEvent *event ) override { event->ignore(); }
      void mouseReleaseEvent( QMouseEvent *event ) override { event->ignore(); }
      void mouseDoubleClickEvent( QMouseEvent *event ) override { event->ignore(); }

    private:
      void updateFloatButton()
      {
        if ( !mDock || !mFloatButton )
          return;
        const bool floating = mDock->isFloating();
        mFloatButton->setIcon( ecoCaptionIcon( floating ? EcoCaptionIcon::Restore : EcoCaptionIcon::Float ) );
        mFloatButton->setToolTip( floating ? tr( "停靠面板" ) : tr( "浮动面板" ) );
      }

      QPointer<QDockWidget> mDock;
      QPointer<QToolButton> mFloatButton;
  };

  void installEcoDockTitleBar( QDockWidget *dock )
  {
    if ( !dock || dock->property( "eco/title-bar-installed" ).toBool() )
      return;
    dock->setProperty( "eco/title-bar-installed", true );
    dock->setTitleBarWidget( new EcoDockTitleBar( dock ) );
  }

  void applyVsCodeNativeTitleBar( QWidget *window );

  QDockWidget *containingDockWidget( QWidget *widget )
  {
    for ( QWidget *candidate = widget; candidate; candidate = candidate->parentWidget() )
    {
      if ( QDockWidget *dock = qobject_cast<QDockWidget *>( candidate ) )
        return dock;
    }
    return nullptr;
  }

  void resizeBottomAttributeDock( QDockWidget *dock )
  {
    if ( !dock )
      return;
    QTimer::singleShot( 0, dock, [dock] {
      if ( dock->isFloating() )
        return;
      QMainWindow *mainWindow = qobject_cast<QMainWindow *>( dock->window() );
      if ( !mainWindow || mainWindow->dockWidgetArea( dock ) != Qt::BottomDockWidgetArea )
        return;
      mainWindow->resizeDocks( QList<QDockWidget *> { dock }, QList<int> { 300 }, Qt::Vertical );
    } );
  }

  void styleBusinessAttributeTable( QWidget *dialog )
  {
    if ( !dialog )
      return;

    const auto refreshContainerStyle = [dialog] {
      if ( QDockWidget *dock = containingDockWidget( dialog ) )
      {
        dock->setStyleSheet( QStringLiteral( R"(
          QDockWidget, QgsDockWidget { color:#d4d4d4; background:#1e1e1e; border:0; }
        )" ) );
        installEcoDockTitleBar( dock );
        if ( !dock->property( "eco/business-attribute-table-resize-connected" ).toBool() )
        {
          dock->setProperty( "eco/business-attribute-table-resize-connected", true );
          QObject::connect( dock, &QDockWidget::topLevelChanged, dock, [dock]( bool floating ) {
            if ( !floating )
              resizeBottomAttributeDock( dock );
          } );
        }
        resizeBottomAttributeDock( dock );
      }
      else if ( QWidget *window = dialog->window(); window && window->isWindow() )
      {
        applyVsCodeNativeTitleBar( window );
      }
    };
    refreshContainerStyle();
    QTimer::singleShot( 0, dialog, refreshContainerStyle );
    QTimer::singleShot( 120, dialog, refreshContainerStyle );

    if ( dialog->property( "eco/business-attribute-table-styled" ).toBool() )
      return;
    dialog->setProperty( "eco/business-attribute-table-styled", true );

    const QSet<QString> visibleActions = {
      QStringLiteral( "mActionToggleEditing" ),
      QStringLiteral( "mActionSaveEdits" ),
      QStringLiteral( "mActionReload" ),
      QStringLiteral( "mActionAddFeature" ),
      QStringLiteral( "mActionDeleteSelected" ),
      QStringLiteral( "mActionSelectAll" ),
      QStringLiteral( "mActionRemoveSelection" ),
      QStringLiteral( "mActionZoomMapToSelectedRows" ),
    };
    if ( QToolBar *toolbar = dialog->findChild<QToolBar *>( QStringLiteral( "mToolbar" ) ) )
    {
      toolbar->setIconSize( QSize( 18, 18 ) );
      for ( QAction *action : toolbar->actions() )
        action->setVisible( !action->isSeparator() && visibleActions.contains( action->objectName() ) );

      if ( QAction *addFeatureAction = dialog->findChild<QAction *>( QStringLiteral( "mActionAddFeature" ) ) )
      {
        if ( QToolButton *addFeatureButton = qobject_cast<QToolButton *>( toolbar->widgetForAction( addFeatureAction ) ) )
        {
          addFeatureButton->setDefaultAction( addFeatureAction );
          addFeatureButton->setPopupMode( QToolButton::DelayedPopup );
          if ( QAction *formAction = dialog->findChild<QAction *>( QStringLiteral( "mActionAddFeatureViaAttributeForm" ) ) )
            addFeatureButton->removeAction( formAction );
        }
      }
    }

    if ( QToolButton *tableViewButton = dialog->findChild<QToolButton *>( QStringLiteral( "mTableViewButton" ) ); tableViewButton && !tableViewButton->isChecked() )
      tableViewButton->click();

    if ( QWidget *expressionBox = dialog->findChild<QWidget *>( QStringLiteral( "mUpdateExpressionBox" ) ) )
    {
      expressionBox->setMaximumHeight( 0 );
      expressionBox->hide();
    }

    for ( const QString &widgetName : { QStringLiteral( "mFeatureFilterWidget" ), QStringLiteral( "mAttributeViewButton" ), QStringLiteral( "mTableViewButton" ) } )
    {
      if ( QWidget *widget = dialog->findChild<QWidget *>( widgetName ) )
        widget->hide();
    }

    for ( QTableView *table : dialog->findChildren<QTableView *>() )
    {
      table->setAlternatingRowColors( false );
      table->setSelectionBehavior( QAbstractItemView::SelectRows );
      table->verticalHeader()->setDefaultSectionSize( 29 );
      table->horizontalHeader()->setMinimumHeight( 31 );
      QPalette palette = table->palette();
      palette.setColor( QPalette::Base, QColor( QStringLiteral( "#181818" ) ) );
      palette.setColor( QPalette::AlternateBase, QColor( QStringLiteral( "#181818" ) ) );
      palette.setColor( QPalette::Text, QColor( QStringLiteral( "#d4d4d4" ) ) );
      palette.setColor( QPalette::Highlight, QColor( QStringLiteral( "#094771" ) ) );
      palette.setColor( QPalette::HighlightedText, QColor( Qt::white ) );
      table->setPalette( palette );
      table->setStyleSheet( QStringLiteral( R"(
        QTableView { color:#d4d4d4; background:#181818; alternate-background-color:#181818; border:0; gridline-color:#2d2d2d; selection-background-color:#094771; selection-color:#ffffff; outline:0; }
        QTableView::item { color:#d4d4d4; background:#181818; padding:4px 7px; border:0; border-bottom:1px solid #252525; }
        QTableView::item:hover { color:#ffffff; background:#2a2d2e; }
        QTableView::item:selected { color:#ffffff; background:#094771; }
        QTableView::item:disabled { color:#777777; background:#181818; }
        QHeaderView { background:#252526; border:0; }
        QHeaderView::section { min-height:30px; color:#e2e8f0; background:#252526; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #454545; padding:0 8px; font-weight:600; }
        QTableCornerButton::section { background:#252526; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #454545; }
        QScrollBar:vertical { width:12px; margin:0; background:#181818; }
        QScrollBar:horizontal { height:12px; margin:0; background:#181818; }
        QScrollBar::handle:vertical { min-height:24px; margin:2px; background:#424242; border-radius:4px; }
        QScrollBar::handle:horizontal { min-width:24px; margin:2px; background:#424242; border-radius:4px; }
        QScrollBar::handle:hover { background:#5a5a5a; }
        QScrollBar::handle:pressed { background:#6b6b6b; }
        QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { width:0; height:0; background:transparent; }
      )" ) );
    }

    dialog->setStyleSheet( dialog->styleSheet() + QStringLiteral( R"(
      QDialog QToolBar#mToolbar { min-height:36px; padding:3px 6px; spacing:3px; background:#252526; border:0; border-bottom:1px solid #3c3c3c; }
      QDialog QToolBar#mToolbar QToolButton { min-width:30px; max-width:30px; min-height:28px; max-height:28px; margin:0; padding:0; color:#d4d4d4; background:transparent; border:1px solid transparent; border-radius:3px; }
      QDialog QToolBar#mToolbar QToolButton:hover { color:#ffffff; background:#3a3d41; border-color:#4a4d52; }
      QDialog QToolBar#mToolbar QToolButton:pressed, QDialog QToolBar#mToolbar QToolButton:checked { color:#ffffff; background:#094771; border-color:#0e639c; }
      QDialog QToolBar#mToolbar QToolButton:disabled { color:#666666; background:transparent; border-color:transparent; }
      QDialog QTableView, QDialog QTreeView, QDialog QAbstractItemView { color:#d4d4d4; background:#181818; alternate-background-color:#181818; border:0; gridline-color:#2d2d2d; selection-background-color:#094771; selection-color:#ffffff; outline:0; }
      QDialog QTableView::item, QDialog QTreeView::item { padding:4px 7px; border:0; border-bottom:1px solid #2d2d2d; }
      QDialog QTableView::item:hover, QDialog QTreeView::item:hover { background:#2a2d2e; color:#ffffff; }
      QDialog QTableView::item:selected, QDialog QTreeView::item:selected { background:#094771; color:#ffffff; }
      QDialog QHeaderView { background:#252526; border:0; }
      QDialog QHeaderView::section { min-height:30px; color:#e2e8f0; background:#252526; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #454545; padding:0 8px; font-weight:600; }
      QDialog QTableCornerButton::section { background:#252526; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #454545; }
      QDialog QScrollBar:vertical { width:12px; margin:0; background:#181818; }
      QDialog QScrollBar:horizontal { height:12px; margin:0; background:#181818; }
      QDialog QScrollBar::handle:vertical { min-height:24px; margin:2px; background:#424242; border-radius:4px; }
      QDialog QScrollBar::handle:horizontal { min-width:24px; margin:2px; background:#424242; border-radius:4px; }
      QDialog QScrollBar::handle:hover { background:#5a5a5a; }
      QDialog QScrollBar::handle:pressed { background:#6b6b6b; }
      QDialog QScrollBar::add-line, QDialog QScrollBar::sub-line, QDialog QScrollBar::add-page, QDialog QScrollBar::sub-page { width:0; height:0; background:transparent; }
    )" ) );
  }

  void applyVsCodeNativeTitleBar( QWidget *window )
  {
    if ( !window || !window->isWindow() )
      return;
    // A dock widget temporarily retains the Qt::Window flag while QMainWindow
    // is docking it. Calling winId() in that state creates a non-top-level
    // native window, which produces a white frame and a QWidgetWindow warning.
    if ( QDockWidget *dock = qobject_cast<QDockWidget *>( window ); dock && !dock->isFloating() )
      return;
#ifdef Q_OS_WIN
    using DwmSetWindowAttributeFunction = long( __stdcall * )( void *, unsigned int, const void *, unsigned int );
    const auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunction>( QLibrary::resolve( QStringLiteral( "dwmapi" ), "DwmSetWindowAttribute" ) );
    if ( setWindowAttribute )
    {
      const int darkMode = 1;
      const unsigned int captionColor = 0x001E1E1E;
      const unsigned int textColor = 0x00F9F5F1;
      const unsigned int borderColor = 0x003C3C3C;
      const void *windowHandle = reinterpret_cast<void *>( window->winId() );
      setWindowAttribute( const_cast<void *>( windowHandle ), 20, &darkMode, sizeof( darkMode ) );
      setWindowAttribute( const_cast<void *>( windowHandle ), 19, &darkMode, sizeof( darkMode ) );
      // Explicitly style the native Windows title bar. Without these
      // attributes, DWM may fall back to a black inactive caption while the
      // dialog is being dragged, making the system buttons nearly invisible.
      setWindowAttribute( const_cast<void *>( windowHandle ), 35, &captionColor, sizeof( captionColor ) );
      setWindowAttribute( const_cast<void *>( windowHandle ), 36, &textColor, sizeof( textColor ) );
      setWindowAttribute( const_cast<void *>( windowHandle ), 34, &borderColor, sizeof( borderColor ) );
    }
#endif
  }

  void applyVsCodeDialogStyle( QWidget *dialog )
  {
    if ( !dialog )
      return;
    // Qt styles the dialog body, while DWM owns the system close, maximize
    // and minimize buttons shown in the native title bar.
    applyVsCodeNativeTitleBar( dialog );
    dialog->setStyleSheet( QStringLiteral( R"(
    QDialog, QMessageBox { background:#1e1e1e; color:#d4d4d4; }
    QDialog QWidget { background:#1e1e1e; color:#d4d4d4; }
    QDialog QLabel, QMessageBox QLabel { color:#d4d4d4; }
    QDialog QGroupBox { color:#cbd5e1; border:1px solid #3c3c3c; border-radius:3px; margin-top:9px; padding:8px 6px 6px 6px; }
    QDialog QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; }
    QDialog QScrollArea, QDialog QTableView, QDialog QTreeView { background:#1e1e1e; color:#d4d4d4; border:1px solid #3c3c3c; }
    QDialog QHeaderView::section { background:#252526; color:#cbd5e1; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #3c3c3c; padding:5px 7px; }
    QDialog QTabWidget::pane { border:1px solid #3c3c3c; background:#1e1e1e; }
    QDialog QTabBar::tab { color:#aeb7c2; background:#252526; border:1px solid #3c3c3c; padding:5px 10px; }
    QDialog QTabBar::tab:selected { color:#ffffff; background:#1e1e1e; border-bottom-color:#0e639c; }
    QDialog QCheckBox, QDialog QRadioButton { color:#d4d4d4; spacing:6px; }
    QDialog QLineEdit, QDialog QComboBox, QDialog QSpinBox, QDialog QDoubleSpinBox, QDialog QDateEdit, QDialog QDateTimeEdit, QDialog QgsDateTimeEdit {
      min-height:26px; color:#f1f5f9; background:#252526; border:1px solid #454545; border-radius:3px; padding:2px 7px;
    }
    QDialog QDateEdit::drop-down, QDialog QDateTimeEdit::drop-down, QDialog QgsDateTimeEdit::drop-down {
      subcontrol-origin:padding; subcontrol-position:top right; width:24px; background:#333333; border:0; border-left:1px solid #454545;
      border-top-right-radius:3px; border-bottom-right-radius:3px;
    }
    QDialog QDateEdit::drop-down:hover, QDialog QDateTimeEdit::drop-down:hover, QDialog QgsDateTimeEdit::drop-down:hover { background:#094771; border-left-color:#1177bb; }
    QDialog QDateEdit::down-arrow, QDialog QDateTimeEdit::down-arrow, QDialog QgsDateTimeEdit::down-arrow {
      image:url(:/images/themes/default/mActionArrowDown.svg); width:12px; height:12px;
    }
    QDialog QTextEdit, QDialog QPlainTextEdit { color:#f1f5f9; background:#252526; border:1px solid #454545; border-radius:3px; padding:4px 6px; }
      QDialog QLineEdit:focus, QDialog QComboBox:focus, QDialog QSpinBox:focus, QDialog QDoubleSpinBox:focus, QDialog QDateEdit:focus, QDialog QDateTimeEdit:focus, QDialog QgsDateTimeEdit:focus { border-color:#007fd4; }
      QCalendarWidget { background:#1e1e1e; color:#d4d4d4; border:1px solid #454545; }
      QCalendarWidget QWidget#qt_calendar_navigationbar { background:#252526; border-bottom:1px solid #3c3c3c; }
      QCalendarWidget QToolButton { min-width:24px; min-height:24px; color:#f1f5f9; background:transparent; border:1px solid transparent; border-radius:3px; padding:1px 5px; }
      QCalendarWidget QToolButton:hover { background:#2a2d2e; border-color:#3c3c3c; color:#ffffff; }
      QCalendarWidget QToolButton:pressed { background:#37373d; border-color:#454545; }
      QCalendarWidget QToolButton#qt_calendar_prevmonth, QCalendarWidget QToolButton#qt_calendar_nextmonth { min-width:24px; max-width:24px; min-height:24px; max-height:24px; color:#c5d1de; font-size:17px; font-weight:600; image:none; padding:0; }
      QCalendarWidget QSpinBox { min-height:22px; color:#f1f5f9; background:#1e1e1e; border:1px solid #454545; padding:1px 4px; }
      QCalendarWidget QAbstractItemView { color:#d4d4d4; background:#1e1e1e; selection-background-color:#0e639c; selection-color:#ffffff; outline:0; }
      QCalendarWidget QAbstractItemView:disabled { color:#666666; }
      QDialog QPushButton, QMessageBox QPushButton {
        min-width:76px; min-height:27px; color:#f1f5f9; background:#333333; border:1px solid #454545; border-radius:3px; padding:2px 10px;
      }
      QDialog QPushButton:hover, QMessageBox QPushButton:hover { background:#414141; border-color:#606060; }
      QDialog QPushButton:default, QMessageBox QPushButton:default { background:#0e639c; border-color:#1177bb; color:#ffffff; }
      QDialog QDialogButtonBox, QMessageBox QDialogButtonBox { button-layout:0; }
    )" ) );
  }

  QList<QPair<QString, Qgis::MarkerShape>> towerMarkerShapeChoices()
  {
    return {
      { QObject::tr( "圆点" ), Qgis::MarkerShape::Circle },
      { QObject::tr( "方形" ), Qgis::MarkerShape::Square },
      { QObject::tr( "菱形" ), Qgis::MarkerShape::Diamond },
      { QObject::tr( "三角形" ), Qgis::MarkerShape::Triangle },
      { QObject::tr( "五边形" ), Qgis::MarkerShape::Pentagon },
      { QObject::tr( "六边形" ), Qgis::MarkerShape::Hexagon },
      { QObject::tr( "星形" ), Qgis::MarkerShape::Star },
      { QObject::tr( "十字" ), Qgis::MarkerShape::CrossFill },
      { QObject::tr( "盾牌" ), Qgis::MarkerShape::Shield },
      { QObject::tr( "圆角方形" ), Qgis::MarkerShape::RoundedSquare },
    };
  }

  std::unique_ptr<QgsMarkerSymbol> createTowerMarkerSymbol(
    Qgis::MarkerShape shape,
    const QColor &fillColor,
    const QColor &strokeColor,
    double opacityPercent,
    double size,
    double strokeWidth
  )
  {
    QVariantMap properties;
    properties.insert( QStringLiteral( "name" ), QgsSimpleMarkerSymbolLayerBase::encodeShape( shape ) );
    properties.insert( QStringLiteral( "color" ), fillColor.name( QColor::HexRgb ) );
    properties.insert( QStringLiteral( "outline_color" ), strokeColor.name( QColor::HexRgb ) );
    properties.insert( QStringLiteral( "outline_width" ), strokeWidth );
    properties.insert( QStringLiteral( "size" ), size );
    std::unique_ptr<QgsMarkerSymbol> symbol = QgsMarkerSymbol::createSimple( properties );
    symbol->setOpacity( std::clamp( opacityPercent / 100.0, 0.0, 1.0 ) );
    symbol->setSize( size );
    symbol->setSizeUnit( Qgis::RenderUnit::Millimeters );
    return symbol;
  }

  class EcoTowerStyleDialog final : public QDialog
  {
    public:
      EcoTowerStyleDialog( QgsVectorLayer *layer, QgisApp *app )
        : QDialog( app )
        , mLayer( layer )
        , mApp( app )
      {
        setWindowTitle( tr( "修改杆塔样式" ) );
        setMinimumWidth( 420 );
        if ( mLayer && mLayer->renderer() )
          mOriginalRenderer.reset( mLayer->renderer()->clone() );
        readCurrentSymbol();

        QVBoxLayout *layout = new QVBoxLayout( this );
        layout->setContentsMargins( 14, 12, 14, 12 );
        layout->setSpacing( 10 );

        QWidget *previewRow = new QWidget( this );
        QHBoxLayout *previewLayout = new QHBoxLayout( previewRow );
        previewLayout->setContentsMargins( 0, 0, 0, 0 );
        previewLayout->setSpacing( 12 );
        mPreviewLabel = new QLabel( previewRow );
        mPreviewLabel->setFixedSize( 64, 64 );
        mPreviewLabel->setAlignment( Qt::AlignCenter );
        mPreviewLabel->setStyleSheet( QStringLiteral( "QLabel { background:#252526; border:1px solid #3c3c3c; border-radius:4px; }" ) );
        QLabel *previewText = new QLabel( tr( "杆塔点符号预览" ), previewRow );
        previewText->setProperty( "muted", true );
        previewLayout->addWidget( mPreviewLabel );
        previewLayout->addWidget( previewText, 1 );
        layout->addWidget( previewRow );

        QFormLayout *form = new QFormLayout;
        form->setContentsMargins( 0, 0, 0, 0 );
        form->setHorizontalSpacing( 10 );
        form->setVerticalSpacing( 8 );

        mShapeCombo = new QComboBox( this );
        const QList<QPair<QString, Qgis::MarkerShape>> shapes = towerMarkerShapeChoices();
        for ( const QPair<QString, Qgis::MarkerShape> &shape : shapes )
          mShapeCombo->addItem( shape.first, static_cast<int>( shape.second ) );
        selectShape( mShape );
        form->addRow( tr( "替换图标" ), mShapeCombo );

        mFillButton = new QPushButton( this );
        form->addRow( tr( "颜色" ), mFillButton );

        mOpacitySpin = new QDoubleSpinBox( this );
        mOpacitySpin->setRange( 0.0, 100.0 );
        mOpacitySpin->setDecimals( 0 );
        mOpacitySpin->setSingleStep( 5.0 );
        mOpacitySpin->setSuffix( tr( " %" ) );
        mOpacitySpin->setValue( mOpacityPercent );
        form->addRow( tr( "透明度" ), mOpacitySpin );

        QWidget *borderControl = new QWidget( this );
        QHBoxLayout *borderLayout = new QHBoxLayout( borderControl );
        borderLayout->setContentsMargins( 0, 0, 0, 0 );
        borderLayout->setSpacing( 6 );
        mStrokeButton = new QPushButton( borderControl );
        mStrokeWidthSpin = new QDoubleSpinBox( borderControl );
        mStrokeWidthSpin->setRange( 0.0, 10.0 );
        mStrokeWidthSpin->setDecimals( 2 );
        mStrokeWidthSpin->setSingleStep( 0.1 );
        mStrokeWidthSpin->setSuffix( tr( " mm" ) );
        mStrokeWidthSpin->setValue( mStrokeWidth );
        borderLayout->addWidget( mStrokeButton, 1 );
        borderLayout->addWidget( mStrokeWidthSpin );
        form->addRow( tr( "边框" ), borderControl );

        mSizeSpin = new QDoubleSpinBox( this );
        mSizeSpin->setRange( 0.5, 30.0 );
        mSizeSpin->setDecimals( 1 );
        mSizeSpin->setSingleStep( 0.5 );
        mSizeSpin->setSuffix( tr( " mm" ) );
        mSizeSpin->setValue( mSize );
        form->addRow( tr( "大小" ), mSizeSpin );

        layout->addLayout( form );

        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        connect( buttons, &QDialogButtonBox::accepted, this, &EcoTowerStyleDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, this, &EcoTowerStyleDialog::reject );
        layout->addWidget( buttons );

        applyVsCodeDialogStyle( this );
        refreshColorButtons();
        refreshShapeIcons();
        updatePreview();

        connect( mShapeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this] { applyUiStyle(); } );
        connect( mFillButton, &QPushButton::clicked, this, [this] { chooseColor( mFillColor ); } );
        connect( mStrokeButton, &QPushButton::clicked, this, [this] { chooseColor( mStrokeColor ); } );
        connect( mOpacitySpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double ) { applyUiStyle(); } );
        connect( mSizeSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double ) { applyUiStyle(); } );
        connect( mStrokeWidthSpin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), this, [this]( double ) { applyUiStyle(); } );
      }

      void accept() override
      {
        applyUiStyle();
        QgsProject::instance()->setDirty( true );
        QDialog::accept();
      }

      void reject() override
      {
        if ( mLayer && mOriginalRenderer )
        {
          mLayer->setRenderer( mOriginalRenderer->clone() );
          mLayer->triggerRepaint();
          if ( mApp && mApp->mapCanvas() )
            mApp->mapCanvas()->refresh();
        }
        QDialog::reject();
      }

    private:
      void readCurrentSymbol()
      {
        std::unique_ptr<QgsSingleSymbolRenderer> singleRenderer;
        if ( mLayer && mLayer->renderer() )
          singleRenderer.reset( QgsSingleSymbolRenderer::convertFromRenderer( mLayer->renderer() ) );

        QgsSymbol *symbol = singleRenderer ? singleRenderer->symbol() : nullptr;
        std::unique_ptr<QgsMarkerSymbol> marker;
        if ( symbol && symbol->type() == Qgis::SymbolType::Marker )
          marker.reset( static_cast<QgsMarkerSymbol *>( symbol->clone() ) );
        if ( !marker )
          marker = createTowerMarkerSymbol( mShape, mFillColor, mStrokeColor, mOpacityPercent, mSize, mStrokeWidth );

        mOpacityPercent = std::clamp( marker->opacity() * 100.0, 0.0, 100.0 );
        mSize = std::max( 0.5, marker->size() );
        if ( marker->symbolLayerCount() > 0 )
        {
          if ( QgsSimpleMarkerSymbolLayer *simpleMarker = dynamic_cast<QgsSimpleMarkerSymbolLayer *>( marker->symbolLayer( 0 ) ) )
          {
            mShape = simpleMarker->shape();
            mFillColor = simpleMarker->color();
            mStrokeColor = simpleMarker->strokeColor();
            mStrokeWidth = simpleMarker->strokeWidth();
          }
        }
      }

      Qgis::MarkerShape selectedShape() const
      {
        return static_cast<Qgis::MarkerShape>( mShapeCombo ? mShapeCombo->currentData().toInt() : static_cast<int>( Qgis::MarkerShape::Circle ) );
      }

      void selectShape( Qgis::MarkerShape shape )
      {
        if ( !mShapeCombo )
          return;
        for ( int i = 0; i < mShapeCombo->count(); ++i )
        {
          if ( static_cast<Qgis::MarkerShape>( mShapeCombo->itemData( i ).toInt() ) == shape )
          {
            mShapeCombo->setCurrentIndex( i );
            return;
          }
        }
        mShapeCombo->setCurrentIndex( 0 );
      }

      void chooseColor( QColor &color )
      {
        QColorDialog dialog( color, this );
        dialog.setOption( QColorDialog::DontUseNativeDialog, true );
        applyVsCodeDialogStyle( &dialog );
        if ( dialog.exec() != QDialog::Accepted || !dialog.selectedColor().isValid() )
          return;
        color = dialog.selectedColor();
        refreshColorButtons();
        refreshShapeIcons();
        applyUiStyle();
      }

      void refreshColorButton( QPushButton *button, const QColor &color )
      {
        if ( !button )
          return;
        const QColor textColor = color.lightness() < 150 ? QColor( QStringLiteral( "#ffffff" ) ) : QColor( QStringLiteral( "#111827" ) );
        button->setText( color.name( QColor::HexRgb ).toUpper() );
        button->setStyleSheet( QStringLiteral( "QPushButton { min-height:26px; color:%1; background:%2; border:1px solid #454545; border-radius:3px; padding:2px 10px; } QPushButton:hover { border-color:#007fd4; }" )
                                 .arg( textColor.name( QColor::HexRgb ), color.name( QColor::HexRgb ) ) );
      }

      void refreshColorButtons()
      {
        refreshColorButton( mFillButton, mFillColor );
        refreshColorButton( mStrokeButton, mStrokeColor );
      }

      void refreshShapeIcons()
      {
        if ( !mShapeCombo )
          return;
        for ( int i = 0; i < mShapeCombo->count(); ++i )
        {
          const Qgis::MarkerShape shape = static_cast<Qgis::MarkerShape>( mShapeCombo->itemData( i ).toInt() );
          std::unique_ptr<QgsMarkerSymbol> symbol = createTowerMarkerSymbol( shape, mFillColor, mStrokeColor, mOpacityPercent, mSize, mStrokeWidth );
          mShapeCombo->setItemIcon( i, QgsSymbolLayerUtils::symbolPreviewIcon( symbol.get(), QSize( 22, 22 ), 2 ) );
        }
      }

      void updatePreview()
      {
        std::unique_ptr<QgsMarkerSymbol> symbol = createCurrentSymbol();
        if ( mPreviewLabel )
          mPreviewLabel->setPixmap( QgsSymbolLayerUtils::symbolPreviewIcon( symbol.get(), QSize( 54, 54 ), 4 ).pixmap( 54, 54 ) );
      }

      std::unique_ptr<QgsMarkerSymbol> createCurrentSymbol() const
      {
        const double opacity = mOpacitySpin ? mOpacitySpin->value() : mOpacityPercent;
        const double size = mSizeSpin ? mSizeSpin->value() : mSize;
        const double strokeWidth = mStrokeWidthSpin ? mStrokeWidthSpin->value() : mStrokeWidth;
        return createTowerMarkerSymbol( selectedShape(), mFillColor, mStrokeColor, opacity, size, strokeWidth );
      }

      void applyUiStyle()
      {
        if ( mShapeCombo )
          mShape = selectedShape();
        if ( mOpacitySpin )
          mOpacityPercent = mOpacitySpin->value();
        if ( mSizeSpin )
          mSize = mSizeSpin->value();
        if ( mStrokeWidthSpin )
          mStrokeWidth = mStrokeWidthSpin->value();
        refreshShapeIcons();
        updatePreview();
        if ( !mLayer )
          return;
        std::unique_ptr<QgsMarkerSymbol> symbol = createCurrentSymbol();
        mLayer->setRenderer( new QgsSingleSymbolRenderer( symbol->clone() ) );
        mLayer->triggerRepaint();
        if ( mApp && mApp->mapCanvas() )
          mApp->mapCanvas()->refresh();
      }

      QPointer<QgsVectorLayer> mLayer;
      QgisApp *mApp = nullptr;
      std::unique_ptr<QgsFeatureRenderer> mOriginalRenderer;
      QLabel *mPreviewLabel = nullptr;
      QComboBox *mShapeCombo = nullptr;
      QPushButton *mFillButton = nullptr;
      QPushButton *mStrokeButton = nullptr;
      QDoubleSpinBox *mOpacitySpin = nullptr;
      QDoubleSpinBox *mSizeSpin = nullptr;
      QDoubleSpinBox *mStrokeWidthSpin = nullptr;
      Qgis::MarkerShape mShape = Qgis::MarkerShape::Circle;
      QColor mFillColor = QColor( QStringLiteral( "#ef4444" ) );
      QColor mStrokeColor = QColor( QStringLiteral( "#7f1d1d" ) );
      double mOpacityPercent = 95.0;
      double mSize = 3.0;
      double mStrokeWidth = 0.4;
  };

  constexpr double sDefaultTowerIconWidthPixels = 25.0;
  constexpr double sDefaultTowerIconHeightPixels = 35.0;
  constexpr double sTowerIconAspectRatio = sDefaultTowerIconHeightPixels / sDefaultTowerIconWidthPixels;
  const QString sTowerIconWidthProperty = QStringLiteral( "eco/towerIconWidthPixels" );
  const QString sTowerIconPathProperty = QStringLiteral( "eco/towerIconPath" );
  const QString sTowerIconCustomSizeProperty = QStringLiteral( "eco/towerIconSizeCustomized" );
  const QString sTowerDisplayModeProperty = QStringLiteral( "eco/towerDisplayMode" );

  QString towerIconResourcePath()
  {
    return QStringLiteral( ":/images/icons/tower.png" );
  }

  bool iconPathExists( const QString &path )
  {
    if ( path.isEmpty() )
      return false;
    return path.startsWith( QLatin1String( ":/" ) ) ? QFile::exists( path ) : QFileInfo::exists( path );
  }

  QString ensureProjectTowerIconAsset( const QString &workspacePath )
  {
    const QString fallback = towerIconResourcePath();
    if ( workspacePath.trimmed().isEmpty() )
      return fallback;

    QDir workspace( workspacePath );
    if ( !workspace.exists() )
      return fallback;

    workspace.mkpath( QStringLiteral( "assets" ) );
    const QString assetPath = workspace.filePath( QStringLiteral( "assets/tower.png" ) );
    if ( QFileInfo::exists( assetPath ) )
      return assetPath;

    QFile source( fallback );
    if ( source.exists() )
      QFile::copy( fallback, assetPath );

    return QFileInfo::exists( assetPath ) ? assetPath : fallback;
  }

  QString towerIconPathForLayer( QgsVectorLayer *layer, const QString &workspacePath )
  {
    const QString customPath = layer ? layer->customProperty( sTowerIconPathProperty ).toString().trimmed() : QString();
    if ( iconPathExists( customPath ) )
      return customPath;

    const QString projectAssetPath = ensureProjectTowerIconAsset( workspacePath );
    if ( iconPathExists( projectAssetPath ) )
      return projectAssetPath;

    return towerIconResourcePath();
  }

  std::unique_ptr<QgsMarkerSymbol> createTowerIconMarkerSymbol( const QString &iconPath, double widthPixels )
  {
    widthPixels = std::clamp( widthPixels, 12.0, 160.0 );
    QgsSymbolLayerList layers;
    QgsRasterMarkerSymbolLayer *markerLayer = new QgsRasterMarkerSymbolLayer( iconPath, widthPixels, 0.0, Qgis::ScaleMethod::ScaleArea );
    markerLayer->setSize( widthPixels );
    markerLayer->setSizeUnit( Qgis::RenderUnit::Pixels );
    markerLayer->setFixedAspectRatio( sTowerIconAspectRatio );
    markerLayer->setHorizontalAnchorPoint( Qgis::HorizontalAnchorPoint::Center );
    markerLayer->setVerticalAnchorPoint( Qgis::VerticalAnchorPoint::Bottom );
    markerLayer->setOpacity( 1.0 );
    layers << markerLayer;

    std::unique_ptr<QgsMarkerSymbol> symbol = std::make_unique<QgsMarkerSymbol>( layers );
    symbol->setSize( widthPixels );
    symbol->setSizeUnit( Qgis::RenderUnit::Pixels );
    symbol->setOpacity( 1.0 );
    return symbol;
  }

  void applyTowerIconRenderer( QgsVectorLayer *layer, const QString &workspacePath, double widthPixels = sDefaultTowerIconWidthPixels, bool customSize = false )
  {
    if ( !layer || layer->geometryType() != Qgis::GeometryType::Point )
      return;

    const QString iconPath = towerIconPathForLayer( layer, workspacePath );
    std::unique_ptr<QgsMarkerSymbol> symbol = createTowerIconMarkerSymbol( iconPath, widthPixels );
    layer->setRenderer( new QgsSingleSymbolRenderer( symbol.release() ) );
    layer->setCustomProperty( sTowerIconPathProperty, iconPath );
    layer->setCustomProperty( sTowerIconWidthProperty, widthPixels );
    layer->setCustomProperty( sTowerIconCustomSizeProperty, customSize );
    layer->setCustomProperty( sTowerDisplayModeProperty, QStringLiteral( "icon" ) );
  }

  class EcoTowerIconSizeDialog final : public QDialog
  {
    public:
      EcoTowerIconSizeDialog( QgsVectorLayer *layer, QgisApp *app, const QString &workspacePath )
        : QDialog( app )
        , mLayer( layer )
        , mApp( app )
        , mWorkspacePath( workspacePath )
      {
        setWindowTitle( tr( "修改杆塔样式" ) );
        setMinimumWidth( 365 );
        if ( mLayer && mLayer->renderer() )
          mOriginalRenderer.reset( mLayer->renderer()->clone() );
        mOriginalIconPath = mLayer ? mLayer->customProperty( sTowerIconPathProperty ).toString() : QString();
        mOriginalDisplayMode = mLayer ? mLayer->customProperty( sTowerDisplayModeProperty ).toString() : QString();
        mOriginalCustomSize = mLayer ? mLayer->customProperty( sTowerIconCustomSizeProperty, false ).toBool() : false;
        mOriginalIconWidth = mOriginalCustomSize
                               ? ( mLayer ? mLayer->customProperty( sTowerIconWidthProperty, sDefaultTowerIconWidthPixels ).toDouble() : sDefaultTowerIconWidthPixels )
                               : sDefaultTowerIconWidthPixels;

        QVBoxLayout *layout = new QVBoxLayout( this );
        layout->setContentsMargins( 14, 12, 14, 12 );
        layout->setSpacing( 10 );

        QWidget *previewRow = new QWidget( this );
        QHBoxLayout *previewLayout = new QHBoxLayout( previewRow );
        previewLayout->setContentsMargins( 0, 0, 0, 0 );
        previewLayout->setSpacing( 12 );
        mPreviewLabel = new QLabel( previewRow );
        mPreviewLabel->setFixedSize( 78, 86 );
        mPreviewLabel->setAlignment( Qt::AlignCenter );
        mPreviewLabel->setStyleSheet( QStringLiteral( "QLabel { background:#252526; border:1px solid #3c3c3c; border-radius:4px; }" ) );
        QLabel *previewText = new QLabel( tr( "杆塔点将使用内置图标，默认宽?25px，按 40脳56 的比例等比缩放。" ), previewRow );
        previewText->setWordWrap( true );
        previewText->setProperty( "muted", true );
        previewLayout->addWidget( mPreviewLabel );
        previewLayout->addWidget( previewText, 1 );
        layout->addWidget( previewRow );

        QFormLayout *form = new QFormLayout;
        form->setContentsMargins( 0, 0, 0, 0 );
        form->setHorizontalSpacing( 10 );
        form->setVerticalSpacing( 8 );

        mSizeSpin = new QSpinBox( this );
        mSizeSpin->setRange( 12, 160 );
        mSizeSpin->setSingleStep( 2 );
        mSizeSpin->setSuffix( tr( " px" ) );
        mSizeSpin->setValue( static_cast<int>( std::round( std::clamp( mOriginalIconWidth, 12.0, 160.0 ) ) ) );
        form->addRow( tr( "图标宽度" ), mSizeSpin );

        mSizeHintLabel = new QLabel( this );
        mSizeHintLabel->setProperty( "muted", true );
        form->addRow( QString(), mSizeHintLabel );
        layout->addLayout( form );

        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        connect( buttons, &QDialogButtonBox::accepted, this, &EcoTowerIconSizeDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, this, &EcoTowerIconSizeDialog::reject );
        layout->addWidget( buttons );

        applyVsCodeDialogStyle( this );
        connect( mSizeSpin, QOverload<int>::of( &QSpinBox::valueChanged ), this, [this]( int ) { applyUiStyle(); } );
        updatePreview();
      }

      void accept() override
      {
        applyUiStyle();
        QgsProject::instance()->setDirty( true );
        QDialog::accept();
      }

      void reject() override
      {
        if ( mLayer && mOriginalRenderer )
        {
          mLayer->setRenderer( mOriginalRenderer->clone() );
          mLayer->setCustomProperty( sTowerIconPathProperty, mOriginalIconPath );
          mLayer->setCustomProperty( sTowerDisplayModeProperty, mOriginalDisplayMode );
          mLayer->setCustomProperty( sTowerIconWidthProperty, mOriginalIconWidth );
          mLayer->setCustomProperty( sTowerIconCustomSizeProperty, mOriginalCustomSize );
          mLayer->triggerRepaint();
          if ( mApp && mApp->mapCanvas() )
            mApp->mapCanvas()->refresh();
        }
        QDialog::reject();
      }

    private:
      int currentWidth() const
      {
        return mSizeSpin ? mSizeSpin->value() : static_cast<int>( sDefaultTowerIconWidthPixels );
      }

      void updatePreview()
      {
        const int width = currentWidth();
        const int height = static_cast<int>( std::round( width * sTowerIconAspectRatio ) );
        if ( mSizeHintLabel )
          mSizeHintLabel->setText( tr( "当前显示尺寸?1 脳 %2 px" ).arg( width ).arg( height ) );

        if ( !mPreviewLabel )
          return;

        QPixmap pixmap( towerIconPathForLayer( mLayer, mWorkspacePath ) );
        if ( pixmap.isNull() )
          pixmap = QPixmap( towerIconResourcePath() );
        mPreviewLabel->setPixmap( pixmap.scaled( QSize( width, height ), Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
      }

      void applyUiStyle()
      {
        if ( !mLayer )
          return;
        applyTowerIconRenderer( mLayer, mWorkspacePath, currentWidth(), true );
        updatePreview();
        mLayer->triggerRepaint();
        if ( mApp && mApp->mapCanvas() )
          mApp->mapCanvas()->refresh();
      }

      QPointer<QgsVectorLayer> mLayer;
      QgisApp *mApp = nullptr;
      QString mWorkspacePath;
      std::unique_ptr<QgsFeatureRenderer> mOriginalRenderer;
      QString mOriginalIconPath;
      QString mOriginalDisplayMode;
      bool mOriginalCustomSize = false;
      double mOriginalIconWidth = sDefaultTowerIconWidthPixels;
      QLabel *mPreviewLabel = nullptr;
      QLabel *mSizeHintLabel = nullptr;
      QSpinBox *mSizeSpin = nullptr;
  };

  QString integratedTowerDisturbanceModelPath()
  {
    return QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "models/disturbanceX_best_new.onnx" ) );
  }

  QString integratedSam2ModelDirectory()
  {
    return QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "models/sam2-onnx/sam2.1-hiera-tiny" ) );
  }

  bool hasIntegratedSam2Models()
  {
    const QDir modelDirectory( integratedSam2ModelDirectory() );
    return modelDirectory.exists( QStringLiteral( "vision_encoder.onnx" ) )
           && modelDirectory.exists( QStringLiteral( "prompt_encoder_mask_decoder.onnx" ) );
  }

  class EcoSam2PromptMapTool final : public QgsMapTool
  {
    public:
      using PromptHandler = std::function<void( const QVector<QgsPointXY> &, const QVector<int> & )>;
      using ConfirmHandler = std::function<void()>;
      using CancelHandler = std::function<void()>;

      EcoSam2PromptMapTool( QgsMapCanvas *canvas, PromptHandler promptHandler, ConfirmHandler confirmHandler, CancelHandler cancelHandler )
        : QgsMapTool( canvas )
        , mPromptHandler( std::move( promptHandler ) )
        , mConfirmHandler( std::move( confirmHandler ) )
        , mCancelHandler( std::move( cancelHandler ) )
      {
        setCursor( Qt::CrossCursor );
        mHoverTimer.setInterval( 2000 );
        mHoverTimer.setSingleShot( true );
        QObject::connect( &mHoverTimer, &QTimer::timeout, this, [this] { requestPreview( true ); } );
      }

      ~EcoSam2PromptMapTool() override
      {
        delete mHoverBand;
        delete mPositiveBand;
        delete mNegativeBand;
        delete mPreviewBand;
      }

      void resetSession()
      {
        mPoints.clear();
        mLabels.clear();
        mHasHoverPoint = false;
        mHoverTimer.stop();
        if ( mHoverBand )
          mHoverBand->reset( Qgis::GeometryType::Point );
        if ( mPositiveBand )
          mPositiveBand->reset( Qgis::GeometryType::Point );
        if ( mNegativeBand )
          mNegativeBand->reset( Qgis::GeometryType::Point );
        clearPreviewGeometry();
      }

      void clearPreviewGeometry()
      {
        if ( mPreviewBand )
        {
          mPreviewBand->reset( Qgis::GeometryType::Polygon );
          mPreviewBand->hide();
        }
      }

      void setPreviewColors( const QColor &strokeColor, const QColor &fillColor )
      {
        mPreviewStrokeColor = strokeColor;
        mPreviewFillColor = fillColor;
        if ( mPreviewBand )
        {
          mPreviewBand->setStrokeColor( mPreviewStrokeColor );
          mPreviewBand->setFillColor( mPreviewFillColor );
        }
      }

      void setPreviewGeometry( const QgsGeometry &geometry )
      {
        if ( geometry.isNull() || geometry.isEmpty() )
        {
          clearPreviewGeometry();
          return;
        }
        if ( !mPreviewBand )
        {
          mPreviewBand = new QgsRubberBand( canvas(), Qgis::GeometryType::Polygon );
          mPreviewBand->setWidth( 1.8 );
        }
        mPreviewBand->setStrokeColor( mPreviewStrokeColor );
        mPreviewBand->setFillColor( mPreviewFillColor );
        mPreviewBand->setToGeometry( geometry );
        mPreviewBand->show();
      }

      void canvasMoveEvent( QgsMapMouseEvent *event ) override
      {
        if ( !event )
          return;
        mHoverPoint = event->mapPoint();
        mHasHoverPoint = true;
        updateHoverBand();
        if ( mPoints.isEmpty() )
          mHoverTimer.start();
        else
          mHoverTimer.stop();
      }

      void canvasReleaseEvent( QgsMapMouseEvent *event ) override
      {
        if ( !event )
          return;
        if ( event->button() == Qt::LeftButton )
        {
          addPromptPoint( event->mapPoint(), 1 );
          requestPreview( false );
          return;
        }
        if ( event->button() == Qt::RightButton )
        {
          addPromptPoint( event->mapPoint(), 0 );
          requestPreview( false );
          return;
        }
      }

      void canvasDoubleClickEvent( QgsMapMouseEvent *event ) override
      {
        if ( event )
          event->accept();
        if ( mConfirmHandler )
          QTimer::singleShot( 0, this, [this] {
            if ( mConfirmHandler )
              mConfirmHandler();
          } );
      }

      void keyPressEvent( QKeyEvent *event ) override
      {
        if ( event->key() == Qt::Key_Escape )
        {
          resetSession();
          if ( mCancelHandler )
            mCancelHandler();
          event->accept();
          return;
        }
        if ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
        {
          event->accept();
          if ( mConfirmHandler )
            QTimer::singleShot( 0, this, [this] {
              if ( mConfirmHandler )
                mConfirmHandler();
            } );
          return;
        }
        if ( event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete )
        {
          if ( !mPoints.isEmpty() )
          {
            mPoints.removeLast();
            mLabels.removeLast();
            updatePromptBands();
            if ( mPoints.isEmpty() )
            {
              clearPreviewGeometry();
              if ( mPromptHandler )
                mPromptHandler( mPoints, mLabels );
            }
            else
            {
              requestPreview( false );
            }
          }
          event->accept();
          return;
        }
        QgsMapTool::keyPressEvent( event );
      }

    private:
      QgsRubberBand *ensurePointBand( QgsRubberBand *&band, const QColor &color, Qgis::RubberBandIconType icon, double size )
      {
        if ( !band )
        {
          band = new QgsRubberBand( canvas(), Qgis::GeometryType::Point );
          band->setIcon( icon );
          band->setIconSize( size );
          band->setWidth( 1.2 );
        }
        band->setStrokeColor( color );
        band->setFillColor( color );
        return band;
      }

      void updateHoverBand()
      {
        if ( !mHasHoverPoint )
          return;
        QgsRubberBand *band = ensurePointBand( mHoverBand, QColor( 248, 113, 113, 220 ), Qgis::RubberBandIconType::Circle, 6 );
        band->reset( Qgis::GeometryType::Point );
        band->addPoint( mHoverPoint );
        band->show();
      }

      void updatePromptBands()
      {
        QgsRubberBand *positiveBand = ensurePointBand( mPositiveBand, QColor( 239, 68, 68, 245 ), Qgis::RubberBandIconType::Circle, 8 );
        QgsRubberBand *negativeBand = ensurePointBand( mNegativeBand, QColor( 96, 165, 250, 245 ), Qgis::RubberBandIconType::CrossX, 9 );
        positiveBand->reset( Qgis::GeometryType::Point );
        negativeBand->reset( Qgis::GeometryType::Point );
        for ( int index = 0; index < mPoints.size(); ++index )
        {
          if ( mLabels.value( index, 1 ) == 0 )
            negativeBand->addPoint( mPoints.at( index ), false );
          else
            positiveBand->addPoint( mPoints.at( index ), false );
        }
        positiveBand->updatePosition();
        negativeBand->updatePosition();
        positiveBand->show();
        negativeBand->show();
      }

      void addPromptPoint( const QgsPointXY &point, int label )
      {
        mHoverTimer.stop();
        mPoints.append( point );
        mLabels.append( label == 0 ? 0 : 1 );
        mHoverPoint = point;
        mHasHoverPoint = true;
        if ( !mHoverBand )
          updateHoverBand();
        else
          mHoverBand->hide();
        updatePromptBands();
      }

      void requestPreview( bool includeHoverPoint )
      {
        QVector<QgsPointXY> points = mPoints;
        QVector<int> labels = mLabels;
        if ( includeHoverPoint && mHasHoverPoint )
        {
          points.append( mHoverPoint );
          labels.append( 1 );
        }
        if ( points.isEmpty() || !mPromptHandler )
          return;
        mPromptHandler( points, labels );
      }

      PromptHandler mPromptHandler;
      ConfirmHandler mConfirmHandler;
      CancelHandler mCancelHandler;
      QTimer mHoverTimer;
      QgsRubberBand *mHoverBand = nullptr;
      QgsRubberBand *mPositiveBand = nullptr;
      QgsRubberBand *mNegativeBand = nullptr;
      QgsRubberBand *mPreviewBand = nullptr;
      QVector<QgsPointXY> mPoints;
      QVector<int> mLabels;
      QgsPointXY mHoverPoint;
      bool mHasHoverPoint = false;
      QColor mPreviewStrokeColor = QColor( 56, 189, 248, 235 );
      QColor mPreviewFillColor = QColor( 56, 189, 248, 88 );
  };

  constexpr const char *sRecognitionVisibilityField = "eco_show";

  void styleEcoCalendarWidget( QCalendarWidget *calendar )
  {
    if ( !calendar )
      return;

    calendar->setFixedSize( 300, 215 );
    calendar->setVerticalHeaderFormat( QCalendarWidget::NoVerticalHeader );
    calendar->setStyleSheet( QStringLiteral( R"(
      QCalendarWidget { background:#1e1e1e; color:#d4d4d4; border:1px solid #454545; }
      QWidget#qt_calendar_navigationbar { min-height:34px; background:#252526; border-bottom:1px solid #3c3c3c; }
      QToolButton { min-width:24px; min-height:24px; color:#f1f5f9; background:transparent; border:1px solid transparent; border-radius:3px; padding:1px 5px; qproperty-iconSize:0px 0px; }
      QToolButton:hover { background:#2a2d2e; border-color:#3c3c3c; color:#ffffff; }
      QToolButton:pressed { background:#37373d; border-color:#454545; color:#ffffff; }
      QToolButton#qt_calendar_prevmonth, QToolButton#qt_calendar_nextmonth { image:none; min-width:24px; max-width:24px; min-height:24px; max-height:24px; color:#c5d1de; font-size:17px; font-weight:600; padding:0; }
      QToolButton#qt_calendar_monthbutton, QToolButton#qt_calendar_yearbutton { color:#f1f5f9; font-weight:600; min-width:58px; border-color:transparent; }
      QSpinBox#qt_calendar_yearedit { min-height:24px; min-width:62px; color:#f1f5f9; background:#1e1e1e; border:1px solid #454545; border-radius:3px; padding:1px 18px 1px 5px; }
      QSpinBox#qt_calendar_yearedit::up-button, QSpinBox#qt_calendar_yearedit::down-button { width:16px; background:#333333; border-left:1px solid #454545; }
      QSpinBox#qt_calendar_yearedit::up-button:hover, QSpinBox#qt_calendar_yearedit::down-button:hover { background:#094771; }
      QSpinBox#qt_calendar_yearedit::up-arrow { image:url(:/images/themes/default/mActionArrowUp.svg); width:10px; height:10px; }
      QSpinBox#qt_calendar_yearedit::down-arrow { image:url(:/images/themes/default/mActionArrowDown.svg); width:10px; height:10px; }
      QTableView#qt_calendar_calendarview { background:#1e1e1e; color:#d4d4d4; border:0; selection-background-color:#0e639c; selection-color:#ffffff; outline:0; }
      QTableView#qt_calendar_calendarview::item { min-width:30px; min-height:22px; padding:1px; border-radius:3px; }
      QTableView#qt_calendar_calendarview::item:selected { background:#0e639c; color:#ffffff; }
      QTableView#qt_calendar_calendarview::item:hover { background:#264f78; color:#ffffff; }
      QTableView#qt_calendar_calendarview::item:disabled { color:#666666; }
      QHeaderView::section { min-height:20px; color:#9fb1c7; background:#1e1e1e; border:0; font-weight:600; }
      QMenu { background:#252526; color:#f1f5f9; border:1px solid #454545; }
      QMenu::item:selected { background:#094771; }
    )" ) );

    QWidget *popup = calendar;
    for ( QWidget *candidate = calendar->parentWidget(); candidate; candidate = candidate->parentWidget() )
    {
      if ( ( candidate->windowFlags() & Qt::WindowType_Mask ) == Qt::Popup )
      {
        popup = candidate;
        break;
      }
    }
    if ( popup != calendar )
      popup->setFixedSize( 300, 215 );

    if ( QToolButton *previousButton = calendar->findChild<QToolButton *>( QStringLiteral( "qt_calendar_prevmonth" ) ) )
    {
      previousButton->setIcon( QIcon() );
      previousButton->setText( QStringLiteral( "鈥筡" ) );
      previousButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
      previousButton->setAutoRaise( true );
      previousButton->setFixedSize( 24, 24 );
      previousButton->setCursor( Qt::PointingHandCursor );
    }
    if ( QToolButton *nextButton = calendar->findChild<QToolButton *>( QStringLiteral( "qt_calendar_nextmonth" ) ) )
    {
      nextButton->setIcon( QIcon() );
      nextButton->setText( QStringLiteral( "鈥篭" ) );
      nextButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
      nextButton->setAutoRaise( true );
      nextButton->setFixedSize( 24, 24 );
      nextButton->setCursor( Qt::PointingHandCursor );
    }
  }

  int ensureRecognitionVisibilityField( QgsVectorLayer *layer )
  {
    if ( !layer )
      return -1;

    int index = layer->fields().indexFromName( QLatin1String( sRecognitionVisibilityField ) );
    if ( index < 0 )
    {
      const bool alreadyEditable = layer->isEditable();
      bool added = false;
      if ( alreadyEditable )
        added = layer->addAttribute( QgsField( QLatin1String( sRecognitionVisibilityField ), QMetaType::Type::Int ) );
      else if ( layer->dataProvider() )
        added = layer->dataProvider()->addAttributes( { QgsField( QLatin1String( sRecognitionVisibilityField ), QMetaType::Type::Int ) } );
      if ( !added )
        return -1;

      layer->updateFields();
      index = layer->fields().indexFromName( QLatin1String( sRecognitionVisibilityField ) );
    }

    if ( index >= 0 )
    {
      layer->setFieldAlias( index, QObject::tr( "图斑显示" ) );
      layer->setDefaultValueDefinition( index, QgsDefaultValue( QStringLiteral( "1" ) ) );
      layer->setEditorWidgetSetup( index, QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), QVariantMap() ) );
    }
    return index;
  }

  QIcon ecoBusinessTreeIcon( QgsLayerTreeNode *node )
  {
    QString kind;
    if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ) )
    {
      kind = group->customProperty( QStringLiteral( "eco/treeIcon" ) ).toString();
      if ( kind.isEmpty() )
      {
        const QString name = group->name();
        if ( group->customProperty( QStringLiteral( "eco/projectRoot" ) ).toBool() )
          kind = QStringLiteral( "project" );
        else if ( group->customProperty( QStringLiteral( "eco/groupType" ) ).toString() == QLatin1String( "phase" ) )
          kind = QStringLiteral( "phase" );
        else if ( name.contains( QStringLiteral( "公共" ) ) )
          kind = QStringLiteral( "common" );
        else if ( name.contains( QStringLiteral( "影像" ) ) )
          kind = QStringLiteral( "imagery" );
        else if ( name.contains( QStringLiteral( "截图" ) ) )
          kind = QStringLiteral( "screenshots" );
        else if ( name.contains( QStringLiteral( "扰动" ) ) )
          kind = QStringLiteral( "disturbance" );
        else if ( name.contains( QStringLiteral( "人工" ) ) || name.contains( QStringLiteral( "核查" ) ) )
          kind = QStringLiteral( "review" );
        else if ( name.contains( QStringLiteral( "分割" ) ) )
          kind = QStringLiteral( "segmentation" );
        else if ( name.contains( QStringLiteral( "成果" ) ) )
          kind = QStringLiteral( "results" );
        else if ( name.contains( QStringLiteral( "杆塔" ) ) || name.contains( QStringLiteral( "线路" ) ) )
          kind = QStringLiteral( "line" );
        else if ( name.contains( QStringLiteral( "范围" ) ) )
          kind = QStringLiteral( "scope" );
        else
          kind = QStringLiteral( "group" );
      }
    }
    else if ( QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node ) )
    {
      if ( QgsMapLayer *layer = layerNode->layer() )
      {
        const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
        const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
        if ( qobject_cast<QgsRasterLayer *>( layer ) )
          kind = QStringLiteral( "raster" );
        else if ( category == QLatin1String( "recognition-result" ) || resultType == QLatin1String( "construction-disturbance" ) )
          kind = QStringLiteral( "disturbance-layer" );
        else if ( category == QLatin1String( "smart-segmentation" ) || resultType == QLatin1String( "smart-segmentation" ) )
          kind = QStringLiteral( "segmentation-layer" );
        else if ( QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
        {
          switch ( vectorLayer->geometryType() )
          {
            case Qgis::GeometryType::Point:
              kind = QStringLiteral( "point-layer" );
              break;
            case Qgis::GeometryType::Line:
              kind = QStringLiteral( "line-layer" );
              break;
            case Qgis::GeometryType::Polygon:
              kind = QStringLiteral( "polygon-layer" );
              break;
            default:
              kind = QStringLiteral( "vector-layer" );
              break;
          }
        }
      }
    }
    if ( kind.isEmpty() )
      kind = QStringLiteral( "group" );

    QColor color( QStringLiteral( "#7dd3fc" ) );
    QColor accent( QStringLiteral( "#dbeafe" ) );
    if ( kind == QLatin1String( "project" ) )
      color = QColor( QStringLiteral( "#60a5fa" ) );
    else if ( kind == QLatin1String( "common" ) )
      color = QColor( QStringLiteral( "#a78bfa" ) );
    else if ( kind == QLatin1String( "phase" ) )
      color = QColor( QStringLiteral( "#22d3ee" ) );
    else if ( kind == QLatin1String( "results" ) )
      color = QColor( QStringLiteral( "#38bdf8" ) );
    else if ( kind == QLatin1String( "disturbance" ) || kind == QLatin1String( "disturbance-layer" ) )
      color = QColor( QStringLiteral( "#f87171" ) );
    else if ( kind == QLatin1String( "review" ) )
      color = QColor( QStringLiteral( "#fb923c" ) );
    else if ( kind == QLatin1String( "segmentation" ) || kind == QLatin1String( "segmentation-layer" ) )
      color = QColor( QStringLiteral( "#22c55e" ) );
    else if ( kind == QLatin1String( "screenshots" ) )
      color = QColor( QStringLiteral( "#fbbf24" ) );
    else if ( kind == QLatin1String( "imagery" ) || kind == QLatin1String( "raster" ) )
      color = QColor( QStringLiteral( "#34d399" ) );
    else if ( kind == QLatin1String( "line" ) || kind == QLatin1String( "line-layer" ) )
      color = QColor( QStringLiteral( "#facc15" ) );
    else if ( kind == QLatin1String( "scope" ) || kind == QLatin1String( "polygon-layer" ) )
      color = QColor( QStringLiteral( "#c084fc" ) );
    else if ( kind == QLatin1String( "point-layer" ) )
      color = QColor( QStringLiteral( "#38bdf8" ) );
    else if ( kind == QLatin1String( "vector-layer" ) )
      color = QColor( QStringLiteral( "#93c5fd" ) );

    QPixmap pixmap( 18, 18 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QPen( color.darker( 125 ), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
    painter.setBrush( QColor( color.red(), color.green(), color.blue(), 54 ) );

    const QRectF r( 2.5, 3.0, 13.0, 12.0 );
    if ( kind == QLatin1String( "project" ) )
    {
      painter.drawRoundedRect( r, 2.2, 2.2 );
      painter.setBrush( color );
      painter.drawEllipse( QRectF( 6.0, 6.0, 6.0, 6.0 ) );
    }
    else if ( kind == QLatin1String( "phase" ) )
    {
      painter.drawRoundedRect( r, 1.8, 1.8 );
      painter.drawLine( QPointF( 2.8, 6.3 ), QPointF( 15.2, 6.3 ) );
      painter.setPen( QPen( color, 1.4, Qt::SolidLine, Qt::RoundCap ) );
      painter.drawPoint( QPointF( 6, 10 ) );
      painter.drawPoint( QPointF( 10, 10 ) );
    }
    else if ( kind == QLatin1String( "raster" ) || kind == QLatin1String( "imagery" ) )
    {
      painter.drawRoundedRect( r, 1.8, 1.8 );
      painter.setPen( QPen( color, 1.0 ) );
      painter.drawLine( QPointF( 6.8, 3.3 ), QPointF( 6.8, 14.7 ) );
      painter.drawLine( QPointF( 11.2, 3.3 ), QPointF( 11.2, 14.7 ) );
      painter.drawLine( QPointF( 2.8, 7.2 ), QPointF( 15.2, 7.2 ) );
      painter.drawLine( QPointF( 2.8, 11.0 ), QPointF( 15.2, 11.0 ) );
    }
    else if ( kind.endsWith( QLatin1String( "-layer" ) ) )
    {
      painter.setBrush( QColor( color.red(), color.green(), color.blue(), 72 ) );
      if ( kind == QLatin1String( "point-layer" ) )
      {
        painter.setBrush( color );
        painter.drawEllipse( QPointF( 9, 9 ), 4.4, 4.4 );
      }
      else if ( kind == QLatin1String( "line-layer" ) )
      {
        painter.setPen( QPen( color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        painter.drawPolyline( QPolygonF() << QPointF( 3, 12 ) << QPointF( 7, 6 ) << QPointF( 11, 10 ) << QPointF( 15, 4 ) );
      }
      else
      {
        painter.drawPolygon( QPolygonF() << QPointF( 4, 12.5 ) << QPointF( 5.8, 5 ) << QPointF( 13.5, 4.8 ) << QPointF( 15, 12 ) << QPointF( 9, 15 ) );
      }
    }
    else
    {
      painter.drawRoundedRect( QRectF( 2.3, 5.0, 13.4, 9.8 ), 1.8, 1.8 );
      painter.drawRoundedRect( QRectF( 3.2, 3.2, 5.2, 3.2 ), 1.0, 1.0 );
      painter.setPen( QPen( accent, 1.1, Qt::SolidLine, Qt::RoundCap ) );
      if ( kind == QLatin1String( "disturbance" ) )
        painter.drawLine( QPointF( 6, 10 ), QPointF( 12, 10 ) );
      else if ( kind == QLatin1String( "segmentation" ) )
        painter.drawEllipse( QRectF( 6.1, 7.6, 5.8, 5.0 ) );
      else if ( kind == QLatin1String( "screenshots" ) )
        painter.drawRect( QRectF( 5.5, 7.2, 7.0, 5.0 ) );
      else
        painter.drawLine( QPointF( 5.5, 10 ), QPointF( 12.5, 10 ) );
    }
    painter.end();
    return QIcon( pixmap );
  }

  class EcoLayerTreeItemDelegate final : public QStyledItemDelegate
  {
    public:
      explicit EcoLayerTreeItemDelegate( QgsLayerTreeView *view )
        : QStyledItemDelegate( view )
        , mView( view )
      {
      }

      void paint( QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index ) const override
      {
        if ( !mView )
          return;

        QStyleOptionViewItem styledOption = option;
        initStyleOption( &styledOption, index );
        QgsLayerTreeNode *node = mView->index2node( index );
        if ( node )
        {
          painter->save();
          painter->restore();
          styledOption.icon = ecoBusinessTreeIcon( node );
          styledOption.decorationSize = QSize( 18, 18 );
        }
        styledOption.rect = option.rect;
        mView->style()->drawControl( QStyle::CE_ItemViewItem, &styledOption, painter, mView );

        if ( QgsLayerTreeNode *node = mView->index2node( index ) )
        {
          QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node );
          if ( layerNode && layerNode->layer() && layerNode->layer()->customProperty( QStringLiteral( "eco/manualTowerDrawingActive" ), false ).toBool() )
          {
            const QRect indicatorRect( option.rect.right() - 24, option.rect.center().y() - 8, 16, 16 );
            painter->save();
            painter->setRenderHint( QPainter::Antialiasing );
            const bool hovered = option.state.testFlag( QStyle::State_MouseOver );
            painter->setPen( QPen( QColor( hovered ? QStringLiteral( "#6ee7b7" ) : QStringLiteral( "#4ade80" ) ), 1.2 ) );
            painter->setBrush( QColor( hovered ? QStringLiteral( "#064e3b" ) : QStringLiteral( "#052e1d" ) ) );
            painter->drawEllipse( indicatorRect.adjusted( 1, 1, -1, -1 ) );
            painter->setPen( QPen( QColor( QStringLiteral( "#86efac" ) ), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
            painter->drawLine( QPointF( indicatorRect.left() + 4.0, indicatorRect.center().y() + 0.5 ),
                               QPointF( indicatorRect.left() + 7.0, indicatorRect.bottom() - 4.0 ) );
            painter->drawLine( QPointF( indicatorRect.left() + 7.0, indicatorRect.bottom() - 4.0 ),
                               QPointF( indicatorRect.right() - 3.5, indicatorRect.top() + 4.0 ) );
            painter->restore();
          }
        }

        if ( !index.data( Qt::CheckStateRole ).isValid() )
          return;
        const QRect checkRect = mView->style()->subElementRect( QStyle::SE_ItemViewItemCheckIndicator, &styledOption, mView ).adjusted( 1, 1, -1, -1 );
        if ( !checkRect.isValid() )
          return;

        const Qt::CheckState checkState = static_cast<Qt::CheckState>( index.data( Qt::CheckStateRole ).toInt() );
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        const bool hovered = option.state.testFlag( QStyle::State_MouseOver );
        painter->setPen( QPen( checkState == Qt::Unchecked
                                ? QColor( hovered ? QStringLiteral( "#c5c5c5" ) : QStringLiteral( "#6b6b6b" ) )
                                : QColor( QStringLiteral( "#007acc" ) ), 1.0 ) );
        painter->setBrush( checkState == Qt::Unchecked ? QColor( QStringLiteral( "#1e1e1e" ) ) : QColor( QStringLiteral( "#007acc" ) ) );
        painter->drawRoundedRect( checkRect, 2, 2 );
        if ( checkState == Qt::Checked )
        {
          painter->setPen( QPen( Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
          painter->drawLine( checkRect.left() + 3, checkRect.center().y(), checkRect.center().x() - 1, checkRect.bottom() - 3 );
          painter->drawLine( checkRect.center().x() - 1, checkRect.bottom() - 3, checkRect.right() - 2, checkRect.top() + 3 );
        }
        else if ( checkState == Qt::PartiallyChecked )
        {
          painter->setPen( QPen( Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap ) );
          painter->drawLine( checkRect.left() + 3, checkRect.center().y(), checkRect.right() - 3, checkRect.center().y() );
        }
        painter->restore();
      }

    private:
      QgsLayerTreeView *mView = nullptr;
  };

  class EcoSwitchButton final : public QToolButton
  {
    public:
      explicit EcoSwitchButton( QWidget *parent = nullptr )
        : QToolButton( parent )
      {
        setCheckable( true );
        setCursor( Qt::PointingHandCursor );
        setFixedSize( 44, 22 );
        setFocusPolicy( Qt::NoFocus );
      }

    protected:
      void paintEvent( QPaintEvent *event ) override
      {
        Q_UNUSED( event )
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );
        const bool active = isChecked();
        const bool hovered = underMouse();
        const QRectF track = rect().adjusted( 1, 1, -1, -1 );
        painter.setPen( QPen( QColor( active ? QStringLiteral( "#4fc1ff" ) : hovered ? QStringLiteral( "#6b7280" ) : QStringLiteral( "#4b5563" ) ), 1.0 ) );
        painter.setBrush( QColor( active ? QStringLiteral( "#0e639c" ) : QStringLiteral( "#3c3c3c" ) ) );
        painter.drawRoundedRect( track, track.height() / 2.0, track.height() / 2.0 );

        const qreal knobDiameter = track.height() - 5.0;
        const qreal knobX = active ? track.right() - knobDiameter - 2.5 : track.left() + 2.5;
        const QRectF knob( knobX, track.top() + 2.5, knobDiameter, knobDiameter );
        painter.setPen( Qt::NoPen );
        painter.setBrush( QColor( active ? QStringLiteral( "#ffffff" ) : QStringLiteral( "#c5c5c5" ) ) );
        painter.drawEllipse( knob );
      }
  };

  bool shouldUseTowerIconRenderer( const QgsVectorLayer *layer );

  class EcoLayerTreeMenuProvider final : public QgsLayerTreeViewMenuProvider
  {
    public:
      EcoLayerTreeMenuProvider( QgsLayerTreeView *view,
                                QgisApp *app,
                                std::function<void( const QString & )> showRecognitionResults,
                                std::function<void( const QString & )> showTowerStyle,
                                std::function<void( const QString & )> toggleTowerDisplay,
                                std::function<void()> createTower,
                                std::function<void()> importTowerVectors,
                                std::function<void()> createPhase,
                                std::function<void( const QString & )> addImageryToPhase,
                                std::function<void( const QString & )> setCurrentPhase,
                                std::function<void( const QString & )> deletePhase,
                                std::function<void( const QStringList & )> removeLayersSafely,
                                std::function<void( const QStringList & )> locateLayers,
                                std::function<void( const QString & )> exportDisturbanceResultYoloSamples,
                                std::function<void( const QString & )> startSmartSegmentationForLayer )
        : mView( view )
        , mApp( app )
        , mShowRecognitionResults( std::move( showRecognitionResults ) )
        , mShowTowerStyle( std::move( showTowerStyle ) )
        , mToggleTowerDisplay( std::move( toggleTowerDisplay ) )
        , mCreateTower( std::move( createTower ) )
        , mImportTowerVectors( std::move( importTowerVectors ) )
        , mCreatePhase( std::move( createPhase ) )
        , mAddImageryToPhase( std::move( addImageryToPhase ) )
        , mSetCurrentPhase( std::move( setCurrentPhase ) )
        , mDeletePhase( std::move( deletePhase ) )
        , mRemoveLayersSafely( std::move( removeLayersSafely ) )
        , mLocateLayers( std::move( locateLayers ) )
        , mExportDisturbanceResultYoloSamples( std::move( exportDisturbanceResultYoloSamples ) )
        , mStartSmartSegmentationForLayer( std::move( startSmartSegmentationForLayer ) )
      {
      }

      QMenu *createContextMenu() override
      {
        QMenu *menu = new QMenu( mView );
        applyVsCodeMenuStyle( menu );
        if ( !mView )
          return menu;

        const QModelIndex cursorIndex = mView->indexAt( mView->viewport()->mapFromGlobal( QCursor::pos() ) );
        if ( cursorIndex.isValid() )
        {
          if ( QItemSelectionModel *selection = mView->selectionModel() )
          {
            if ( !selection->isSelected( cursorIndex ) )
              selection->select( cursorIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
          }
          mView->setCurrentIndex( cursorIndex );
        }

        const QList<QgsLayerTreeNode *> nodes = mView->selectedNodes( true );
        QList<QgsLayerTreeLayer *> layerNodes = mView->selectedLayerNodes();
        QgsLayerTreeNode *primaryNode = cursorIndex.isValid() ? mView->index2node( cursorIndex )
                                                              : ( nodes.size() == 1 ? nodes.constFirst() : nullptr );
        QgsLayerTreeGroup *primaryGroup = qobject_cast<QgsLayerTreeGroup *>( primaryNode );
        const bool isPhaseNode = primaryGroup && primaryGroup->customProperty( QStringLiteral( "eco/groupType" ) ).toString() == QLatin1String( "phase" );
        QString phaseId;
        for ( QgsLayerTreeNode *current = primaryNode; current; current = current->parent() )
        {
          if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( current ) )
          {
            phaseId = group->customProperty( QStringLiteral( "eco/phaseId" ) ).toString().trimmed();
            if ( !phaseId.isEmpty() )
              break;
          }
        }
        if ( layerNodes.isEmpty() )
        {
          if ( QgsLayerTreeLayer *primaryLayerNode = qobject_cast<QgsLayerTreeLayer *>( primaryNode ) )
            layerNodes << primaryLayerNode;
        }
        if ( !layerNodes.isEmpty() )
        {
          QAction *zoomAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::FullExtent ), QObject::tr( "定位至图层" ) );
          QObject::connect( zoomAction, &QAction::triggered, menu, [app = mApp, layerNodes, locateLayers = mLocateLayers] {
            const QStringList layerIds = [layerNodes] {
              QStringList ids;
              for ( QgsLayerTreeLayer *node : layerNodes )
              {
                if ( node && node->layer() )
                  ids << node->layer()->id();
              }
              return ids;
            }();
            if ( locateLayers )
            {
              if ( app )
                QTimer::singleShot( 0, app, [locateLayers, layerIds] { locateLayers( layerIds ); } );
              else
                locateLayers( layerIds );
            }
            else if ( app )
              app->actionZoomToLayer()->trigger();
          } );
        }
        else if ( !nodes.isEmpty() )
        {
          QAction *zoomAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::FullExtent ), QObject::tr( "定位至工程范围" ) );
          QObject::connect( zoomAction, &QAction::triggered, menu, [app = mApp, primaryGroup, isPhaseNode, locateLayers = mLocateLayers] {
            if ( isPhaseNode && primaryGroup && locateLayers )
            {
              const QStringList phaseLayerIds = [primaryGroup] {
                QStringList ids;
                for ( QgsLayerTreeLayer *node : primaryGroup->findLayers() )
                {
                  if ( node && node->layer() )
                    ids << node->layer()->id();
                }
                return ids;
              }();
              if ( !phaseLayerIds.isEmpty() )
              {
                if ( app )
                  QTimer::singleShot( 0, app, [locateLayers, phaseLayerIds] { locateLayers( phaseLayerIds ); } );
                else
                  locateLayers( phaseLayerIds );
                return;
              }
            }
            if ( app && app->mapCanvas() )
              app->mapCanvas()->zoomToFullExtent();
          } );
        }

        if ( !nodes.isEmpty() )
        {
          const bool visible = std::any_of( nodes.cbegin(), nodes.cend(), []( QgsLayerTreeNode *node ) { return node && node->itemVisibilityChecked(); } );
          QAction *visibilityAction = menu->addAction( visible ? QObject::tr( "隐藏图层" ) : QObject::tr( "显示图层" ) );
          QObject::connect( visibilityAction, &QAction::triggered, menu, [nodes, visible] {
            for ( QgsLayerTreeNode *node : nodes )
            {
              if ( node )
                node->setItemVisibilityChecked( !visible );
            }
          } );
        }

        if ( primaryGroup )
        {
          const bool isProjectRoot = primaryGroup->customProperty( QStringLiteral( "eco/projectRoot" ) ).toBool()
                                     || primaryGroup->customProperty( QStringLiteral( "eco/groupType" ) ).toString() == QLatin1String( "工程" );
          const bool isPhaseNode = primaryGroup->customProperty( QStringLiteral( "eco/groupType" ) ).toString() == QLatin1String( "phase" );
          const bool isImageryNode = primaryGroup->name().startsWith( QStringLiteral( "03 影像数据" ) );

          if ( isProjectRoot )
          {
            menu->addSeparator();
            QAction *createPhaseAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::Phase ), QObject::tr( "新建期次" ) );
            createPhaseAction->setToolTip( QObject::tr( "在当前工程下创建一个新的影像解译期次" ) );
            QObject::connect( createPhaseAction, &QAction::triggered, menu, [createPhase = mCreatePhase] {
              if ( createPhase )
                createPhase();
            } );
            menu->addSeparator();
            menu->addAction( QObject::tr( "全部展开" ), mView, &QgsLayerTreeView::expandAll );
            menu->addAction( QObject::tr( "全部折叠" ), mView, &QgsLayerTreeView::collapseAll );
          }
          else if ( !phaseId.isEmpty() && ( isPhaseNode || isImageryNode ) )
          {
            menu->addSeparator();
            QAction *addImageryAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::Raster ), QObject::tr( "添加影像到此期次…" ) );
            addImageryAction->setToolTip( QObject::tr( "灏?TIF/TIFF 影像归档到当前期次的“影像数据”节点" ) );
            QObject::connect( addImageryAction, &QAction::triggered, menu, [addImageryToPhase = mAddImageryToPhase, phaseId] {
              if ( addImageryToPhase )
                addImageryToPhase( phaseId );
            } );

            const bool isCurrent = phaseId == QgsProject::instance()->readEntry( QStringLiteral( "生态修复" ), QStringLiteral( "currentPhaseId" ) ).trimmed();
            if ( !isCurrent )
            {
              QAction *setCurrentAction = menu->addAction( QObject::tr( "设为当前期次" ) );
              QObject::connect( setCurrentAction, &QAction::triggered, menu, [setCurrentPhase = mSetCurrentPhase, phaseId] {
                if ( setCurrentPhase )
                  setCurrentPhase( phaseId );
              } );
            }
            if ( isPhaseNode )
            {
              QAction *deletePhaseAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::Delete ), QObject::tr( "删除期次" ) );
              deletePhaseAction->setToolTip( QObject::tr( "删除该期次下的影像、解译成果和截图等工程数据" ) );
              QObject::connect( deletePhaseAction, &QAction::triggered, menu, [deletePhase = mDeletePhase, phaseId] {
                if ( deletePhase )
                  deletePhase( phaseId );
              } );
            }
          }
        }

        if ( nodes.count() == 1 )
        {
          if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( nodes.constFirst() );
               group && ( group->customProperty( QStringLiteral( "eco/treeIcon" ) ).toString() == QLatin1String( "line" )
                          || group->customProperty( QStringLiteral( "eco/groupType" ) ).toString() == QLatin1String( "line-data" )
                          || group->name().contains( QObject::tr( "杆塔与线路" ) ) ) )
          {
            menu->addSeparator();
            QAction *createTowerAction = menu->addAction( QgsApplication::getThemeIcon( QStringLiteral( "mActionCapturePoint.svg" ) ), QObject::tr( "绘制杆塔点" ) );
            createTowerAction->setToolTip( QObject::tr( "建立带杆塔编号、名称和状态属性的杆塔点图层" ) );
            QObject::connect( createTowerAction, &QAction::triggered, menu, [createTower = mCreateTower] {
              if ( createTower )
                createTower();
            } );
            QAction *importTowerAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::Vector ), QObject::tr( "添加杆塔矢量…" ) );
            importTowerAction->setToolTip( QObject::tr( "导入现成的杆塔点矢量并自动归档到“杆塔与线路”" ) );
            QObject::connect( importTowerAction, &QAction::triggered, menu, [importTowerVectors = mImportTowerVectors] {
              if ( importTowerVectors )
                importTowerVectors();
            } );
          }
        }

        if ( !layerNodes.isEmpty() )
        {
          if ( layerNodes.count() == 1 && layerNodes.constFirst()->layer() && qobject_cast<QgsVectorLayer *>( layerNodes.constFirst()->layer() ) )
          {
            const QString layerId = layerNodes.constFirst()->layer()->id();
            QgsVectorLayer *selectedLayer = qobject_cast<QgsVectorLayer *>( layerNodes.constFirst()->layer() );
            QAction *attributesAction = menu->addAction( QgsApplication::getThemeIcon( QStringLiteral( "mActionOpenTable.svg" ) ), QObject::tr( "编辑属性表" ) );
            QObject::connect( attributesAction, &QAction::triggered, menu, [app = mApp, layerId] {
              QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( layerId ) );
              if ( app && layer )
              {
                app->setActiveLayer( layer );
                if ( !layer->isEditable() )
                  app->toggleEditing( layer );
                app->actionOpenTable()->trigger();
              }
            } );
            if ( selectedLayer && selectedLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "construction-disturbance" ) )
            {
              QAction *resultsAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::Disturbance ), QObject::tr( "扰动结果表格" ) );
              QObject::connect( resultsAction, &QAction::triggered, menu, [showResults = mShowRecognitionResults, layerId] {
                if ( showResults )
                  showResults( layerId );
              } );
              QAction *exportYoloAction = menu->addAction( QObject::tr( "导出YOLO样本…" ) );
              QObject::connect( exportYoloAction, &QAction::triggered, menu, [this, layerId] {
                if ( mExportDisturbanceResultYoloSamples )
                  mExportDisturbanceResultYoloSamples( layerId );
              } );
            }
            if ( selectedLayer && ( selectedLayer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" )
                                   || selectedLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "smart-segmentation" ) ) )
            {
              QAction *activateSmartSegmentationAction = menu->addAction( ecoToolbarIcon( EcoToolbarIcon::SmartSegment ), QObject::tr( "激活智能分割工具" ) );
              activateSmartSegmentationAction->setToolTip( QObject::tr( "在当前智能分割成果图层中继续新增图斑，结果将自动写回该图层。" ) );
              QObject::connect( activateSmartSegmentationAction, &QAction::triggered, menu, [startSmartSegmentationForLayer = mStartSmartSegmentationForLayer, layerId] {
                if ( startSmartSegmentationForLayer )
                  startSmartSegmentationForLayer( layerId );
              } );
            }
          if ( selectedLayer && selectedLayer->geometryType() == Qgis::GeometryType::Point )
          {
            QAction *styleAction = menu->addAction( QObject::tr( "修改杆塔样式" ) );
            QObject::connect( styleAction, &QAction::triggered, menu, [showTowerStyle = mShowTowerStyle, layerId] {
              if ( showTowerStyle )
                showTowerStyle( layerId );
            } );
            QAction *displayAction = menu->addAction( shouldUseTowerIconRenderer( selectedLayer ) ? QObject::tr( "显示矢量点" ) : QObject::tr( "显示图标点" ) );
            QObject::connect( displayAction, &QAction::triggered, menu, [toggleTowerDisplay = mToggleTowerDisplay, layerId] {
              if ( toggleTowerDisplay )
                toggleTowerDisplay( layerId );
            } );
          }
            QAction *exportAction = menu->addAction( QObject::tr( "导出矢量…" ) );
            QObject::connect( exportAction, &QAction::triggered, menu, [app = mApp, layerId] {
              QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( layerId ) );
              if ( app && layer )
              {
                app->setActiveLayer( layer );
                app->saveAsFile( layer );
              }
            } );
          }
          const QStringList layerIds = [layerNodes] {
            QStringList ids;
            for ( QgsLayerTreeLayer *node : layerNodes )
            {
              if ( node && node->layer() )
                ids << node->layer()->id();
            }
            return ids;
          }();
          QAction *removeAction = menu->addAction( QObject::tr( "从工程中移除" ) );
          QObject::connect( removeAction, &QAction::triggered, menu, [view = mView, layerIds, removeLayersSafely = mRemoveLayersSafely] {
            QMessageBox confirmation( QMessageBox::Question, QObject::tr( "移除图层" ), QObject::tr( "确定从当前工程中移除所选图层吗？源数据文件不会被删除。" ), QMessageBox::Yes | QMessageBox::No, view );
            confirmation.setDefaultButton( QMessageBox::No );
            applyVsCodeDialogStyle( &confirmation );
            if ( layerIds.isEmpty() || confirmation.exec() != QMessageBox::Yes )
              return;
            if ( removeLayersSafely )
              removeLayersSafely( layerIds );
          } );
          menu->addSeparator();
        }

        if ( menu->actions().isEmpty() )
        {
          menu->addAction( QObject::tr( "全部展开" ), mView, &QgsLayerTreeView::expandAll );
          menu->addAction( QObject::tr( "全部折叠" ), mView, &QgsLayerTreeView::collapseAll );
        }
        return menu;
      }

    private:
      QgsLayerTreeView *mView = nullptr;
      QPointer<QgisApp> mApp;
      std::function<void( const QString & )> mShowRecognitionResults;
      std::function<void( const QString & )> mShowTowerStyle;
      std::function<void( const QString & )> mToggleTowerDisplay;
      std::function<void()> mCreateTower;
      std::function<void()> mImportTowerVectors;
      std::function<void()> mCreatePhase;
      std::function<void( const QString & )> mAddImageryToPhase;
      std::function<void( const QString & )> mSetCurrentPhase;
      std::function<void( const QString & )> mDeletePhase;
      std::function<void( const QStringList & )> mRemoveLayersSafely;
      std::function<void( const QStringList & )> mLocateLayers;
      std::function<void( const QString & )> mExportDisturbanceResultYoloSamples;
      std::function<void( const QString & )> mStartSmartSegmentationForLayer;
  };

  const QString sProjectGroup = QStringLiteral( "生态修复" );
  const QString sProjectRootGroupName = QStringLiteral( "工程" );
  const QString sCommonGroup = QStringLiteral( "00 公共数据" );
  const QString sLineGroup = QStringLiteral( "01 杆塔与线路" );
  const QString sScopeGroup = QStringLiteral( "02 调查范围" );
  const QString sPhaseResultsGroup = QStringLiteral( "01 解译成果" );
  const QString sPhaseScreenshotsGroup = QStringLiteral( "02 截图成果" );
  const QString sImageryGroup = QStringLiteral( "03 影像数据" );
  const QString sRecognitionGroup = QStringLiteral( "01 扰动识别成果" );
  const QString sReviewGroup = QStringLiteral( "02 修复成果" );
  const QString sSmartSegmentationGroup = QStringLiteral( "03 智能分割成果" );
  const QString sProjectRootProperty = QStringLiteral( "eco/projectRoot" );
  const QString sGroupTypeProperty = QStringLiteral( "eco/groupType" );
  const QString sPhaseIdProperty = QStringLiteral( "eco/phaseId" );
  const QString sPhaseNameProperty = QStringLiteral( "eco/phaseName" );
  const QString sTreeIconProperty = QStringLiteral( "eco/treeIcon" );
  const QString sPhasePrefix = QStringLiteral( "期次 路 " );

  QgsLayerTreeGroup *directChildGroup( QgsLayerTreeGroup *parent, const QString &name )
  {
    if ( !parent )
      return nullptr;
    const QList<QgsLayerTreeNode *> children = parent->children();
    for ( QgsLayerTreeNode *node : children )
    {
      if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ); group && group->name() == name )
        return group;
    }
    return nullptr;
  }

  QgsLayerTreeGroup *directChildGroupByProperty( QgsLayerTreeGroup *parent, const QString &key, const QVariant &value )
  {
    if ( !parent )
      return nullptr;
    const QList<QgsLayerTreeNode *> children = parent->children();
    for ( QgsLayerTreeNode *node : children )
    {
      if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ); group && group->customProperty( key ) == value )
        return group;
    }
    return nullptr;
  }

  bool nodeContainsMapLayer( QgsLayerTreeNode *node )
  {
    if ( !node )
      return false;
    if ( qobject_cast<QgsLayerTreeLayer *>( node ) )
      return true;
    if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ) )
      return !group->findLayers().isEmpty();
    return false;
  }

  void appendNodeToGroupSafely( QgsLayerTreeGroup *source, QgsLayerTreeNode *node, QgsLayerTreeGroup *target )
  {
    if ( !source || !node || !target )
      return;
    if ( source == target && !target->children().isEmpty() && target->children().constLast() == node )
      return;

    // Reordering siblings in the same group does not require cloning.  More
    // importantly, cloning a group which contains a result layer and then
    // deleting the original invalidates pointers returned by
    // phaseResultGroup()/phaseSubGroup().  That dangling pointer was the
    // source of the intermittent access violation when the recognition
    // result was moved during a whole-line run.
    if ( source == target )
    {
      if ( nodeContainsMapLayer( node ) )
        return;

      // QgsLayerTreeNode::takeChild() deliberately detaches and destroys a
      // node's children. Preserve them while reordering a non-empty group.
      const QList<QgsLayerTreeNode *> children = node->abandonChildren();
      if ( source->takeChild( node ) )
      {
        target->addChildNode( node );
        if ( QgsLayerTreeGroup *groupNode = QgsLayerTree::toGroup( node ) )
          groupNode->insertChildNodes( -1, children );
      }
      else
      {
        if ( QgsLayerTreeGroup *groupNode = QgsLayerTree::toGroup( node ) )
          groupNode->insertChildNodes( -1, children );
      }
      return;
    }

    // The QGIS registry bridge removes map layers when their last layer-tree
    // node disappears. For nodes containing layers, clone/add the destination
    // node first and only then remove the original, so the registry never sees
    // a moment where the layer is absent from the tree.
    if ( nodeContainsMapLayer( node ) )
    {
      QgsLayerTreeNode *clone = node->clone();
      target->addChildNode( clone );
      source->removeChildNode( node );
      return;
    }

    if ( source->takeChild( node ) )
      target->addChildNode( node );
  }

  void ensureChildGroupOrder( QgsLayerTreeGroup *parent, const QStringList &orderedNames )
  {
    if ( !parent )
      return;
    for ( const QString &name : orderedNames )
    {
      if ( QgsLayerTreeGroup *group = directChildGroup( parent, name ) )
        appendNodeToGroupSafely( parent, group, parent );
    }
  }

  void collectVisibleSpatialLayers( QgsLayerTreeNode *node, QList<QgsMapLayer *> &layers )
  {
    if ( !node )
      return;

    if ( QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node ) )
    {
      if ( QgsMapLayer *layer = layerNode->layer(); layerNode->isVisible() && layer && layer->isSpatial() )
        layers << layer;
      return;
    }

    if ( node->parent() && !node->isVisible() )
      return;

    const QList<QgsLayerTreeNode *> children = node->children();
    for ( QgsLayerTreeNode *child : children )
      collectVisibleSpatialLayers( child, layers );
  }

  QString layerPhaseIdForLayer( QgsMapLayer *layer )
  {
    if ( !layer )
      return QString();

    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    if ( root )
    {
      QgsLayerTreeLayer *node = root->findLayer( layer->id() );
      for ( QgsLayerTreeNode *current = node; current; current = current->parent() )
      {
        if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( current ) )
        {
          const QString phaseId = group->customProperty( sPhaseIdProperty ).toString().trimmed();
          if ( !phaseId.isEmpty() )
            return phaseId;
        }
      }
    }

    // The physical business-tree location is the authoritative phase for a
    // layer. A custom property is retained only as a fallback for transient
    // layers which have not yet been attached to the project tree.
    return layer->customProperty( sPhaseIdProperty ).toString().trimmed();
  }

  QString sanitizedName( QString value )
  {
    value = value.trimmed();
    value.replace( QRegularExpression( QStringLiteral( R"([\\/:*?"<>|])" ) ), QStringLiteral( "_" ) );
    return value.isEmpty() ? QStringLiteral( "未命名工程" ) : value;
  }

  QgsPointXY featurePoint( const QgsGeometry &geometry )
  {
    if ( geometry.isNull() )
      return QgsPointXY();
    if ( geometry.isMultipart() )
    {
      const QgsMultiPointXY points = geometry.asMultiPoint();
      return points.isEmpty() ? QgsPointXY() : points.constFirst();
    }
    return geometry.asPoint();
  }

  QString featureLabel( const QgsFeature &feature, const QgsFields &fields, qint64 fallbackIndex )
  {
    const QStringList candidates = {
      QStringLiteral( "tower_no" ), QStringLiteral( "tower_id" ), QStringLiteral( "pole_no" ),
      QStringLiteral( "杆塔号" ), QStringLiteral( "塔号" ), QStringLiteral( "编号" ), QStringLiteral( "name" )
    };
    for ( const QString &candidate : candidates )
    {
      const int index = fields.indexFromName( candidate );
      if ( index >= 0 )
      {
        const QString value = feature.attribute( index ).toString().trimmed();
        if ( !value.isEmpty() )
          return sanitizedName( value );
      }
    }
    return QStringLiteral( "杆塔_%1" ).arg( fallbackIndex, 4, 10, QLatin1Char( '0' ) );
  }

  QString towerLabelField( const QgsFields &fields )
  {
    const QStringList candidates = {
      QStringLiteral( "tower_no" ), QStringLiteral( "tower_id" ), QStringLiteral( "pole_no" ),
      QStringLiteral( "杆塔号" ), QStringLiteral( "塔号" ), QStringLiteral( "编号" ), QStringLiteral( "name" )
    };
    for ( const QString &candidate : candidates )
    {
      if ( fields.indexFromName( candidate ) >= 0 )
        return candidate;
    }
    return QString();
  }

  QString recognitionResultType( const QString &source )
  {
    if ( source == QObject::tr( "修正" ) || source == QStringLiteral( "修正" ) )
      return QStringLiteral( "修正" );
    if ( source == QObject::tr( "新增" ) || source == QObject::tr( "人工标绘" ) || source == QStringLiteral( "新增" ) || source == QStringLiteral( "人工标绘" ) )
      return QStringLiteral( "新增" );
    return QStringLiteral( "识别" );
  }

  int recognitionResultTypePriority( const QString &type )
  {
    if ( type == QStringLiteral( "新增" ) )
      return 3;
    if ( type == QStringLiteral( "修正" ) )
      return 2;
    return 1;
  }

  QColor defaultWorkbenchSelectionColor()
  {
    QColor color( QStringLiteral( "#38bdf8" ) );
    color.setAlpha( 72 );
    return color;
  }

  void applyWorkbenchSelectionColor( QgsMapCanvas *canvas )
  {
    const QColor color = defaultWorkbenchSelectionColor();
    QgsProject::instance()->setSelectionColor( color );
    if ( canvas )
      canvas->setSelectionColor( color );
  }

  void applyBusinessSelectionStyle( QgsVectorLayer *layer )
  {
    if ( !layer )
      return;

    QColor color = defaultWorkbenchSelectionColor();
    const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
    if ( resultType == QLatin1String( "construction-disturbance" ) )
      color = QColor( 220, 38, 38, 72 );
    else if ( resultType == QLatin1String( "ecological-restoration" ) )
      color = QColor( 34, 197, 94, 72 );
    else if ( layer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" )
              || resultType == QLatin1String( "smart-segmentation" ) )
      color = QColor( 56, 189, 248, 72 );

    if ( layer->geometryType() == Qgis::GeometryType::Point || layer->geometryType() == Qgis::GeometryType::Line )
      color.setAlpha( 180 );

    if ( QgsVectorLayerSelectionProperties *selectionProperties = qobject_cast<QgsVectorLayerSelectionProperties *>( layer->selectionProperties() ) )
    {
      selectionProperties->setSelectionRenderingMode( Qgis::SelectionRenderingMode::CustomColor );
      selectionProperties->setSelectionColor( color );
    }
  }

  QString currentBusinessWorkspacePath()
  {
    QString workspacePath = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "workspace" ) );
    if ( workspacePath.isEmpty() && !QgsProject::instance()->fileName().isEmpty() )
      workspacePath = QFileInfo( QgsProject::instance()->fileName() ).absolutePath();
    return workspacePath;
  }

  bool shouldUseTowerIconRenderer( const QgsVectorLayer *layer )
  {
    if ( !layer || layer->geometryType() != Qgis::GeometryType::Point )
      return false;

    const QString displayMode = layer->customProperty( sTowerDisplayModeProperty ).toString().trimmed();
    if ( displayMode == QLatin1String( "vector" ) )
      return false;
    if ( displayMode == QLatin1String( "icon" ) )
      return true;

    const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
    if ( category == QLatin1String( "manual-tower" ) || category == QLatin1String( "tower" ) )
      return true;

    const QStringList towerFieldCandidates = {
      QStringLiteral( "tower_no" ), QStringLiteral( "tower_id" ), QStringLiteral( "pole_no" ),
      QStringLiteral( "杆塔号" ), QStringLiteral( "塔号" )
    };
    for ( const QString &candidate : towerFieldCandidates )
    {
      if ( layer->fields().indexFromName( candidate ) >= 0 )
        return true;
    }
    return false;
  }

  bool undoLastTowerPointEdit( QgisApp *app )
  {
    if ( !app )
      return false;

    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( app->activeLayer() );
    if ( !layer || !layer->isEditable() || !shouldUseTowerIconRenderer( layer ) || !layer->undoStack() || !layer->undoStack()->canUndo() )
      return false;

    layer->undoStack()->undo();
    layer->triggerRepaint();
    if ( app->mapCanvas() )
      app->mapCanvas()->refresh();
    if ( app->statusBar() )
      app->statusBar()->showMessage( QObject::tr( "已撤回上一处杆塔点" ), 2500 );
    return true;
  }

  void applyBusinessStyle( QgsVectorLayer *layer )
  {
    if ( !layer )
      return;

    QColor color( QStringLiteral( "#38bdf8" ) );
    double opacity = 0.75;
    switch ( layer->geometryType() )
    {
      case Qgis::GeometryType::Point:
        color = QColor( QStringLiteral( "#ef4444" ) );
        opacity = 0.95;
        break;
      case Qgis::GeometryType::Line:
        color = QColor( QStringLiteral( "#ff5a1f" ) );
        opacity = 1.0;
        break;
      case Qgis::GeometryType::Polygon:
      {
        const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
        if ( resultType == QLatin1String( "ecological-restoration" ) )
          color = QColor( QStringLiteral( "#22c55e" ) );
        else if ( resultType == QLatin1String( "construction-disturbance" ) )
          color = QColor( QStringLiteral( "#dc2626" ) );
        else
          color = QColor( QStringLiteral( "#38bdf8" ) );
        opacity = resultType == QLatin1String( "construction-disturbance" ) ? 0.55 : 0.42;
        break;
      }
      default:
        break;
    }

    const bool useTowerIcon = shouldUseTowerIconRenderer( layer );
    double towerIconWidthPixels = sDefaultTowerIconWidthPixels;
    if ( useTowerIcon )
    {
      const bool customSize = layer->customProperty( sTowerIconCustomSizeProperty, false ).toBool();
      if ( customSize )
        towerIconWidthPixels = layer->customProperty( sTowerIconWidthProperty, sDefaultTowerIconWidthPixels ).toDouble();
      if ( towerIconWidthPixels <= 0.0 )
        towerIconWidthPixels = sDefaultTowerIconWidthPixels;
      applyTowerIconRenderer( layer, currentBusinessWorkspacePath(), towerIconWidthPixels, customSize );
    }
    else
    {
      std::unique_ptr<QgsSymbol> symbol( QgsSymbol::defaultSymbol( layer->geometryType() ) );
      if ( !symbol )
        return;
      symbol->setColor( color );
      symbol->setOpacity( opacity );
      if ( layer->geometryType() == Qgis::GeometryType::Line )
      {
        if ( QgsLineSymbol *lineSymbol = dynamic_cast<QgsLineSymbol *>( symbol.get() ) )
          lineSymbol->setWidth( 0.8 );
      }
      if ( layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "construction-disturbance" )
           && layer->fields().indexFromName( QLatin1String( sRecognitionVisibilityField ) ) >= 0 )
      {
        symbol->setOpacity( 1.0 );
        symbol->setDataDefinedProperty(
          QgsSymbol::Property::Opacity,
          QgsProperty::fromExpression( QStringLiteral( "CASE WHEN coalesce(\"%1\", 1) = 1 THEN 55 ELSE 0 END" ).arg( QLatin1String( sRecognitionVisibilityField ) ) )
        );
      }
      layer->setRenderer( new QgsSingleSymbolRenderer( symbol.release() ) );
    }
    applyBusinessSelectionStyle( layer );

    // Imported tower layers are immediately readable without exposing QGIS'
    // labeling workflow. Users can still refine both symbol and labels from
    // the point-layer context menu.
    if ( layer->geometryType() == Qgis::GeometryType::Point )
    {
      const QString labelField = towerLabelField( layer->fields() );
      if ( !labelField.isEmpty() )
      {
        QgsPalLayerSettings settings;
        settings.fieldName = labelField;
        settings.drawLabels = true;
        if ( useTowerIcon )
        {
          settings.placement = Qgis::LabelPlacement::OrderedPositionsAroundPoint;
          settings.pointSettings().setPredefinedPositionOrder( QVector<Qgis::LabelPredefinedPointPosition> { Qgis::LabelPredefinedPointPosition::TopMiddle } );
          settings.placementSettings().setAllowDegradedPlacement( false );
        }
        else
        {
          settings.placement = Qgis::LabelPlacement::AroundPoint;
          settings.pointSettings().setQuadrant( Qgis::LabelQuadrantPosition::Above );
        }
        settings.offsetType = useTowerIcon ? Qgis::LabelOffsetType::FromSymbolBounds : Qgis::LabelOffsetType::FromPoint;
        settings.dist = useTowerIcon ? 15.0 : 4.5;
        settings.distUnits = useTowerIcon ? Qgis::RenderUnit::Pixels : Qgis::RenderUnit::Millimeters;
        settings.xOffset = 0.0;
        settings.yOffset = 0.0;
        settings.offsetUnits = Qgis::RenderUnit::Pixels;
        QgsTextFormat textFormat;
        textFormat.setColor( Qt::white );
        textFormat.setSize( useTowerIcon ? 7 : 9 );
        textFormat.setSizeUnit( Qgis::RenderUnit::Points );
        QgsTextBufferSettings buffer;
        buffer.setEnabled( true );
        buffer.setColor( Qt::black );
        buffer.setSize( 1.2 );
        buffer.setSizeUnit( Qgis::RenderUnit::Points );
        textFormat.setBuffer( buffer );
        settings.setFormat( textFormat );
        layer->setLabeling( new QgsVectorLayerSimpleLabeling( settings ) );
        layer->setLabelsEnabled( true );
      }
    }
    layer->triggerRepaint();
  }

  // Satellite GeoTIFFs frequently carry a black collar where the source has
  // no valid pixels but no NoData metadata. Keep the source untouched and
  // make that collar transparent only when all four image corners are black.
  void prepareBusinessImagery( QgsRasterLayer *layer )
  {
    if ( !layer || !layer->dataProvider() )
      return;

    QgsRasterDataProvider *provider = layer->dataProvider();
    const int bandCount = layer->bandCount();
    for ( int band = 1; band <= std::min( bandCount, 3 ); ++band )
    {
      if ( provider->sourceHasNoDataValue( band ) )
        provider->setUseSourceNoDataValue( band, true );
    }

    if ( bandCount < 3 || !layer->renderer() || layer->renderer()->alphaBand() > 0 )
      return;

    const QgsRectangle extent = layer->extent();
    if ( extent.isEmpty() || !extent.isFinite() )
      return;
    std::unique_ptr<QgsRasterBlock> red( provider->block( 1, extent, 2, 2 ) );
    std::unique_ptr<QgsRasterBlock> green( provider->block( 2, extent, 2, 2 ) );
    std::unique_ptr<QgsRasterBlock> blue( provider->block( 3, extent, 2, 2 ) );
    if ( !red || !green || !blue || !red->isValid() || !green->isValid() || !blue->isValid() )
      return;

    bool allCornersBlack = true;
    for ( const QPoint corner : { QPoint( 0, 0 ), QPoint( 0, 1 ), QPoint( 1, 0 ), QPoint( 1, 1 ) } )
    {
      if ( red->isNoData( corner.y(), corner.x() ) || green->isNoData( corner.y(), corner.x() ) || blue->isNoData( corner.y(), corner.x() )
           || !qgsDoubleNear( red->value( corner.y(), corner.x() ), 0.0, 1.0 )
           || !qgsDoubleNear( green->value( corner.y(), corner.x() ), 0.0, 1.0 )
           || !qgsDoubleNear( blue->value( corner.y(), corner.x() ), 0.0, 1.0 ) )
      {
        allCornersBlack = false;
        break;
      }
    }
    if ( !allCornersBlack )
      return;

    std::unique_ptr<QgsRasterTransparency> transparency = layer->renderer()->rasterTransparency()
      ? std::make_unique<QgsRasterTransparency>( *layer->renderer()->rasterTransparency() )
      : std::make_unique<QgsRasterTransparency>();
    QVector<QgsRasterTransparency::TransparentThreeValuePixel> pixels = transparency->transparentThreeValuePixelList();
    if ( std::none_of( pixels.cbegin(), pixels.cend(), []( const QgsRasterTransparency::TransparentThreeValuePixel &pixel ) {
      return qgsDoubleNear( pixel.red, 0.0 ) && qgsDoubleNear( pixel.green, 0.0 ) && qgsDoubleNear( pixel.blue, 0.0 ) && qgsDoubleNear( pixel.opacity, 0.0 );
    } ) )
    {
      pixels.append( QgsRasterTransparency::TransparentThreeValuePixel( 0, 0, 0, 0, 1, 1, 1 ) );
      transparency->setTransparentThreeValuePixelList( pixels );
      layer->renderer()->setRasterTransparency( transparency.release() );
      layer->setCustomProperty( QStringLiteral( "eco/black-border-transparent" ), true );
      layer->triggerRepaint();
    }
  }
}

QgsEcoRestorationController::QgsEcoRestorationController( QgisApp *app )
  : QObject( app )
  , mApp( app )
{
  applyVsCodeNativeTitleBar( app );
  mDock = new QgsDockWidget( tr( "遥感解译" ), app );
  mDock->setObjectName( QStringLiteral( "EcoRestorationDock" ) );
  mDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
  mDock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
  mDock->setMinimumWidth( 350 );
  mDock->setStyleSheet( QStringLiteral( R"(
    QDockWidget#EcoRestorationDock, QgsDockWidget#EcoRestorationDock { color:#f1f5f9; background:#1e1e1e; border:1px solid #3c3c3c; margin:10px 0; }
    QDockWidget#EcoRestorationDock::title, QgsDockWidget#EcoRestorationDock::title { background:#181818; color:#f1f5f9; padding:8px 38px 8px 10px; border:0; font-weight:600; }
    QDockWidget#EcoRestorationDock::close-button, QgsDockWidget#EcoRestorationDock::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
    QDockWidget#EcoRestorationDock::close-button:hover, QgsDockWidget#EcoRestorationDock::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
    QDockWidget#EcoRestorationDock::close-button:pressed, QgsDockWidget#EcoRestorationDock::close-button:pressed { background:#8f1d14; }
  )" ) );
  QPalette dockPalette = mDock->palette();
  for ( const QPalette::ColorGroup colorGroup : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
  {
    dockPalette.setColor( colorGroup, QPalette::WindowText, QColor( QStringLiteral( "#f1f5f9" ) ) );
    dockPalette.setColor( colorGroup, QPalette::Text, QColor( QStringLiteral( "#f1f5f9" ) ) );
  }
  mDock->setPalette( dockPalette );
  installEcoDockTitleBar( mDock );
  mDock->setWidget( createDockContents() );
  app->addDockWidget( Qt::RightDockWidgetArea, mDock );
  mDock->show();

  mRecognitionDock = new QgsDockWidget( tr( "智能识别" ), app );
  mRecognitionDock->setObjectName( QStringLiteral( "EcoRecognitionDock" ) );
  mRecognitionDock->setAllowedAreas( Qt::RightDockWidgetArea );
  mRecognitionDock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
  mRecognitionDock->setMinimumWidth( 365 );
  mRecognitionDock->setStyleSheet( QStringLiteral( R"(
    QDockWidget#EcoRecognitionDock, QgsDockWidget#EcoRecognitionDock { color:#f1f5f9; background:#1e1e1e; border:1px solid #3c3c3c; margin:10px 0; }
    QDockWidget#EcoRecognitionDock::title, QgsDockWidget#EcoRecognitionDock::title { background:#181818; color:#f1f5f9; padding:8px 38px 8px 10px; border:0; font-weight:600; }
    QDockWidget#EcoRecognitionDock::close-button, QgsDockWidget#EcoRecognitionDock::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
    QDockWidget#EcoRecognitionDock::close-button:hover, QgsDockWidget#EcoRecognitionDock::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
    QDockWidget#EcoRecognitionDock::close-button:pressed, QgsDockWidget#EcoRecognitionDock::close-button:pressed { background:#8f1d14; }
  )" ) );
  QPalette recognitionPalette = mRecognitionDock->palette();
  for ( const QPalette::ColorGroup colorGroup : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
  {
    recognitionPalette.setColor( colorGroup, QPalette::WindowText, QColor( QStringLiteral( "#f1f5f9" ) ) );
    recognitionPalette.setColor( colorGroup, QPalette::Text, QColor( QStringLiteral( "#f1f5f9" ) ) );
  }
  mRecognitionDock->setPalette( recognitionPalette );
  installEcoDockTitleBar( mRecognitionDock );
  mRecognitionDock->setWidget( createRecognitionPage() );
  app->addDockWidget( Qt::RightDockWidgetArea, mRecognitionDock );
  mRecognitionDock->hide();
  connect( mRecognitionDock, &QDockWidget::visibilityChanged, this, [this]( bool visible ) {
    mRecognitionPreviewActive = visible;
    if ( mRecognitionPanelAction )
    {
      const QSignalBlocker blocker( mRecognitionPanelAction );
      mRecognitionPanelAction->setChecked( visible );
    }
    if ( mProjectTransitionInProgress )
    {
      clearRecognitionPreview();
      return;
    }
    if ( visible )
      updateRecognitionPreview();
    else
    {
      clearRecognitionPreview();
      if ( mRecognitionProgressBar )
        mRecognitionProgressBar->hide();
    }
  } );
  mPhotoDock = new QgsDockWidget( tr( "照片处理" ), app );
  mPhotoDock->setObjectName( QStringLiteral( "EcoPhotoProcessingDock" ) );
  mPhotoDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );
  mPhotoDock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
  mPhotoDock->setMinimumWidth( 560 );
  mPhotoDock->setWidget( createPhotoPage() );
  installEcoDockTitleBar( mPhotoDock );
  app->addDockWidget( Qt::RightDockWidgetArea, mPhotoDock );
  mPhotoDock->hide();
  connect( mPhotoDock, &QDockWidget::visibilityChanged, this, [this]( bool visible ) {
    if ( visible )
      refreshPhotoWorkbenchContext();
  } );
  // QGIS creates feature attribute forms itself. Observe application events
  // so those dialogs receive the same visual treatment as business dialogs.
  QCoreApplication::instance()->installEventFilter( this );

  // Keep the business task progress in the familiar QGIS status bar instead
  // of opening a modal dialog over the map. It deliberately remains a
  // separate widget so normal canvas-render progress still behaves normally.
  mRecognitionProgressBar = new QProgressBar( app->statusBar() );
  mRecognitionProgressBar->setObjectName( QStringLiteral( "EcoRecognitionProgress" ) );
  mRecognitionProgressBar->setMinimumWidth( 330 );
  mRecognitionProgressBar->setMaximumWidth( 440 );
  mRecognitionProgressBar->setMinimumHeight( 22 );
  mRecognitionProgressBar->setMaximumHeight( 24 );
  mRecognitionProgressBar->setTextVisible( true );
  mRecognitionProgressBar->setStyleSheet( QStringLiteral( "QProgressBar#EcoRecognitionProgress { color:#ffffff; background:#111827; border:2px solid #38bdf8; border-radius:6px; text-align:center; font-weight:700; } QProgressBar#EcoRecognitionProgress::chunk { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22d3ee,stop:0.55 #0ea5e9,stop:1 #a855f7); border-radius:4px; }" ) );
  app->statusBar()->addPermanentWidget( mRecognitionProgressBar, 2 );
  mRecognitionProgressBar->hide();
  applyWorkbenchSelectionColor( app->mapCanvas() );

  createToolbar();
  if ( mApp->mapCanvas() )
  {
    connect( mApp->mapCanvas(), &QgsMapCanvas::mapToolSet, this, [this]( QgsMapTool *newTool, QgsMapTool *oldTool ) {
      Q_UNUSED( oldTool )
      if ( mProjectTransitionInProgress )
        return;
      if ( mSmartSegmentationTool && newTool != mSmartSegmentationTool )
      {
        if ( EcoSam2PromptMapTool *tool = static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) )
          tool->resetSession();
        mSmartSegmentationPendingGeometry = QgsGeometry();
        mSmartSegmentationPendingScore = 0.0;
        mSmartSegmentationRequestedLayerId.clear();
        mSmartSegmentationTargetLayerId.clear();
        mSmartSegmentationFeatureId = -1;
        mSmartSegmentationStandalone = true;
        mSmartSegmentationOutputCrs = QgsCoordinateReferenceSystem();
        mSmartSegmentationPreviousMapTool = nullptr;
        if ( mSmartSegmentationAction )
        {
          const QSignalBlocker blocker( mSmartSegmentationAction );
          mSmartSegmentationAction->setChecked( false );
        }
      }
    } );
  }
  createWelcomeOverlay();
  if ( mApp->layerTreeView() )
  {
    mApp->layerTreeView()->installEventFilter( this );
    mApp->layerTreeView()->viewport()->installEventFilter( this );
    mApp->layerTreeView()->setDragDropMode( QAbstractItemView::InternalMove );
    mApp->layerTreeView()->setItemDelegate( new EcoLayerTreeItemDelegate( mApp->layerTreeView() ) );
    // Replace only the entry point with the business menu. QGIS actions and
    // APIs remain intact for future combined workflows.
    mApp->layerTreeView()->setMenuProvider( new EcoLayerTreeMenuProvider(
      mApp->layerTreeView(),
      mApp,
      [this]( const QString &layerId ) { showRecognitionResultTable( layerId ); },
      [this]( const QString &layerId ) {
        QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( layerId ) );
        if ( layer )
          showTowerStyleDialog( layer );
      },
      [this]( const QString &layerId ) {
        QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( layerId ) );
        if ( layer )
          toggleTowerDisplayMode( layer );
      },
      [this] { createManualVectorLayer( QStringLiteral( "tower" ) ); },
      [this] { importTowerVectors(); },
      [this] { createPhase(); },
      [this]( const QString &phaseId ) { importImagery( phaseId ); },
      [this]( const QString &phaseId ) { setCurrentPhase( phaseId ); },
      [this]( const QString &phaseId ) { deletePhase( phaseId ); },
      [this]( const QStringList &layerIds ) { removeProjectLayersSafely( layerIds ); },
      [this]( const QStringList &layerIds ) { locateLayers( layerIds ); },
      [this]( const QString &layerId ) { exportDisturbanceResultYoloSamples( layerId ); },
      [this]( const QString &layerId ) { startSmartSegmentationForLayer( layerId ); } ) );
  }
  mApp->setStyleSheet( QStringLiteral( R"(
    QMainWindow#MainWindow { background:#181818; border:0; }
    QMainWindow#MainWindow::separator, QMainWindow::separator { background:#2b2b2b; border:0; width:1px; height:1px; }
    QWidget#centralwidget, QStackedWidget { background:#1e1e1e; border:0; }
    QgsMapCanvas#theMapCanvas, QGraphicsView#theMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; padding:0; }
    QDockWidget::close-button, QgsDockWidget::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
    QDockWidget::close-button:hover, QgsDockWidget::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
    QDockWidget::close-button:pressed, QgsDockWidget::close-button:pressed { background:#8f1d14; }
    QMenu { background:#252526; color:#d4d4d4; border:1px solid #454545; padding:4px; }
    QMenu::item { min-height:24px; padding:4px 28px 4px 10px; border-radius:3px; }
    QMenu::item:selected { background:#094771; color:#ffffff; }
    QMenu::item:disabled { color:#777777; }
    QMenu::separator { height:1px; margin:4px 7px; background:#3c3c3c; }
    QCheckBox::indicator { width:14px; height:14px; background:#1e1e1e; border:1px solid #6b6b6b; border-radius:2px; }
    QCheckBox::indicator:hover { border-color:#c5c5c5; }
    QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate { background:#007acc; border-color:#007acc; image:url(:/images/themes/default/mEditorWidgetCheckbox.svg); }
    QCheckBox::indicator:disabled { background:#252526; border-color:#454545; }
    QTableView, QTableWidget { color:#d4d4d4; background:#181818; alternate-background-color:#181818; gridline-color:#2d2d2d; selection-background-color:#094771; selection-color:#ffffff; }
    QTableView::item, QTableWidget::item { color:#d4d4d4; background:#181818; }
    QTableView::item:hover, QTableWidget::item:hover { color:#ffffff; background:#2a2d2e; }
    QTableView::item:selected, QTableWidget::item:selected { color:#ffffff; background:#094771; }
    QScrollBar:vertical { width:12px; margin:0; background:#181818; }
    QScrollBar:horizontal { height:12px; margin:0; background:#181818; }
    QScrollBar::handle:vertical { min-height:24px; margin:2px; background:#424242; border-radius:4px; }
    QScrollBar::handle:horizontal { min-width:24px; margin:2px; background:#424242; border-radius:4px; }
    QScrollBar::handle:hover { background:#5a5a5a; }
    QScrollBar::handle:pressed { background:#6b6b6b; }
    QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { width:0; height:0; background:transparent; }
    QScrollBar::up-arrow, QScrollBar::down-arrow, QScrollBar::left-arrow, QScrollBar::right-arrow { width:0; height:0; background:transparent; }
    QAbstractScrollArea::corner { background:#181818; border:0; }
  )" ) );
  if ( mApp->centralWidget() )
    mApp->centralWidget()->installEventFilter( this );

  connect( QgsProject::instance(), &QgsProject::readProject, this, [this] { scheduleProjectStateRefresh(); } );
  connect( QgsProject::instance(), &QgsProject::cleared, this, [this] { scheduleProjectStateRefresh(); } );
  connect( QgsProject::instance(), &QgsProject::layersAdded, this, [this]( const QList<QgsMapLayer *> & ) {
    if ( mProjectTransitionInProgress || mLayerImportInProgress )
      return;
    QTimer::singleShot( 0, this, [this] {
      if ( mProjectTransitionInProgress || mLayerImportInProgress )
        return;
      classifyAddedLayers( QgsProject::instance()->mapLayers().values() );
      refreshLayerChoices();
    } );
  } );
  connect( QgsProject::instance(), &QgsProject::layersRemoved, this, [this] {
    if ( mProjectTransitionInProgress )
      return;
    clearRecognitionPreview();
    refreshLayerChoices();
    QTimer::singleShot( 0, this, [this] {
      if ( !mProjectTransitionInProgress && !mLayerImportInProgress )
        syncBusinessProjectView();
    } );
  } );
  connect( QgsProject::instance(), &QgsProject::fileNameChanged, this, [this] { scheduleProjectStateRefresh(); } );
  if ( QgsLayerTree *root = QgsProject::instance()->layerTreeRoot() )
  {
    // The comparison canvases own independent layer lists. Forward every
    // layer-tree checkbox change to them so the left-side visibility controls
    // remain authoritative while comparison mode is open.
    connect( root, &QgsLayerTreeNode::visibilityChanged, this, [this]( QgsLayerTreeNode * ) {
      refreshPhaseComparisonCanvases();
    } );
    connect( root, &QgsLayerTreeNode::addedChildren, this, [this]( QgsLayerTreeNode *, int, int ) {
      refreshPhaseComparisonCanvases();
    } );
    connect( root, &QgsLayerTreeNode::removedChildren, this, [this]( QgsLayerTreeNode *, int, int ) {
      refreshPhaseComparisonCanvases();
    } );
  }
  connect( app, &QgisApp::initializationCompleted, this, [this] {
    QTimer::singleShot( 0, this, [this] {
      setCompactMode( true );
      if ( mDock )
      {
        mDock->show();
        mDock->raise();
      }
    } );
    // QGIS can restore its saved dock layout after initializationCompleted.
    // Re-assert the workbench panel once that restoration has settled so the
    // business panel is open on a fresh application start.
    QTimer::singleShot( 350, this, [this] {
      if ( mDock )
      {
        mDock->show();
        mDock->raise();
      }
    } );
  } );

  QTimer::singleShot( 0, this, [this] {
    scheduleProjectStateRefresh();
    setCompactMode( true );
    if ( mDock )
    {
      mDock->show();
      mDock->raise();
    }
  } );
}

void QgsEcoRestorationController::activateBusinessStartupLayout()
{
  // QGIS restores its dock layout near the end of initialization. Reassert
  // the full business workbench after that restore has completed.
  setCompactMode( true );
  hideNativeQgisWidgets();
  refreshProjectState();
  if ( mToolbar )
    mToolbar->show();
  if ( mDock )
  {
    mDock->show();
    mDock->raise();
  }
  if ( QDockWidget *layers = mApp ? mApp->findChild<QDockWidget *>( QStringLiteral( "Layers" ) ) : nullptr )
    layers->show();
}
QWidget *QgsEcoRestorationController::createDockContents()
{
  QWidget *root = new QWidget;
  root->setObjectName( QStringLiteral( "EcoWorkspace" ) );
  root->setStyleSheet( QStringLiteral( R"(
    #EcoWorkspace { background: #1e1e1e; color: #d4d4d4; }
    #EcoWorkspace QLabel { color: #d4d4d4; }
    #EcoWorkspace QLabel[muted="true"] { color: #969696; }
    #EcoWorkspace QTabWidget { border:1px solid #2b2b2b; }
    #EcoWorkspace QTabWidget::pane { border:0; background:#252526; }
    #EcoWorkspace QTabBar { background:#181818; border:0; }
    #EcoWorkspace QTabBar::tab { background:#202020; color:#bcbcbc; border:0; border-right:1px solid #2b2b2b; padding:7px 8px; }
    #EcoWorkspace QTabBar::tab:selected { background:#0e639c; color:white; }
    #EcoWorkspace QGroupBox { border: 1px solid #3c3c3c; margin-top: 14px; padding-top: 10px; font-weight: 600; }
    #EcoWorkspace QGroupBox::title { subcontrol-origin: margin; left: 8px; color: #f0f0f0; }
    #EcoWorkspace QPushButton { min-height: 26px; border: 1px solid #4a4a4a; border-radius: 3px; background: #333337; color: #e5e5e5; padding: 2px 8px; }
    #EcoWorkspace QPushButton:hover { background: #3e4248; border-color: #59636e; }
    #EcoWorkspace QPushButton[primary="true"] { background: #0e639c; border-color: #1177bb; color: white; }
    #EcoWorkspace QPushButton[primary="true"]:hover { background: #1177bb; }
    #EcoWorkspace QLineEdit, #EcoWorkspace QComboBox, #EcoWorkspace QSpinBox { min-height: 25px; border: 1px solid #484848; background: #1f1f1f; color: #e0e0e0; padding: 1px 5px; }
    #EcoWorkspace QSpinBox { padding-right:24px; }
    #EcoWorkspace QSpinBox::up-button, #EcoWorkspace QSpinBox::down-button { subcontrol-origin:border; width:22px; background:#333337; border-left:1px solid #4a4a4a; }
    #EcoWorkspace QSpinBox::up-button { subcontrol-position:top right; border-bottom:1px solid #4a4a4a; }
    #EcoWorkspace QSpinBox::down-button { subcontrol-position:bottom right; }
    #EcoWorkspace QSpinBox::up-button:hover, #EcoWorkspace QSpinBox::down-button:hover { background:#094771; }
    #EcoWorkspace QSpinBox::up-arrow { image:url(:/images/themes/default/mActionArrowUp.svg); width:10px; height:10px; }
    #EcoWorkspace QSpinBox::down-arrow { image:url(:/images/themes/default/mActionArrowDown.svg); width:10px; height:10px; }
    #EcoWorkspace QFrame[workflowCard="true"] { background: #232323; border: 1px solid #383838; border-radius: 4px; }
  )" ) );

  QVBoxLayout *layout = new QVBoxLayout( root );
  layout->setContentsMargins( 5, 5, 5, 5 );
  layout->setSpacing( 6 );

  QFrame *phaseBar = new QFrame( root );
  phaseBar->setObjectName( QStringLiteral( "EcoPhaseBar" ) );
  phaseBar->setStyleSheet( QStringLiteral( R"(
    QFrame#EcoPhaseBar { background:#252526; border:1px solid #3c3c3c; border-radius:4px; }
    QFrame#EcoPhaseBar QLabel { color:#d4d4d4; background:transparent; }
  )" ) );
  QHBoxLayout *phaseLayout = new QHBoxLayout( phaseBar );
  phaseLayout->setContentsMargins( 8, 6, 8, 6 );
  phaseLayout->setSpacing( 6 );
  QLabel *phaseLabel = new QLabel( tr( "当前期次" ), phaseBar );
  mCurrentPhaseCombo = new QComboBox( phaseBar );
  mCurrentPhaseCombo->setEditable( false );
  QPushButton *comparePhaseButton = new QPushButton( tr( "对比" ), phaseBar );
  comparePhaseButton->setToolTip( tr( "打开左右双地图期次对比" ) );
  phaseLayout->addWidget( phaseLabel );
  phaseLayout->addWidget( mCurrentPhaseCombo, 1 );
  phaseLayout->addWidget( comparePhaseButton );
  connect( comparePhaseButton, &QPushButton::clicked, this, [this] { showPhaseComparisonDialog(); } );
  connect( mCurrentPhaseCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int index ) {
    if ( !mCurrentPhaseCombo || index < 0 )
      return;
    const QString phaseId = mCurrentPhaseCombo->itemData( index ).toString();
    if ( phaseId.isEmpty() || phaseId == currentPhaseId() )
      return;
    setCurrentPhase( phaseId );
  } );
  layout->addWidget( phaseBar );

  QTabWidget *tabs = new QTabWidget;
  tabs->setDocumentMode( true );
  if ( QTabBar *tabBar = tabs->findChild<QTabBar *>() )
    tabBar->setDrawBase( false );
  tabs->addTab( createScreenshotPage(), tr( "截图" ) );
  layout->addWidget( tabs, 1 );
  return root;
}

void QgsEcoRestorationController::createWelcomeOverlay()
{
  if ( !mApp || !mApp->centralWidget() )
    return;
  mWelcomeOverlay = new QWidget( mApp->centralWidget() );
  mWelcomeOverlay->setObjectName( QStringLiteral( "EcoWelcomeOverlay" ) );
  mWelcomeOverlay->setStyleSheet( QStringLiteral( R"(
    #EcoWelcomeOverlay { background:#1e1e1e; border:1px solid #3c3c3c; }
    #EcoWelcomeOverlay QFrame[welcomeCard="true"] { background:#252526; border:1px solid #3c3c3c; border-radius:8px; }
    #EcoWelcomeOverlay QLabel { color:#d4d4d4; }
    #EcoWelcomeOverlay QLabel[eyebrow="true"] { color:#8c9aaa; font-size:13px; }
    #EcoWelcomeOverlay QLabel[headline="true"] { color:#f3f3f3; font-size:26px; font-weight:650; }
    #EcoWelcomeOverlay QLabel[muted="true"] { color:#8c8c8c; }
    #EcoWelcomeOverlay QPushButton { min-height:32px; border:1px solid #1177bb; border-radius:4px; background:#0e639c; color:white; padding:3px 18px; }
    #EcoWelcomeOverlay QPushButton:hover { background:#1177bb; }
  )" ) );

  QVBoxLayout *outerLayout = new QVBoxLayout( mWelcomeOverlay );
  outerLayout->setContentsMargins( 48, 48, 48, 48 );
  outerLayout->addStretch( 1 );
  QFrame *card = new QFrame;
  card->setProperty( "welcomeCard", true );
  card->setMaximumWidth( 760 );
  QVBoxLayout *cardLayout = new QVBoxLayout( card );
  cardLayout->setContentsMargins( 34, 30, 34, 30 );
  QLabel *eyebrow = new QLabel( tr( "工程工作台" ) );
  eyebrow->setProperty( "eyebrow", true );
  QLabel *headline = new QLabel( tr( "线路遥感解译处理工程" ) );
  headline->setProperty( "headline", true );
  QLabel *description = new QLabel( tr( "创建遥感解译工程后，再加载影像、杆?线路矢量并开展塔基施工扰动识别。所有成果将按工程归档，并在左侧工程图层树中集中管理。" ) );
  description->setWordWrap( true );
  description->setProperty( "muted", true );
  QPushButton *createButton = new QPushButton( tr( "新建遥感解译工程" ) );
  createButton->setProperty( "primary", true );
  connect( createButton, &QPushButton::clicked, this, [this] { createBusinessProject(); } );
  cardLayout->addWidget( eyebrow );
  cardLayout->addWidget( headline );
  cardLayout->addSpacing( 8 );
  cardLayout->addWidget( description );
  cardLayout->addSpacing( 18 );
  cardLayout->addWidget( createButton, 0, Qt::AlignLeft );
  outerLayout->addWidget( card, 0, Qt::AlignHCenter );
  outerLayout->addStretch( 1 );
  mWelcomeOverlay->setGeometry( mApp->centralWidget()->rect().adjusted( 10, 10, -10, -10 ) );
  mWelcomeOverlay->raise();
  updateWelcomeOverlay();
}

void QgsEcoRestorationController::updateWelcomeOverlay()
{
  if ( !mWelcomeOverlay )
    return;
  bool ok = false;
  const bool enabled = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &ok );
  const bool show = !ok || !enabled || QgsProject::instance()->fileName().isEmpty();
  mWelcomeOverlay->setVisible( show );
  if ( show )
    mWelcomeOverlay->raise();
}

QWidget *QgsEcoRestorationController::createProjectPage()
{
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout( page );
  layout->setContentsMargins( 12, 12, 12, 12 );

  QLabel *intro = new QLabel( tr( "以工程为载体归集线路、影像、智能识别和修复成果。" ) );
  intro->setWordWrap( true );
  intro->setProperty( "muted", true );
  layout->addWidget( intro );

  QGroupBox *current = new QGroupBox( tr( "当前工程" ) );
  QVBoxLayout *currentLayout = new QVBoxLayout( current );
  mProjectPathLabel = new QLabel;
  mProjectPathLabel->setWordWrap( true );
  mProjectPathLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
  mProjectPathLabel->setProperty( "muted", true );
  currentLayout->addWidget( mProjectPathLabel );
  mProjectInfoLabel = new QLabel;
  mProjectInfoLabel->setWordWrap( true );
  mProjectInfoLabel->setProperty( "muted", true );
  currentLayout->addWidget( mProjectInfoLabel );
  layout->addWidget( current );

  QGroupBox *phaseBox = new QGroupBox( tr( "期次管理" ) );
  QVBoxLayout *phaseLayout = new QVBoxLayout( phaseBox );
  QHBoxLayout *phaseRow = new QHBoxLayout;
  phaseRow->setContentsMargins( 0, 0, 0, 0 );
  phaseRow->setSpacing( 8 );
  mCurrentPhaseCombo = new QComboBox;
  mCurrentPhaseCombo->setEditable( false );
  mCurrentPhaseCombo->setMinimumHeight( 27 );
  phaseRow->addWidget( mCurrentPhaseCombo, 1 );
  QPushButton *comparePhaseButton = new QPushButton( tr( "期次对比" ) );
  phaseRow->addWidget( comparePhaseButton );
  phaseLayout->addLayout( phaseRow );
  QLabel *phaseHint = new QLabel( tr( "工程下以“期次”为组织单元；杆塔与线路属于公共数据，影像、识别和截图成果按期次归档。建议在左侧工程树上对期次节点直接右键操作。" ) );
  phaseHint->setWordWrap( true );
  phaseHint->setProperty( "muted", true );
  phaseLayout->addWidget( phaseHint );
  connect( comparePhaseButton, &QPushButton::clicked, this, [this] { showPhaseComparisonDialog(); } );
  connect( mCurrentPhaseCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int index ) {
    if ( !mCurrentPhaseCombo || index < 0 )
      return;
    const QString phaseId = mCurrentPhaseCombo->itemData( index ).toString();
    if ( phaseId.isEmpty() || phaseId == currentPhaseId() )
      return;
    setCurrentPhase( phaseId );
  } );
  layout->addWidget( phaseBox );

  QGroupBox *workflow = new QGroupBox( tr( "推荐工作流" ) );
  QVBoxLayout *workflowLayout = new QVBoxLayout( workflow );
  workflowLayout->addWidget( createWorkflowCard( QStringLiteral( "1" ), tr( "建立工程" ), tr( "填写工程名称并自动建立标准成果目录" ) ) );
  workflowLayout->addWidget( createWorkflowCard( QStringLiteral( "2" ), tr( "加载数据" ), tr( "导入 TIF 与杆塔、线路、调查范?SHP" ) ) );
  workflowLayout->addWidget( createWorkflowCard( QStringLiteral( "3" ), tr( "智能识别" ), tr( "自动生成塔基施工扰动候选图斑" ) ) );
  workflowLayout->addWidget( createWorkflowCard( QStringLiteral( "4" ), tr( "修复成果" ), tr( "新增、修改、删除并确认扰动与恢复边界" ) ) );
  workflowLayout->addWidget( createWorkflowCard( QStringLiteral( "5" ), tr( "成果输出" ), tr( "按杆塔范围批量截图并归档监管成果" ) ) );
  layout->addWidget( workflow );

  layout->addStretch();
  return page;
}

QWidget *QgsEcoRestorationController::createDataPage()
{
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout( page );
  layout->setContentsMargins( 12, 12, 12, 12 );

  QLabel *hint = new QLabel( tr( "请在左侧工程图层树中，右键“工程”创建期次，右键“期次”添加影像；矢量数据也会按业务类型自动归档。" ) );
  hint->setWordWrap( true );
  hint->setProperty( "muted", true );
  layout->addWidget( hint );

  QGroupBox *summary = new QGroupBox( tr( "工程数据概况" ) );
  QVBoxLayout *summaryLayout = new QVBoxLayout( summary );
  mDataSummaryLabel = new QLabel;
  mDataSummaryLabel->setWordWrap( true );
  summaryLayout->addWidget( mDataSummaryLabel );
  layout->addWidget( summary );
  layout->addStretch();
  return page;
}

QWidget *QgsEcoRestorationController::createRecognitionPage()
{
  QWidget *page = new QWidget;
  page->setObjectName( QStringLiteral( "EcoRecognitionPage" ) );
  page->setStyleSheet( QStringLiteral( R"(
    QWidget#EcoRecognitionPage { background:#181818; color:#d4d4d4; }
    QWidget#EcoRecognitionPage QGroupBox { color:#d4d4d4; background:#1e1e1e; border:1px solid #3c3c3c; border-radius:4px; margin-top:12px; padding:12px 8px 8px 8px; }
    QWidget#EcoRecognitionPage QGroupBox::title { color:#e5e7eb; subcontrol-origin:margin; left:8px; padding:0 4px; background:#1e1e1e; }
    QWidget#EcoRecognitionPage QLabel { color:#cdd6e0; }
    QWidget#EcoRecognitionPage QLabel[muted="true"] { color:#858585; }
    QWidget#EcoRecognitionPage QComboBox,
    QWidget#EcoRecognitionPage QSpinBox,
    QWidget#EcoRecognitionPage QDoubleSpinBox,
    QWidget#EcoRecognitionPage QLineEdit { color:#e5e7eb; background:#252526; border:1px solid #3c3c3c; border-radius:3px; min-height:26px; padding:2px 8px; selection-background-color:#094771; }
    QWidget#EcoRecognitionPage QComboBox:hover,
    QWidget#EcoRecognitionPage QSpinBox:hover,
    QWidget#EcoRecognitionPage QDoubleSpinBox:hover,
    QWidget#EcoRecognitionPage QLineEdit:hover { border-color:#4b5563; }
    QWidget#EcoRecognitionPage QComboBox:focus,
    QWidget#EcoRecognitionPage QSpinBox:focus,
    QWidget#EcoRecognitionPage QDoubleSpinBox:focus,
    QWidget#EcoRecognitionPage QLineEdit:focus { border-color:#0e639c; }
    QWidget#EcoRecognitionPage QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:24px; border-left:1px solid #454545; background:#333337; border-top-right-radius:3px; border-bottom-right-radius:3px; }
    QWidget#EcoRecognitionPage QComboBox::drop-down:hover { background:#094771; border-left-color:#5a5a5a; }
    QWidget#EcoRecognitionPage QComboBox::down-arrow { image:url(:/images/themes/default/mActionArrowDown.svg); width:12px; height:12px; }
    QWidget#EcoRecognitionPage QComboBox QAbstractItemView { color:#e5e7eb; background:#252526; border:1px solid #454545; selection-color:#ffffff; selection-background-color:#0e639c; outline:0; padding:2px; }
    QWidget#EcoRecognitionPage QComboBox QAbstractItemView::item { min-height:26px; padding:4px 8px; }
    QWidget#EcoRecognitionPage QComboBox QAbstractItemView::item:hover { color:#ffffff; background:#094771; }
    QWidget#EcoRecognitionPage QComboBox QAbstractItemView::item:selected { color:#ffffff; background:#0e639c; }
    QWidget#EcoRecognitionPage QSpinBox::up-button,
    QWidget#EcoRecognitionPage QSpinBox::down-button,
    QWidget#EcoRecognitionPage QDoubleSpinBox::up-button,
    QWidget#EcoRecognitionPage QDoubleSpinBox::down-button { subcontrol-origin:border; width:20px; background:#333337; border-left:1px solid #454545; }
    QWidget#EcoRecognitionPage QSpinBox::up-button,
    QWidget#EcoRecognitionPage QDoubleSpinBox::up-button { subcontrol-position:top right; border-bottom:1px solid #454545; border-top-right-radius:3px; }
    QWidget#EcoRecognitionPage QSpinBox::down-button,
    QWidget#EcoRecognitionPage QDoubleSpinBox::down-button { subcontrol-position:bottom right; border-bottom-right-radius:3px; }
    QWidget#EcoRecognitionPage QSpinBox::up-button:hover,
    QWidget#EcoRecognitionPage QSpinBox::down-button:hover,
    QWidget#EcoRecognitionPage QDoubleSpinBox::up-button:hover,
    QWidget#EcoRecognitionPage QDoubleSpinBox::down-button:hover { background:#094771; }
    QWidget#EcoRecognitionPage QSpinBox::up-arrow,
    QWidget#EcoRecognitionPage QDoubleSpinBox::up-arrow { image:url(:/images/themes/default/mActionArrowUp.svg); width:10px; height:10px; }
    QWidget#EcoRecognitionPage QSpinBox::down-arrow,
    QWidget#EcoRecognitionPage QDoubleSpinBox::down-arrow { image:url(:/images/themes/default/mActionArrowDown.svg); width:10px; height:10px; }
    QWidget#EcoRecognitionPage QPushButton { color:#d4d4d4; background:#252526; border:1px solid #3c3c3c; border-radius:4px; min-height:28px; padding:4px 10px; }
    QWidget#EcoRecognitionPage QPushButton:hover { background:#2a2d2e; border-color:#4b5563; }
    QWidget#EcoRecognitionPage QPushButton[primary="true"] { color:#ffffff; background:#0e639c; border-color:#1177bb; font-weight:600; }
    QWidget#EcoRecognitionPage QPushButton[primary="true"]:hover { background:#1177bb; }
  )" ) );
  QVBoxLayout *layout = new QVBoxLayout( page );
  layout->setContentsMargins( 12, 12, 12, 12 );
  layout->setSpacing( 10 );

  mRecognitionSelectionLabel = new QLabel( tr( "就绪" ), page );
  mRecognitionSelectionLabel->setObjectName( QStringLiteral( "EcoRecognitionSelectionLabel" ) );
  mRecognitionSelectionLabel->setWordWrap( true );
  mRecognitionSelectionLabel->setMinimumHeight( 34 );
  mRecognitionSelectionLabel->setStyleSheet( QStringLiteral( "QLabel#EcoRecognitionSelectionLabel { color:#4ade80; background:#102a1a; border:1px solid #14532d; border-radius:4px; padding:6px 8px; }" ) );
  layout->addWidget( mRecognitionSelectionLabel );

  QGroupBox *inferenceBox = new QGroupBox( tr( "整线塔基扰动识别" ) );
  QFormLayout *inferenceForm = new QFormLayout;
  mRecognitionRasterCombo = new QComboBox;
  mConfidenceSpin = new QDoubleSpinBox;
  mConfidenceSpin->setRange( 0.05, 0.95 );
  mConfidenceSpin->setSingleStep( 0.05 );
  mConfidenceSpin->setValue( 0.35 );
  mConfidenceSpin->setDecimals( 2 );
  mRecognitionTowerCombo = new QComboBox;
  // The popup view is a separately rendered Qt view on some platforms and
  // does not always inherit the parent QComboBox palette.  Apply the same
  // dark palette directly to both recognition selectors so their expanded
  // lists remain consistent with the panel.
  const QString recognitionComboPopupStyle = QStringLiteral( R"(
    QAbstractItemView { color:#e5e7eb; background:#252526; border:1px solid #454545; selection-color:#ffffff; selection-background-color:#0e639c; outline:0; padding:2px; }
    QAbstractItemView::item { min-height:26px; padding:4px 8px; }
    QAbstractItemView::item:hover { color:#ffffff; background:#094771; }
    QAbstractItemView::item:selected { color:#ffffff; background:#0e639c; }
  )" );
  mRecognitionRasterCombo->view()->setStyleSheet( recognitionComboPopupStyle );
  mRecognitionTowerCombo->view()->setStyleSheet( recognitionComboPopupStyle );
  mRecognitionRasterCombo->view()->setAutoFillBackground( true );
  mRecognitionTowerCombo->view()->setAutoFillBackground( true );
  mRecognitionRangeSpin = new QSpinBox;
  mRecognitionRangeSpin->setRange( 20, 10000 );
  mRecognitionRangeSpin->setValue( 100 );
  mRecognitionRangeSpin->setButtonSymbols( QAbstractSpinBox::UpDownArrows );
  mRecognitionRangeSpin->setKeyboardTracking( true );
  mRecognitionPreviewSwitch = new EcoSwitchButton;
  mRecognitionPreviewSwitch->setObjectName( QStringLiteral( "EcoRecognitionPreviewSwitch" ) );
  mRecognitionPreviewSwitch->setChecked( true );
  mRecognitionPreviewSwitch->setToolTip( tr( "显示或隐藏地图上的杆塔识别范围框" ) );
  inferenceForm->addRow( tr( "识别影像" ), mRecognitionRasterCombo );
  inferenceForm->addRow( tr( "杆塔点图层" ), mRecognitionTowerCombo );
  QWidget *recognitionRangeControl = new QWidget;
  QHBoxLayout *recognitionRangeLayout = new QHBoxLayout( recognitionRangeControl );
  recognitionRangeLayout->setContentsMargins( 0, 0, 0, 0 );
  recognitionRangeLayout->setSpacing( 6 );
  recognitionRangeLayout->addWidget( mRecognitionRangeSpin, 1 );
  recognitionRangeLayout->addWidget( new QLabel( tr( "米" ), recognitionRangeControl ) );
  recognitionRangeLayout->addSpacing( 6 );
  recognitionRangeLayout->addWidget( mRecognitionPreviewSwitch );
  inferenceForm->addRow( tr( "杆塔识别范围" ), recognitionRangeControl );
  QVBoxLayout *inferenceLayout = new QVBoxLayout( inferenceBox );
  inferenceLayout->addLayout( inferenceForm );
  mRecognitionScanPreview = new EcoRecognitionScanPreview( inferenceBox );
  inferenceLayout->addWidget( mRecognitionScanPreview );
  QPushButton *towerRunButton = new QPushButton( tr( "开始整线塔基扰动识别" ) );
  towerRunButton->setObjectName( QStringLiteral( "EcoRecognitionRunButton" ) );
  mRecognitionRunButton = towerRunButton;
  towerRunButton->setProperty( "primary", true );
  towerRunButton->setStyleSheet( QStringLiteral( R"(
    QPushButton#EcoRecognitionRunButton { color:#ffffff; background:#0e639c; border:1px solid #1177bb; border-radius:4px; min-height:30px; padding:4px 10px; font-weight:600; }
    QPushButton#EcoRecognitionRunButton:hover { background:#1177bb; }
    QPushButton#EcoRecognitionRunButton:pressed { background:#0b5a8d; }
    QPushButton#EcoRecognitionRunButton:disabled { color:#858585; background:#252526; border-color:#3c3c3c; }
  )" ) );
  towerRunButton->setEnabled( QgsEcoOnnxInference::isRuntimeAvailable() );
  const QString runtimeTip = QgsEcoOnnxInference::isRuntimeAvailable() ? tr( "沿杆塔点图层逐塔执行识别" ) : tr( "智能识别组件暂不可用" );
  towerRunButton->setToolTip( runtimeTip );
  connect( towerRunButton, &QPushButton::clicked, this, [this] { runTowerRecognition(); } );
  connect( mRecognitionRasterCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this] {
    // Changing the selected recognition image must not rebuild the complete
    // business layer tree synchronously. QGIS emits several canvas/tree
    // notifications while a combo-box signal is being delivered, and doing a
    // full sync here can re-enter the bridge with an incomplete layer list.
    clearRecognitionPreview();
    if ( mRecognitionRasterRefreshPending )
      return;
    mRecognitionRasterRefreshPending = true;
    ecoImportTrace( QStringLiteral( "recognition raster switch scheduled layerId=%1" ).arg( mRecognitionRasterCombo->currentData().toString() ) );
    QTimer::singleShot( 0, this, [this] {
      mRecognitionRasterRefreshPending = false;
      if ( mProjectTransitionInProgress || !mApp || !mRecognitionRasterCombo )
        return;
      ecoImportTrace( QStringLiteral( "recognition raster switch refresh layerId=%1" ).arg( mRecognitionRasterCombo->currentData().toString() ) );
      refreshRecognitionRasterOrder();
      updateRecognitionPreview();
      ecoImportTrace( QStringLiteral( "recognition raster switch complete layerId=%1" ).arg( mRecognitionRasterCombo->currentData().toString() ) );
    } );
  } );
  connect( mRecognitionTowerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this] { updateRecognitionPreview(); } );
  connect( mRecognitionRangeSpin, QOverload<int>::of( &QSpinBox::valueChanged ), this, [this] { updateRecognitionPreview(); } );
  connect( mRecognitionPreviewSwitch, &QToolButton::toggled, this, [this]( bool enabled ) {
    if ( enabled )
      updateRecognitionPreview();
    else
      clearRecognitionPreview();
  } );
  inferenceLayout->addWidget( towerRunButton );
  layout->addWidget( inferenceBox );
  layout->addStretch();
  return page;
}

QWidget *QgsEcoRestorationController::createReviewPage()
{
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout( page );
  layout->setContentsMargins( 12, 12, 12, 12 );

  QLabel *hint = new QLabel( tr( "复用 QGIS 原生拓扑编辑与撤销机制，适合对识别图斑进行精细边界修订。" ) );
  hint->setWordWrap( true );
  hint->setProperty( "muted", true );
  layout->addWidget( hint );

  QFormLayout *form = new QFormLayout;
  mResultLayerCombo = new QComboBox;
  form->addRow( tr( "成果面图层" ), mResultLayerCombo );
  layout->addLayout( form );

  QGridLayout *buttons = new QGridLayout;
  QPushButton *addButton = new QPushButton( tr( "新增面" ) );
  addButton->setProperty( "primary", true );
  QPushButton *vertexButton = new QPushButton( tr( "修改节点" ) );
  QPushButton *selectButton = new QPushButton( tr( "选择图斑" ) );
  QPushButton *deleteButton = new QPushButton( tr( "删除所选" ) );
  QPushButton *saveButton = new QPushButton( tr( "保存编辑" ) );
  connect( addButton, &QPushButton::clicked, this, [this] { startAddingPolygon(); } );
  connect( vertexButton, &QPushButton::clicked, this, [this] { startVertexEditing(); } );
  connect( selectButton, &QPushButton::clicked, this, [this] { selectResultFeatures(); } );
  connect( deleteButton, &QPushButton::clicked, this, [this] { deleteSelectedFeatures(); } );
  connect( saveButton, &QPushButton::clicked, this, [this] { saveResultEdits(); } );
  buttons->addWidget( addButton, 0, 0 );
  buttons->addWidget( vertexButton, 0, 1 );
  buttons->addWidget( selectButton, 1, 0 );
  buttons->addWidget( deleteButton, 1, 1 );
  buttons->addWidget( saveButton, 2, 0, 1, 2 );
  layout->addLayout( buttons );

  QGroupBox *businessLayers = new QGroupBox( tr( "业务成果层" ) );
  QVBoxLayout *businessLayerLayout = new QVBoxLayout( businessLayers );
  QLabel *businessLayerHint = new QLabel( tr( "识别成果用于保存模型输出；修复成果用于保存复核、修正和补充标绘结果。两类成果分别归档。" ) );
  businessLayerHint->setWordWrap( true );
  businessLayerHint->setProperty( "muted", true );
  QHBoxLayout *businessLayerButtons = new QHBoxLayout;
  QPushButton *createRestorationButton = new QPushButton( tr( "新建修复层" ) );
  connect( createRestorationButton, &QPushButton::clicked, this, [this] { createResultLayer( true ); } );
  businessLayerButtons->addWidget( createRestorationButton );
  businessLayerLayout->addWidget( businessLayerHint );
  businessLayerLayout->addLayout( businessLayerButtons );
  layout->addWidget( businessLayers );

  QLabel *fields = new QLabel( tr( "扰动字段：扰动类型、置信度、修复状态、数据来源、影像日期、备注。" ) );
  fields->setWordWrap( true );
  fields->setProperty( "muted", true );
  layout->addWidget( fields );
  layout->addStretch();
  return page;
}

QWidget *QgsEcoRestorationController::createPhotoPage()
{
  mPhotoWorkbench = new QgsEcoPhotoWorkbench(
    [this]( const QString &title, const QString &message, bool warning ) {
      showEcoToast( mApp, title, message, warning, warning ? 3600 : 3000 );
    } );
  refreshPhotoWorkbenchContext();
  return mPhotoWorkbench;
}

QWidget *QgsEcoRestorationController::createScreenshotPage()
{
  QWidget *page = new QWidget;
  QVBoxLayout *layout = new QVBoxLayout( page );
  layout->setContentsMargins( 12, 12, 12, 12 );

  QLabel *hint = new QLabel( tr( "按每个杆塔点生成同一地面范围?PNG，同时输出截图索?CSV，便于解译成果台账和报告引用。" ) );
  hint->setWordWrap( true );
  hint->setProperty( "muted", true );
  layout->addWidget( hint );

  QFormLayout *form = new QFormLayout;
  mTowerLayerCombo = new QComboBox;
  mScreenshotRangeSpin = new QSpinBox;
  mScreenshotRangeSpin->setRange( 20, 10000 );
  mScreenshotRangeSpin->setValue( 500 );
  mScreenshotRangeSpin->setSuffix( tr( " 米" ) );
  mScreenshotSizeSpin = new QSpinBox;
  mScreenshotSizeSpin->setRange( 256, 4096 );
  mScreenshotSizeSpin->setSingleStep( 256 );
  mScreenshotSizeSpin->setValue( 1024 );
  mScreenshotSizeSpin->setSuffix( tr( " 像素" ) );
  form->addRow( tr( "杆塔点图层" ), mTowerLayerCombo );
  form->addRow( tr( "截图范围" ), mScreenshotRangeSpin );
  form->addRow( tr( "输出尺寸" ), mScreenshotSizeSpin );
  layout->addLayout( form );

  QPushButton *exportButton = new QPushButton( QgsApplication::getThemeIcon( QStringLiteral( "/mActionSaveMapAsImage.svg" ) ), tr( "批量生成杆塔截图" ) );
  exportButton->setProperty( "primary", true );
  connect( exportButton, &QPushButton::clicked, this, [this] { exportTowerScreenshots(); } );
  layout->addWidget( exportButton );
  layout->addStretch();
  return page;
}

QWidget *QgsEcoRestorationController::createWorkflowCard( const QString &number, const QString &title, const QString &description )
{
  QFrame *card = new QFrame;
  card->setProperty( "workflowCard", true );
  QHBoxLayout *layout = new QHBoxLayout( card );
  layout->setContentsMargins( 8, 7, 8, 7 );
  QLabel *badge = new QLabel( number );
  badge->setFixedSize( 24, 24 );
  badge->setAlignment( Qt::AlignCenter );
  badge->setStyleSheet( QStringLiteral( "background:#0e639c;color:white;border-radius:12px;font-weight:700;" ) );
  QVBoxLayout *textLayout = new QVBoxLayout;
  textLayout->setSpacing( 1 );
  QLabel *titleLabel = new QLabel( title );
  titleLabel->setStyleSheet( QStringLiteral( "font-weight:600;color:#eeeeee;" ) );
  QLabel *descriptionLabel = new QLabel( description );
  descriptionLabel->setWordWrap( true );
  descriptionLabel->setProperty( "muted", true );
  textLayout->addWidget( titleLabel );
  textLayout->addWidget( descriptionLabel );
  layout->addWidget( badge );
  layout->addLayout( textLayout, 1 );
  return card;
}

void QgsEcoRestorationController::createToolbar()
{
  mToolbar = new QToolBar( tr( "遥感解译工作面板" ), mApp );
  mToolbar->setObjectName( QStringLiteral( "EcoRestorationToolBar" ) );
  mToolbar->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
  mToolbar->setMovable( false );
  mToolbar->setFloatable( false );
  mToolbar->setIconSize( QSize( 20, 20 ) );
  mToolbar->setStyleSheet( QStringLiteral( R"(
    QToolBar { background:#181818; border:1px solid #3c3c3c; margin:6px 10px; spacing:3px; padding:4px 8px; }
    QToolButton { color:#cdd6e0; background:transparent; border:1px solid transparent; border-radius:4px; padding:4px 7px; }
    QToolButton:hover { background:#252a30; border-color:#3d4a55; }
    QToolButton:checked { background:#0e639c; color:white; border-color:#1177bb; }
  )" ) );

  QAction *newProject = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Project ), tr( "新建工程" ) );
  mPhaseCompareAction = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Compare ), tr( "期次对比" ) );
  mPhaseCompareAction->setEnabled( false );
  QAction *addVector = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Vector ), tr( "添加矢量" ) );
  mToolbar->addSeparator();
  QAction *drawPoint = mToolbar->addAction( QgsApplication::getThemeIcon( QStringLiteral( "mActionCapturePoint.svg" ) ), tr( "绘制点" ) );
  QAction *drawLine = mToolbar->addAction( QgsApplication::getThemeIcon( QStringLiteral( "mActionCaptureLine.svg" ) ), tr( "绘制线" ) );
  QAction *drawPolygon = mToolbar->addAction( QgsApplication::getThemeIcon( QStringLiteral( "mActionCapturePolygon.svg" ) ), tr( "绘制面" ) );
  drawPoint->setToolTip( tr( "在当前点矢量图层上使?QGIS 原生采集工具绘制点" ) );
  drawLine->setToolTip( tr( "在当前线矢量图层上使?QGIS 原生采集工具绘制线" ) );
  drawPolygon->setToolTip( tr( "在当前面矢量图层上使?QGIS 原生采集工具绘制面" ) );
  auto addNavigationAction = [this]( EcoToolbarIcon icon, const QString &text, QAction *source ) {
    QAction *action = mToolbar->addAction( ecoToolbarIcon( icon ), text );
    action->setCheckable( source->isCheckable() );
    connect( action, &QAction::triggered, source, &QAction::trigger );
    connect( source, &QAction::toggled, action, &QAction::setChecked );
    return action;
  };
  mToolbar->addSeparator();
  addNavigationAction( EcoToolbarIcon::Pan, tr( "平移" ), mApp->actionPan() );
  addNavigationAction( EcoToolbarIcon::ZoomIn, tr( "放大" ), mApp->actionZoomIn() );
  addNavigationAction( EcoToolbarIcon::ZoomOut, tr( "缩小" ), mApp->actionZoomOut() );
  addNavigationAction( EcoToolbarIcon::FullExtent, tr( "全图" ), mApp->actionZoomFullExtent() );

  mThreeDSwitch = new QToolButton;
  mThreeDSwitch->setText( tr( "二维" ) );
  mThreeDSwitch->setIcon( ecoToolbarIcon( EcoToolbarIcon::ThreeD ) );
  mThreeDSwitch->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
  mThreeDSwitch->setCheckable( true );
  mThreeDSwitch->setToolTip( tr( "切换二维 / 三维地图视图" ) );
  connect( mThreeDSwitch, &QToolButton::toggled, this, [this]( bool enabled ) {
    mThreeDSwitch->setText( enabled ? tr( "三维" ) : tr( "二维" ) );
    setThreeDMode( enabled );
  } );
  mToolbar->addWidget( mThreeDSwitch );
  mToolbar->addSeparator();
  mExtentRecognitionAction = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Disturbance ), tr( "框选塔基识别" ) );
  mExtentRecognitionAction->setCheckable( true );
  mExtentRecognitionAction->setToolTip( tr( "在地图上框选一个范围，执行一次塔基扰动识别" ) );
  mSmartSegmentationAction = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::SmartSegment ), tr( "智能分割" ) );
  mSmartSegmentationAction->setCheckable( true );
  mSmartSegmentationAction->setEnabled( hasIntegratedSam2Models() );
  mSmartSegmentationAction->setToolTip( mSmartSegmentationAction->isEnabled() ? tr( "使用正点和排除点交互生成精细图斑" ) : tr( "智能分割组件准备中" ) );
  mRecognitionPanelAction = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Panel ), tr( "智能识别工具" ) );
  mRecognitionPanelAction->setCheckable( true );
  mRecognitionPanelAction->setToolTip( tr( "在右侧单独打开智能识别面板" ) );
  QAction *photoProcessing = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Raster ), tr( "照片处理" ) );
  photoProcessing->setToolTip( tr( "打开当前期次的照片智能识别、分割、标绘和成果导出面板" ) );
  QAction *screenshot = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Screenshot ), tr( "批量截图" ) );
  QAction *showPanel = mToolbar->addAction( ecoToolbarIcon( EcoToolbarIcon::Panel ), tr( "业务面板" ) );

  connect( newProject, &QAction::triggered, this, [this] { createBusinessProject(); } );
  connect( mPhaseCompareAction, &QAction::triggered, this, [this] { showPhaseComparisonDialog(); } );
  connect( addVector, &QAction::triggered, this, [this] { importVectors(); } );
  connect( drawPoint, &QAction::triggered, this, [this] { startDrawingOnActiveLayer( QStringLiteral( "point" ) ); } );
  connect( drawLine, &QAction::triggered, this, [this] { startDrawingOnActiveLayer( QStringLiteral( "line" ) ); } );
  connect( drawPolygon, &QAction::triggered, this, [this] { startDrawingOnActiveLayer( QStringLiteral( "polygon" ) ); } );
  connect( mSmartSegmentationAction, &QAction::triggered, this, [this]( bool checked ) {
    if ( checked )
      startSmartSegmentation();
    else
      leaveSmartSegmentationTool();
  } );
  connect( photoProcessing, &QAction::triggered, this, [this] {
    if ( !mPhotoDock )
      return;
    refreshPhotoWorkbenchContext();
    mPhotoDock->show();
    mPhotoDock->raise();
  } );
  connect( screenshot, &QAction::triggered, this, [this] { exportTowerScreenshots(); } );
  connect( mRecognitionPanelAction, &QAction::toggled, this, [this]( bool checked ) {
    if ( !mRecognitionDock )
      return;
    if ( checked )
      showRecognitionPanel();
    else
      mRecognitionDock->hide();
  } );
  connect( showPanel, &QAction::triggered, mDock, &QgsDockWidget::show );
  connect( showPanel, &QAction::triggered, mDock, &QgsDockWidget::raise );
  connect( mExtentRecognitionAction, &QAction::toggled, this, [this]( bool enabled ) {
    if ( enabled )
      startExtentRecognition();
    else
      cancelExtentRecognition();
  } );

  mApp->addToolBar( mToolbar, Qt::TopToolBarArea );
}

void QgsEcoRestorationController::createBusinessProject()
{
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "识别期间可继续浏览地图和操作面板；新建工程请在识别完成后执行。" ), true, 3200 );
    return;
  }

  QDialog dialog( mApp );
  dialog.setWindowTitle( tr( "新建遥感解译工程" ) );
  dialog.setMinimumWidth( 460 );
  applyVsCodeDialogStyle( &dialog );
  QFormLayout *formLayout = new QFormLayout( &dialog );
  QLineEdit *nameEdit = new QLineEdit( tr( "遥感解译工程" ), &dialog );
  QLineEdit *lineEdit = new QLineEdit( &dialog );
  lineEdit->setPlaceholderText( tr( "例如：白苏特高压重庆段" ) );
  QComboBox *voltageEdit = new QComboBox( &dialog );
  voltageEdit->addItems( { tr( "卤800kV" ), tr( "卤500kV" ), tr( "1000kV" ), tr( "500kV" ), tr( "220kV" ), tr( "110kV" ), tr( "35kV" ), tr( "其他" ) } );
  voltageEdit->setCurrentIndex( 0 );
  QLineEdit *provinceEdit = new QLineEdit( &dialog );
  provinceEdit->setPlaceholderText( tr( "省份或区域" ) );
  QLineEdit *ownerEdit = new QLineEdit( &dialog );
  ownerEdit->setPlaceholderText( tr( "建设单位" ) );
  QLineEdit *phaseEdit = new QLineEdit( tr( "绗?期" ), &dialog );
  formLayout->addRow( tr( "工程名称" ), nameEdit );
  formLayout->addRow( tr( "线路名称" ), lineEdit );
  formLayout->addRow( tr( "电压等级" ), voltageEdit );
  formLayout->addRow( tr( "省份 / 区域" ), provinceEdit );
  formLayout->addRow( tr( "建设单位" ), ownerEdit );
  formLayout->addRow( tr( "默认期次" ), phaseEdit );
  QDialogButtonBox *dialogButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
  formLayout->addRow( dialogButtons );
  connect( dialogButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
  connect( dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
  if ( dialog.exec() != QDialog::Accepted )
    return;

  const QString inputName = nameEdit->text().trimmed();
  if ( inputName.isEmpty() )
    return;

  const QString parentPath = QFileDialog::getExistingDirectory( mApp, tr( "选择工程保存位置" ), QDir::homePath() );
  if ( parentPath.isEmpty() )
    return;

  // QgisApp::fileNewBlank() synchronously clears the current project and
  // emits several signals while QGIS is tearing down its layer tree. Keep the
  // workbench quiet for the whole transition, then refresh once at the end.
  mProjectTransitionInProgress = true;
  EcoScopeGuard transitionGuard( [this] {
    mProjectTransitionInProgress = false;
    scheduleProjectStateRefresh();
  } );

  // Detach all project-bound interaction state before QGIS starts removing
  // the old layer tree. These objects are owned by the canvas/controller, but
  // their pending geometry and previous map tools refer to the old project.
  mRecognitionRunning = false;
  mRecognitionResultRefreshPending = false;
  mRecognitionResultLayerId.clear();
  mRecognitionObservedResultLayerIds.clear();
  mRecognitionEditingTowerLabel.clear();
  mManualTowerDrawingLayerId.clear();
  mRecognitionPreviousMapTool = nullptr;
  if ( mRecognitionProgressBar )
    mRecognitionProgressBar->hide();
  if ( mRecognitionScanPreview )
    mRecognitionScanPreview->hide();
  if ( mExtentRecognitionAction )
  {
    const QSignalBlocker blocker( mExtentRecognitionAction );
    mExtentRecognitionAction->setChecked( false );
  }
  if ( mRecognitionExtentTool )
    mRecognitionExtentTool->clearRubberBand();
  clearRecognitionPreview();

  if ( mSmartSegmentationTool )
  {
    if ( EcoSam2PromptMapTool *tool = static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) )
      tool->resetSession();
  }
  mSmartSegmentationPendingGeometry = QgsGeometry();
  mSmartSegmentationPendingScore = 0.0;
  mSmartSegmentationTargetLayerId.clear();
  mSmartSegmentationFeatureId = -1;
  mSmartSegmentationStandalone = true;
  mSmartSegmentationOutputCrs = QgsCoordinateReferenceSystem();
  mSmartSegmentationPreviousMapTool = nullptr;
  if ( mSmartSegmentationAction )
  {
    const QSignalBlocker blocker( mSmartSegmentationAction );
    mSmartSegmentationAction->setChecked( false );
  }

  if ( mApp->mapCanvas() )
  {
    QgsMapTool *currentMapTool = mApp->mapCanvas()->mapTool();
    if ( currentMapTool == mRecognitionExtentTool || currentMapTool == mSmartSegmentationTool )
    {
      if ( mApp->actionPan() )
        mApp->actionPan()->trigger();
    }
  }

  if ( !mApp->createBlankProjectForIntegratedWorkflow() )
    return;

  const QString projectName = sanitizedName( inputName );
  QDir parentDir( parentPath );
  const QString workspacePath = parentDir.filePath( projectName );
  if ( !parentDir.mkpath( projectName ) )
  {
    showMessage( tr( "创建工程失败" ), tr( "无法创建工程目录?1" ).arg( workspacePath ), true );
    return;
  }

  QDir workspace( workspacePath );
  const QStringList folders = { QStringLiteral( "vectors" ), QStringLiteral( "models" ), QStringLiteral( "reports" ), QStringLiteral( "cache" ), QStringLiteral( "phases" ), QStringLiteral( "assets" ) };
  for ( const QString &folder : folders )
    workspace.mkpath( folder );
  ensureProjectTowerIconAsset( workspacePath );

  QgsProject *project = QgsProject::instance();
  project->setTitle( inputName );
  project->writeEntry( sProjectGroup, QStringLiteral( "enabled" ), true );
  project->writeEntry( sProjectGroup, QStringLiteral( "name" ), inputName );
  project->writeEntry( sProjectGroup, QStringLiteral( "lineName" ), lineEdit->text().trimmed() );
  project->writeEntry( sProjectGroup, QStringLiteral( "voltageLevel" ), voltageEdit->currentText().trimmed() );
  project->writeEntry( sProjectGroup, QStringLiteral( "province" ), provinceEdit->text().trimmed() );
  project->writeEntry( sProjectGroup, QStringLiteral( "ownerUnit" ), ownerEdit->text().trimmed() );
  project->writeEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ), phaseEdit->text().trimmed() );
  project->writeEntry( sProjectGroup, QStringLiteral( "workspace" ), workspacePath );
  project->writeEntry( sProjectGroup, QStringLiteral( "workflowVersion" ), QStringLiteral( "1.0" ) );
  project->writeEntry( sProjectGroup, QStringLiteral( "createdAt" ), QDateTime::currentDateTime().toString( Qt::ISODate ) );
  const QString defaultPhaseName = phaseEdit->text().trimmed().isEmpty() ? tr( "绗?期" ) : phaseEdit->text().trimmed();
  const QString phaseId = createPhaseInternal( defaultPhaseName, true );
  if ( phaseId.isEmpty() )
  {
    showMessage( tr( "创建工程失败" ), tr( "工程已建立，但未能初始化默认期次。" ), true );
    return;
  }

  if ( !projectTreeRoot() )
    ensureBusinessGroups();
  else
    commonDataGroup( true );
  if ( QgsLayerTreeGroup *common = commonDataGroup( true ) )
  {
    common->setExpanded( true );
    common->setItemVisibilityChecked( true );
    if ( QgsLayerTreeGroup *lineGroup = directChildGroup( common, sLineGroup ) )
    {
      lineGroup->setExpanded( true );
      lineGroup->setItemVisibilityChecked( true );
    }
  }

  const QString projectFile = workspace.filePath( projectName + QStringLiteral( ".qgz" ) );
  if ( !project->write( projectFile ) )
  {
    showMessage( tr( "保存工程失败" ), tr( "工程目录已建立，?QGIS 工程文件未能写入?1" ).arg( projectFile ), true );
    return;
  }

  ensureBusinessGroups();
  syncBusinessProjectView();
  showMessage( tr( "工程已创建" ), tr( "已创建标准工程目录并保存：\n%1" ).arg( projectFile ) );
}

void QgsEcoRestorationController::importImagery( const QString &targetPhaseId )
{
  ecoImportTrace( QStringLiteral( "importImagery entered targetPhaseId=%1" ).arg( targetPhaseId ) );
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "正在读取识别影像，暂不能增删影像图层；地图浏览不受影响。" ), true, 3200 );
    return;
  }

  bool projectEnabled = false;
  const bool hasBusinessProject = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &projectEnabled ) && projectEnabled && !QgsProject::instance()->fileName().isEmpty();
  if ( !hasBusinessProject )
  {
    showMessage( tr( "添加影像" ), tr( "请先新建并保存遥感解译工程，再加?TIF/TIFF 影像。" ), true );
    return;
  }
  QString initialDirectory = projectWorkspace();
  if ( !targetPhaseId.trimmed().isEmpty() )
  {
    const QString phaseDirectory = phaseWorkspace( targetPhaseId.trimmed() );
    if ( !phaseDirectory.isEmpty() )
      initialDirectory = phaseDirectory;
  }
  const QStringList paths = QFileDialog::getOpenFileNames( mApp, tr( "添加遥感影像" ), initialDirectory, tr( "GeoTIFF 影像 (*.tif *.tiff);;所有文?(*.*)" ) );
  ecoImportTrace( QStringLiteral( "file dialog returned count=%1" ).arg( paths.size() ) );
  if ( paths.isEmpty() )
    return;

  // A raster import must not start by normalizing the whole business tree.
  // In particular, organizeProjectTree() can move every existing group and
  // layer node. When a native file dialog has just returned, Qt is still
  // unwinding its modal event loop and a full tree rearrangement can re-enter
  // the layer-tree model through its bridge. Besides being needlessly costly
  // for a large project, that re-entrancy can leave the desktop application
  // permanently unresponsive before the first raster is even opened.
  //
  // Keep the import transaction deliberately narrow: create/find only the
  // selected phase's imagery group, attach the new layers there, and use the
  // existing light-weight canvas synchronization below.
  mLayerImportInProgress = true;
  EcoScopeGuard importGuard( [this] {
    mLayerImportInProgress = false;
  } );

  QString phaseId = targetPhaseId.trimmed();
  if ( phaseId.isEmpty() )
    phaseId = currentPhaseId();
  else if ( !phaseIds().contains( phaseId ) )
    phaseId.clear();
  if ( phaseId.isEmpty() )
  {
    showMessage( tr( "添加影像" ), tr( "当前工程尚未初始化期次，无法归档影像。" ), true );
    return;
  }

  QgsProject *project = QgsProject::instance();
  const QString phaseName = currentPhaseName( phaseId );
  project->writeEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ), phaseId );
  project->writeEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ), phaseName );

  QgsLayerTreeGroup *projectLayerRoot = project->layerTreeRoot();
  if ( !projectLayerRoot )
  {
    ecoImportTrace( QStringLiteral( "imagery import has no project layer-tree root" ) );
    showMessage( tr( "添加影像" ), tr( "当前工程图层树不可用，无法加载影像。" ), true );
    return;
  }

  QgsLayerTreeGroup *businessRoot = projectTreeRoot();
  if ( !businessRoot )
  {
    QString projectName = project->readEntry( sProjectGroup, QStringLiteral( "name" ) );
    if ( projectName.isEmpty() )
      projectName = project->title();
    businessRoot = projectLayerRoot->addGroup( tr( "工程 路 %1" ).arg( projectName.isEmpty() ? tr( "未命名工程" ) : projectName ) );
    businessRoot->setCustomProperty( sProjectRootProperty, true );
  }
  businessRoot->setCustomProperty( sProjectRootProperty, true );
  businessRoot->setCustomProperty( sGroupTypeProperty, sProjectRootGroupName );
  businessRoot->setCustomProperty( sTreeIconProperty, QStringLiteral( "project" ) );
  businessRoot->setExpanded( true );
  businessRoot->setItemVisibilityChecked( true );

  QgsLayerTreeGroup *phaseGroupNode = directChildGroupByProperty( businessRoot, sPhaseIdProperty, phaseId );
  if ( !phaseGroupNode )
    phaseGroupNode = directChildGroup( businessRoot, sPhasePrefix + phaseName );
  if ( !phaseGroupNode )
    phaseGroupNode = businessRoot->addGroup( sPhasePrefix + phaseName );
  phaseGroupNode->setName( sPhasePrefix + phaseName );
  phaseGroupNode->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase" ) );
  phaseGroupNode->setCustomProperty( sPhaseIdProperty, phaseId );
  phaseGroupNode->setCustomProperty( sPhaseNameProperty, phaseName );
  phaseGroupNode->setCustomProperty( sTreeIconProperty, QStringLiteral( "phase" ) );
  phaseGroupNode->setExpanded( true );
  phaseGroupNode->setItemVisibilityChecked( true );

  QgsLayerTreeGroup *targetImageryGroup = directChildGroup( phaseGroupNode, sImageryGroup );
  if ( !targetImageryGroup )
    targetImageryGroup = phaseGroupNode->addGroup( sImageryGroup );
  targetImageryGroup->setCustomProperty( sPhaseIdProperty, phaseId );
  targetImageryGroup->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase-imagery" ) );
  targetImageryGroup->setCustomProperty( sTreeIconProperty, QStringLiteral( "imagery" ) );
  targetImageryGroup->setExpanded( true );
  targetImageryGroup->setItemVisibilityChecked( true );
  ecoImportTrace( QStringLiteral( "import imagery target group ready phaseId=%1" ).arg( phaseId ) );

  const auto existingLayers = project->mapLayers();
  const bool hadSpatialLayers = std::any_of( existingLayers.cbegin(), existingLayers.cend(), []( QgsMapLayer *layer ) {
    return layer && layer->isSpatial();
  } );
  QList<QgsMapLayer *> loaded;
  QStringList loadedLayerIds;
  QgsRectangle loadedExtent;
  bool hasLoadedExtent = false;
  QStringList invalidPaths;
  for ( const QString &path : paths )
  {
    ecoImportTrace( QStringLiteral( "begin raster path=%1" ).arg( path ) );
    std::unique_ptr<QgsRasterLayer> layer = std::make_unique<QgsRasterLayer>( path, QFileInfo( path ).completeBaseName(), QStringLiteral( "gdal" ) );
    if ( !layer || !layer->isValid() )
    {
      ecoImportTrace( QStringLiteral( "invalid raster path=%1" ).arg( path ) );
      invalidPaths.append( QFileInfo( path ).fileName() );
      continue;
    }
    ecoImportTrace( QStringLiteral( "raster layer valid path=%1" ).arg( path ) );

    QgsRasterLayer *rawLayer = layer.release();
    rawLayer->setCustomProperty( QStringLiteral( "eco/category" ), QStringLiteral( "imagery" ) );
    rawLayer->setCustomProperty( sPhaseIdProperty, phaseId );
    ecoImportTrace( QStringLiteral( "before prepare imagery layerId=%1" ).arg( rawLayer->id() ) );
    prepareBusinessImagery( rawLayer );
    ecoImportTrace( QStringLiteral( "after prepare imagery layerId=%1" ).arg( rawLayer->id() ) );
    rawLayer->setCustomProperty( QStringLiteral( "eco/imageId" ), rawLayer->id() );
    rawLayer->setCustomProperty( QStringLiteral( "eco/imagePath" ), path );
    ecoImportTrace( QStringLiteral( "before addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    project->addMapLayer( rawLayer, false );
    ecoImportTrace( QStringLiteral( "after addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    if ( targetImageryGroup )
    {
      ecoImportTrace( QStringLiteral( "before attach imagery tree layerId=%1" ).arg( rawLayer->id() ) );
      targetImageryGroup->setExpanded( true );
      targetImageryGroup->setItemVisibilityChecked( true );
      QgsLayerTreeLayer *treeNode = projectLayerRoot ? projectLayerRoot->findLayer( rawLayer->id() ) : nullptr;
      if ( !treeNode )
        treeNode = targetImageryGroup->addLayer( rawLayer );
      else if ( treeNode->parent() != targetImageryGroup )
      {
        QgsLayerTreeLayer *businessNode = targetImageryGroup->addLayer( rawLayer );
        if ( businessNode )
        {
          businessNode->setExpanded( false );
          businessNode->setItemVisibilityChecked( true );
        }
        if ( QgsLayerTreeGroup *parent = qobject_cast<QgsLayerTreeGroup *>( treeNode->parent() ) )
        {
          parent->removeChildNode( treeNode );
          treeNode = businessNode;
        }
      }
      if ( treeNode )
      {
        treeNode->setExpanded( false );
        treeNode->setItemVisibilityChecked( true );
      }
      ecoImportTrace( QStringLiteral( "after attach imagery tree layerId=%1" ).arg( rawLayer->id() ) );
    }
    if ( QgsLayerTreeLayer *treeNode = project->layerTreeRoot()->findLayer( rawLayer->id() ) )
    {
      treeNode->setExpanded( false );
      treeNode->setItemVisibilityChecked( true );
    }
    if ( QgsLayerTreeGroup *phase = phaseGroup( phaseId, false ) )
      phase->setItemVisibilityChecked( true );
    if ( QgsLayerTreeLayer *layerNode = project->layerTreeRoot()->findLayer( rawLayer->id() ) )
      layerNode->setItemVisibilityChecked( true );
    if ( !hasLoadedExtent )
    {
      loadedExtent = rawLayer->extent();
      hasLoadedExtent = true;
    }
    else
    {
      loadedExtent.combineExtentWith( rawLayer->extent() );
    }
    loaded.append( rawLayer );
    loadedLayerIds.append( rawLayer->id() );
  }
  if ( loaded.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "no valid rasters loaded" ) );
    if ( mApp && mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "没有成功加载有效?TIF/TIFF 影像，请检查文件格式或 GDAL 支持。" ), 6500 );
    return;
  }
  ecoImportTrace( QStringLiteral( "loaded rasters count=%1" ).arg( loaded.size() ) );
  if ( mApp && mApp->mapCanvas() && hasLoadedExtent && !loadedExtent.isEmpty() && loadedExtent.isFinite() )
  {
    ecoImportTrace( QStringLiteral( "before canvas extent" ) );
    if ( !hadSpatialLayers && loaded.constFirst() && loaded.constFirst()->crs().isValid() )
      project->setCrs( loaded.constFirst()->crs() );

    QgsRectangle canvasExtent = loadedExtent;
    const QgsCoordinateReferenceSystem imageCrs = loaded.constFirst()->crs();
    const QgsCoordinateReferenceSystem canvasCrs = mApp->mapCanvas()->mapSettings().destinationCrs();
    if ( imageCrs.isValid() && canvasCrs.isValid() && imageCrs != canvasCrs )
    {
      try
      {
        QgsCoordinateTransform transform( imageCrs, canvasCrs, project->transformContext() );
        canvasExtent = transform.transformBoundingBox( loadedExtent );
      }
      catch ( const QgsCsException & )
      {
        canvasExtent = loadedExtent;
      }
    }
    if ( !canvasExtent.isEmpty() && canvasExtent.isFinite() )
      mApp->mapCanvas()->setExtent( canvasExtent );
    ecoImportTrace( QStringLiteral( "after canvas extent" ) );
  }
  if ( mApp )
    mApp->setActiveLayer( loaded.constFirst() );
  ecoImportTrace( QStringLiteral( "before refreshLayerChoices" ) );
  refreshLayerChoices();
  ecoImportTrace( QStringLiteral( "before syncBusinessProjectView" ) );
  syncBusinessProjectView( loaded );
  ecoImportTrace( QStringLiteral( "after syncBusinessProjectView" ) );
  if ( mApp && mApp->mapCanvas() && loaded.constFirst() )
    mApp->mapCanvas()->setCurrentLayer( loaded.constFirst() );
  ecoImportTrace( QStringLiteral( "after setCurrentLayer" ) );
  if ( mApp && mApp->statusBar() )
  {
    int rasterCount = 0;
    for ( QgsMapLayer *layer : project->mapLayers() )
    {
      if ( qobject_cast<QgsRasterLayer *>( layer ) )
        ++rasterCount;
    }
    const QgsLayerTreeGroup *imageryGroup = phaseSubGroup( phaseId, sImageryGroup, false );
    const int imageryChildCount = imageryGroup ? imageryGroup->children().size() : -1;
    QString message = tr( "已添?%1 幅影像到?2”，影像节点 %3 个，工程栅格 %4 个。" )
                        .arg( loaded.size() )
                        .arg( currentPhaseName( phaseId ) )
                        .arg( imageryChildCount )
                        .arg( rasterCount );
    if ( !invalidPaths.isEmpty() )
      message += tr( " %1 个文件未能加载。" ).arg( invalidPaths.size() );
    mApp->statusBar()->showMessage( message, 9000 );
  }
  QTimer::singleShot( 0, this, [this, phaseId, loadedLayerIds, invalidCount = invalidPaths.size()] {
    Q_UNUSED( phaseId )
    Q_UNUSED( loadedLayerIds )
    Q_UNUSED( invalidCount )
    return;

    QgsProject *project = QgsProject::instance();
    if ( mProjectTransitionInProgress )
      return;
    QList<QgsMapLayer *> liveLoaded;
    for ( const QString &layerId : loadedLayerIds )
    {
      if ( QgsMapLayer *layer = project->mapLayer( layerId ) )
        liveLoaded << layer;
    }
    if ( liveLoaded.size() != loadedLayerIds.size() )
      return;
    ensureBusinessGroups();
    QgsLayerTreeGroup *imageryGroup = phaseSubGroup( phaseId, sImageryGroup, true );
    QgsLayerTreeGroup *root = project->layerTreeRoot();
    int attachedCount = 0;
    for ( const QString &layerId : loadedLayerIds )
    {
      QgsRasterLayer *layer = qobject_cast<QgsRasterLayer *>( project->mapLayer( layerId ) );
      if ( !layer || !imageryGroup || !root )
        continue;

      layer->setCustomProperty( QStringLiteral( "eco/category" ), QStringLiteral( "imagery" ) );
      layer->setCustomProperty( sPhaseIdProperty, phaseId );
      QgsLayerTreeLayer *treeNode = root->findLayer( layerId );
      if ( treeNode && treeNode->parent() != imageryGroup )
        moveLayerToBusinessGroup( layer, sImageryGroup );
      treeNode = root->findLayer( layerId );
      if ( !treeNode )
        treeNode = imageryGroup->addLayer( layer );
      if ( treeNode )
      {
        treeNode->setItemVisibilityChecked( true );
        ++attachedCount;
      }
      layer->triggerRepaint();
    }

    if ( imageryGroup )
    {
      imageryGroup->setExpanded( true );
      imageryGroup->setItemVisibilityChecked( true );
    }
    if ( QgsLayerTreeGroup *phase = phaseGroup( phaseId, false ) )
    {
      phase->setExpanded( true );
      phase->setItemVisibilityChecked( true );
    }
    if ( QgsLayerTreeView *view = mApp ? mApp->layerTreeView() : nullptr )
    {
      if ( QgsLayerTreeModel *model = view->layerTreeModel() )
      {
        model->setFilterSettings( nullptr );
        view->expandAll();
        view->viewport()->update();
      }
    }
    refreshLayerChoices();
    syncBusinessProjectView( liveLoaded );
    if ( mApp && mApp->mapCanvas() && !liveLoaded.isEmpty() )
      mApp->mapCanvas()->setCurrentLayer( liveLoaded.constFirst() );

    int rasterCount = 0;
    for ( QgsMapLayer *layer : project->mapLayers() )
    {
      if ( qobject_cast<QgsRasterLayer *>( layer ) )
        ++rasterCount;
    }
    const int imageryChildCount = imageryGroup ? imageryGroup->children().size() : -1;
    if ( mApp && mApp->statusBar() )
    {
      QString message = tr( "已添?%1 幅影像到?2”，影像节点 %3 个，工程栅格 %4 个。" )
                          .arg( attachedCount )
                          .arg( currentPhaseName( phaseId ) )
                          .arg( imageryChildCount )
                          .arg( rasterCount );
      if ( invalidCount > 0 )
        message += tr( " %1 个文件未能加载。" ).arg( invalidCount );
      mApp->statusBar()->showMessage( message, 9000 );
    }
  } );
}

void QgsEcoRestorationController::removeProjectLayersSafely( const QStringList &layerIds )
{
  if ( layerIds.isEmpty() )
    return;

  QSet<QString> idsToRemove;
  for ( const QString &id : layerIds )
  {
    if ( !id.trimmed().isEmpty() && QgsProject::instance()->mapLayer( id ) )
      idsToRemove.insert( id );
  }
  if ( idsToRemove.isEmpty() )
    return;

  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "移除图层" ), tr( "智能识别正在读取影像，请等待识别结束后再移除图层。" ), true, 3200 );
    return;
  }

  clearRecognitionPreview();
  if ( mApp && mApp->mapCanvas() )
  {
    QgsMapCanvas *canvas = mApp->mapCanvas();
    canvas->stopRendering();
    QList<QgsMapLayer *> remainingLayers;
    for ( QgsMapLayer *layer : canvas->layers() )
    {
      if ( layer && !idsToRemove.contains( layer->id() ) )
        remainingLayers << layer;
    }
    canvas->setLayers( remainingLayers );
    if ( QgsMapLayer *currentLayer = canvas->currentLayer(); currentLayer && idsToRemove.contains( currentLayer->id() ) )
      canvas->setCurrentLayer( nullptr );
  }

  if ( mApp && mApp->layerTreeView() )
  {
    if ( QItemSelectionModel *selection = mApp->layerTreeView()->selectionModel() )
      selection->clear();
    mApp->layerTreeView()->setCurrentIndex( QModelIndex() );
  }

  if ( mRecognitionRasterCombo && idsToRemove.contains( mRecognitionRasterCombo->currentData().toString() ) )
  {
    const QSignalBlocker blocker( mRecognitionRasterCombo );
    mRecognitionRasterCombo->setCurrentIndex( -1 );
  }
  if ( mRecognitionTowerCombo && idsToRemove.contains( mRecognitionTowerCombo->currentData().toString() ) )
  {
    const QSignalBlocker blocker( mRecognitionTowerCombo );
    mRecognitionTowerCombo->setCurrentIndex( -1 );
  }
  if ( mResultLayerCombo && idsToRemove.contains( mResultLayerCombo->currentData().toString() ) )
  {
    const QSignalBlocker blocker( mResultLayerCombo );
    mResultLayerCombo->setCurrentIndex( -1 );
  }
  if ( idsToRemove.contains( mRecognitionResultLayerId ) )
  {
    mRecognitionResultLayerId.clear();
    if ( mRecognitionResultDock )
      mRecognitionResultDock->hide();
  }

  QgsProject::instance()->removeMapLayers( idsToRemove.values() );
  QgsProject::instance()->setDirty( true );
  refreshLayerChoices();
  syncBusinessProjectView();
  QTimer::singleShot( 0, this, [this] {
    if ( mProjectTransitionInProgress )
      return;
    refreshLayerChoices();
    syncBusinessProjectView();
  } );
}

void QgsEcoRestorationController::importVectors()
{
  ecoImportTrace( QStringLiteral( "importVectors entered" ) );
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "正在读取识别影像，暂不能导入矢量图层；地图浏览不受影响。" ), true, 3200 );
    return;
  }

  bool projectEnabled = false;
  const bool hasBusinessProject = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &projectEnabled ) && projectEnabled && !QgsProject::instance()->fileName().isEmpty();
  if ( !hasBusinessProject )
  {
    showMessage( tr( "添加矢量" ), tr( "请先新建并保存遥感解译工程，再加载杆塔、线路或调查范围矢量。" ), true );
    return;
  }
  const QStringList paths = QFileDialog::getOpenFileNames( mApp, tr( "添加杆塔、线路或面矢量" ), projectWorkspace(), tr( "Shapefile (*.shp);;常用矢量 (*.shp *.gpkg *.geojson);;所有文?(*.*)" ) );
  ecoImportTrace( QStringLiteral( "vector file dialog returned count=%1" ).arg( paths.size() ) );
  if ( paths.isEmpty() )
    return;
  if ( !projectTreeRoot() )
    ensureBusinessGroups();
  else
    commonDataGroup( true );

  mLayerImportInProgress = true;
  EcoScopeGuard importGuard( [this] {
    mLayerImportInProgress = false;
  } );

  QgsProject *project = QgsProject::instance();
  QList<QgsMapLayer *> loaded;
  for ( const QString &path : paths )
  {
    ecoImportTrace( QStringLiteral( "begin vector path=%1" ).arg( path ) );
    std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>( path, QFileInfo( path ).completeBaseName(), QStringLiteral( "ogr" ) );
    if ( !layer->isValid() )
    {
      ecoImportTrace( QStringLiteral( "invalid vector path=%1" ).arg( path ) );
      continue;
    }
    ecoImportTrace( QStringLiteral( "vector layer valid path=%1 geometry=%2" ).arg( path ).arg( static_cast<int>( layer->geometryType() ) ) );
    ecoImportTrace( QStringLiteral( "before vector style path=%1" ).arg( path ) );
    applyBusinessStyle( layer.get() );
    ecoImportTrace( QStringLiteral( "after vector style path=%1" ).arg( path ) );
    QgsVectorLayer *rawLayer = layer.release();
    ecoImportTrace( QStringLiteral( "before vector addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    project->addMapLayer( rawLayer, false );
    ecoImportTrace( QStringLiteral( "after vector addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    if ( QgsLayerTreeGroup *group = targetBusinessGroupForLayer( rawLayer ) )
    {
      ecoImportTrace( QStringLiteral( "before vector attach tree layerId=%1" ).arg( rawLayer->id() ) );
      group->setExpanded( true );
      group->setItemVisibilityChecked( true );
      group->addLayer( rawLayer );
      if ( QgsLayerTreeLayer *treeNode = group->findLayer( rawLayer->id() ) )
      {
        treeNode->setExpanded( false );
        treeNode->setItemVisibilityChecked( true );
      }
      ecoImportTrace( QStringLiteral( "after vector attach tree layerId=%1" ).arg( rawLayer->id() ) );
    }
    else if ( project->layerTreeRoot() )
      project->layerTreeRoot()->addLayer( rawLayer );
    loaded.append( rawLayer );
  }
  if ( loaded.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "no valid vectors loaded" ) );
    showMessage( tr( "添加矢量" ), tr( "没有成功加载有效的矢量数据。请确认 SHP 配套?DBF銆丼HX銆丳RJ 文件完整。" ), true );
    return;
  }
  ecoImportTrace( QStringLiteral( "loaded vectors count=%1" ).arg( loaded.size() ) );
  mApp->mapCanvas()->setExtent( loaded.constFirst()->extent() );
  mApp->mapCanvas()->refresh();
  refreshLayerChoices();
  syncBusinessProjectView( loaded );
  ecoImportTrace( QStringLiteral( "importVectors finished" ) );
}

void QgsEcoRestorationController::importTowerVectors()
{
  ecoImportTrace( QStringLiteral( "importTowerVectors entered" ) );
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "正在读取识别影像，暂不能导入杆塔图层；地图浏览不受影响。" ), true, 3200 );
    return;
  }

  bool projectEnabled = false;
  const bool hasBusinessProject = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &projectEnabled ) && projectEnabled && !QgsProject::instance()->fileName().isEmpty();
  if ( !hasBusinessProject )
  {
    showMessage( tr( "添加杆塔矢量" ), tr( "请先新建并保存遥感解译工程，再加载杆塔点矢量。" ), true );
    return;
  }

  const QStringList paths = QFileDialog::getOpenFileNames( mApp, tr( "添加杆塔矢量" ), projectWorkspace(), tr( "点矢?(*.shp *.gpkg *.geojson);;Shapefile (*.shp);;GeoPackage (*.gpkg);;GeoJSON (*.geojson);;所有文?(*.*)" ) );
  ecoImportTrace( QStringLiteral( "tower file dialog returned count=%1" ).arg( paths.size() ) );
  if ( paths.isEmpty() )
    return;

  if ( !projectTreeRoot() )
    ensureBusinessGroups();
  else
    commonDataGroup( true );

  mLayerImportInProgress = true;
  EcoScopeGuard importGuard( [this] {
    mLayerImportInProgress = false;
  } );

  QgsProject *project = QgsProject::instance();
  QList<QgsMapLayer *> loaded;
  for ( const QString &path : paths )
  {
    ecoImportTrace( QStringLiteral( "begin tower vector path=%1" ).arg( path ) );
    std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>( path, QFileInfo( path ).completeBaseName(), QStringLiteral( "ogr" ) );
    if ( !layer->isValid() )
    {
      ecoImportTrace( QStringLiteral( "invalid tower vector path=%1" ).arg( path ) );
      continue;
    }
    ecoImportTrace( QStringLiteral( "tower vector layer valid path=%1 geometry=%2" ).arg( path ).arg( static_cast<int>( layer->geometryType() ) ) );
    if ( layer->geometryType() != Qgis::GeometryType::Point )
    {
      ecoImportTrace( QStringLiteral( "skip non-point tower vector path=%1" ).arg( path ) );
      continue;
    }

    layer->setCustomProperty( QStringLiteral( "eco/category" ), QStringLiteral( "tower" ) );
    ecoImportTrace( QStringLiteral( "before tower style path=%1" ).arg( path ) );
    applyBusinessStyle( layer.get() );
    ecoImportTrace( QStringLiteral( "after tower style path=%1" ).arg( path ) );
    QgsVectorLayer *rawLayer = layer.release();
    ecoImportTrace( QStringLiteral( "before tower addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    project->addMapLayer( rawLayer, false );
    ecoImportTrace( QStringLiteral( "after tower addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
    if ( QgsLayerTreeGroup *group = targetBusinessGroupForLayer( rawLayer ) )
    {
      ecoImportTrace( QStringLiteral( "before tower attach tree layerId=%1" ).arg( rawLayer->id() ) );
      group->setExpanded( true );
      group->setItemVisibilityChecked( true );
      group->addLayer( rawLayer );
      if ( QgsLayerTreeLayer *treeNode = group->findLayer( rawLayer->id() ) )
      {
        treeNode->setExpanded( false );
        treeNode->setItemVisibilityChecked( true );
      }
      ecoImportTrace( QStringLiteral( "after tower attach tree layerId=%1" ).arg( rawLayer->id() ) );
    }
    else if ( QgsLayerTreeGroup *root = project->layerTreeRoot() )
      root->addLayer( rawLayer );
    loaded.append( rawLayer );
  }

  if ( loaded.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "no valid tower vectors loaded" ) );
    showMessage( tr( "添加杆塔矢量" ), tr( "没有成功加载有效的点类型杆塔矢量。请确认文件为点图层且配套文件完整。" ), true );
    return;
  }

  ecoImportTrace( QStringLiteral( "loaded tower vectors count=%1" ).arg( loaded.size() ) );
  mApp->setActiveLayer( loaded.constFirst() );
  if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
  {
    ecoImportTrace( QStringLiteral( "before tower canvas update" ) );
    canvas->setCurrentLayer( loaded.constFirst() );
    canvas->setExtent( loaded.constFirst()->extent() );
    canvas->refresh();
    ecoImportTrace( QStringLiteral( "after tower canvas update" ) );
  }
  QgsProject::instance()->setDirty( true );
  refreshLayerChoices();
  syncBusinessProjectView( loaded );
  ecoImportTrace( QStringLiteral( "importTowerVectors finished" ) );
}

void QgsEcoRestorationController::createManualVectorLayer( const QString &kind )
{
  const QString workspacePath = projectWorkspace();
  if ( workspacePath.isEmpty() || QgsProject::instance()->fileName().isEmpty() )
  {
    showMessage( tr( "地图手工绘制" ), tr( "请先新建或保存遥感解译工程。" ), true );
    return;
  }

  Qgis::WkbType wkbType = Qgis::WkbType::Unknown;
  QString filePrefix;
  QString layerName;
  QString category;
  QString groupName;
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "object_id" ), QMetaType::Type::Int ) );
  if ( kind == QLatin1String( "point" ) )
  {
    wkbType = Qgis::WkbType::Point;
    filePrefix = QStringLiteral( "point_manual" );
    layerName = tr( "手工普通点" );
    category = QStringLiteral( "manual-point" );
    groupName = sScopeGroup;
  }
  else if ( kind == QLatin1String( "tower" ) )
  {
    wkbType = Qgis::WkbType::Point;
    filePrefix = QStringLiteral( "tower_manual" );
    layerName = tr( "手工杆塔点" );
    category = QStringLiteral( "manual-tower" );
    groupName = sLineGroup;
    fields.append( QgsField( QStringLiteral( "tower_no" ), QMetaType::Type::QString, QString(), 40 ) );
    fields.append( QgsField( QStringLiteral( "tower_name" ), QMetaType::Type::QString, QString(), 60 ) );
    fields.append( QgsField( QStringLiteral( "status" ), QMetaType::Type::QString, QString(), 20 ) );
  }
  else if ( kind == QLatin1String( "line" ) )
  {
    wkbType = Qgis::WkbType::LineString;
    filePrefix = QStringLiteral( "line_manual" );
    layerName = tr( "手工线路" );
    category = QStringLiteral( "manual-line" );
    groupName = sLineGroup;
    fields.append( QgsField( QStringLiteral( "line_name" ), QMetaType::Type::QString, QString(), 80 ) );
    fields.append( QgsField( QStringLiteral( "voltage" ), QMetaType::Type::QString, QString(), 20 ) );
    fields.append( QgsField( QStringLiteral( "section" ), QMetaType::Type::QString, QString(), 40 ) );
  }
  else if ( kind == QLatin1String( "scope" ) )
  {
    wkbType = Qgis::WkbType::Polygon;
    filePrefix = QStringLiteral( "scope_manual" );
    layerName = tr( "手工调查范围" );
    category = QStringLiteral( "manual-scope" );
    groupName = sScopeGroup;
    fields.append( QgsField( QStringLiteral( "area_type" ), QMetaType::Type::QString, QString(), 40 ) );
    fields.append( QgsField( QStringLiteral( "area_name" ), QMetaType::Type::QString, QString(), 80 ) );
  }
  else
  {
    return;
  }
  fields.append( QgsField( QStringLiteral( "remark" ), QMetaType::Type::QString, QString(), 120 ) );

  QDir workspace( workspacePath );
  workspace.mkpath( QStringLiteral( "vectors" ) );
  QDir vectorDir( workspace.filePath( QStringLiteral( "vectors" ) ) );
  const QString outputPath = vectorDir.filePath(
    QStringLiteral( "%1_%2.shp" ).arg( filePrefix, QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) ) )
  );
  QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
  if ( !crs.isValid() )
    crs = QgsCoordinateReferenceSystem::fromEpsgId( 4490 );

  QgsVectorFileWriter::SaveVectorOptions options;
  options.driverName = QStringLiteral( "ESRI Shapefile" );
  options.fileEncoding = QStringLiteral( "UTF-8" );
  std::unique_ptr<QgsVectorFileWriter> writer( QgsVectorFileWriter::create( outputPath, fields, wkbType, crs, QgsProject::instance()->transformContext(), options ) );
  if ( !writer || writer->hasError() != QgsVectorFileWriter::NoError )
  {
    showMessage( tr( "创建手工矢量层失败" ), writer ? writer->errorMessage() : tr( "未知写入错误" ), true );
    return;
  }
  writer.reset();

  std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>( outputPath, layerName, QStringLiteral( "ogr" ) );
  if ( !layer->isValid() )
  {
    showMessage( tr( "创建手工矢量层失败" ), tr( "文件已生成，但无法载?QGIS銆俓" ), true );
    return;
  }
  const int objectIdIndex = layer->fields().indexFromName( QStringLiteral( "object_id" ) );
  if ( objectIdIndex >= 0 )
  {
    layer->setFieldAlias( objectIdIndex, tr( "编号" ) );
    layer->setDefaultValueDefinition( objectIdIndex, QgsDefaultValue( QStringLiteral( "coalesce(maximum(\"object_id\"), 0) + 1" ) ) );
  }
  if ( kind == QLatin1String( "point" ) )
  {
    QgsEditFormConfig formConfig = layer->editFormConfig();
    formConfig.setSuppress( Qgis::AttributeFormSuppression::On );
    layer->setEditFormConfig( formConfig );
  }
  layer->setCustomProperty( QStringLiteral( "eco/category" ), category );
  applyBusinessStyle( layer.get() );
  QgsVectorLayer *rawLayer = layer.release();
  QgsProject::instance()->addMapLayer( rawLayer, false );
  if ( !projectTreeRoot() )
    ensureBusinessGroups();
  else
    commonDataGroup( true );
  if ( QgsLayerTreeGroup *group = targetBusinessGroupForLayer( rawLayer ) )
  {
    group->setExpanded( true );
    group->setItemVisibilityChecked( true );
    group->addLayer( rawLayer );
    if ( QgsLayerTreeLayer *treeNode = group->findLayer( rawLayer->id() ) )
    {
      treeNode->setExpanded( false );
      treeNode->setItemVisibilityChecked( true );
    }
  }
  else if ( QgsLayerTreeGroup *fallback = QgsProject::instance()->layerTreeRoot()->findGroup( groupName ) )
  {
    fallback->setExpanded( true );
    fallback->setItemVisibilityChecked( true );
    fallback->addLayer( rawLayer );
    if ( QgsLayerTreeLayer *treeNode = fallback->findLayer( rawLayer->id() ) )
    {
      treeNode->setExpanded( false );
      treeNode->setItemVisibilityChecked( true );
    }
  }
  QgsProject::instance()->setDirty( true );
  refreshLayerChoices();
  mApp->setActiveLayer( rawLayer );
  if ( QgsLayerTreeView *view = mApp->layerTreeView() )
    view->setCurrentLayer( rawLayer );
  if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
    canvas->setCurrentLayer( rawLayer );
  if ( !rawLayer->startEditing() )
  {
    showMessage( tr( "地图手工绘制" ), tr( "图层已创建，但无法进入编辑状态。" ), true );
    return;
  }
  const QString rawLayerId = rawLayer->id();
  if ( kind == QLatin1String( "tower" ) )
  {
    mManualTowerDrawingLayerId = rawLayerId;
    rawLayer->setCustomProperty( QStringLiteral( "eco/manualTowerDrawingActive" ), true );
    if ( mApp && mApp->layerTreeView() && mApp->layerTreeView()->viewport() )
      mApp->layerTreeView()->viewport()->update();
  }
  QTimer::singleShot( 0, this, [this, rawLayerId, layerName] {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( rawLayerId ) );
    if ( !mApp || !layer )
      return;
    mApp->setActiveLayer( layer );
    if ( QgsLayerTreeView *view = mApp->layerTreeView() )
      view->setCurrentLayer( layer );
    if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
    {
      canvas->setCurrentLayer( layer );
      canvas->setFocus();
    }
    if ( !layer->isEditable() && !layer->startEditing() )
    {
      showMessage( tr( "地图手工绘制" ), tr( "图层已创建，但无法进入编辑状态。" ), true );
      return;
    }
    QTimer::singleShot( 0, this, [this, rawLayerId] {
      QgsVectorLayer *activeTowerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( rawLayerId ) );
      if ( !mApp || !activeTowerLayer )
        return;
      mApp->setActiveLayer( activeTowerLayer );
      if ( QgsLayerTreeView *view = mApp->layerTreeView() )
        view->setCurrentLayer( activeTowerLayer );
      if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
      {
        canvas->setCurrentLayer( activeTowerLayer );
        canvas->setFocus();
      }
      if ( !activeTowerLayer->isEditable() && !activeTowerLayer->startEditing() )
        return;
      mApp->actionAddFeature()->trigger();
    } );
    if ( mDrawingStatusLabel )
      mDrawingStatusLabel->setText( tr( "%1 绘制中：左键添加杆塔点；Ctrl+Z 可撤回上一处杆塔点。按住中键拖动或空格 + 左键拖动可临时平移，松开后继续绘制。" ).arg( layerName ) );
  } );
  if ( mDrawingStatusLabel )
  {
    mDrawingStatusLabel->setText( tr( "%1 绘制准备中…" ).arg( layerName ) );
  }
}

void QgsEcoRestorationController::finishManualTowerDrawing()
{
  if ( mManualTowerDrawingLayerId.isEmpty() )
    return;

  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mManualTowerDrawingLayerId ) );
  if ( mApp )
    mApp->actionPan()->trigger();

  if ( layer && layer->isEditable() )
  {
    if ( !layer->commitChanges() )
    {
      if ( mApp && mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "杆塔点保存失败，请检查图层写入权限。" ), 3500 );
      if ( mApp && mApp->layerTreeView() && mApp->layerTreeView()->viewport() )
        mApp->layerTreeView()->viewport()->update();
      return;
    }
  }

  if ( layer )
  {
    layer->setCustomProperty( QStringLiteral( "eco/manualTowerDrawingActive" ), false );
    applyBusinessStyle( layer );
    layer->triggerRepaint();
  }
  mManualTowerDrawingLayerId.clear();
  if ( mDrawingStatusLabel )
    mDrawingStatusLabel->setText( tr( "杆塔点绘制已结束。" ) );
  if ( mApp && mApp->statusBar() )
    mApp->statusBar()->showMessage( tr( "杆塔点绘制已结束" ), 2500 );
  if ( mApp && mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
  if ( mApp && mApp->layerTreeView() && mApp->layerTreeView()->viewport() )
    mApp->layerTreeView()->viewport()->update();
  QgsProject::instance()->setDirty( true );
  refreshLayerChoices();
}

void QgsEcoRestorationController::startDrawingOnActiveLayer( const QString &geometryKind )
{
  if ( !mApp )
    return;

  Qgis::GeometryType expectedType = Qgis::GeometryType::Unknown;
  QString geometryName;
  if ( geometryKind == QLatin1String( "point" ) )
  {
    expectedType = Qgis::GeometryType::Point;
    geometryName = tr( "点" );
  }
  else if ( geometryKind == QLatin1String( "line" ) )
  {
    expectedType = Qgis::GeometryType::Line;
    geometryName = tr( "线" );
  }
  else if ( geometryKind == QLatin1String( "polygon" ) )
  {
    expectedType = Qgis::GeometryType::Polygon;
    geometryName = tr( "面" );
  }
  else
  {
    return;
  }

  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( mApp->activeLayer() );
  if ( !layer )
  {
    createManualVectorLayer( geometryKind == QLatin1String( "polygon" ) ? QStringLiteral( "scope" ) : geometryKind );
    return;
  }
  if ( layer->geometryType() != expectedType )
  {
    showMessage( tr( "绘制%1矢量" ).arg( geometryName ), tr( "请先在左侧工程图层中选择一个可编辑?1图层。" ).arg( geometryName ), true );
    return;
  }
  if ( !layer->isEditable() && !mApp->toggleEditing( layer ) )
  {
    showMessage( tr( "绘制%1矢量" ).arg( geometryName ), tr( "当前图层无法进入编辑状态，请检查数据写入权限。" ), true );
    return;
  }

  if ( shouldUseTowerIconRenderer( layer ) )
    applyBusinessStyle( layer );

  mApp->actionAddFeature()->trigger();
  mApp->mapCanvas()->setFocus();
}

void QgsEcoRestorationController::runTowerRecognition()
{
  ecoImportTrace( QStringLiteral( "runTowerRecognition entered" ) );

  QgsRasterLayer *rasterLayer = mRecognitionRasterCombo
                                  ? qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) )
                                  : nullptr;
  QgsVectorLayer *towerLayer = mRecognitionTowerCombo
                                 ? qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionTowerCombo->currentData().toString() ) )
                                 : nullptr;
  ecoImportTrace( QStringLiteral( "runTowerRecognition selected raster=%1 tower=%2" )
                    .arg( rasterLayer ? rasterLayer->id() : QStringLiteral( "<null>" ),
                          towerLayer ? towerLayer->id() : QStringLiteral( "<null>" ) ) );
  if ( !rasterLayer )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "请先选择用于识别?TIF 影像。" ), true );
    return;
  }
  if ( !towerLayer || towerLayer->geometryType() != Qgis::GeometryType::Point )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "请先选择有效的杆塔点图层。" ), true );
    return;
  }
  if ( !rasterLayer->crs().isValid() || !towerLayer->crs().isValid() )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "影像和杆塔点图层必须具有有效的坐标系，请先设置坐标系。" ), true );
    return;
  }
  const qint64 featureCount = towerLayer->featureCount();
  ecoImportTrace( QStringLiteral( "runTowerRecognition featureCount=%1" ).arg( featureCount ) );
  if ( featureCount <= 0 )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "所选杆塔图层没有要素。" ), true );
    return;
  }
  if ( featureCount > 5000 )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "当前线路包含 %1 个点，超过单次上?5000，请按标段拆分。" ).arg( featureCount ), true );
    return;
  }
  if ( featureCount > 500 && QMessageBox::question( mApp, tr( "杆塔批量识别" ), tr( "将生成并识别 %1 个杆塔矩形影像块，可能耗时较长，是否继续？" ).arg( featureCount ) ) != QMessageBox::Yes )
    return;

  QStringList labels;
  ecoImportTrace( QStringLiteral( "runTowerRecognition before extents" ) );
  const QVector<QgsRectangle> extents = towerRecognitionExtents( towerLayer, rasterLayer->crs(), &labels );
  ecoImportTrace( QStringLiteral( "runTowerRecognition extents=%1 labels=%2" ).arg( extents.size() ).arg( labels.size() ) );
  if ( extents.isEmpty() )
  {
    showMessage( tr( "杆塔批量识别" ), tr( "没有生成有效的杆塔识别范围，请检查图层坐标系。" ), true );
    return;
  }
  ecoImportTrace( QStringLiteral( "runTowerRecognition before runRecognition" ) );
  runRecognition( extents, labels, towerLayer->id() );
}

QVector<QgsRectangle> QgsEcoRestorationController::towerRecognitionExtents( QgsVectorLayer *towerLayer, const QgsCoordinateReferenceSystem &targetCrs, QStringList *labels ) const
{
  QVector<QgsRectangle> extents;
  if ( labels )
    labels->clear();
  if ( !towerLayer || !targetCrs.isValid() || !mRecognitionRangeSpin )
    return extents;

  const qint64 featureCount = towerLayer->featureCount();
  extents.reserve( static_cast<int>( std::min<qint64>( featureCount, 5000 ) ) );
  const double halfRange = mRecognitionRangeSpin->value() / 2.0;
  QgsCoordinateTransform pointTransform( towerLayer->crs(), targetCrs, QgsProject::instance()->transformContext() );
  QgsDistanceArea distanceArea;
  distanceArea.setSourceCrs( targetCrs, QgsProject::instance()->transformContext() );
  distanceArea.setEllipsoid( QgsProject::instance()->ellipsoid() );
  constexpr double halfPi = 1.57079632679489661923;
  constexpr double pi = 3.14159265358979323846;

  QgsFeature feature;
  QgsFeatureIterator iterator = towerLayer->getFeatures();
  qint64 featureIndex = 0;
  while ( iterator.nextFeature( feature ) )
  {
    ++featureIndex;
    QgsPointXY center = featurePoint( feature.geometry() );
    if ( center.isEmpty() )
      continue;
    try
    {
      center = pointTransform.transform( center );
    }
    catch ( const QgsCsException & )
    {
      continue;
    }

    QgsPointXY east;
    QgsPointXY west;
    QgsPointXY north;
    QgsPointXY south;
    distanceArea.measureLineProjected( center, halfRange, halfPi, &east );
    distanceArea.measureLineProjected( center, halfRange, -halfPi, &west );
    distanceArea.measureLineProjected( center, halfRange, 0.0, &north );
    distanceArea.measureLineProjected( center, halfRange, pi, &south );
    const QgsRectangle extent( west.x(), south.y(), east.x(), north.y() );
    if ( extent.isEmpty() )
      continue;
    extents.append( extent );
    if ( labels )
      labels->append( featureLabel( feature, towerLayer->fields(), featureIndex ) );
  }
  return extents;
}

void QgsEcoRestorationController::updateRecognitionPreview()
{
  if ( mProjectTransitionInProgress )
  {
    clearRecognitionPreview();
    return;
  }
  if ( !mRecognitionPreviewActive || !mApp || !mRecognitionTowerCombo || !mRecognitionRasterCombo || !mRecognitionPreviewSwitch || !mRecognitionPreviewSwitch->isChecked() )
  {
    clearRecognitionPreview();
    return;
  }

  QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) );
  QgsVectorLayer *towerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionTowerCombo->currentData().toString() ) );
  if ( !rasterLayer || !towerLayer || towerLayer->geometryType() != Qgis::GeometryType::Point )
  {
    clearRecognitionPreview();
    return;
  }

  const QVector<QgsRectangle> extents = towerRecognitionExtents( towerLayer, rasterLayer->crs() );
  if ( extents.isEmpty() )
  {
    clearRecognitionPreview();
    return;
  }

  if ( !mRecognitionPreview )
  {
    mRecognitionPreview = new QgsRubberBand( mApp->mapCanvas(), Qgis::GeometryType::Polygon );
    mRecognitionPreview->setStrokeColor( QColor( 251, 191, 36, 220 ) );
    mRecognitionPreview->setFillColor( QColor( 251, 191, 36, 38 ) );
    mRecognitionPreview->setSecondaryStrokeColor( QColor( 15, 23, 42, 180 ) );
    mRecognitionPreview->setWidth( 2 );
    mRecognitionPreview->setLineStyle( Qt::DashLine );
  }
  mRecognitionPreview->reset( Qgis::GeometryType::Polygon );
  for ( const QgsRectangle &extent : extents )
  mRecognitionPreview->addGeometry( QgsGeometry::fromRect( extent ), rasterLayer->crs(), false );
  mRecognitionPreview->updatePosition();
  mRecognitionPreview->show();
}

void QgsEcoRestorationController::clearRecognitionPreview()
{
  if ( mRecognitionPreview )
  {
    mRecognitionPreview->reset( Qgis::GeometryType::Polygon );
    mRecognitionPreview->hide();
  }
}

void QgsEcoRestorationController::startExtentRecognition()
{
  QgsRasterLayer *rasterLayer = mRecognitionRasterCombo
                                  ? qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) )
                                  : nullptr;
  if ( !rasterLayer )
  {
    showMessage( tr( "框选范围识别" ), tr( "请先选择用于识别?TIF 影像。" ), true );
    return;
  }
  if ( !mRecognitionExtentTool )
  {
    mRecognitionExtentTool = new QgsMapToolExtent( mApp->mapCanvas() );
    connect( mRecognitionExtentTool, &QgsMapToolExtent::extentChanged, this, [this]( const QgsRectangle &canvasExtent ) {
      QgsRasterLayer *selectedRaster = mRecognitionRasterCombo
                                         ? qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) )
                                         : nullptr;
      if ( !selectedRaster || canvasExtent.isEmpty() )
        return;
      QgsRectangle rasterExtent;
      try
      {
        QgsCoordinateTransform transform( mApp->mapCanvas()->mapSettings().destinationCrs(), selectedRaster->crs(), QgsProject::instance()->transformContext() );
        rasterExtent = transform.transformBoundingBox( canvasExtent );
      }
      catch ( const QgsCsException & )
      {
        showMessage( tr( "框选范围识别" ), tr( "框选范围无法转换到影像坐标系。" ), true );
        cancelExtentRecognition();
        return;
      }
      if ( mRecognitionPreviousMapTool )
        mApp->mapCanvas()->setMapTool( mRecognitionPreviousMapTool );
      else
        mApp->actionPan()->trigger();
      mRecognitionPreviousMapTool = nullptr;
      mRecognitionExtentTool->clearRubberBand();
      if ( mRecognitionSelectionLabel )
        mRecognitionSelectionLabel->setText( tr( "已框选范围，正在启动塔基扰动识别..." ) );
      QTimer::singleShot( 0, this, [this, rasterExtent] {
        runRecognition( { rasterExtent }, { tr( "地图框选范围" ) } );
      } );
    } );
  }
  mRecognitionExtentTool->clearRubberBand();
  if ( mApp->mapCanvas()->mapTool() != mRecognitionExtentTool )
    mRecognitionPreviousMapTool = mApp->mapCanvas()->mapTool();
  mApp->mapCanvas()->setMapTool( mRecognitionExtentTool );
  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( tr( "请在地图上按住左键拖出矩形识别范围。" ) );
}

void QgsEcoRestorationController::startSmartSegmentation()
{
  if ( !mApp )
    return;

  const QString requestedLayerId = mSmartSegmentationRequestedLayerId;
  mSmartSegmentationRequestedLayerId.clear();

  if ( !QgsEcoSam2OnnxInference::isRuntimeAvailable() || !hasIntegratedSam2Models() )
  {
    if ( mSmartSegmentationAction )
    {
      const QSignalBlocker blocker( mSmartSegmentationAction );
      mSmartSegmentationAction->setChecked( false );
    }
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割组件准备中" ), 3500 );
    return;
  }

  QgsRasterLayer *rasterLayer = selectedRecognitionRasterLayer();
  if ( !rasterLayer )
  {
    if ( mSmartSegmentationAction )
    {
      const QSignalBlocker blocker( mSmartSegmentationAction );
      mSmartSegmentationAction->setChecked( false );
    }
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "请先加载一?TIF 影像，再使用智能分割。" ), 4500 );
    return;
  }

  QgsVectorLayer *layer = nullptr;
  const bool smartSegmentationLayerContext = !requestedLayerId.isEmpty();
  const bool disturbanceEditContext = !smartSegmentationLayerContext && !mRecognitionEditingTowerLabel.isEmpty();
  if ( smartSegmentationLayerContext )
  {
    layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( requestedLayerId ) );
    const bool validSmartSegmentationLayer = layer
                                             && layer->geometryType() == Qgis::GeometryType::Polygon
                                             && ( layer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" )
                                                  || layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "smart-segmentation" ) );
    if ( !validSmartSegmentationLayer || !ensureEditableResultLayer( layer ) )
    {
      if ( mSmartSegmentationAction )
      {
        const QSignalBlocker blocker( mSmartSegmentationAction );
        mSmartSegmentationAction->setChecked( false );
      }
      if ( mApp->statusBar() && !validSmartSegmentationLayer )
        mApp->statusBar()->showMessage( tr( "请选择有效的智能分割成果面图层。" ), 4500 );
      return;
    }
  }
  else if ( disturbanceEditContext )
  {
    layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
    if ( !layer || layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() != QLatin1String( "construction-disturbance" ) )
    {
      if ( mSmartSegmentationAction )
      {
        const QSignalBlocker blocker( mSmartSegmentationAction );
        mSmartSegmentationAction->setChecked( false );
      }
      if ( mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "请先从扰动结果表格选择要新增或编辑的杆塔图斑。" ), 4500 );
      return;
    }
    if ( !ensureEditableResultLayer( layer ) )
    {
      if ( mSmartSegmentationAction )
      {
        const QSignalBlocker blocker( mSmartSegmentationAction );
        mSmartSegmentationAction->setChecked( false );
      }
      return;
    }
  }

  mSmartSegmentationStandalone = !disturbanceEditContext && !smartSegmentationLayerContext;
  mSmartSegmentationTargetLayerId = layer ? layer->id() : QString();
  mSmartSegmentationFeatureId = -1;
  mSmartSegmentationPendingGeometry = QgsGeometry();
  mSmartSegmentationPendingScore = 0.0;
  mSmartSegmentationOutputCrs = layer ? layer->crs() : QgsProject::instance()->crs();
  if ( !mSmartSegmentationOutputCrs.isValid() )
    mSmartSegmentationOutputCrs = rasterLayer->crs();
  if ( disturbanceEditContext && layer && layer->selectedFeatureCount() == 1 )
    mSmartSegmentationFeatureId = *layer->selectedFeatureIds().constBegin();

  EcoSam2PromptMapTool *tool = nullptr;
  if ( mSmartSegmentationTool )
    tool = static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() );
  if ( !tool )
  {
    tool = new EcoSam2PromptMapTool(
      mApp->mapCanvas(),
      [this]( const QVector<QgsPointXY> &points, const QVector<int> &labels ) {
        previewSmartSegmentationPrompt( points, labels );
      },
      [this] {
        commitSmartSegmentationPreview();
      },
      [this] {
        leaveSmartSegmentationTool();
      }
    );
    mSmartSegmentationTool = tool;
  }
  tool->resetSession();
  tool->setPreviewColors(
    mSmartSegmentationStandalone ? QColor( 56, 189, 248, 235 ) : QColor( 220, 38, 38, 235 ),
    mSmartSegmentationStandalone ? QColor( 56, 189, 248, 88 ) : QColor( 220, 38, 38, 95 )
  );

  if ( mApp->mapCanvas()->mapTool() != tool )
    mSmartSegmentationPreviousMapTool = mApp->mapCanvas()->mapTool();
  if ( layer )
    mApp->setActiveLayer( layer );
  else
    mApp->setActiveLayer( rasterLayer );
  mApp->mapCanvas()->setMapTool( tool );
  if ( mSmartSegmentationAction )
  {
    const QSignalBlocker blocker( mSmartSegmentationAction );
    mSmartSegmentationAction->setChecked( true );
  }
  if ( mRecognitionSelectionLabel )
  {
    if ( mSmartSegmentationStandalone )
      mRecognitionSelectionLabel->setText( tr( "智能分割已启用：确认后将保存为独立分割成果图层，不写入扰动结果表。" ) );
    else if ( smartSegmentationLayerContext )
      mRecognitionSelectionLabel->setText( tr( "图层智能分割已启用：新增图斑确认后会自动保存到当前智能分割图层。" ) );
    else
      mRecognitionSelectionLabel->setText( tr( "智能分割已启用：结果将写入当前正在编辑的扰动图斑。" ) );
  }
  if ( mApp->statusBar() )
    mApp->statusBar()->showMessage( tr( "智能分割：首点前停留 2 秒预览；左键保留，右键排除，每加一点立即更新，Enter 或双击确认。" ), 8000 );
}

void QgsEcoRestorationController::startSmartSegmentationForLayer( const QString &layerId )
{
  if ( !mApp || layerId.isEmpty() )
    return;

  QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( layerId ) );
  const bool validSmartLayer = layer
                               && layer->geometryType() == Qgis::GeometryType::Polygon
                               && ( layer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" )
                                    || layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "smart-segmentation" ) );
  if ( !validSmartLayer )
  {
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "请选择有效的智能分割成果面图层。" ), 4500 );
    return;
  }

  if ( mSmartSegmentationTool && mApp->mapCanvas() && mApp->mapCanvas()->mapTool() == mSmartSegmentationTool )
    leaveSmartSegmentationTool();

  // Prefer the image recorded on the result layer. This keeps layer-level
  // segmentation tied to the same phase/image used when the layer was made.
  const QString sourceImageId = layer->customProperty( QStringLiteral( "eco/sourceImageLayerId" ) ).toString().trimmed();
  if ( mRecognitionRasterCombo && !sourceImageId.isEmpty() )
  {
    const int index = mRecognitionRasterCombo->findData( sourceImageId );
    if ( index >= 0 )
    {
      const QSignalBlocker blocker( mRecognitionRasterCombo );
      mRecognitionRasterCombo->setCurrentIndex( index );
    }
  }

  if ( QgsLayerTreeView *view = mApp->layerTreeView() )
    view->setCurrentLayer( layer );
  mApp->setActiveLayer( layer );
  mSmartSegmentationRequestedLayerId = layerId;

  // Start after the context menu has closed. Starting a map tool directly
  // from the layer-tree menu can re-enter the view while QGIS is still
  // dispatching the menu action.
  QTimer::singleShot( 0, this, [this, layerId] {
    if ( QgsProject::instance()->mapLayer( layerId ) )
      startSmartSegmentation();
  } );
}

void QgsEcoRestorationController::previewSmartSegmentationPrompt( const QVector<QgsPointXY> &canvasPoints, const QVector<int> &promptLabels )
{
  if ( !mApp )
    return;

  QgsVectorLayer *resultLayer = mSmartSegmentationTargetLayerId.isEmpty()
                                  ? nullptr
                                  : qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mSmartSegmentationTargetLayerId ) );
  QgsRasterLayer *rasterLayer = selectedRecognitionRasterLayer();
  if ( !rasterLayer || ( !mSmartSegmentationStandalone && !resultLayer ) )
  {
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割需要先加载并选择一幅影像。" ), 4500 );
    return;
  }
  if ( canvasPoints.isEmpty() )
  {
    mSmartSegmentationPendingGeometry = QgsGeometry();
    mSmartSegmentationPendingScore = 0.0;
    if ( EcoSam2PromptMapTool *tool = mSmartSegmentationTool ? static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) : nullptr )
      tool->clearPreviewGeometry();
    return;
  }

  QgsCoordinateReferenceSystem outputCrs = mSmartSegmentationOutputCrs;
  if ( !outputCrs.isValid() )
    outputCrs = resultLayer ? resultLayer->crs() : QgsProject::instance()->crs();
  if ( !outputCrs.isValid() )
    outputCrs = rasterLayer->crs();
  mSmartSegmentationOutputCrs = outputCrs;

  QVector<QgsPointXY> rasterPoints;
  rasterPoints.reserve( canvasPoints.size() );
  try
  {
    QgsCoordinateTransform transform(
      mApp->mapCanvas()->mapSettings().destinationCrs(),
      rasterLayer->crs(),
      QgsProject::instance()->transformContext()
    );
    for ( const QgsPointXY &point : canvasPoints )
      rasterPoints.append( transform.transform( point ) );
  }
  catch ( const QgsCsException & )
  {
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割无法转换当前地图坐标。" ), 4500 );
    return;
  }

  const double fullRange = std::max( 20.0, static_cast<double>( mRecognitionRangeSpin ? mRecognitionRangeSpin->value() : 100 ) );
  const double halfRange = fullRange / 2.0;
  QgsDistanceArea distanceArea;
  distanceArea.setSourceCrs( rasterLayer->crs(), QgsProject::instance()->transformContext() );
  distanceArea.setEllipsoid( QgsProject::instance()->ellipsoid() );
  constexpr double halfPi = 1.57079632679489661923;
  constexpr double pi = 3.14159265358979323846;
  QgsRectangle cropExtent;
  bool hasCropExtent = false;
  for ( const QgsPointXY &point : std::as_const( rasterPoints ) )
  {
    QgsPointXY east;
    QgsPointXY west;
    QgsPointXY north;
    QgsPointXY south;
    distanceArea.measureLineProjected( point, halfRange, halfPi, &east );
    distanceArea.measureLineProjected( point, halfRange, -halfPi, &west );
    distanceArea.measureLineProjected( point, halfRange, 0.0, &north );
    distanceArea.measureLineProjected( point, halfRange, pi, &south );
    const QgsRectangle pointExtent( west.x(), south.y(), east.x(), north.y() );
    if ( pointExtent.isEmpty() )
      continue;
    if ( !hasCropExtent )
    {
      cropExtent = pointExtent;
      hasCropExtent = true;
    }
    else
    {
      cropExtent.combineExtentWith( pointExtent );
    }
  }

  if ( resultLayer && mSmartSegmentationFeatureId >= 0 )
  {
    QgsFeature feature = resultLayer->getFeature( static_cast<QgsFeatureId>( mSmartSegmentationFeatureId ) );
    QgsGeometry geometry = feature.geometry();
    if ( feature.isValid() && !geometry.isNull() && !geometry.isEmpty() )
    {
      try
      {
        if ( resultLayer->crs() != rasterLayer->crs() )
        {
          QgsCoordinateTransform transform( resultLayer->crs(), rasterLayer->crs(), QgsProject::instance()->transformContext() );
          geometry.transform( transform );
        }
        const QgsRectangle featureExtent = geometry.boundingBox();
        if ( !featureExtent.isEmpty() )
        {
          if ( hasCropExtent )
            cropExtent.combineExtentWith( featureExtent );
          else
          {
            cropExtent = featureExtent;
            hasCropExtent = true;
          }
        }
      }
      catch ( const QgsCsException & )
      {
      }
    }
  }

  if ( !hasCropExtent )
    return;
  if ( cropExtent.width() > 0 && cropExtent.height() > 0 )
  {
    const double paddingX = std::max( cropExtent.width() * 0.25, 1e-12 );
    const double paddingY = std::max( cropExtent.height() * 0.25, 1e-12 );
    cropExtent.grow( std::max( paddingX, paddingY ) );
  }
  cropExtent = cropExtent.intersect( rasterLayer->extent() );
  if ( cropExtent.isEmpty() )
  {
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割范围不在影像覆盖范围内。" ), 4500 );
    return;
  }

  QgsEcoSam2OnnxInference::Parameters parameters;
  const QDir modelDirectory( integratedSam2ModelDirectory() );
  parameters.encoderModelPath = modelDirectory.filePath( QStringLiteral( "vision_encoder.onnx" ) );
  parameters.decoderModelPath = modelDirectory.filePath( QStringLiteral( "prompt_encoder_mask_decoder.onnx" ) );
  parameters.cropExtent = cropExtent;
  parameters.promptPoint = rasterPoints.constFirst();
  parameters.promptPoints = rasterPoints;
  parameters.promptLabels = promptLabels;
  parameters.promptMode = QgsEcoSam2OnnxInference::PromptMode::Point;

  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( tr( "正在生成智能分割预览…" ) );
  QApplication::setOverrideCursor( Qt::WaitCursor );
  const QgsEcoSam2OnnxInference::Result inference = QgsEcoSam2OnnxInference::run( rasterLayer, parameters );
  QApplication::restoreOverrideCursor();
  if ( !inference.success )
  {
    mSmartSegmentationPendingGeometry = QgsGeometry();
    mSmartSegmentationPendingScore = 0.0;
    if ( EcoSam2PromptMapTool *tool = mSmartSegmentationTool ? static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) : nullptr )
      tool->clearPreviewGeometry();
    if ( mRecognitionSelectionLabel )
      mRecognitionSelectionLabel->setText( tr( "未生成预览，请增加保留点或排除点后重试。" ) );
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割未生成有效预览，请换点或增加排除点。" ), 5000 );
    return;
  }

  QgsGeometry resultGeometry = inference.geometry;
  QgsGeometry canvasGeometry = inference.geometry;
  try
  {
    if ( rasterLayer->crs() != outputCrs )
    {
      QgsCoordinateTransform transform( rasterLayer->crs(), outputCrs, QgsProject::instance()->transformContext() );
      resultGeometry.transform( transform );
    }
    const QgsCoordinateReferenceSystem canvasCrs = mApp->mapCanvas()->mapSettings().destinationCrs();
    if ( rasterLayer->crs() != canvasCrs )
    {
      QgsCoordinateTransform transform( rasterLayer->crs(), canvasCrs, QgsProject::instance()->transformContext() );
      canvasGeometry.transform( transform );
    }
  }
  catch ( const QgsCsException & )
  {
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割结果坐标转换失败。" ), 4500 );
    return;
  }
  if ( resultGeometry.isNull() || resultGeometry.isEmpty() )
    return;

  mSmartSegmentationPendingGeometry = resultGeometry;
  mSmartSegmentationPendingScore = inference.score;
  if ( EcoSam2PromptMapTool *tool = mSmartSegmentationTool ? static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) : nullptr )
    tool->setPreviewGeometry( canvasGeometry );
  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( mSmartSegmentationStandalone
                                           ? tr( "分割预览已更新：Enter 或双击后可命名并保存为独立成果图层。" )
                                           : tr( "结果已更新：左键继续加保留点，右键加排除点；Enter、双击或绿色对钩确认写入图斑。" ) );
  if ( mApp->statusBar() )
    mApp->statusBar()->showMessage( tr( "智能分割预览已更新。" ), 2500 );
}

void QgsEcoRestorationController::commitSmartSegmentationPreview()
{
  if ( mSmartSegmentationCommitInProgress )
    return;

  if ( !mApp || mSmartSegmentationPendingGeometry.isNull() || mSmartSegmentationPendingGeometry.isEmpty() )
  {
    if ( mApp && mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "请先等待智能分割生成预览，再确认写入。" ), 3500 );
    return;
  }

  mSmartSegmentationCommitInProgress = true;
  EcoScopeGuard commitGuard( [this] {
    mSmartSegmentationCommitInProgress = false;
  } );

  if ( mSmartSegmentationStandalone )
  {
    ecoImportTrace( QStringLiteral( "smart segmentation standalone commit entered" ) );
    const QString layerName = askSmartSegmentationLayerName();
    if ( layerName.isEmpty() )
      return;

    ecoImportTrace( QStringLiteral( "smart segmentation creating layer name=%1" ).arg( layerName ) );
    const bool previousLayerImportInProgress = mLayerImportInProgress;
    mLayerImportInProgress = true;
    EcoScopeGuard standaloneSaveGuard( [this, previousLayerImportInProgress] {
      mLayerImportInProgress = previousLayerImportInProgress;
    } );
    QgsVectorLayer *segmentationLayer = createSmartSegmentationLayer( layerName, mSmartSegmentationOutputCrs, false );
    if ( !segmentationLayer )
    {
      if ( mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "智能分割成果保存失败，请确认工程已保存且成果目录可写。" ), 4500 );
      return;
    }
    QgsFeature feature( segmentationLayer->fields() );
    feature.setGeometry( mSmartSegmentationPendingGeometry );
    const int objectIdIndex = segmentationLayer->fields().indexFromName( QStringLiteral( "object_id" ) );
    const int nameIndex = segmentationLayer->fields().indexFromName( QStringLiteral( "seg_name" ) );
    const int scoreIndex = segmentationLayer->fields().indexFromName( QStringLiteral( "score" ) );
    const int sourceIndex = segmentationLayer->fields().indexFromName( QStringLiteral( "source" ) );
    const int createdIndex = segmentationLayer->fields().indexFromName( QStringLiteral( "created" ) );
    if ( objectIdIndex >= 0 )
      feature.setAttribute( objectIdIndex, segmentationLayer->maximumValue( objectIdIndex ).toLongLong() + 1 );
    if ( nameIndex >= 0 )
      feature.setAttribute( nameIndex, layerName );
    if ( scoreIndex >= 0 )
      feature.setAttribute( scoreIndex, mSmartSegmentationPendingScore );
    if ( sourceIndex >= 0 )
      feature.setAttribute( sourceIndex, tr( "智能分割" ) );
    if ( createdIndex >= 0 )
      feature.setAttribute( createdIndex, QDate::currentDate() );

    QgsFeatureList featureList;
    featureList << feature;
    QgsVectorDataProvider *provider = segmentationLayer->dataProvider();
    const bool added = provider && provider->addFeatures( featureList );
    ecoImportTrace( QStringLiteral( "smart segmentation provider add feature added=%1 layerId=%2" )
                      .arg( added )
                      .arg( segmentationLayer->id() ) );
    if ( !added )
    {
      QgsProject::instance()->removeMapLayer( segmentationLayer->id() );
      if ( mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "智能分割成果写入失败，请重新尝试。" ), 4500 );
      return;
    }
    segmentationLayer->updateExtents();
    ecoImportTrace( QStringLiteral( "smart segmentation standalone provider write complete layerId=%1" ).arg( segmentationLayer->id() ) );

    applyBusinessStyle( segmentationLayer );
    segmentationLayer->removeSelection();
    segmentationLayer->triggerRepaint();
    QgsProject::instance()->setDirty( true );
    mApp->setActiveLayer( segmentationLayer );
    leaveSmartSegmentationTool();
    QPointer<QgsVectorLayer> savedLayer( segmentationLayer );
    QTimer::singleShot( 0, this, [this, savedLayer] {
      if ( !savedLayer || !QgsProject::instance()->mapLayer( savedLayer->id() ) )
        return;
      refreshLayerChoices();
      const bool previousImportState = mLayerImportInProgress;
      mLayerImportInProgress = true;
      EcoScopeGuard canvasSyncGuard( [this, previousImportState] {
        mLayerImportInProgress = previousImportState;
      } );
      syncBusinessProjectView( QList<QgsMapLayer *>() << savedLayer.data() );
      savedLayer->updateExtents();
      savedLayer->triggerRepaint();
      if ( mApp && mApp->mapCanvas() )
        mApp->mapCanvas()->refresh();
      ecoImportTrace( QStringLiteral( "smart segmentation standalone save UI refresh complete layerId=%1" ).arg( savedLayer->id() ) );
    } );
    if ( mApp->mapCanvas() )
    {
      mApp->actionPan()->trigger();
      mApp->mapCanvas()->refresh();
    }
    if ( mRecognitionSelectionLabel )
      mRecognitionSelectionLabel->setText( tr( "智能分割成果已保存为独立图层，不计入扰动识别结果表。" ) );
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "已保存智能分割成果：%1" ).arg( layerName ), 5000 );
    return;
  }

  QgsVectorLayer *smartSegmentationLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mSmartSegmentationTargetLayerId ) );
  const bool targetIsSmartSegmentationLayer = smartSegmentationLayer
                                               && ( smartSegmentationLayer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" )
                                                    || smartSegmentationLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "smart-segmentation" ) );
  if ( targetIsSmartSegmentationLayer )
  {
    if ( !ensureEditableResultLayer( smartSegmentationLayer ) )
      return;

    smartSegmentationLayer->beginEditCommand( tr( "新增智能分割图斑" ) );
    QgsFeature feature( smartSegmentationLayer->fields() );
    feature.setGeometry( mSmartSegmentationPendingGeometry );
    const int objectIdIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "object_id" ) );
    const int nameIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "seg_name" ) );
    const int scoreIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "score" ) );
    const int sourceIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "source" ) );
    const int createdIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "created" ) );
    const int remarkIndex = smartSegmentationLayer->fields().indexFromName( QStringLiteral( "remark" ) );
    if ( objectIdIndex >= 0 )
      feature.setAttribute( objectIdIndex, smartSegmentationLayer->maximumValue( objectIdIndex ).toLongLong() + 1 );
    if ( nameIndex >= 0 )
      feature.setAttribute( nameIndex, smartSegmentationLayer->name() );
    if ( scoreIndex >= 0 )
      feature.setAttribute( scoreIndex, mSmartSegmentationPendingScore );
    if ( sourceIndex >= 0 )
      feature.setAttribute( sourceIndex, tr( "智能分割" ) );
    if ( createdIndex >= 0 )
      feature.setAttribute( createdIndex, QDate::currentDate() );
    if ( remarkIndex >= 0 )
      feature.setAttribute( remarkIndex, tr( "图层智能分割" ) );

    const bool added = smartSegmentationLayer->addFeature( feature );
    if ( !added )
    {
      smartSegmentationLayer->destroyEditCommand();
      if ( mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "智能分割图斑写入失败，请重新尝试。" ), 4500 );
      return;
    }
    smartSegmentationLayer->endEditCommand();
    if ( !smartSegmentationLayer->commitChanges() )
    {
      smartSegmentationLayer->rollBack();
      if ( mApp->statusBar() )
        mApp->statusBar()->showMessage( tr( "智能分割图层保存失败，请检查文件写入权限。" ), 4500 );
      return;
    }

    applyBusinessStyle( smartSegmentationLayer );
    smartSegmentationLayer->triggerRepaint();
    QgsProject::instance()->setDirty( true );
    mApp->setActiveLayer( smartSegmentationLayer );
    leaveSmartSegmentationTool();
    if ( mApp->mapCanvas() )
      mApp->mapCanvas()->refresh();
    if ( mRecognitionSelectionLabel )
      mRecognitionSelectionLabel->setText( tr( "新图斑已自动保存到当前智能分割图层。" ) );
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割图斑已保存到图层?1" ).arg( smartSegmentationLayer->name() ), 5000 );
    return;
  }

  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mSmartSegmentationTargetLayerId ) );
  if ( !ensureEditableResultLayer( resultLayer ) )
    return;

  resultLayer->beginEditCommand( tr( "智能分割图斑" ) );
  const int sourceIndex = resultLayer->fields().indexFromName( QStringLiteral( "source" ) );
  const int confidenceIndex = resultLayer->fields().indexFromName( QStringLiteral( "confidence" ) );
  const int objectIdIndex = resultLayer->fields().indexFromName( QStringLiteral( "object_id" ) );
  const int typeIndex = resultLayer->fields().indexFromName( QStringLiteral( "dist_type" ) );
  const int verifyIndex = resultLayer->fields().indexFromName( QStringLiteral( "verify" ) );
  const int dateIndex = resultLayer->fields().indexFromName( QStringLiteral( "image_date" ) );
  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  const int visibilityIndex = ensureRecognitionVisibilityField( resultLayer );
  QgsFeatureId affectedFeatureId = static_cast<QgsFeatureId>( mSmartSegmentationFeatureId );
  bool changed = false;

  const QgsFeature originalFeature = mSmartSegmentationFeatureId >= 0
                                      ? resultLayer->getFeature( affectedFeatureId )
                                      : QgsFeature();
  const bool replaceExisting = originalFeature.isValid();
  if ( replaceExisting )
  {
    changed = resultLayer->changeGeometry( affectedFeatureId, mSmartSegmentationPendingGeometry );
    if ( changed && confidenceIndex >= 0 )
      changed = resultLayer->changeAttributeValue( affectedFeatureId, confidenceIndex, mSmartSegmentationPendingScore ) && changed;
    if ( changed && sourceIndex >= 0 && recognitionResultType( originalFeature.attribute( sourceIndex ).toString() ) != QStringLiteral( "新增" ) )
      changed = resultLayer->changeAttributeValue( affectedFeatureId, sourceIndex, tr( "修正" ) ) && changed;
  }
  else
  {
    QgsFeature feature( resultLayer->fields() );
    feature.setGeometry( mSmartSegmentationPendingGeometry );
    if ( objectIdIndex >= 0 )
      feature.setAttribute( objectIdIndex, resultLayer->maximumValue( objectIdIndex ).toLongLong() + 1 );
    if ( typeIndex >= 0 )
      feature.setAttribute( typeIndex, tr( "塔基施工扰动" ) );
    if ( confidenceIndex >= 0 )
      feature.setAttribute( confidenceIndex, mSmartSegmentationPendingScore );
    if ( verifyIndex >= 0 )
      feature.setAttribute( verifyIndex, tr( "待核查" ) );
    if ( sourceIndex >= 0 )
      feature.setAttribute( sourceIndex, tr( "新增" ) );
    if ( dateIndex >= 0 )
      feature.setAttribute( dateIndex, QDate::currentDate() );
    if ( remarkIndex >= 0 )
      feature.setAttribute( remarkIndex, mRecognitionEditingTowerLabel.isEmpty() ? tr( "智能分割" ) : mRecognitionEditingTowerLabel );
    if ( visibilityIndex >= 0 )
      feature.setAttribute( visibilityIndex, 1 );
    changed = resultLayer->addFeature( feature );
    affectedFeatureId = feature.id();
  }

  if ( !changed )
  {
    resultLayer->destroyEditCommand();
    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "智能分割图斑写入失败，请重新尝试。" ), 4500 );
    return;
  }
  resultLayer->endEditCommand();

  if ( mApp->mapCanvas() && !mRecognitionSelectionColorChanged )
  {
    mRecognitionOriginalSelectionColor = mApp->mapCanvas()->selectionColor();
    mRecognitionSelectionColorChanged = true;
  }
  if ( mApp->mapCanvas() )
  {
    QColor editSelectionColor( QStringLiteral( "#dc2626" ) );
    editSelectionColor.setAlpha( 45 );
    mApp->mapCanvas()->setSelectionColor( editSelectionColor );
  }
  resultLayer->selectByIds( QgsFeatureIds() << affectedFeatureId );
  applyBusinessStyle( resultLayer );
  moveLayerToBusinessGroup( resultLayer, sRecognitionGroup );
  resultLayer->triggerRepaint();
  mApp->setActiveLayer( resultLayer );
  QgsProject::instance()->setDirty( true );
  leaveSmartSegmentationTool();
  mApp->actionVertexToolActiveLayer()->trigger();
  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( tr( "智能分割图斑已生成，可直接拖动顶点微调，再点击绿色对钩完成编辑。" ) );
  if ( mApp->statusBar() )
    mApp->statusBar()->showMessage( tr( "智能分割图斑已生成，可直接微调边界。" ), 5000 );
  scheduleRecognitionResultTableRefresh();
}

void QgsEcoRestorationController::leaveSmartSegmentationTool()
{
  if ( !mApp || !mApp->mapCanvas() )
    return;

  if ( EcoSam2PromptMapTool *tool = mSmartSegmentationTool ? static_cast<EcoSam2PromptMapTool *>( mSmartSegmentationTool.data() ) : nullptr )
    tool->resetSession();
  mSmartSegmentationPendingGeometry = QgsGeometry();
  mSmartSegmentationPendingScore = 0.0;
  if ( mSmartSegmentationAction )
  {
    const QSignalBlocker blocker( mSmartSegmentationAction );
    mSmartSegmentationAction->setChecked( false );
  }
  if ( mSmartSegmentationTool && mApp->mapCanvas()->mapTool() == mSmartSegmentationTool )
  {
    if ( mSmartSegmentationPreviousMapTool && mSmartSegmentationPreviousMapTool != mSmartSegmentationTool )
      mApp->mapCanvas()->setMapTool( mSmartSegmentationPreviousMapTool );
    else
      mApp->actionPan()->trigger();
  }
  mSmartSegmentationPreviousMapTool = nullptr;
  mSmartSegmentationRequestedLayerId.clear();
  mSmartSegmentationTargetLayerId.clear();
  mSmartSegmentationFeatureId = -1;
  mSmartSegmentationStandalone = true;
  mSmartSegmentationOutputCrs = QgsCoordinateReferenceSystem();
}

void QgsEcoRestorationController::cancelExtentRecognition()
{
  if ( mRecognitionExtentTool )
    mRecognitionExtentTool->clearRubberBand();
  if ( mApp->mapCanvas()->mapTool() == mRecognitionExtentTool )
  {
    if ( mRecognitionPreviousMapTool )
      mApp->mapCanvas()->setMapTool( mRecognitionPreviousMapTool );
    else
      mApp->actionPan()->trigger();
  }
  mRecognitionPreviousMapTool = nullptr;
  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( tr( "已退出框选模式，可以继续平移、缩放地图。" ) );
}

void QgsEcoRestorationController::runRecognition( const QVector<QgsRectangle> &targetExtents, const QStringList &targetLabels, const QString &towerLayerId )
{
  ecoImportTrace( QStringLiteral( "runRecognition entered targets=%1 labels=%2 towerLayerId=%3" )
                    .arg( targetExtents.size() )
                    .arg( targetLabels.size() )
                    .arg( towerLayerId ) );
  if ( mRecognitionRunning )
  {
    showMessage( tr( "塔基扰动识别" ), tr( "识别任务正在运行，请稍候。" ) );
    return;
  }
  mRecognitionRunning = true;
  const bool previousLayerImportInProgress = mLayerImportInProgress;
  const bool resultDockWasVisible = mRecognitionResultDock && mRecognitionResultDock->isVisible();
  QString compactedTowerLayerId;
  QPointer<QgsRubberBand> currentRecognitionBand;
  double previousTowerIconWidth = sDefaultTowerIconWidthPixels;
  bool previousTowerIconCustomSize = false;
  QString previousTowerDisplayMode;
  std::unique_ptr<QgsFeatureRenderer> previousTowerRenderer;
  bool towerIconWasCompacted = false;
  if ( resultDockWasVisible )
    mRecognitionResultDock->hide();
  mLayerImportInProgress = true;
  EcoScopeGuard resetRecognitionState( [this, previousLayerImportInProgress, resultDockWasVisible,
                                        &currentRecognitionBand,
                                        &compactedTowerLayerId, &previousTowerIconWidth,
                                        &previousTowerIconCustomSize, &previousTowerDisplayMode,
                                        &previousTowerRenderer,
                                        &towerIconWasCompacted] {
    mRecognitionRunning = false;
    mLayerImportInProgress = previousLayerImportInProgress;
    if ( currentRecognitionBand )
    {
      currentRecognitionBand->reset( Qgis::GeometryType::Polygon );
      currentRecognitionBand->hide();
      currentRecognitionBand->deleteLater();
    }
    if ( towerIconWasCompacted )
    {
      if ( QgsVectorLayer *towerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( compactedTowerLayerId ) ) )
      {
        towerLayer->setCustomProperty( sTowerIconWidthProperty, previousTowerIconWidth );
        towerLayer->setCustomProperty( sTowerIconCustomSizeProperty, previousTowerIconCustomSize );
        towerLayer->setCustomProperty( sTowerDisplayModeProperty, previousTowerDisplayMode );
        if ( previousTowerRenderer )
          towerLayer->setRenderer( previousTowerRenderer->clone() );
        else
          applyBusinessStyle( towerLayer );
        towerLayer->triggerRepaint();
      }
    }
    if ( mRecognitionRunButton )
      mRecognitionRunButton->setEnabled( QgsEcoOnnxInference::isRuntimeAvailable() );
    if ( resultDockWasVisible && mRecognitionResultDock && !mRecognitionResultDock->isVisible() )
      mRecognitionResultDock->show();
  } );

  if ( !QgsEcoOnnxInference::isRuntimeAvailable() )
  {
    ecoImportTrace( QStringLiteral( "runRecognition runtime unavailable" ) );
    showMessage( tr( "塔基扰动识别" ), tr( "智能识别组件不可用，请联系系统管理员检查安装。" ), true );
    return;
  }
  QgsRasterLayer *rasterLayer = mRecognitionRasterCombo
                                  ? qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) )
                                  : nullptr;
  if ( !rasterLayer )
  {
    ecoImportTrace( QStringLiteral( "runRecognition no raster" ) );
    showMessage( tr( "塔基扰动识别" ), tr( "请先添加并选择一?TIF 影像。" ), true );
    return;
  }
  QString recognitionPhaseId = layerPhaseIdForLayer( rasterLayer );
  if ( recognitionPhaseId.isEmpty() )
    recognitionPhaseId = currentPhaseId();
  ecoImportTrace( QStringLiteral( "runRecognition raster=%1 size=%2x%3" ).arg( rasterLayer->id() ).arg( rasterLayer->width() ).arg( rasterLayer->height() ) );
  // Stop any in-flight canvas render before reading blocks from the same
  // raster provider. This avoids provider re-entry during inference.
  if ( mApp && mApp->mapCanvas() && mApp->mapCanvas()->isDrawing() )
    mApp->mapCanvas()->stopRendering();
  if ( !rasterLayer->dataProvider() || rasterLayer->width() <= 0 || rasterLayer->height() <= 0 || !rasterLayer->extent().isFinite() || rasterLayer->extent().isEmpty() )
  {
    showMessage( tr( "塔基扰动识别" ), tr( "影像数据无效，请重新加载 TIF銆俓" ), true );
    return;
  }
  if ( targetExtents.isEmpty() )
  {
    showMessage( tr( "塔基扰动识别" ), tr( "未找到可用于识别的杆塔范围。" ), true );
    return;
  }
  const QString modelPath = integratedTowerDisturbanceModelPath();
  if ( !QFileInfo::exists( modelPath ) )
  {
    ecoImportTrace( QStringLiteral( "runRecognition model missing path=%1" ).arg( modelPath ) );
    showMessage( tr( "塔基扰动识别" ), tr( "内置的塔基扰动识别组件未找到，请联系系统管理员检查安装。" ), true );
    return;
  }
  ecoImportTrace( QStringLiteral( "runRecognition before ensureDisturbanceResultLayer phaseId=%1" ).arg( recognitionPhaseId ) );
  QgsVectorLayer *resultLayer = ensureDisturbanceResultLayer( recognitionPhaseId );
  if ( !resultLayer )
  {
    ecoImportTrace( QStringLiteral( "runRecognition result layer unavailable" ) );
    return;
  }
  ecoImportTrace( QStringLiteral( "runRecognition resultLayer=%1" ).arg( resultLayer->id() ) );
  resultLayer->setCustomProperty( QStringLiteral( "eco/category" ), QStringLiteral( "recognition-result" ) );
  resultLayer->setCustomProperty( QStringLiteral( "eco/resultType" ), QStringLiteral( "construction-disturbance" ) );
  if ( !recognitionPhaseId.isEmpty() )
    resultLayer->setCustomProperty( sPhaseIdProperty, recognitionPhaseId );
  ecoImportTrace( QStringLiteral( "runRecognition before prepare result layer" ) );
  ensureRecognitionVisibilityField( resultLayer );
  applyBusinessStyle( resultLayer );
  attachRecognitionResultLayer( resultLayer );
  ecoImportTrace( QStringLiteral( "runRecognition after attach result layer" ) );
  mRecognitionResultLayerId = resultLayer->id();

  if ( mApp && mApp->mapCanvas() )
  {
    QgsRectangle overviewExtent;
    for ( const QgsRectangle &extent : targetExtents )
    {
      if ( extent.isEmpty() || !extent.isFinite() )
        continue;
      if ( overviewExtent.isEmpty() )
        overviewExtent = extent;
      else
        overviewExtent.combineExtentWith( extent );
    }
    if ( !overviewExtent.isEmpty() && overviewExtent.isFinite() )
    {
      QgsRectangle canvasExtent = overviewExtent;
      try
      {
        const QgsCoordinateReferenceSystem canvasCrs = mApp->mapCanvas()->mapSettings().destinationCrs();
        if ( rasterLayer->crs().isValid() && canvasCrs.isValid() && rasterLayer->crs() != canvasCrs )
        {
          QgsCoordinateTransform transform( rasterLayer->crs(), canvasCrs, QgsProject::instance()->transformContext() );
          canvasExtent = transform.transformBoundingBox( overviewExtent );
        }
      }
      catch ( const QgsCsException & )
      {
        canvasExtent = QgsRectangle();
      }
      if ( !canvasExtent.isEmpty() && canvasExtent.isFinite() )
      {
        const double padding = std::max( canvasExtent.width(), canvasExtent.height() ) * 0.12;
        if ( padding > 0.0 )
          canvasExtent.grow( padding );
        mApp->mapCanvas()->setExtent( canvasExtent );
        mApp->mapCanvas()->refresh();
      }
    }
  }

  // Do not force a canvas rebuild immediately before reading raster blocks.
  // QGIS renders the canvas in parallel, and starting a raster render here can
  // race the provider reads used by the ONNX input preparation. The result
  // layer is already in the business tree; the full canvas/tree sync is done
  // after inference completes.
  if ( QgsLayerTreeView *view = mApp ? mApp->layerTreeView() : nullptr )
    view->viewport()->update();
  if ( !towerLayerId.isEmpty() )
  {
    resultLayer->setCustomProperty( QStringLiteral( "eco/towerLayerId" ), towerLayerId );
    QgsProject::instance()->setDirty( true );
    if ( QgsVectorLayer *towerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( towerLayerId ) ) )
    {
      if ( towerLayer->geometryType() == Qgis::GeometryType::Point )
      {
        compactedTowerLayerId = towerLayerId;
        if ( towerLayer->renderer() )
          previousTowerRenderer.reset( towerLayer->renderer()->clone() );
        previousTowerIconWidth = towerLayer->customProperty( sTowerIconWidthProperty, sDefaultTowerIconWidthPixels ).toDouble();
        previousTowerIconCustomSize = towerLayer->customProperty( sTowerIconCustomSizeProperty, false ).toBool();
        previousTowerDisplayMode = towerLayer->customProperty( sTowerDisplayModeProperty ).toString();
        towerLayer->setCustomProperty( sTowerDisplayModeProperty, QStringLiteral( "vector" ) );
        std::unique_ptr<QgsMarkerSymbol> compactSymbol = createTowerMarkerSymbol(
          Qgis::MarkerShape::Circle,
          QColor( QStringLiteral( "#38bdf8" ) ),
          QColor( QStringLiteral( "#0f172a" ) ),
          62.0,
          0.85,
          0.10
        );
        towerLayer->setRenderer( new QgsSingleSymbolRenderer( compactSymbol.release() ) );
        towerLayer->triggerRepaint();
        towerIconWasCompacted = true;
      }
    }
  }
  if ( resultLayer->isEditable() )
  {
    showMessage( tr( "塔基扰动识别" ), tr( "请先保存当前人工编辑，再执行识别。" ), true );
    return;
  }
  if ( mRecognitionRunButton )
    mRecognitionRunButton->setEnabled( false );
  ecoImportTrace( QStringLiteral( "runRecognition before parameters" ) );

  QgsEcoOnnxInference::Parameters parameters;
  parameters.modelPath = modelPath;
  parameters.confidence = mConfidenceSpin->value();
  parameters.overlapPercent = 20;
  parameters.maximumTiles = 5000;
  parameters.targetExtents = targetExtents;
  parameters.targetLabels = targetLabels;

  int lastProgressTotal = 0;
  mRecognitionScanTileIndex = 0;
  if ( mRecognitionScanPreview )
    static_cast<EcoRecognitionScanPreview *>( mRecognitionScanPreview.data() )->beginTask( std::max( 1, static_cast<int>( targetExtents.size() ) ) );
  setRecognitionProgress( 0, std::max( 1, static_cast<int>( targetExtents.size() ) ) );

  // Never read the same GDAL provider instance which may still be held by a
  // canvas render worker. stopRendering() is intentionally non-blocking in
  // QGIS, so using the selected layer directly here can race a cancelled
  // render and crash inside GDAL. A detached clone owns a separate provider
  // handle while preserving the source CRS, extent and dimensions.
  std::unique_ptr<QgsRasterLayer> inferenceRasterLayer( rasterLayer->clone() );
  if ( !inferenceRasterLayer || !inferenceRasterLayer->isValid() )
  {
    if ( mRecognitionRunButton )
      mRecognitionRunButton->setEnabled( true );
    showMessage( tr( "塔基扰动识别" ), tr( "无法建立影像读取副本，未启动识别，以避免影响当前地图渲染。" ), true );
    return;
  }
  ecoImportTrace( QStringLiteral( "runRecognition cloned raster layer valid" ) );
  QgsRasterLayer *inferenceSource = inferenceRasterLayer.get();
  if ( mApp && mApp->mapCanvas() )
  {
    currentRecognitionBand = new QgsRubberBand( mApp->mapCanvas(), Qgis::GeometryType::Polygon );
    currentRecognitionBand->setStrokeColor( QColor( 255, 37, 37, 255 ) );
    currentRecognitionBand->setFillColor( QColor( 255, 37, 37, 42 ) );
    currentRecognitionBand->setSecondaryStrokeColor( QColor( 255, 255, 255, 210 ) );
    currentRecognitionBand->setWidth( 3.2 );
    currentRecognitionBand->setLineStyle( Qt::SolidLine );
    currentRecognitionBand->setBrushStyle( Qt::NoBrush );
    currentRecognitionBand->setZValue( 930.0 );
    currentRecognitionBand->hide();
  }
  int lastShownTile = -1;
  const auto followRecognitionTarget = [this, rasterLayer, &parameters, &lastShownTile, &currentRecognitionBand]( int current, int total ) {
    if ( !mApp || !mApp->mapCanvas() || !rasterLayer || current <= 0 || current > total )
      return;
    if ( current == lastShownTile )
      return;
    lastShownTile = current;

    const QgsRectangle sourceExtent = parameters.targetExtents.value( current - 1 );
    if ( sourceExtent.isEmpty() || !sourceExtent.isFinite() )
      return;
    if ( currentRecognitionBand )
    {
      currentRecognitionBand->reset( Qgis::GeometryType::Polygon );
      currentRecognitionBand->setStrokeColor( QColor( 255, 37, 37, 255 ) );
      currentRecognitionBand->setFillColor( QColor( 255, 37, 37, 28 ) );
      currentRecognitionBand->setSecondaryStrokeColor( QColor( 255, 255, 255, 210 ) );
      currentRecognitionBand->setWidth( 3.2 );
      currentRecognitionBand->setLineStyle( Qt::SolidLine );
      currentRecognitionBand->setBrushStyle( Qt::NoBrush );
      currentRecognitionBand->addGeometry( QgsGeometry::fromRect( sourceExtent ), rasterLayer->crs(), false );
      currentRecognitionBand->updatePosition();
      currentRecognitionBand->show();
    }
    if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
      canvas->viewport()->update();
    ecoImportTrace( QStringLiteral( "runRecognition highlighted target %1/%2" ).arg( current ).arg( total ) );
  };
  QElapsedTimer recognitionUiClock;
  recognitionUiClock.start();
  qint64 lastUiRefreshMs = -1000;
  ecoImportTrace( QStringLiteral( "runRecognition before onnx run targets=%1" ).arg( targetExtents.size() ) );
  const QgsEcoOnnxInference::Result inference = QgsEcoOnnxInference::run(
    inferenceSource, parameters,
    [this, &lastProgressTotal, &followRecognitionTarget, &recognitionUiClock, &lastUiRefreshMs]( int current, int total, const QString &message, const QString &sourceName, const QImage &previewImage ) {
      lastProgressTotal = total;
      if ( current == 1 || current == total || current % 50 == 0 )
        ecoImportTrace( QStringLiteral( "runRecognition progress %1/%2 source=%3" ).arg( current ).arg( total ).arg( sourceName ) );
      setRecognitionProgress( current, total );
      if ( mRecognitionSelectionLabel )
        mRecognitionSelectionLabel->setText( message );

      // Rendering the preview and moving the map are the most expensive GUI
      // operations in a long recognition run. Throttle them independently
      // from the progress counter so the operator still sees live progress
      // without forcing a full canvas repaint for every tile.
      const qint64 elapsedMs = recognitionUiClock.elapsed();
      if ( current == 1 || current == total || elapsedMs - lastUiRefreshMs >= 120 )
      {
        lastUiRefreshMs = elapsedMs;
        if ( mRecognitionScanPreview )
          static_cast<EcoRecognitionScanPreview *>( mRecognitionScanPreview.data() )->setCurrentImage( previewImage, sourceName, current, total );
        followRecognitionTarget( current, total );
      }

      // Cooperative asynchronous execution: QGIS/GDAL objects remain on the
      // GUI thread, but user input and dock/canvas events are processed at
      // every tile boundary before the next model step begins.
      QCoreApplication::processEvents( QEventLoop::AllEvents, 12 );
      return true;
    }
  );
  ecoImportTrace( QStringLiteral( "runRecognition after onnx run success=%1 detections=%2 processed=%3" )
                    .arg( inference.success )
                    .arg( inference.detections.size() )
                    .arg( inference.processedTiles ) );
  // Render only after all provider reads and the ONNX session have finished.
  // This preserves the last-tower follow position without allowing a render
  // worker to re-enter the raster provider during inference.
  if ( mApp && mApp->mapCanvas() )
    QTimer::singleShot( 0, mApp->mapCanvas(), [canvas = mApp->mapCanvas()] {
      if ( canvas )
        canvas->refresh();
    } );
  setRecognitionProgress( std::max( 1, lastProgressTotal ), std::max( 1, lastProgressTotal ) );
  if ( mRecognitionScanPreview )
    static_cast<EcoRecognitionScanPreview *>( mRecognitionScanPreview.data() )->finishTask( inference.success );
  if ( !inference.success )
  {
    Q_UNUSED( inference )
    syncBusinessProjectView( QList<QgsMapLayer *>() << resultLayer );
    showMessage( tr( "塔基扰动识别" ), tr( "塔基扰动识别未完成，请检查影像与杆塔范围。" ), true );
    return;
  }
  if ( inference.detections.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "runRecognition no detections branch" ) );
    syncBusinessProjectView( QList<QgsMapLayer *>() << resultLayer );
    showRecognitionResultTable( resultLayer->id() );
    if ( mRecognitionResultFilter && mRecognitionResultFilter->currentIndex() != 0 )
      mRecognitionResultFilter->setCurrentIndex( 0 );
    refreshRecognitionResultTable();
    showMessage( tr( "塔基扰动识别完成" ), tr( "识别完成：已处理 %1 个杆塔范围，未发现扰动图斑。" ).arg( inference.processedTiles ) );
    if ( mRecognitionSelectionLabel )
      mRecognitionSelectionLabel->setText( tr( "识别完成：已处理 %1 个杆塔，未发现扰动图斑。已打开下方结果表，可对无结果杆塔新增图斑。" ).arg( inference.processedTiles ) );
    if ( mRecognitionResultLayerId == resultLayer->id() )
      refreshRecognitionResultTable();
    return;
  }

  if ( !resultLayer->startEditing() )
  {
    ecoImportTrace( QStringLiteral( "runRecognition result layer startEditing failed" ) );
    showMessage( tr( "写入识别成果" ), tr( "成果层无法进入编辑状态，请检查文件权限。" ), true );
    return;
  }
  ecoImportTrace( QStringLiteral( "runRecognition before write detections count=%1" ).arg( inference.detections.size() ) );
  const int objectIdIndex = resultLayer->fields().indexFromName( QStringLiteral( "object_id" ) );
  const int typeIndex = resultLayer->fields().indexFromName( QStringLiteral( "dist_type" ) );
  const int confidenceIndex = resultLayer->fields().indexFromName( QStringLiteral( "confidence" ) );
  const int verifyIndex = resultLayer->fields().indexFromName( QStringLiteral( "verify" ) );
  const int sourceIndex = resultLayer->fields().indexFromName( QStringLiteral( "source" ) );
  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  const int visibilityIndex = ensureRecognitionVisibilityField( resultLayer );
  qlonglong nextObjectId = objectIdIndex >= 0 ? resultLayer->maximumValue( objectIdIndex ).toLongLong() + 1 : 1;
  QgsCoordinateTransform transform( rasterLayer->crs(), resultLayer->crs(), QgsProject::instance()->transformContext() );
  int addedCount = 0;
  for ( const QgsEcoOnnxDetection &detection : inference.detections )
  {
    QgsGeometry geometry = detection.geometry;
    if ( geometry.isNull() || geometry.isEmpty() || !geometry.boundingBox().isFinite() || geometry.boundingBox().isEmpty() )
      continue;
    try
    {
      if ( rasterLayer->crs() != resultLayer->crs() )
        geometry.transform( transform );
    }
    catch ( const QgsCsException & )
    {
      continue;
    }
    if ( geometry.isNull() || geometry.isEmpty() || !geometry.boundingBox().isFinite() || geometry.boundingBox().isEmpty() )
      continue;
    QgsFeature feature( resultLayer->fields() );
    feature.setGeometry( geometry );
    if ( objectIdIndex >= 0 ) feature.setAttribute( objectIdIndex, nextObjectId++ );
    if ( typeIndex >= 0 ) feature.setAttribute( typeIndex, tr( "塔基施工扰动" ) );
    if ( confidenceIndex >= 0 ) feature.setAttribute( confidenceIndex, detection.confidence );
    if ( verifyIndex >= 0 ) feature.setAttribute( verifyIndex, tr( "待核查" ) );
    if ( sourceIndex >= 0 ) feature.setAttribute( sourceIndex, tr( "智能识别" ) );
    if ( remarkIndex >= 0 ) feature.setAttribute( remarkIndex, detection.sourceName.isEmpty() ? tr( "识别范围 %1" ).arg( detection.tileIndex ) : detection.sourceName );
    if ( visibilityIndex >= 0 )
      feature.setAttribute( visibilityIndex, 1 );
    if ( resultLayer->addFeature( feature ) )
      ++addedCount;
  }
  if ( !resultLayer->commitChanges() )
  {
    ecoImportTrace( QStringLiteral( "runRecognition commit failed" ) );
    const QString errors = resultLayer->commitErrors().join( QLatin1Char( '\n' ) );
    resultLayer->rollBack();
    showMessage( tr( "写入识别成果失败" ), errors, true );
    return;
  }
  ecoImportTrace( QStringLiteral( "runRecognition after commit addedCount=%1" ).arg( addedCount ) );
  // OGR providers can defer their extent update until after a commit.  Make
  // the extent explicit before synchronizing the canvas, otherwise the
  // result layer is present in the registry but the canvas still sees an
  // empty extent and does not move to the detected polygons.
  resultLayer->updateExtents();
  applyBusinessStyle( resultLayer );
  attachRecognitionResultLayer( resultLayer );
  resultLayer->triggerRepaint();
  QgsProject::instance()->setDirty( true );
  mApp->setActiveLayer( resultLayer );
  ecoImportTrace( QStringLiteral( "runRecognition before final sync" ) );
  syncBusinessProjectView( QList<QgsMapLayer *>() << resultLayer );
  ecoImportTrace( QStringLiteral( "runRecognition after final sync" ) );
  if ( mApp->mapCanvas() )
  {
    QgsRectangle resultExtent = resultLayer->extent();
    try
    {
      if ( resultLayer->crs() != mApp->mapCanvas()->mapSettings().destinationCrs() )
      {
        QgsCoordinateTransform extentTransform( resultLayer->crs(), mApp->mapCanvas()->mapSettings().destinationCrs(), QgsProject::instance()->transformContext() );
        resultExtent = extentTransform.transformBoundingBox( resultExtent );
      }
    }
    catch ( const QgsCsException & )
    {
      resultExtent = QgsRectangle();
    }
    if ( !resultExtent.isEmpty() && resultExtent.isFinite() )
    {
      const double padding = std::max( resultExtent.width(), resultExtent.height() ) * 0.12;
      if ( padding > 0.0 )
        resultExtent.grow( padding );
      QgsMapCanvas *canvas = mApp->mapCanvas();
      const QString resultLayerId = resultLayer->id();
      QTimer::singleShot( 0, canvas, [this, canvas, resultExtent, resultLayerId] {
        if ( mProjectTransitionInProgress || !QgsProject::instance()->mapLayer( resultLayerId ) )
          return;
        if ( canvas )
        {
          canvas->setExtent( resultExtent );
          canvas->refresh();
        }
      } );
    }
  }
  showRecognitionResultTable( resultLayer->id() );
  ecoImportTrace( QStringLiteral( "runRecognition after show result table" ) );
  if ( mRecognitionResultFilter && mRecognitionResultFilter->currentIndex() != 0 )
    mRecognitionResultFilter->setCurrentIndex( 0 );
  refreshRecognitionResultTable();
  if ( mRecognitionSelectionLabel )
    mRecognitionSelectionLabel->setText( tr( "识别完成：写?%1 个施工扰动候选图斑。" ).arg( addedCount ) );
  showMessage( tr( "塔基扰动识别完成" ), tr( "已处理 %1 个塔位范围，写入 %2 个塔基施工扰动候选图斑。" ).arg( inference.processedTiles ).arg( addedCount ) );
  ecoImportTrace( QStringLiteral( "runRecognition finished" ) );
}

QgsVectorLayer *QgsEcoRestorationController::createResultLayer( bool restoration, bool notify, const QString &phaseId )
{
  ecoImportTrace( QStringLiteral( "createResultLayer entered restoration=%1 notify=%2" ).arg( restoration ).arg( notify ) );

  QString workspacePath = projectWorkspace();
  if ( workspacePath.isEmpty() || QgsProject::instance()->fileName().isEmpty() )
  {
    showMessage( tr( "创建成果层" ), tr( "请先新建或保存遥感解译工程。" ), true );
    return nullptr;
  }

  QString targetPhaseId = phaseId.trimmed();
  if ( targetPhaseId.isEmpty() )
    targetPhaseId = ensureCurrentPhase();
  if ( targetPhaseId.isEmpty() )
  {
    showMessage( tr( "创建成果层" ), tr( "当前工程尚未初始化期次，无法创建成果层。" ), true );
    return nullptr;
  }

  QDir phaseDir( phaseWorkspace( targetPhaseId ) );
  phaseDir.mkpath( QStringLiteral( "results" ) );
  QDir resultsDir( phaseDir.filePath( QStringLiteral( "results" ) ) );
  resultsDir.mkpath( restoration ? QStringLiteral( "review" ) : QStringLiteral( "disturbance" ) );
  QDir targetDir( resultsDir.filePath( restoration ? QStringLiteral( "review" ) : QStringLiteral( "disturbance" ) ) );
  const QString stamp = QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) );
  const QString outputPath = targetDir.filePath(
    restoration
      ? QStringLiteral( "restoration_result_%1.shp" ).arg( stamp )
      : QStringLiteral( "disturbance_result_%1.shp" ).arg( stamp )
  );

  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "object_id" ), QMetaType::Type::Int ) );
  if ( restoration )
  {
    fields.append( QgsField( QStringLiteral( "recovery" ), QMetaType::Type::QString, QString(), 20 ) );
    fields.append( QgsField( QStringLiteral( "restore_pc" ), QMetaType::Type::Double, QString(), 8, 2 ) );
  }
  else
  {
    fields.append( QgsField( QStringLiteral( "dist_type" ), QMetaType::Type::QString, QString(), 40 ) );
    fields.append( QgsField( QStringLiteral( "confidence" ), QMetaType::Type::Double, QString(), 12, 4 ) );
  }
  fields.append( QgsField( QStringLiteral( "verify" ), QMetaType::Type::QString, QString(), 20 ) );
  fields.append( QgsField( QStringLiteral( "source" ), QMetaType::Type::QString, QString(), 20 ) );
  fields.append( QgsField( restoration ? QStringLiteral( "check_date" ) : QStringLiteral( "image_date" ), QMetaType::Type::QDate ) );
  fields.append( QgsField( QStringLiteral( "remark" ), QMetaType::Type::QString, QString(), 120 ) );
  if ( !restoration )
    fields.append( QgsField( QLatin1String( sRecognitionVisibilityField ), QMetaType::Type::Int ) );

  QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
  if ( !crs.isValid() )
    crs = QgsCoordinateReferenceSystem::fromEpsgId( 4490 );

  QgsVectorFileWriter::SaveVectorOptions options;
  options.driverName = QStringLiteral( "ESRI Shapefile" );
  options.fileEncoding = QStringLiteral( "UTF-8" );
  std::unique_ptr<QgsVectorFileWriter> writer( QgsVectorFileWriter::create( outputPath, fields, Qgis::WkbType::Polygon, crs, QgsProject::instance()->transformContext(), options ) );
  if ( !writer || writer->hasError() != QgsVectorFileWriter::NoError )
  {
    const QString error = writer ? writer->errorMessage() : tr( "未知写入错误" );
    showMessage( tr( "创建成果层失败" ), error, true );
    return nullptr;
  }
  writer.reset();

  std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>(
    outputPath,
    restoration ? tr( "修复成果" ) : tr( "施工扰动识别成果" ),
    QStringLiteral( "ogr" )
  );
  if ( !layer->isValid() )
  {
    showMessage( tr( "创建成果层失败" ), tr( "成果文件已生成，但无法载?QGIS銆俓" ), true );
    return nullptr;
  }
  layer->setCustomProperty( QStringLiteral( "eco/category" ), restoration ? QStringLiteral( "review-result" ) : QStringLiteral( "recognition-result" ) );
  layer->setCustomProperty( QStringLiteral( "eco/resultType" ), restoration ? QStringLiteral( "ecological-restoration" ) : QStringLiteral( "construction-disturbance" ) );
  layer->setCustomProperty( sPhaseIdProperty, targetPhaseId );
  if ( QgsRasterLayer *sourceImage = selectedRecognitionRasterLayer() )
  {
    const QString sourcePhaseId = sourceImage->customProperty( sPhaseIdProperty ).toString();
    if ( sourcePhaseId.isEmpty() || sourcePhaseId == targetPhaseId )
    {
      layer->setCustomProperty( QStringLiteral( "eco/sourceImageLayerId" ), sourceImage->id() );
      layer->setCustomProperty( QStringLiteral( "eco/sourceImageName" ), sourceImage->name() );
    }
  }

  const auto configureField = [&layer]( const QString &name, const QString &alias, const QString &defaultExpression = QString() ) {
    const int index = layer->fields().indexFromName( name );
    if ( index < 0 )
      return;
    layer->setFieldAlias( index, alias );
    if ( !defaultExpression.isEmpty() )
      layer->setDefaultValueDefinition( index, QgsDefaultValue( defaultExpression ) );
  };
  const auto configureValueMap = [&layer]( const QString &name, const QStringList &values ) {
    const int index = layer->fields().indexFromName( name );
    if ( index < 0 )
      return;
    QVariantList valueMap;
    for ( const QString &value : values )
      valueMap.append( QVariantMap { { value, value } } );
    layer->setEditorWidgetSetup(
      index,
      QgsEditorWidgetSetup( QStringLiteral( "ValueMap" ), QVariantMap { { QStringLiteral( "map" ), valueMap } } )
    );
  };

  configureField( QStringLiteral( "object_id" ), tr( "图斑编号" ) );
  configureField( QStringLiteral( "verify" ), tr( "核查状态" ), QStringLiteral( "'待核?" ) );
  configureField( QStringLiteral( "source" ), tr( "数据来源" ), QStringLiteral( "'新增'" ) );
  configureField( QStringLiteral( "remark" ), tr( "备注" ) );
  configureValueMap( QStringLiteral( "verify" ), { tr( "待核查" ), tr( "已确认" ), tr( "需整改" ), tr( "已销号" ) } );
  if ( restoration )
  {
    const QStringList statuses = { tr( "未恢复" ), tr( "恢复中" ), tr( "已恢复" ), tr( "复垦完成" ) };
    configureField( QStringLiteral( "recovery" ), tr( "恢复状态" ), QStringLiteral( "'未恢?" ) );
    configureField( QStringLiteral( "restore_pc" ), tr( "恢复率（%锛塡" ), QStringLiteral( "0" ) );
    configureField( QStringLiteral( "check_date" ), tr( "核查日期" ), QStringLiteral( "current_date()" ) );
    configureValueMap( QStringLiteral( "recovery" ), statuses );
  }
  else
  {
    configureField( QStringLiteral( "dist_type" ), tr( "扰动类型" ), QStringLiteral( "'塔基施工扰动'" ) );
    configureField( QStringLiteral( "confidence" ), tr( "置信度" ) );
    configureField( QStringLiteral( "image_date" ), tr( "影像日期" ), QStringLiteral( "current_date()" ) );
    const int visibilityIndex = ensureRecognitionVisibilityField( layer.get() );
    if ( visibilityIndex >= 0 )
      layer->setDefaultValueDefinition( visibilityIndex, QgsDefaultValue( QStringLiteral( "1" ) ) );
    configureValueMap(
      QStringLiteral( "dist_type" ),
      { tr( "塔基施工扰动" ) }
    );
  }
  applyBusinessStyle( layer.get() );
  const bool previousLayerImportInProgress = mLayerImportInProgress;
  const bool deferBusinessTreeReorganization = mRecognitionRunning || previousLayerImportInProgress;
  mLayerImportInProgress = true;
  EcoScopeGuard resultLayerImportGuard( [this, previousLayerImportInProgress] {
    mLayerImportInProgress = previousLayerImportInProgress;
  } );
  QgsVectorLayer *rawLayer = layer.release();
  ecoImportTrace( QStringLiteral( "createResultLayer before addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
  // Keep result layers out of the bridge's automatic insertion point.  That
  // point can refer to a stale/native group while the Jianghe project tree is
  // being reorganized.  We register the layer first and attach exactly one
  // node to the resolved phase result group below, which makes the registry,
  // legend and canvas converge on the same node.
  QgsProject::instance()->addMapLayer( rawLayer, false );
  ecoImportTrace( QStringLiteral( "createResultLayer after addMapLayer layerId=%1" ).arg( rawLayer->id() ) );
  if ( !deferBusinessTreeReorganization )
  {
    ecoImportTrace( QStringLiteral( "createResultLayer before ensureBusinessGroups layerId=%1" ).arg( rawLayer->id() ) );
    ensureBusinessGroups();
    ecoImportTrace( QStringLiteral( "createResultLayer after ensureBusinessGroups layerId=%1" ).arg( rawLayer->id() ) );
  }
  else
  {
    ecoImportTrace( QStringLiteral( "createResultLayer skipped ensureBusinessGroups during active layer operation layerId=%1" ).arg( rawLayer->id() ) );
  }
  if ( restoration )
    moveLayerToBusinessGroup( rawLayer, sRecognitionGroup );
  else
    attachRecognitionResultLayer( rawLayer );
  ecoImportTrace( QStringLiteral( "createResultLayer after attach layerId=%1" ).arg( rawLayer->id() ) );
  QgsProject::instance()->setDirty( true );
  refreshLayerChoices();
  ecoImportTrace( QStringLiteral( "createResultLayer after refreshLayerChoices layerId=%1" ).arg( rawLayer->id() ) );

  if ( mResultLayerCombo )
  {
    const int index = mResultLayerCombo->findData( rawLayer->id() );
    if ( index >= 0 )
      mResultLayerCombo->setCurrentIndex( index );
  }
  if ( notify )
  {
    showMessage(
      tr( "成果层已创建" ),
      restoration
        ? tr( "已建立修复成果层。状态可选择：未恢复、恢复中、已恢复、复垦完成。" )
        : tr( "已建立可编辑的塔基施工扰动成果：\n%1" ).arg( outputPath )
    );
  }
  return rawLayer;
}

QgsVectorLayer *QgsEcoRestorationController::createSmartSegmentationLayer( const QString &layerName, const QgsCoordinateReferenceSystem &crs, bool notify )
{
  QString workspacePath = projectWorkspace();
  if ( workspacePath.isEmpty() || QgsProject::instance()->fileName().isEmpty() )
  {
    showMessage( tr( "保存智能分割成果" ), tr( "请先新建或保存遥感解译工程。" ), true );
    return nullptr;
  }

  const QString phaseId = ensureCurrentPhase();
  if ( phaseId.isEmpty() )
  {
    showMessage( tr( "保存智能分割成果" ), tr( "当前工程尚未初始化期次，无法保存智能分割成果。" ), true );
    return nullptr;
  }

  const QString displayName = sanitizedName( layerName );
  QDir phaseDir( phaseWorkspace( phaseId ) );
  phaseDir.mkpath( QStringLiteral( "results/smart_segmentation" ) );
  QDir resultsDir( phaseDir.filePath( QStringLiteral( "results/smart_segmentation" ) ) );
  const QString stamp = QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) );
  const QString outputPath = resultsDir.filePath( QStringLiteral( "%1_%2.shp" ).arg( displayName, stamp ) );

  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "object_id" ), QMetaType::Type::Int ) );
  fields.append( QgsField( QStringLiteral( "seg_name" ), QMetaType::Type::QString, QString(), 80 ) );
  fields.append( QgsField( QStringLiteral( "score" ), QMetaType::Type::Double, QString(), 12, 4 ) );
  fields.append( QgsField( QStringLiteral( "source" ), QMetaType::Type::QString, QString(), 20 ) );
  fields.append( QgsField( QStringLiteral( "created" ), QMetaType::Type::QDate ) );
  fields.append( QgsField( QStringLiteral( "remark" ), QMetaType::Type::QString, QString(), 120 ) );

  QgsCoordinateReferenceSystem outputCrs = crs;
  if ( !outputCrs.isValid() )
    outputCrs = QgsProject::instance()->crs();
  if ( !outputCrs.isValid() )
    outputCrs = QgsCoordinateReferenceSystem::fromEpsgId( 4490 );

  QgsVectorFileWriter::SaveVectorOptions options;
  options.driverName = QStringLiteral( "ESRI Shapefile" );
  options.fileEncoding = QStringLiteral( "UTF-8" );
  std::unique_ptr<QgsVectorFileWriter> writer( QgsVectorFileWriter::create( outputPath, fields, Qgis::WkbType::Polygon, outputCrs, QgsProject::instance()->transformContext(), options ) );
  if ( !writer || writer->hasError() != QgsVectorFileWriter::NoError )
  {
    const QString error = writer ? writer->errorMessage() : tr( "未知写入错误" );
    showMessage( tr( "保存智能分割成果失败" ), error, true );
    return nullptr;
  }
  writer.reset();

  std::unique_ptr<QgsVectorLayer> layer = std::make_unique<QgsVectorLayer>( outputPath, displayName, QStringLiteral( "ogr" ) );
  if ( !layer->isValid() )
  {
    showMessage( tr( "保存智能分割成果失败" ), tr( "成果文件已生成，但无法载入系统。" ), true );
    return nullptr;
  }
  layer->setCustomProperty( QStringLiteral( "eco/category" ), QStringLiteral( "smart-segmentation" ) );
  layer->setCustomProperty( QStringLiteral( "eco/resultType" ), QStringLiteral( "smart-segmentation" ) );
  layer->setCustomProperty( sPhaseIdProperty, phaseId );
  if ( QgsRasterLayer *sourceImage = selectedRecognitionRasterLayer() )
  {
    const QString sourcePhaseId = sourceImage->customProperty( sPhaseIdProperty ).toString();
    if ( sourcePhaseId.isEmpty() || sourcePhaseId == phaseId )
    {
      layer->setCustomProperty( QStringLiteral( "eco/sourceImageLayerId" ), sourceImage->id() );
      layer->setCustomProperty( QStringLiteral( "eco/sourceImageName" ), sourceImage->name() );
    }
  }

  const auto configureField = [&layer]( const QString &name, const QString &alias, const QString &defaultExpression = QString() ) {
    const int index = layer->fields().indexFromName( name );
    if ( index < 0 )
      return;
    layer->setFieldAlias( index, alias );
    if ( !defaultExpression.isEmpty() )
      layer->setDefaultValueDefinition( index, QgsDefaultValue( defaultExpression ) );
  };
  configureField( QStringLiteral( "object_id" ), tr( "编号" ), QStringLiteral( "coalesce(maximum(\"object_id\"), 0) + 1" ) );
  configureField( QStringLiteral( "seg_name" ), tr( "成果名称" ) );
  configureField( QStringLiteral( "score" ), tr( "分割得分" ) );
  configureField( QStringLiteral( "source" ), tr( "数据来源" ), QStringLiteral( "'智能分割'" ) );
  configureField( QStringLiteral( "created" ), tr( "创建日期" ), QStringLiteral( "current_date()" ) );
  configureField( QStringLiteral( "remark" ), tr( "备注" ) );

  applyBusinessStyle( layer.get() );
  const bool previousLayerImportInProgress = mLayerImportInProgress;
  mLayerImportInProgress = true;
  EcoScopeGuard smartLayerImportGuard( [this, previousLayerImportInProgress] {
    mLayerImportInProgress = previousLayerImportInProgress;
  } );
  QgsVectorLayer *rawLayer = layer.release();
  ecoImportTrace( QStringLiteral( "createSmartSegmentationLayer before addMapLayer layerId=%1 phaseId=%2" ).arg( rawLayer->id(), phaseId ) );
  QgsProject::instance()->addMapLayer( rawLayer, false );
  ecoImportTrace( QStringLiteral( "createSmartSegmentationLayer after addMapLayer layerId=%1" ).arg( rawLayer->id() ) );

  // Attach directly to the phase result group. Reorganizing the whole
  // business tree while a just-created provider is registering may re-enter
  // the layer-tree bridge from the Enter-key save callback.
  QgsLayerTreeGroup *resultGroup = phaseResultGroup( phaseId, sSmartSegmentationGroup, true );
  QgsLayerTreeLayer *destinationNode = resultGroup ? resultGroup->findLayer( rawLayer->id() ) : nullptr;
  if ( resultGroup && !destinationNode )
    destinationNode = resultGroup->addLayer( rawLayer );
  if ( !destinationNode )
  {
    ecoImportTrace( QStringLiteral( "createSmartSegmentationLayer failed to attach tree layerId=%1" ).arg( rawLayer->id() ) );
    QgsProject::instance()->removeMapLayer( rawLayer->id() );
    return nullptr;
  }
  resultGroup->setExpanded( true );
  resultGroup->setItemVisibilityChecked( true );
  destinationNode->setItemVisibilityChecked( true );
  for ( QgsLayerTreeNode *ancestor = resultGroup->parent(); ancestor; ancestor = ancestor->parent() )
  {
    if ( QgsLayerTreeGroup *ancestorGroup = QgsLayerTree::toGroup( ancestor ) )
    {
      ancestorGroup->setExpanded( true );
      ancestorGroup->setItemVisibilityChecked( true );
    }
  }
  ecoImportTrace( QStringLiteral( "createSmartSegmentationLayer attached tree layerId=%1" ).arg( rawLayer->id() ) );
  QgsProject::instance()->setDirty( true );
  if ( notify )
    showMessage( tr( "智能分割成果已保存" ), tr( "已建立独立智能分割成果图层：\n%1" ).arg( outputPath ) );
  return rawLayer;
}

bool QgsEcoRestorationController::ensureEditableResultLayer( QgsVectorLayer *layer )
{
  if ( !layer )
  {
    showMessage( tr( "修复成果" ), tr( "请先选择或创建一个面成果图层。" ), true );
    return false;
  }
  mApp->setActiveLayer( layer );
  if ( !layer->isEditable() && !mApp->toggleEditing( layer ) )
  {
    showMessage( tr( "修复成果" ), tr( "该图层无法进入编辑状态，请检查文件写入权限。" ), true );
    return false;
  }
  return true;
}

void QgsEcoRestorationController::startAddingPolygon()
{
  QgsVectorLayer *layer = selectedResultLayer();
  if ( !ensureEditableResultLayer( layer ) )
    return;
  mApp->actionAddFeature()->trigger();
}

void QgsEcoRestorationController::startVertexEditing()
{
  QgsVectorLayer *layer = selectedResultLayer();
  if ( !ensureEditableResultLayer( layer ) )
    return;
  if ( mApp->mapCanvas() && !mRecognitionSelectionColorChanged )
  {
    mRecognitionOriginalSelectionColor = mApp->mapCanvas()->selectionColor();
    mRecognitionSelectionColorChanged = true;
  }
  if ( mApp->mapCanvas() )
  {
    QColor editSelectionColor( QStringLiteral( "#dc2626" ) );
    editSelectionColor.setAlpha( 45 );
    mApp->mapCanvas()->setSelectionColor( editSelectionColor );
  }
  mApp->actionVertexToolActiveLayer()->trigger();
}

void QgsEcoRestorationController::selectResultFeatures()
{
  QgsVectorLayer *layer = selectedResultLayer();
  if ( !layer )
    return;
  mApp->setActiveLayer( layer );
  mApp->actionSelectRectangle()->trigger();
}

void QgsEcoRestorationController::deleteSelectedFeatures()
{
  QgsVectorLayer *layer = selectedResultLayer();
  if ( !ensureEditableResultLayer( layer ) )
    return;
  if ( layer->selectedFeatureCount() == 0 )
  {
    showMessage( tr( "删除图斑" ), tr( "请先选择需要删除的图斑。" ), true );
    return;
  }
  mApp->deleteSelected( layer, mApp, true );
}

void QgsEcoRestorationController::saveResultEdits()
{
  QgsVectorLayer *layer = selectedResultLayer();
  if ( !layer || !layer->isEditable() )
  {
    showMessage( tr( "保存编辑" ), tr( "当前成果层没有待保存的编辑。" ), true );
    return;
  }
  finishRecognitionEditing( layer );
  QgsProject::instance()->setDirty( true );
}

void QgsEcoRestorationController::exportTowerScreenshots()
{
  QgsVectorLayer *towerLayer = selectedTowerLayer();
  if ( !towerLayer )
  {
    showMessage( tr( "批量截图" ), tr( "请先添加并选择杆塔点图层。" ), true );
    return;
  }

  const qint64 featureCount = towerLayer->featureCount();
  if ( featureCount <= 0 )
  {
    showMessage( tr( "批量截图" ), tr( "所选杆塔图层没有要素。" ), true );
    return;
  }
  if ( featureCount > 1000 && QMessageBox::question( mApp, tr( "批量截图" ), tr( "将生?%1 张截图，可能耗时较长，是否继续？" ).arg( featureCount ) ) != QMessageBox::Yes )
    return;

  ensureCurrentPhase();
  QString defaultPath = projectWorkspace();
  if ( !defaultPath.isEmpty() )
    defaultPath = QDir( phaseWorkspace() ).filePath( QStringLiteral( "screenshots" ) );
  QDir().mkpath( defaultPath );
  const QString outputPath = QFileDialog::getExistingDirectory( mApp, tr( "选择截图输出目录" ), defaultPath );
  if ( outputPath.isEmpty() )
    return;

  QDir outputDir( outputPath );
  const int groundRangeMeters = mScreenshotRangeSpin->value();
  const int pixelSize = mScreenshotSizeSpin->value();
  const double halfRange = groundRangeMeters / 2.0;
  QgsMapSettings baseSettings = mApp->mapCanvas()->mapSettings();
  const QgsCoordinateReferenceSystem destinationCrs = baseSettings.destinationCrs();
  QgsCoordinateTransform pointTransform( towerLayer->crs(), destinationCrs, QgsProject::instance()->transformContext() );
  QgsDistanceArea distanceArea;
  distanceArea.setSourceCrs( destinationCrs, QgsProject::instance()->transformContext() );
  distanceArea.setEllipsoid( QgsProject::instance()->ellipsoid() );

  QFile indexFile( outputDir.filePath( QStringLiteral( "截图索引.csv" ) ) );
  if ( !indexFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    showMessage( tr( "批量截图" ), tr( "无法创建截图索引文件。" ), true );
    return;
  }
  indexFile.write( QByteArray::fromHex( "EFBBBF" ) );
  QTextStream indexStream( &indexFile );
  indexStream << QStringLiteral( "序号,杆塔标识,文件?中心X,中心Y,范围?像素\n" );

  QProgressDialog progress( tr( "正在生成杆塔截图..." ), tr( "取消" ), 0, static_cast<int>( featureCount ), mApp );
  progress.setWindowModality( Qt::WindowModal );
  progress.setMinimumDuration( 0 );

  QgsFeature feature;
  QgsFeatureIterator iterator = towerLayer->getFeatures();
  qint64 current = 0;
  int savedCount = 0;
  while ( iterator.nextFeature( feature ) )
  {
    if ( progress.wasCanceled() )
      break;
    ++current;
    progress.setValue( static_cast<int>( current ) );
    progress.setLabelText( tr( "正在生成?%1 / %2 寮?.." ).arg( current ).arg( featureCount ) );

    QgsPointXY center = featurePoint( feature.geometry() );
    if ( center.isEmpty() )
      continue;
    try
    {
      center = pointTransform.transform( center );
    }
    catch ( const QgsCsException & )
    {
      continue;
    }

    QgsPointXY east;
    QgsPointXY west;
    QgsPointXY north;
    QgsPointXY south;
    constexpr double halfPi = 1.57079632679489661923;
    constexpr double pi = 3.14159265358979323846;
    distanceArea.measureLineProjected( center, halfRange, halfPi, &east );
    distanceArea.measureLineProjected( center, halfRange, -halfPi, &west );
    distanceArea.measureLineProjected( center, halfRange, 0.0, &north );
    distanceArea.measureLineProjected( center, halfRange, pi, &south );
    const QgsRectangle extent( west.x(), south.y(), east.x(), north.y() );
    if ( extent.isEmpty() )
      continue;

    QgsMapSettings settings = baseSettings;
    settings.setExtent( extent );
    settings.setOutputSize( QSize( pixelSize, pixelSize ) );
    settings.setOutputDpi( 96.0 );
    QgsMapRendererParallelJob renderJob( settings );
    renderJob.start();
    renderJob.waitForFinished();

    const QString label = featureLabel( feature, towerLayer->fields(), current );
    const QString fileName = QStringLiteral( "%1_%2m.png" ).arg( label ).arg( groundRangeMeters );
    if ( renderJob.renderedImage().save( outputDir.filePath( fileName ), "PNG" ) )
    {
      ++savedCount;
      indexStream << current << ',' << label << ',' << fileName << ','
                  << QString::number( center.x(), 'f', 8 ) << ',' << QString::number( center.y(), 'f', 8 ) << ','
                  << groundRangeMeters << ',' << pixelSize << '\n';
    }
  }
  progress.setValue( static_cast<int>( featureCount ) );
  indexFile.close();

  showMessage( tr( "批量截图完成" ), tr( "已生成 %1 张杆塔截图和截图索引：\n%2" ).arg( savedCount ).arg( outputPath ) );
}

void QgsEcoRestorationController::exportDisturbanceResultYoloSamples( const QString &resultLayerId )
{
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( resultLayerId ) );
  if ( !resultLayer || resultLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() != QLatin1String( "construction-disturbance" ) )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "请先在扰动解译识别结果图层上点击该菜单。" ), true );
    return;
  }

  const qint64 featureCount = resultLayer->featureCount();
  if ( featureCount <= 0 )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "当前结果图层没有可导出的面要素。" ), true );
    return;
  }

  const QString resultPhaseId = layerPhaseIdForLayer( resultLayer );
  QString defaultPath = projectWorkspace();
  if ( !resultPhaseId.isEmpty() )
    defaultPath = QDir( phaseWorkspace( resultPhaseId ) ).filePath( QStringLiteral( "yolo_samples" ) );
  else if ( !defaultPath.isEmpty() )
    defaultPath = QDir( defaultPath ).filePath( QStringLiteral( "yolo_samples" ) );
  if ( defaultPath.isEmpty() )
    defaultPath = QDir::homePath();
  QDir().mkpath( defaultPath );

  const QString outputPath = QFileDialog::getExistingDirectory( mApp, tr( "选择YOLO样本导出目录" ), defaultPath );
  if ( outputPath.isEmpty() )
    return;

  QgsRasterLayer *sourceImage = nullptr;
  const QString sourceImageLayerId = resultLayer->customProperty( QStringLiteral( "eco/sourceImageLayerId" ) ).toString().trimmed();
  if ( !sourceImageLayerId.isEmpty() )
    sourceImage = qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( sourceImageLayerId ) );

  if ( !sourceImage )
  {
    const QString sourceImageName = resultLayer->customProperty( QStringLiteral( "eco/sourceImageName" ) ).toString().trimmed();
    QgsRasterLayer *phaseRasterFallback = nullptr;
    const auto layers = QgsProject::instance()->mapLayers();
    for ( QgsMapLayer *layer : layers )
    {
      QgsRasterLayer *candidate = qobject_cast<QgsRasterLayer *>( layer );
      if ( !candidate )
        continue;
      const QString candidatePhaseId = layerPhaseIdForLayer( candidate );
      if ( !resultPhaseId.isEmpty() && !candidatePhaseId.isEmpty() && candidatePhaseId != resultPhaseId )
        continue;
      if ( !sourceImageName.isEmpty() && candidate->name() == sourceImageName )
      {
        sourceImage = candidate;
        break;
      }
      if ( !phaseRasterFallback )
        phaseRasterFallback = candidate;
    }
    if ( !sourceImage )
      sourceImage = phaseRasterFallback;
  }
  if ( !sourceImage )
    sourceImage = selectedRecognitionRasterLayer();
  if ( !sourceImage || !sourceImage->isValid() )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "未找到可用于导出的源影像图层。" ), true );
    return;
  }

  const QgsCoordinateReferenceSystem imageCrs = sourceImage->crs();
  if ( !imageCrs.isValid() )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "源影像坐标系无效，无法导出样本。" ), true );
    return;
  }

  const QgsCoordinateReferenceSystem resultCrs = resultLayer->crs();
  std::unique_ptr<QgsCoordinateTransform> toImageTransform;
  if ( resultCrs.isValid() && resultCrs != imageCrs )
  {
    try
    {
      toImageTransform = std::make_unique<QgsCoordinateTransform>( resultCrs, imageCrs, QgsProject::instance()->transformContext() );
    }
    catch ( const QgsCsException & )
    {
      showMessage( tr( "导出YOLO样本" ), tr( "结果图层与源影像之间无法建立坐标转换。" ), true );
      return;
    }
  }

  const double pixelSize = std::max( std::abs( sourceImage->rasterUnitsPerPixelX() ), std::abs( sourceImage->rasterUnitsPerPixelY() ) );
  if ( pixelSize <= 0.0 )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "源影像像元大小无效，无法导出样本。" ), true );
    return;
  }

  QDir rootDir( outputPath );
  rootDir.mkpath( QStringLiteral( "images/train" ) );
  rootDir.mkpath( QStringLiteral( "labels/train" ) );

  QFile classesFile( rootDir.filePath( QStringLiteral( "classes.txt" ) ) );
  if ( !classesFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "无法创建 classes.txt銆俓" ), true );
    return;
  }
  classesFile.write( QByteArray::fromHex( "EFBBBF" ) );
  QTextStream classesStream( &classesFile );
  classesStream << QStringLiteral( "construction_disturbance\n" );
  classesFile.close();

  QFile yamlFile( rootDir.filePath( QStringLiteral( "data.yaml" ) ) );
  if ( yamlFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    yamlFile.write( QByteArray::fromHex( "EFBBBF" ) );
    QTextStream yamlStream( &yamlFile );
    yamlStream << QStringLiteral( "path: .\n" );
    yamlStream << QStringLiteral( "train: images/train\n" );
    yamlStream << QStringLiteral( "val: images/train\n" );
    yamlStream << QStringLiteral( "nc: 1\n" );
    yamlStream << QStringLiteral( "names:\n" );
    yamlStream << QStringLiteral( "  0: construction_disturbance\n" );
    yamlFile.close();
  }

  QFile indexFile( rootDir.filePath( QStringLiteral( "index.csv" ) ) );
  if ( !indexFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "无法创建 index.csv銆俓" ), true );
    return;
  }
  indexFile.write( QByteArray::fromHex( "EFBBBF" ) );
  QTextStream indexStream( &indexFile );
  indexStream << QStringLiteral( "sample_id,feature_id,image_file,label_file,source_image_id,source_image_name,phase_id\n" );

  auto normalizePoint = []( const QgsPointXY &point, const QgsRectangle &extent ) -> QPointF {
    const double width = extent.width();
    const double height = extent.height();
    if ( width <= 0.0 || height <= 0.0 )
      return QPointF();
    const double x = std::clamp( ( point.x() - extent.xMinimum() ) / width, 0.0, 1.0 );
    const double y = std::clamp( ( extent.yMaximum() - point.y() ) / height, 0.0, 1.0 );
    return QPointF( x, y );
  };

  const auto ringToLabel = [&normalizePoint]( const QVector<QgsPointXY> &ring, const QgsRectangle &extent ) -> QString {
    QVector<QgsPointXY> points = ring;
    while ( points.size() >= 2 && points.first() == points.last() )
      points.removeLast();
    if ( points.size() < 3 )
      return QString();

    QString line = QStringLiteral( "0" );
    for ( const QgsPointXY &point : points )
    {
      const QPointF normalized = normalizePoint( point, extent );
      line += QStringLiteral( " %1 %2" )
                .arg( QString::number( normalized.x(), 'f', 6 ),
                      QString::number( normalized.y(), 'f', 6 ) );
    }
    return line;
  };

  constexpr int outputSize = 640;
  const double minimumMapSpan = pixelSize * 256.0;
  const QString sourceImageId = sourceImage->id();
  const QString sourceImageName = sourceImage->name();

  QgsMapSettings baseSettings;
  baseSettings.setDestinationCrs( imageCrs );
  baseSettings.setTransformContext( QgsProject::instance()->transformContext() );
  baseSettings.setBackgroundColor( QColor( QStringLiteral( "#000000" ) ) );
  baseSettings.setLayers( QList<QgsMapLayer *>() << sourceImage );
  baseSettings.setOutputSize( QSize( outputSize, outputSize ) );
  baseSettings.setOutputDpi( 96.0 );

  QProgressDialog progress( tr( "正在导出YOLO样本..." ), tr( "取消" ), 0, static_cast<int>( featureCount ), mApp );
  progress.setWindowModality( Qt::WindowModal );
  progress.setMinimumDuration( 0 );

  int current = 0;
  int exportedCount = 0;
  QgsFeatureIterator iterator = resultLayer->getFeatures();
  QgsFeature feature;
  while ( iterator.nextFeature( feature ) )
  {
    if ( progress.wasCanceled() )
      break;

    ++current;
    progress.setValue( current );
    progress.setLabelText( tr( "正在导出?%1 / %2 个样?.." ).arg( current ).arg( featureCount ) );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );

    QgsGeometry geometry = feature.geometry();
    if ( geometry.isNull() || geometry.isEmpty() )
      continue;
    if ( geometry.type() != Qgis::GeometryType::Polygon )
      geometry = geometry.convertToType( Qgis::GeometryType::Polygon, true );
    if ( geometry.isNull() || geometry.isEmpty() )
      continue;
    if ( !geometry.isGeosValid() )
      geometry = geometry.makeValid();
    if ( geometry.isNull() || geometry.isEmpty() )
      continue;
    if ( toImageTransform )
    {
      try
      {
        geometry.transform( *toImageTransform );
      }
      catch ( const QgsCsException & )
      {
        continue;
      }
    }
    if ( geometry.isNull() || geometry.isEmpty() )
      continue;

    QgsRectangle bbox = geometry.boundingBox();
    if ( bbox.isEmpty() || !bbox.isFinite() )
      continue;
    const QgsPointXY center = bbox.center();
    const double span = std::max( std::max( bbox.width(), bbox.height() ) * 1.5, minimumMapSpan );
    if ( !std::isfinite( span ) || span <= 0.0 )
      continue;
    const QgsRectangle extent( center.x() - span / 2.0, center.y() - span / 2.0, center.x() + span / 2.0, center.y() + span / 2.0 );
    if ( extent.isEmpty() || !extent.isFinite() )
      continue;

    QStringList labelLines;
    QgsMultiPolygonXY multiPolygon;
    if ( geometry.isMultipart() )
      multiPolygon = geometry.asMultiPolygon();
    else
      multiPolygon << geometry.asPolygon();
    for ( const QgsPolygonXY &polygon : multiPolygon )
    {
      if ( polygon.isEmpty() )
        continue;
      const QString line = ringToLabel( polygon.constFirst(), extent );
      if ( !line.isEmpty() )
        labelLines << line;
    }
    if ( labelLines.isEmpty() )
      continue;

    QgsMapSettings settings = baseSettings;
    settings.setExtent( extent );
    QgsMapRendererParallelJob renderJob( settings );
    renderJob.start();
    renderJob.waitForFinished();
    const QImage renderedImage = renderJob.renderedImage();
    if ( renderedImage.isNull() )
      continue;

    const QString sampleName = QStringLiteral( "sample_%1" ).arg( current, 6, 10, QLatin1Char( '0' ) );
    const QString imageFileName = sampleName + QStringLiteral( ".png" );
    const QString labelFileName = sampleName + QStringLiteral( ".txt" );
    const QString imagePath = rootDir.filePath( QStringLiteral( "images/train/%1" ).arg( imageFileName ) );
    const QString labelPath = rootDir.filePath( QStringLiteral( "labels/train/%1" ).arg( labelFileName ) );

    if ( !renderedImage.save( imagePath, "PNG" ) )
      continue;

    QFile labelFile( labelPath );
    if ( !labelFile.open( QIODevice::WriteOnly | QIODevice::Text ) )
    {
      QFile::remove( imagePath );
      continue;
    }
    QTextStream labelStream( &labelFile );
    for ( const QString &line : labelLines )
      labelStream << line << '\n';
    labelFile.close();

    ++exportedCount;
    indexStream << sampleName << ','
                << feature.id() << ','
                << imageFileName << ','
                << labelFileName << ','
                << sourceImageId << ','
                << sourceImageName << ','
                << resultPhaseId << '\n';
  }
  progress.setValue( static_cast<int>( featureCount ) );
  indexFile.close();

  if ( exportedCount <= 0 )
  {
    showMessage( tr( "导出YOLO样本" ), tr( "未导出任何样本，请检查结果图层是否包含有效面要素。" ), true );
    return;
  }

  showMessage( tr( "导出YOLO样本" ), tr( "已导?%1 个YOLO样本到：%2" ).arg( exportedCount ).arg( outputPath ) );
}

bool QgsEcoRestorationController::eventFilter( QObject *watched, QEvent *event )
{
  if ( mApp && mApp->mapCanvas()
       && ( event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ) )
  {
    const QKeyEvent *keyEvent = static_cast<QKeyEvent *>( event );
    QWidget *watchedWidget = qobject_cast<QWidget *>( watched );
    const bool fromMapCanvas = watched == mApp->mapCanvas()
                               || watched == mApp->mapCanvas()->viewport()
                               || ( watchedWidget && mApp->mapCanvas()->isAncestorOf( watchedWidget ) );
    if ( fromMapCanvas
         && keyEvent->key() == Qt::Key_Z
         && keyEvent->modifiers().testFlag( Qt::ControlModifier )
         && !keyEvent->modifiers().testFlag( Qt::AltModifier )
         && !keyEvent->modifiers().testFlag( Qt::ShiftModifier ) )
    {
      if ( event->type() == QEvent::ShortcutOverride )
      {
        event->accept();
        return false;
      }
      if ( undoLastTowerPointEdit( mApp ) )
        return true;
    }
  }

  if ( watched == mPhaseComparePage && event->type() == QEvent::Resize && mPhaseSwipeHandle && mPhaseCompareSplitter )
  {
    if ( mPhaseSwipeMode )
    {
      const QList<int> sizes = mPhaseCompareSplitter->sizes();
      const int leftWidth = sizes.value( 0, mPhaseCompareSplitter->width() / 2 );
      const QPoint topLeft = mPhaseCompareSplitter->mapTo( mPhaseComparePage, QPoint( leftWidth - mPhaseSwipeHandle->width() / 2, 0 ) );
      mPhaseSwipeHandle->setGeometry( topLeft.x(), topLeft.y(), mPhaseSwipeHandle->width(), mPhaseCompareSplitter->height() );
      mPhaseSwipeHandle->raise();
      mPhaseSwipeHandle->show();
    }
  }
  if ( mPhaseCompareRightCanvas
       && event->type() == QEvent::Resize
       && ( watched == mPhaseCompareRightCanvas || watched == mPhaseCompareRightCanvas->viewport() ) )
  {
    QWidget *legendHost = mPhaseCompareRightCanvas->viewport() ? mPhaseCompareRightCanvas->viewport() : mPhaseCompareRightCanvas.data();
    QWidget *legend = legendHost ? legendHost->findChild<QWidget *>( QStringLiteral( "EcoPhaseDiffLegendOverlay" ), Qt::FindDirectChildrenOnly ) : nullptr;
    if ( !legend && mPhaseCompareRightCanvas )
      legend = mPhaseCompareRightCanvas->findChild<QWidget *>( QStringLiteral( "EcoPhaseDiffLegendOverlay" ) );
    if ( legendHost && legend )
    {
      legend->adjustSize();
      const QSize legendSize = legend->sizeHint().expandedTo( QSize( 132, 72 ) );
      legend->resize( legendSize );
      const int margin = 14;
      legend->move( std::max( 6, legendHost->width() - legend->width() - margin ),
                    std::max( 6, legendHost->height() - legend->height() - margin ) );
      legend->raise();
    }
  }
  if ( watched == mPhaseSwipeHandle )
  {
    if ( event->type() == QEvent::MouseButtonPress )
    {
      const auto *mouseEvent = static_cast<QMouseEvent *>( event );
      if ( mouseEvent->button() == Qt::LeftButton )
      {
        mPhaseSwipeDragging = true;
        return true;
      }
    }
    if ( event->type() == QEvent::MouseMove && mPhaseSwipeDragging && mPhaseCompareSplitter && mPhaseSwipeHandle )
    {
      const auto *mouseEvent = static_cast<QMouseEvent *>( event );
      const int width = mPhaseCompareSplitter->width();
      int x = mPhaseCompareSplitter->mapFromGlobal( mouseEvent->globalPosition().toPoint() ).x();
      x = std::clamp( x, 120, std::max( 120, width - 120 ) );
      if ( mPhaseComparePage )
      {
        const QPoint topLeft = mPhaseCompareSplitter->mapTo( mPhaseComparePage, QPoint( x - mPhaseSwipeHandle->width() / 2, 0 ) );
        mPhaseSwipeHandle->setGeometry( topLeft.x(), topLeft.y(), mPhaseSwipeHandle->width(), mPhaseCompareSplitter->height() );
        mPhaseSwipeHandle->raise();
        mPhaseComparePage->setProperty( "eco/swipePendingX", x );
        if ( QTimer *resizeTimer = mPhaseComparePage->findChild<QTimer *>( QStringLiteral( "EcoPhaseSwipeResizeTimer" ) ) )
        {
          if ( !resizeTimer->isActive() )
            resizeTimer->start();
        }
      }
      return true;
    }
    if ( event->type() == QEvent::MouseButtonRelease )
    {
      mPhaseSwipeDragging = false;
      if ( mPhaseComparePage )
      {
        if ( QTimer *resizeTimer = mPhaseComparePage->findChild<QTimer *>( QStringLiteral( "EcoPhaseSwipeResizeTimer" ) ) )
          resizeTimer->start( 0 );
      }
      return true;
    }
  }

  const auto phaseComparePanelForObject = []( QObject *object ) -> QWidget * {
    QWidget *widget = qobject_cast<QWidget *>( object );
    for ( QWidget *current = widget; current; current = current->parentWidget() )
    {
      if ( current->property( "eco/phaseComparePanel" ).toBool() )
        return current;
    }
    return nullptr;
  };
  QWidget *watchedWidget = qobject_cast<QWidget *>( watched );
  const bool phaseCompareTitleDrag = watchedWidget && watchedWidget->property( "compareTitle" ).toBool();
  if ( phaseCompareTitleDrag )
  {
    QWidget *phasePanel = phaseComparePanelForObject( watched );
    if ( event->type() == QEvent::MouseButtonPress )
    {
      const auto *mouseEvent = static_cast<QMouseEvent *>( event );
      if ( mouseEvent->button() == Qt::LeftButton )
      {
        mPhasePanelDragging = true;
        mPhasePanelDragStart = mouseEvent->globalPosition().toPoint();
        watchedWidget->setCursor( Qt::ClosedHandCursor );
        return true;
      }
    }
    if ( event->type() == QEvent::MouseButtonRelease && mPhasePanelDragging )
    {
      const auto *mouseEvent = static_cast<QMouseEvent *>( event );
      const QPoint releaseGlobal = mouseEvent->globalPosition().toPoint();
      const bool moved = ( releaseGlobal - mPhasePanelDragStart ).manhattanLength() > 12;
      mPhasePanelDragging = false;
      watchedWidget->setCursor( Qt::OpenHandCursor );

      QWidget *otherPanel = nullptr;
      if ( phasePanel && phasePanel == mPhaseCompareLeftPanel )
        otherPanel = mPhaseCompareRightPanel;
      else if ( phasePanel && phasePanel == mPhaseCompareRightPanel )
        otherPanel = mPhaseCompareLeftPanel;

      if ( moved && otherPanel && mPhaseCompareSplitter && otherPanel->rect().contains( otherPanel->mapFromGlobal( releaseGlobal ) ) )
      {
        const bool leftPanelCurrentlyFirst = mPhaseCompareSplitter->indexOf( mPhaseCompareLeftPanel ) < mPhaseCompareSplitter->indexOf( mPhaseCompareRightPanel );
        mPhaseCompareSplitter->insertWidget( 0, leftPanelCurrentlyFirst ? mPhaseCompareRightPanel : mPhaseCompareLeftPanel );
        mPhaseCompareSplitter->setSizes( QList<int>() << 1 << 1 );
        if ( mPhaseSwipeHandle && mPhaseComparePage && mPhaseSwipeMode )
        {
          const int leftWidth = mPhaseCompareSplitter->sizes().value( 0, mPhaseCompareSplitter->width() / 2 );
          const QPoint topLeft = mPhaseCompareSplitter->mapTo( mPhaseComparePage, QPoint( leftWidth - mPhaseSwipeHandle->width() / 2, 0 ) );
          mPhaseSwipeHandle->setGeometry( topLeft.x(), topLeft.y(), mPhaseSwipeHandle->width(), mPhaseCompareSplitter->height() );
          mPhaseSwipeHandle->raise();
          mPhaseSwipeHandle->show();
        }
      }
      return true;
    }
  }

  if ( event->type() == QEvent::Show && watched && watched->inherits( "QgsMessageBar" ) )
  {
    if ( QWidget *messageBar = qobject_cast<QWidget *>( watched ) )
      QTimer::singleShot( 0, messageBar, [messageBar] { messageBar->hide(); } );
  }
  if ( event->type() == QEvent::Show && watched && watched->inherits( "QDialog" ) )
  {
    if ( QWidget *dialog = qobject_cast<QWidget *>( watched ); dialog && !dialog->property( "eco/dark-dialog-styled" ).toBool() )
    {
      dialog->setProperty( "eco/dark-dialog-styled", true );
      applyVsCodeDialogStyle( dialog );
    }
  }
  if ( event->type() == QEvent::Show && watched && watched->inherits( "QWidget" ) )
  {
    if ( QWidget *window = qobject_cast<QWidget *>( watched ); window && window->isWindow()
         && window->windowType() != Qt::Popup && window->windowType() != Qt::ToolTip && window->windowType() != Qt::SplashScreen )
      applyVsCodeNativeTitleBar( window );
  }
  if ( ( event->type() == QEvent::Show || event->type() == QEvent::ParentChange ) && watched && watched->inherits( "QgsAttributeTableDialog" ) )
  {
    if ( QWidget *dialog = qobject_cast<QWidget *>( watched ) )
    {
      styleBusinessAttributeTable( dialog );
      QTimer::singleShot( 0, dialog, [dialog] { styleBusinessAttributeTable( dialog ); } );
    }
  }
  if ( event->type() == QEvent::Show && watched && watched->objectName() == QLatin1String( "mUpdateExpressionBox" ) )
  {
    if ( QWidget *expressionBox = qobject_cast<QWidget *>( watched ) )
    {
      expressionBox->setMaximumHeight( 0 );
      expressionBox->hide();
    }
    return true;
  }
  if ( event->type() == QEvent::Show && watched && watched->inherits( "QCalendarWidget" ) )
  {
    if ( QCalendarWidget *calendar = qobject_cast<QCalendarWidget *>( watched ) )
    {
      styleEcoCalendarWidget( calendar );
      QTimer::singleShot( 0, calendar, [calendar] { styleEcoCalendarWidget( calendar ); } );
    }
  }
  if ( event->type() == QEvent::Show && watched && ( watched->objectName() == QLatin1String( "VertexEditor" ) || watched->inherits( "QgsVertexEditor" ) ) )
  {
    if ( QWidget *vertexEditor = qobject_cast<QWidget *>( watched ) )
      vertexEditor->hide();
    return true;
  }
  if ( event->type() == QEvent::Show && watched && watched->inherits( "QgsAttributeDialog" ) )
  {
    QWidget *dialog = qobject_cast<QWidget *>( watched );
    if ( dialog && !dialog->property( "eco/attribute-form-sized" ).toBool() )
    {
      dialog->setProperty( "eco/attribute-form-sized", true );
      dialog->setMinimumSize( QSize( 365, 300 ) );
      dialog->resize( 365, 300 );
      // Apply synchronously while the native window is being shown. A queued
      // style update made the dialog start with a light Windows caption and
      // switch to a dark caption only after the first drag/activation.
      for ( QGridLayout *gridLayout : dialog->findChildren<QGridLayout *>() )
      {
        gridLayout->setHorizontalSpacing( 10 );
        gridLayout->setVerticalSpacing( 7 );
        gridLayout->invalidate();
      }
    }
  }
  if ( watched == mApp->centralWidget() && event->type() == QEvent::Resize && mWelcomeOverlay )
    mWelcomeOverlay->setGeometry( mApp->centralWidget()->rect().adjusted( 10, 10, -10, -10 ) );
  if ( watched == mApp->layerTreeView() && event->type() == QEvent::Drop )
  {
    auto *dropEvent = static_cast<QDropEvent *>( event );
    const QModelIndex targetIndex = mApp->layerTreeView()->indexAt( dropEvent->position().toPoint() );
    QgsLayerTreeNode *targetNode = targetIndex.isValid() ? mApp->layerTreeView()->index2node( targetIndex ) : nullptr;
    if ( !targetNode || !isNodeInsideProject( targetNode ) )
    {
      dropEvent->ignore();
      return true;
    }
  }
  QgsLayerTreeView *layerTreeView = mApp ? mApp->layerTreeView() : nullptr;
  if ( layerTreeView && ( watched == layerTreeView || watched == layerTreeView->viewport() )
       && event->type() == QEvent::ContextMenu )
  {
    auto *contextEvent = static_cast<QContextMenuEvent *>( event );
    QPoint viewPoint = contextEvent->pos();
    if ( watched == layerTreeView )
      viewPoint = layerTreeView->viewport()->mapFrom( layerTreeView, viewPoint );
    const QModelIndex index = layerTreeView->indexAt( viewPoint );
    if ( index.isValid() )
    {
      if ( QItemSelectionModel *selection = layerTreeView->selectionModel() )
      {
        if ( !selection->isSelected( index ) )
          selection->select( index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
      }
      layerTreeView->setCurrentIndex( index );
    }
    if ( QgsLayerTreeViewMenuProvider *provider = layerTreeView->menuProvider() )
    {
      std::unique_ptr<QMenu> menu( provider->createContextMenu() );
      if ( menu && !menu->actions().isEmpty() )
      {
        emit layerTreeView->contextMenuAboutToShow( menu.get() );
        menu->exec( contextEvent->globalPos() );
      }
      return true;
    }
  }
  if ( layerTreeView && ( watched == layerTreeView || watched == layerTreeView->viewport() )
       && event->type() == QEvent::MouseButtonRelease
       && !mManualTowerDrawingLayerId.isEmpty() )
  {
    const auto *mouseEvent = static_cast<QMouseEvent *>( event );
    if ( mouseEvent->button() == Qt::LeftButton )
    {
      QPoint viewPoint = mouseEvent->position().toPoint();
      if ( watched == layerTreeView )
        viewPoint = layerTreeView->viewport()->mapFrom( layerTreeView, viewPoint );
      const QModelIndex index = layerTreeView->indexAt( viewPoint );
      if ( !index.isValid() )
        return false;
      QgsLayerTreeNode *node = layerTreeView->index2node( index );
      QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node );
      QgsVectorLayer *layer = layerNode ? qobject_cast<QgsVectorLayer *>( layerNode->layer() ) : nullptr;
      const QRect rowRect = layerTreeView->visualRect( index );
      const QRect finishRect( rowRect.right() - 24, rowRect.center().y() - 8, 16, 16 );
      if ( layer && layer->id() == mManualTowerDrawingLayerId && finishRect.contains( viewPoint ) )
      {
        finishManualTowerDrawing();
        return true;
      }
    }
  }
  if ( ( watched == mApp->layerTreeView() || watched == mApp->layerTreeView()->viewport() ) && event->type() == QEvent::MouseButtonDblClick )
  {
    const auto *mouseEvent = static_cast<QMouseEvent *>( event );
    const QModelIndex index = mApp->layerTreeView()->indexAt( mouseEvent->position().toPoint() );
    QgsLayerTreeNode *node = index.isValid() ? mApp->layerTreeView()->index2node( index ) : nullptr;
    QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node );
    QgsVectorLayer *layer = layerNode ? qobject_cast<QgsVectorLayer *>( layerNode->layer() ) : nullptr;
    if ( layer && layer->geometryType() == Qgis::GeometryType::Point )
    {
      showTowerStyleDialog( layer );
      return true;
    }
  }
  return QObject::eventFilter( watched, event );
}

void QgsEcoRestorationController::hideNativeQgisWidgets()
{
  if ( !mApp )
    return;
  const QList<QWidget *> widgets = mApp->findChildren<QWidget *>();
  for ( QWidget *widget : widgets )
  {
    if ( !widget )
      continue;
    const QString className = QString::fromLatin1( widget->metaObject()->className() );
    if ( className == QLatin1String( "QgsWelcomeScreen" )
         || className == QLatin1String( "QgsMessageBar" )
         || className == QLatin1String( "QgsLocatorWidget" )
         || widget->objectName() == QLatin1String( "VertexEditor" )
         || widget->objectName() == QLatin1String( "MessageLog" )
         || widget->objectName() == QLatin1String( "mMessageLogViewerButton" ) )
      widget->hide();
  }
}

void QgsEcoRestorationController::setCompactMode( bool enabled )
{
  if ( !mApp )
    return;
  const QSet<QString> keep = { QStringLiteral( "EcoRestorationToolBar" ) };
  const QSet<QString> keepDocks = {
    QStringLiteral( "EcoRestorationDock" ),
    QStringLiteral( "EcoPhotoProcessingDock" ),
    QStringLiteral( "EcoRecognitionResultDock" ),
    QStringLiteral( "EcoPhaseCompareLeftDock" ),
    QStringLiteral( "EcoPhaseCompareRightDock" ),
    QStringLiteral( "Layers" )
  };

  if ( enabled )
  {
    QPalette workbenchPalette = mApp->palette();
    for ( const QPalette::ColorGroup colorGroup : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
    {
      workbenchPalette.setColor( colorGroup, QPalette::Light, QColor( QStringLiteral( "#3c3c3c" ) ) );
      workbenchPalette.setColor( colorGroup, QPalette::Midlight, QColor( QStringLiteral( "#333333" ) ) );
      workbenchPalette.setColor( colorGroup, QPalette::Mid, QColor( QStringLiteral( "#2b2b2b" ) ) );
      workbenchPalette.setColor( colorGroup, QPalette::Dark, QColor( QStringLiteral( "#1e1e1e" ) ) );
      workbenchPalette.setColor( colorGroup, QPalette::Shadow, QColor( QStringLiteral( "#111111" ) ) );
    }
    mApp->setPalette( workbenchPalette );

    if ( mApp->centralWidget() && mApp->centralWidget()->layout() )
    {
      mApp->centralWidget()->layout()->setContentsMargins( 10, 10, 10, 10 );
      mApp->centralWidget()->layout()->setSpacing( 0 );
    }

    if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
    {
      canvas->setFrameShape( QFrame::NoFrame );
      canvas->setLineWidth( 0 );
      canvas->setMidLineWidth( 0 );
      canvas->setStyleSheet( QStringLiteral( "QgsMapCanvas#theMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; padding:0; }" ) );
      canvas->viewport()->setStyleSheet( QStringLiteral( "border:0;" ) );
      applyWorkbenchSelectionColor( canvas );
      if ( QWidget *canvasContainer = canvas->parentWidget() )
        canvasContainer->setStyleSheet( QStringLiteral( "border:0; background:#1e1e1e;" ) );
    }

    // This slot is invoked both during construction and after QGIS finishes
    // initialization. Keep the first list of hidden widgets so that toggling
    // the checkbox back to the native layout can restore every widget.
    const QList<QToolBar *> toolbars = mApp->findChildren<QToolBar *>();
    for ( QToolBar *toolbar : toolbars )
    {
      if ( toolbar && toolbar->isVisible() && !keep.contains( toolbar->objectName() ) )
      {
        mHiddenToolbars.append( toolbar );
        toolbar->hide();
      }
    }
    const QList<QDockWidget *> docks = mApp->findChildren<QDockWidget *>();
    for ( QDockWidget *dock : docks )
    {
      if ( dock && dock->isVisible() && !keepDocks.contains( dock->objectName() ) )
      {
        mHiddenDocks.append( dock );
        dock->hide();
      }
    }
    mApp->menuBar()->hide();
    // The status bar is intentionally retained for task progress and the
    // essential map readouts; all non-business toolbars remain hidden.
    mApp->statusBar()->show();
    mApp->statusBar()->setStyleSheet( QStringLiteral( R"(
      QStatusBar { background:#007acc; color:#ffffff; border:0; min-height:24px; }
      QStatusBar::item { border:0; }
      QStatusBar QLabel, QStatusBar QCheckBox, QStatusBar QToolButton { color:#ffffff; background:transparent; border:0; }
      QStatusBar QToolButton:hover, QStatusBar QCheckBox:hover { background:#1f8ad2; }
      QStatusBar QLineEdit, QStatusBar QComboBox, QStatusBar QSpinBox, QStatusBar QDoubleSpinBox { color:#ffffff; background:transparent; border:0; }
    )" ) );
    mToolbar->show();
    mDock->show();
    if ( QDockWidget *layerDock = mApp->findChild<QDockWidget *>( QStringLiteral( "Layers" ) ) )
    {
      layerDock->setWindowTitle( tr( "工程图层" ) );
      layerDock->setMinimumWidth( 270 );
      layerDock->setStyleSheet( QStringLiteral( R"(
        QDockWidget { color:#d4d4d4; background:#202020; border:1px solid #3c3c3c; margin:10px 0; }
        QDockWidget::title { background:#181818; color:#f1f5f9; padding:8px 38px 8px 8px; border:0; font-weight:600; }
        QDockWidget::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
        QDockWidget::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
        QDockWidget::close-button:pressed { background:#8f1d14; }
      )" ) );
      layerDock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
      installEcoDockTitleBar( layerDock );
      QPalette layerDockPalette = layerDock->palette();
      for ( const QPalette::ColorGroup colorGroup : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
      {
        layerDockPalette.setColor( colorGroup, QPalette::WindowText, QColor( QStringLiteral( "#f1f5f9" ) ) );
        layerDockPalette.setColor( colorGroup, QPalette::Text, QColor( QStringLiteral( "#f1f5f9" ) ) );
      }
      layerDock->setPalette( layerDockPalette );
      if ( QgsLayerTreeView *layerTree = mApp->layerTreeView() )
      {
        // Keep layer rows visible, but let the dedicated dark workbench view
        // own the foreground colors and default legend presentation.
        layerTree->layerTreeModel()->setFlag( QgsLayerTreeModel::UseTextFormatting, false );
        layerTree->layerTreeModel()->setFlag( QgsLayerTreeModel::ShowLegend, false );
        QPalette palette = layerTree->palette();
        for ( const QPalette::ColorGroup colorGroup : { QPalette::Active, QPalette::Inactive, QPalette::Disabled } )
        {
          palette.setColor( colorGroup, QPalette::Text, QColor( QStringLiteral( "#f1f5f9" ) ) );
          palette.setColor( colorGroup, QPalette::WindowText, QColor( QStringLiteral( "#f1f5f9" ) ) );
          palette.setColor( colorGroup, QPalette::HighlightedText, QColor( Qt::white ) );
        }
        layerTree->setPalette( palette );
        layerTree->setStyleSheet( QStringLiteral( R"(
          QgsLayerTreeView { background:#202020; color:#f1f5f9; border:0; outline:0; padding:4px; }
          QgsLayerTreeView::item { min-height:24px; color:#f1f5f9; border-radius:3px; }
          QgsLayerTreeView::item:selected { background:#0e639c; color:white; }
          QgsLayerTreeView::item:hover { background:#2d333b; color:#ffffff; }
          QgsLayerTreeView::item:disabled { color:#94a3b8; }
          QgsLayerTreeView::indicator, QTreeView::indicator { width:14px; height:14px; border:1px solid #6b6b6b; border-radius:2px; background:#1e1e1e; }
          QgsLayerTreeView::indicator:hover, QTreeView::indicator:hover { border-color:#c5c5c5; background:#1e1e1e; }
          QgsLayerTreeView::indicator:checked, QTreeView::indicator:checked { border-color:#007acc; background:#007acc; }
          QgsLayerTreeView::indicator:indeterminate, QTreeView::indicator:indeterminate { border-color:#007acc; background:#007acc; }
        )" ) );
      }
      mApp->addDockWidget( Qt::LeftDockWidgetArea, layerDock );
      layerDock->show();
    }
    mApp->mapCanvas()->setCanvasColor( QColor( QStringLiteral( "#1e1e1e" ) ) );
    hideNativeQgisWidgets();
    QTimer::singleShot( 1200, this, [this] { hideNativeQgisWidgets(); } );
    mApp->setWindowTitle( tr( "江河 路 遥感解译" ) );
    updateWelcomeOverlay();
  }
  else
  {
    for ( const QPointer<QToolBar> &toolbar : std::as_const( mHiddenToolbars ) )
    {
      if ( toolbar )
        toolbar->show();
    }
    mHiddenToolbars.clear();
    for ( const QPointer<QDockWidget> &dock : std::as_const( mHiddenDocks ) )
    {
      if ( dock )
        dock->show();
    }
    mHiddenDocks.clear();
    mApp->menuBar()->show();
    mApp->statusBar()->show();
    if ( QgsLayerTreeView *layerTree = mApp->layerTreeView() )
    {
      layerTree->layerTreeModel()->setFlag( QgsLayerTreeModel::UseTextFormatting, true );
      layerTree->layerTreeModel()->setFlag( QgsLayerTreeModel::ShowLegend, true );
      layerTree->setStyleSheet( QString() );
    }
    mApp->setWindowTitle( tr( "QGIS" ) );
    updateWelcomeOverlay();
  }
}

void QgsEcoRestorationController::setThreeDMode( bool enabled )
{
  if ( !mApp )
    return;
  if ( !enabled )
  {
    if ( !mThreeDViewName.isEmpty() )
    {
      if ( mApp->get3DMapView( mThreeDViewName ) )
        mApp->close3DMapView( mThreeDViewName );
      mThreeDViewName.clear();
    }
    mApp->mapCanvas()->setFocus();
    return;
  }

  // QGIS requires a finite project extent to initialize a 3D scene. Give the
  // user a business-facing message and leave the switch in 2D when the
  // project is still empty, rather than exposing the native English warning.
  const QgsRectangle projectExtent = mApp->mapCanvas()->fullExtent();
  if ( QgsProject::instance()->mapLayers().isEmpty() || projectExtent.isEmpty() || !projectExtent.isFinite() )
  {
    {
      QSignalBlocker blocker( mThreeDSwitch );
      mThreeDSwitch->setChecked( false );
      mThreeDSwitch->setText( tr( "二维" ) );
    }
    showMessage( tr( "三维地图" ), tr( "当前工程还没有可用图层范围，请先加载 TIF 或矢量数据后再切换三维。" ), true );
    return;
  }

  const QString viewName = QStringLiteral( "遥感解译三维" );
  Qgs3DMapCanvas *canvas = mApp->createNewMapCanvas3D( viewName, Qgis::SceneMode::Local );
  if ( !canvas )
  {
    QSignalBlocker blocker( mThreeDSwitch );
    mThreeDSwitch->setChecked( false );
    mThreeDSwitch->setText( tr( "二维" ) );
    return;
  }
  mThreeDViewName = viewName;
  if ( Qgs3DMapCanvasWidget *widget = mApp->get3DMapView( viewName ) )
  {
    if ( widget->dockableWidgetHelper() && widget->dockableWidgetHelper()->dockWidget() )
    {
      widget->dockableWidgetHelper()->dockWidget()->show();
      widget->dockableWidgetHelper()->dockWidget()->raise();
    }
  }
}

void QgsEcoRestorationController::ensureBusinessGroups()
{
  QgsProject *project = QgsProject::instance();
  QgsLayerTreeGroup *root = project->layerTreeRoot();
  QgsLayerTreeGroup *projectGroup = projectTreeRoot();
  if ( !projectGroup )
  {
    QString projectName = project->readEntry( sProjectGroup, QStringLiteral( "name" ) );
    if ( projectName.isEmpty() )
      projectName = project->title();
    if ( projectName.isEmpty() )
      projectName = tr( "未命名工程" );
    projectGroup = root->addGroup( tr( "工程 路 %1" ).arg( projectName ) );
    projectGroup->setCustomProperty( sProjectRootProperty, true );
    projectGroup->setExpanded( true );
  }
  projectGroup->setCustomProperty( sProjectRootProperty, true );
  projectGroup->setCustomProperty( sGroupTypeProperty, sProjectRootGroupName );
  projectGroup->setCustomProperty( sTreeIconProperty, QStringLiteral( "project" ) );

  QString projectName = project->readEntry( sProjectGroup, QStringLiteral( "name" ) );
  if ( projectName.isEmpty() )
    projectName = project->title();
  if ( !projectName.isEmpty() )
    projectGroup->setName( tr( "工程 路 %1" ).arg( projectName ) );

  commonDataGroup( true );
  for ( const QString &phaseId : phaseIds() )
    phaseGroup( phaseId, true );

  organizeProjectTree();

  if ( QgsLayerTreeGroup *commonGroup = commonDataGroup( false ) )
  {
    if ( commonGroup->parent() == projectGroup )
      appendNodeToGroupSafely( projectGroup, commonGroup, projectGroup );
  }

  for ( const QString &phaseId : phaseIds() )
  {
    if ( QgsLayerTreeGroup *group = phaseGroup( phaseId, false ) )
    {
      if ( group->parent() == projectGroup )
        appendNodeToGroupSafely( projectGroup, group, projectGroup );
    }
  }
}

QgsLayerTreeGroup *QgsEcoRestorationController::projectTreeRoot() const
{
  QgsLayerTreeGroup *root = QgsProject::instance()->layerTreeRoot();
  for ( QgsLayerTreeNode *node : root->children() )
  {
    if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ) )
    {
      if ( group->customProperty( sProjectRootProperty ).toBool() )
        return group;
    }
  }
  return nullptr;
}

QString QgsEcoRestorationController::ensureCurrentPhase()
{
  const QString current = currentPhaseId();
  if ( !current.isEmpty() )
    return current;

  const QStringList ids = phaseIds();
  if ( !ids.isEmpty() )
  {
    setCurrentPhase( ids.constFirst() );
    return ids.constFirst();
  }

  QString defaultName = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ) ).trimmed();
  if ( defaultName.isEmpty() )
    defaultName = tr( "绗?期" );
  return createPhaseInternal( defaultName, true );
}

QString QgsEcoRestorationController::currentPhaseId() const
{
  const QString id = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ) ).trimmed();
  return phaseIds().contains( id ) ? id : QString();
}

QString QgsEcoRestorationController::currentPhaseName( const QString &phaseId ) const
{
  QString id = phaseId;
  if ( id.isEmpty() )
    id = currentPhaseId();
  if ( id.isEmpty() )
    return tr( "未指定期次" );

  QString name = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "phase/%1/name" ).arg( id ) ).trimmed();
  if ( name.isEmpty() )
  {
    QgsLayerTreeGroup *group = nullptr;
    if ( QgsLayerTreeGroup *projectGroup = projectTreeRoot() )
      group = directChildGroupByProperty( projectGroup, sPhaseIdProperty, id );
    if ( group )
      name = group->customProperty( sPhaseNameProperty ).toString().trimmed();
  }
  return name.isEmpty() ? id : name;
}

QStringList QgsEcoRestorationController::phaseIds() const
{
  bool ok = false;
  QStringList ids = QgsProject::instance()->readListEntry( sProjectGroup, QStringLiteral( "phaseIds" ), QStringList(), &ok );
  if ( !ok )
    ids.clear();

  const QString current = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ) ).trimmed();
  if ( !current.isEmpty() && !ids.contains( current ) )
    ids.prepend( current );

  ids.removeAll( QString() );
  ids.removeDuplicates();
  return ids;
}

QString QgsEcoRestorationController::phaseWorkspace( const QString &phaseId ) const
{
  QString id = phaseId;
  if ( id.isEmpty() )
    id = currentPhaseId();
  if ( id.isEmpty() )
    id = QStringLiteral( "phase_default" );

  QString path = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "phase/%1/workspace" ).arg( id ) ).trimmed();
  if ( !path.isEmpty() )
    return path;

  const QString workspacePath = projectWorkspace();
  if ( workspacePath.isEmpty() )
    return QString();
  return QDir( workspacePath ).filePath( QStringLiteral( "phases/%1" ).arg( id ) );
}

QgsLayerTreeGroup *QgsEcoRestorationController::commonDataGroup( bool create ) const
{
  QgsLayerTreeGroup *projectGroup = projectTreeRoot();
  if ( !projectGroup )
    return nullptr;

  QgsLayerTreeGroup *group = directChildGroupByProperty( projectGroup, sGroupTypeProperty, sCommonGroup );
  if ( !group )
    group = directChildGroup( projectGroup, sCommonGroup );
  if ( !group && create )
    group = projectGroup->addGroup( sCommonGroup );
  if ( !group )
    return nullptr;

  group->setName( sCommonGroup );
  group->setCustomProperty( sGroupTypeProperty, sCommonGroup );
  group->setCustomProperty( sTreeIconProperty, QStringLiteral( "common" ) );
  group->setExpanded( true );
  group->setItemVisibilityChecked( true );

  if ( create )
  {
    QgsLayerTreeGroup *lineGroup = directChildGroupByProperty( group, sGroupTypeProperty, QStringLiteral( "line-data" ) );
    if ( !lineGroup )
      lineGroup = directChildGroup( group, sLineGroup );
    if ( !lineGroup )
      lineGroup = group->insertGroup( 0, sLineGroup );
    if ( lineGroup )
    {
      lineGroup->setName( sLineGroup );
      lineGroup->setCustomProperty( sGroupTypeProperty, QStringLiteral( "line-data" ) );
      lineGroup->setCustomProperty( sTreeIconProperty, QStringLiteral( "line" ) );
      lineGroup->setExpanded( true );
      lineGroup->setItemVisibilityChecked( true );
    }

    QgsLayerTreeGroup *scopeGroup = directChildGroupByProperty( group, sGroupTypeProperty, QStringLiteral( "scope-data" ) );
    if ( !scopeGroup )
      scopeGroup = directChildGroup( group, sScopeGroup );
    if ( !scopeGroup )
      scopeGroup = group->insertGroup( lineGroup ? 1 : 0, sScopeGroup );
    if ( scopeGroup )
    {
      scopeGroup->setName( sScopeGroup );
      scopeGroup->setCustomProperty( sGroupTypeProperty, QStringLiteral( "scope-data" ) );
      scopeGroup->setCustomProperty( sTreeIconProperty, QStringLiteral( "scope" ) );
      scopeGroup->setExpanded( true );
      scopeGroup->setItemVisibilityChecked( true );
    }
  }
  return group;
}

QgsLayerTreeGroup *QgsEcoRestorationController::phaseGroup( const QString &phaseId, bool create ) const
{
  if ( phaseId.isEmpty() )
    return nullptr;

  QgsLayerTreeGroup *projectGroup = projectTreeRoot();
  if ( !projectGroup )
    return nullptr;

  const QString phaseName = currentPhaseName( phaseId );
  const QString visibleName = sPhasePrefix + phaseName;
  QgsLayerTreeGroup *group = directChildGroupByProperty( projectGroup, sPhaseIdProperty, phaseId );
  if ( !group )
    group = directChildGroup( projectGroup, visibleName );
  if ( !group && create )
    group = projectGroup->addGroup( visibleName );
  if ( !group )
    return nullptr;

  group->setName( visibleName );
  group->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase" ) );
  group->setCustomProperty( sPhaseIdProperty, phaseId );
  group->setCustomProperty( sPhaseNameProperty, phaseName );
  group->setCustomProperty( sTreeIconProperty, QStringLiteral( "phase" ) );
  group->setExpanded( true );

  if ( create )
  {
    if ( !directChildGroup( group, sPhaseResultsGroup ) )
      group->addGroup( sPhaseResultsGroup );
    if ( !directChildGroup( group, sPhaseScreenshotsGroup ) )
      group->addGroup( sPhaseScreenshotsGroup );
    if ( !directChildGroup( group, sImageryGroup ) )
      group->addGroup( sImageryGroup );
    if ( !mLayerImportInProgress )
      ensureChildGroupOrder( group, { sPhaseResultsGroup, sPhaseScreenshotsGroup, sImageryGroup } );
    if ( QgsLayerTreeGroup *screenshots = directChildGroup( group, sPhaseScreenshotsGroup ) )
    {
      screenshots->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase-screenshots" ) );
      screenshots->setCustomProperty( sTreeIconProperty, QStringLiteral( "screenshots" ) );
    }
    if ( QgsLayerTreeGroup *imagery = directChildGroup( group, sImageryGroup ) )
    {
      imagery->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase-imagery" ) );
      imagery->setCustomProperty( sTreeIconProperty, QStringLiteral( "imagery" ) );
    }

    if ( QgsLayerTreeGroup *results = directChildGroup( group, sPhaseResultsGroup ) )
    {
      results->setCustomProperty( sGroupTypeProperty, QStringLiteral( "phase-results" ) );
      results->setCustomProperty( sTreeIconProperty, QStringLiteral( "results" ) );
      results->setExpanded( true );
      if ( !directChildGroup( results, sRecognitionGroup ) )
        results->addGroup( sRecognitionGroup );
      
      if ( !directChildGroup( results, sSmartSegmentationGroup ) )
        results->addGroup( sSmartSegmentationGroup );
      if ( QgsLayerTreeGroup *recognition = directChildGroup( results, sRecognitionGroup ) )
        recognition->setCustomProperty( sTreeIconProperty, QStringLiteral( "disturbance" ) );
      
      if ( QgsLayerTreeGroup *segmentation = directChildGroup( results, sSmartSegmentationGroup ) )
        segmentation->setCustomProperty( sTreeIconProperty, QStringLiteral( "segmentation" ) );
      if ( !mLayerImportInProgress )
        ensureChildGroupOrder( results, { sRecognitionGroup, sSmartSegmentationGroup } );
    }
  }
  return group;
}

QgsLayerTreeGroup *QgsEcoRestorationController::phaseSubGroup( const QString &phaseId, const QString &groupName, bool create ) const
{
  QgsLayerTreeGroup *phase = phaseGroup( phaseId, create );
  if ( !phase )
    return nullptr;
  QgsLayerTreeGroup *group = directChildGroup( phase, groupName );
  if ( !group && create )
    group = phase->addGroup( groupName );
  if ( group )
  {
    group->setCustomProperty( sPhaseIdProperty, phaseId );
    if ( groupName == sImageryGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "imagery" ) );
    else if ( groupName == sPhaseScreenshotsGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "screenshots" ) );
    else if ( groupName == sPhaseResultsGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "results" ) );
  }
  return group;
}

QgsLayerTreeGroup *QgsEcoRestorationController::phaseResultGroup( const QString &phaseId, const QString &groupName, bool create ) const
{
  QgsLayerTreeGroup *results = phaseSubGroup( phaseId, sPhaseResultsGroup, create );
  if ( !results )
    return nullptr;
  const QString resolvedGroupName = groupName == sReviewGroup ? sRecognitionGroup : groupName;
  QgsLayerTreeGroup *group = directChildGroup( results, resolvedGroupName );
  if ( !group && create )
    group = results->addGroup( resolvedGroupName );
  if ( group )
  {
    group->setCustomProperty( sPhaseIdProperty, phaseId );
    if ( resolvedGroupName == sRecognitionGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "disturbance" ) );
    else if ( resolvedGroupName == sReviewGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "review" ) );
    else if ( resolvedGroupName == sSmartSegmentationGroup )
      group->setCustomProperty( sTreeIconProperty, QStringLiteral( "segmentation" ) );
  }
  if ( !mLayerImportInProgress )
    ensureChildGroupOrder( results, { sRecognitionGroup, sSmartSegmentationGroup } );
  return group;
}

QString QgsEcoRestorationController::createPhaseInternal( const QString &phaseName, bool makeCurrent )
{
  ecoImportTrace( QStringLiteral( "createPhaseInternal entered phaseName=%1 makeCurrent=%2 transition=%3" )
                    .arg( phaseName )
                    .arg( makeCurrent ? 1 : 0 )
                    .arg( mProjectTransitionInProgress ? 1 : 0 ) );
  const QString workspacePath = projectWorkspace();
  if ( workspacePath.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "createPhaseInternal aborted empty workspace" ) );
    return QString();
  }

  const bool startedTransition = makeCurrent && !mProjectTransitionInProgress;
  if ( startedTransition )
    mProjectTransitionInProgress = true;
  EcoScopeGuard transitionGuard( [this, startedTransition] {
    if ( !startedTransition )
      return;
    mProjectTransitionInProgress = false;
    scheduleProjectStateRefresh();
    ecoImportTrace( QStringLiteral( "createPhaseInternal transition complete" ) );
  } );

  QgsProject *project = QgsProject::instance();
  QString name = phaseName.trimmed();
  if ( name.isEmpty() )
    name = tr( "绗?1期" ).arg( phaseIds().size() + 1 );

  QString id = QStringLiteral( "phase_%1" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss_zzz" ) ) );
  QStringList ids = phaseIds();
  int suffix = 1;
  while ( ids.contains( id ) )
    id = QStringLiteral( "phase_%1_%2" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss_zzz" ) ) ).arg( suffix++ );
  ids.append( id );

  QDir workspace( workspacePath );
  workspace.mkpath( QStringLiteral( "phases/%1/imagery" ).arg( id ) );
  workspace.mkpath( QStringLiteral( "phases/%1/results/disturbance" ).arg( id ) );
  workspace.mkpath( QStringLiteral( "phases/%1/results/review" ).arg( id ) );
  workspace.mkpath( QStringLiteral( "phases/%1/results/smart_segmentation" ).arg( id ) );
  workspace.mkpath( QStringLiteral( "phases/%1/screenshots" ).arg( id ) );
  workspace.mkpath( QStringLiteral( "phases/%1/photos/fused" ).arg( id ) );

  project->writeEntry( sProjectGroup, QStringLiteral( "phaseIds" ), ids );
  project->writeEntry( sProjectGroup, QStringLiteral( "phase/%1/name" ).arg( id ), name );
  project->writeEntry( sProjectGroup, QStringLiteral( "phase/%1/workspace" ).arg( id ), workspace.filePath( QStringLiteral( "phases/%1" ).arg( id ) ) );
  project->writeEntry( sProjectGroup, QStringLiteral( "phase/%1/createdAt" ).arg( id ), QDateTime::currentDateTime().toString( Qt::ISODate ) );

  if ( makeCurrent )
  {
    project->writeEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ), id );
    project->writeEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ), name );
  }

  if ( makeCurrent )
  {
    ecoImportTrace( QStringLiteral( "createPhaseInternal defer current phase apply id=%1" ).arg( id ) );
  }
  else
  {
    ecoImportTrace( QStringLiteral( "createPhaseInternal refreshPhaseChoices only id=%1" ).arg( id ) );
    ensureBusinessGroups();
    refreshPhaseChoices();
  }

  project->setDirty( true );
  ecoImportTrace( QStringLiteral( "createPhaseInternal finished id=%1 makeCurrent=%2" ).arg( id ).arg( makeCurrent ? 1 : 0 ) );
  return id;
}

void QgsEcoRestorationController::createPhase()
{
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "识别期间暂不能新增期次，请在任务完成后操作。" ), true, 3200 );
    return;
  }

  bool ok = false;
  const bool enabled = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &ok );
  if ( !ok || !enabled || QgsProject::instance()->fileName().isEmpty() )
  {
    showMessage( tr( "新建期次" ), tr( "请先新建并保存遥感解译工程。" ), true );
    return;
  }

  QDialog dialog( mApp );
  dialog.setWindowTitle( tr( "新建期次" ) );
  dialog.setMinimumWidth( 380 );
  applyVsCodeDialogStyle( &dialog );
  QVBoxLayout *layout = new QVBoxLayout( &dialog );
  layout->setContentsMargins( 16, 14, 16, 14 );
  layout->setSpacing( 10 );
  QLabel *hint = new QLabel( tr( "期次用于归档一批影像及其对应的解译成果。" ), &dialog );
  hint->setWordWrap( true );
  hint->setProperty( "muted", true );
  layout->addWidget( hint );
  QFormLayout *form = new QFormLayout;
  QLineEdit *nameEdit = new QLineEdit( tr( "绗?1期" ).arg( phaseIds().size() + 1 ), &dialog );
  form->addRow( tr( "期次名称" ), nameEdit );
  layout->addLayout( form );
  QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
  if ( QPushButton *okButton = buttons->button( QDialogButtonBox::Ok ) )
    okButton->setText( tr( "创建" ) );
  if ( QPushButton *cancelButton = buttons->button( QDialogButtonBox::Cancel ) )
    cancelButton->setText( tr( "取消" ) );
  layout->addWidget( buttons );
  connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );

  if ( dialog.exec() != QDialog::Accepted )
    return;
  const QString name = nameEdit->text().trimmed();
  if ( name.isEmpty() )
    return;
  const QString id = createPhaseInternal( name, true );
  if ( !id.isEmpty() )
    showEcoToast( mApp, tr( "期次已创建" ), tr( "已创建并切换到：%1" ).arg( name ), false, 3000 );
}

void QgsEcoRestorationController::deletePhase( const QString &phaseId )
{
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "识别期间暂不能删除期次，请在任务完成后操作。" ), true, 3200 );
    return;
  }

  QStringList ids = phaseIds();
  if ( phaseId.isEmpty() || !ids.contains( phaseId ) )
    return;

  if ( ids.size() <= 1 )
  {
    if ( mApp && mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "当前工程至少需要保留一个期次。" ), 3500 );
    return;
  }

  const QString phaseName = currentPhaseName( phaseId );
  const QString phaseDirectoryPath = phaseWorkspace( phaseId );
  QgsProject *project = QgsProject::instance();
  QStringList layerIds;

  if ( QgsLayerTreeGroup *group = phaseGroup( phaseId, false ) )
  {
    const QList<QgsLayerTreeLayer *> nodes = group->findLayers();
    for ( QgsLayerTreeLayer *node : nodes )
    {
      if ( node && node->layer() )
        layerIds << node->layer()->id();
    }
  }

  const auto layers = project->mapLayers();
  for ( QgsMapLayer *layer : layers )
  {
    if ( layer && layer->customProperty( sPhaseIdProperty ).toString() == phaseId )
      layerIds << layer->id();
  }
  layerIds.removeDuplicates();

  QMessageBox confirmation( QMessageBox::Question,
                            tr( "删除期次" ),
                            tr( "确定删除期次?1”吗？" ).arg( phaseName ),
                            QMessageBox::Yes | QMessageBox::No,
                            mApp );
  confirmation.setInformativeText( tr( "该期次下的影像、解译成果、智能分割成果和截图索引会从当前工程中移除，并删除工程目录内该期次的文件；外部原?TIF/SHP 文件不会被删除。" ) );
  confirmation.setDefaultButton( QMessageBox::No );
  applyVsCodeDialogStyle( &confirmation );
  if ( confirmation.exec() != QMessageBox::Yes )
    return;

  if ( !layerIds.isEmpty() )
    project->removeMapLayers( layerIds );

  bool phaseDirectoryRemoveFailed = false;
  if ( !phaseDirectoryPath.isEmpty() )
  {
    const QString phasesRootPath = QDir::cleanPath( QDir::fromNativeSeparators( QDir( projectWorkspace() ).absoluteFilePath( QStringLiteral( "phases" ) ) ) );
    const QString phasePath = QDir::cleanPath( QDir::fromNativeSeparators( QDir( phaseDirectoryPath ).absolutePath() ) );
    if ( !phasesRootPath.isEmpty() && phasePath.startsWith( phasesRootPath + QLatin1Char( '/' ) ) && phasePath != phasesRootPath )
    {
      QDir phaseDir( phasePath );
      if ( phaseDir.exists() && !phaseDir.removeRecursively() )
        phaseDirectoryRemoveFailed = true;
    }
  }

  if ( QgsLayerTreeGroup *group = phaseGroup( phaseId, false ) )
  {
    if ( QgsLayerTreeGroup *parent = qobject_cast<QgsLayerTreeGroup *>( group->parent() ) )
      parent->removeChildNode( group );
  }

  ids.removeAll( phaseId );
  project->writeEntry( sProjectGroup, QStringLiteral( "phaseIds" ), ids );
  project->removeEntry( sProjectGroup, QStringLiteral( "phase/%1" ).arg( phaseId ) );
  if ( project->readEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ) ).trimmed() == phaseId )
  {
    project->writeEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ), ids.constFirst() );
    project->writeEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ), currentPhaseName( ids.constFirst() ) );
  }

  if ( layerIds.contains( mRecognitionResultLayerId ) )
    mRecognitionResultLayerId.clear();
  mRecognitionEditingTowerLabel.clear();

  ensureBusinessGroups();
  refreshPhaseChoices();
  refreshLayerChoices();
  syncBusinessProjectView();
  if ( mRecognitionPreviewActive )
    updateRecognitionPreview();
  if ( mApp && mApp->statusBar() )
  {
    mApp->statusBar()->showMessage( phaseDirectoryRemoveFailed
                                      ? tr( "已删除期次：%1；工程目录中部分文件未能删除，请稍后手动清理。" ).arg( phaseName )
                                      : tr( "已删除期次：%1" ).arg( phaseName ),
                                    5000 );
  }
  project->setDirty( true );
}

void QgsEcoRestorationController::setCurrentPhase( const QString &phaseId )
{
  ecoImportTrace( QStringLiteral( "setCurrentPhase entered phaseId=%1 transition=%2" ).arg( phaseId ).arg( mProjectTransitionInProgress ? 1 : 0 ) );
  if ( mRecognitionRunning )
  {
    showEcoToast( mApp, tr( "智能识别进行中" ), tr( "当前识别任务仍在使用选定影像，期次切换将在任务完成后恢复。" ), true, 2800 );
    return;
  }

  if ( phaseId.isEmpty() || !phaseIds().contains( phaseId ) )
    return;

  QgsProject *project = QgsProject::instance();
  const QString phaseName = currentPhaseName( phaseId );
  project->writeEntry( sProjectGroup, QStringLiteral( "currentPhaseId" ), phaseId );
  project->writeEntry( sProjectGroup, QStringLiteral( "inspectionPhase" ), phaseName );

  if ( mProjectTransitionInProgress )
  {
    ecoImportTrace( QStringLiteral( "setCurrentPhase deferred during transition phaseId=%1" ).arg( phaseId ) );
    project->setDirty( true );
    return;
  }
  ecoImportTrace( QStringLiteral( "setCurrentPhase before ensureBusinessGroups phaseId=%1" ).arg( phaseId ) );
  ensureBusinessGroups();
  ecoImportTrace( QStringLiteral( "setCurrentPhase after ensureBusinessGroups phaseId=%1" ).arg( phaseId ) );
  if ( QgsLayerTreeGroup *common = commonDataGroup( false ) )
    common->setItemVisibilityChecked( true );
  for ( const QString &id : phaseIds() )
  {
    if ( QgsLayerTreeGroup *group = phaseGroup( id, false ) )
    {
      group->setItemVisibilityChecked( true );
      group->setExpanded( true );
    }
  }

  ecoImportTrace( QStringLiteral( "setCurrentPhase before refreshPhaseChoices phaseId=%1" ).arg( phaseId ) );
  refreshPhaseChoices();
  refreshLayerChoices();
  ecoImportTrace( QStringLiteral( "setCurrentPhase after refreshLayerChoices phaseId=%1" ).arg( phaseId ) );
  if ( mRecognitionPreviewActive )
    updateRecognitionPreview();
  if ( mApp && mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
  project->setDirty( true );
}

void QgsEcoRestorationController::showPhaseComparisonDialog()
{
  const QStringList ids = phaseIds();
  if ( ids.size() < 2 )
  {
    showMessage( tr( "期次对比" ), tr( "当前工程至少需要两个期次后，才能打开左右对比地图。" ) );
    return;
  }

  QDialog dialog( mApp );
  dialog.setWindowTitle( tr( "期次对比" ) );
  dialog.setMinimumWidth( 420 );
  applyVsCodeDialogStyle( &dialog );
  QVBoxLayout *layout = new QVBoxLayout( &dialog );
  layout->setContentsMargins( 16, 14, 16, 14 );
  layout->setSpacing( 10 );
  QLabel *hint = new QLabel( tr( "左、右地图会同时显示工程公共杆?线路，并分别叠加所选期次的影像和解译成果。" ), &dialog );
  hint->setWordWrap( true );
  hint->setProperty( "muted", true );
  layout->addWidget( hint );

  QFormLayout *form = new QFormLayout;
  QComboBox *leftCombo = new QComboBox( &dialog );
  QComboBox *rightCombo = new QComboBox( &dialog );
  for ( const QString &id : ids )
  {
    const QString name = currentPhaseName( id );
    leftCombo->addItem( name, id );
    rightCombo->addItem( name, id );
  }
  const QString current = currentPhaseId();
  int rightIndex = rightCombo->findData( current );
  if ( rightIndex < 0 )
    rightIndex = ids.size() - 1;
  rightCombo->setCurrentIndex( rightIndex );
  leftCombo->setCurrentIndex( std::max( 0, rightIndex - 1 ) );
  form->addRow( tr( "左侧期次" ), leftCombo );
  form->addRow( tr( "右侧期次" ), rightCombo );
  layout->addLayout( form );

  QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
  if ( QPushButton *okButton = buttons->button( QDialogButtonBox::Ok ) )
    okButton->setText( tr( "打开对比" ) );
  if ( QPushButton *cancelButton = buttons->button( QDialogButtonBox::Cancel ) )
    cancelButton->setText( tr( "取消" ) );
  layout->addWidget( buttons );
  connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );

  if ( dialog.exec() != QDialog::Accepted )
    return;

  const QString leftPhaseId = leftCombo->currentData().toString();
  const QString rightPhaseId = rightCombo->currentData().toString();
  if ( leftPhaseId.isEmpty() || rightPhaseId.isEmpty() )
    return;
  openPhaseComparison( leftPhaseId, rightPhaseId );
}

void QgsEcoRestorationController::openPhaseComparison( const QString &leftPhaseId, const QString &rightPhaseId )
{
  if ( !mApp || !mApp->mapCanvas() )
    return;

  {
    const quint64 comparisonSession = ++mPhaseCompareSession;
    // Close comparison views created by earlier versions which used QGIS dock
    // widgets. The business comparison view below is a dedicated two-map
    // central page, so the main map is no longer shown as a third map.
    mApp->closeMapCanvas( QStringLiteral( "EcoPhaseCompareLeft" ) );
    mApp->closeMapCanvas( QStringLiteral( "EcoPhaseCompareRight" ) );

    QStackedWidget *centralStack = qobject_cast<QStackedWidget *>( mApp->mapCanvas()->parentWidget() );
    if ( !centralStack )
      return;

    if ( mPhaseComparePage )
    {
      centralStack->removeWidget( mPhaseComparePage );
      mPhaseComparePage->deleteLater();
      mPhaseComparePage = nullptr;
      mPhaseCompareLeftDock = nullptr;
      mPhaseCompareRightDock = nullptr;
    }

    QWidget *page = new QWidget( centralStack );
    page->setObjectName( QStringLiteral( "EcoPhaseComparePage" ) );
    page->setStyleSheet( QStringLiteral( R"(
      QWidget#EcoPhaseComparePage { background:#1e1e1e; color:#d4d4d4; }
      QWidget#EcoPhaseCompareHeader { background:#181818; border:1px solid #3c3c3c; border-radius:4px; }
      QLabel#EcoPhaseCompareTitle { color:#f1f5f9; font-weight:600; }
      QLabel#EcoPhaseCompareHint { color:#9fb1c7; }
      QToolButton[compareAction="true"] { color:#d4d4d4; background:transparent; border:1px solid transparent; border-radius:3px; padding:4px 10px; }
      QToolButton[compareAction="true"]:hover { color:#ffffff; background:#2a2d2e; border-color:#454545; }
      QToolButton[compareAction="true"]:checked { color:#ffffff; background:#0e639c; border-color:#1177bb; }
      QWidget#EcoPhaseSwipeHandle { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #22d3ee,stop:0.42 #e0f2fe,stop:0.5 #ffffff,stop:0.58 #f0abfc,stop:1 #d946ef); border:1px solid #ffffff; border-radius:3px; }
      QSplitter::handle:horizontal { background:#3c3c3c; width:5px; border:0; }
      QSplitter::handle:horizontal:hover { background:#4a4a4a; }
    )" ) );

    QVBoxLayout *pageLayout = new QVBoxLayout( page );
    pageLayout->setContentsMargins( 0, 0, 0, 0 );
    pageLayout->setSpacing( 8 );

    QWidget *header = new QWidget( page );
    header->setObjectName( QStringLiteral( "EcoPhaseCompareHeader" ) );
    QHBoxLayout *headerLayout = new QHBoxLayout( header );
    headerLayout->setContentsMargins( 12, 7, 8, 7 );
    headerLayout->setSpacing( 10 );
    QLabel *titleLabel = new QLabel( tr( "期次影像对比" ), header );
    titleLabel->setObjectName( QStringLiteral( "EcoPhaseCompareTitle" ) );
    QLabel *hintLabel = new QLabel( tr( "%1  /  %2" ).arg( currentPhaseName( leftPhaseId ), currentPhaseName( rightPhaseId ) ), header );
    hintLabel->setObjectName( QStringLiteral( "EcoPhaseCompareHint" ) );
    QToolButton *exitButton = new QToolButton( header );
    exitButton->setObjectName( QStringLiteral( "EcoPhaseCompareExit" ) );
    exitButton->setText( tr( "退出对比" ) );
    exitButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
    exitButton->setCursor( Qt::PointingHandCursor );
    exitButton->setProperty( "compareAction", true );
    QToolButton *swapButton = new QToolButton( header );
    swapButton->setText( tr( "左右互换" ) );
    swapButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
    swapButton->setCursor( Qt::PointingHandCursor );
    swapButton->setProperty( "compareAction", true );
    QToolButton *swipeButton = new QToolButton( header );
    swipeButton->setText( tr( "卷帘模式" ) );
    swipeButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
    swipeButton->setCursor( Qt::PointingHandCursor );
    swipeButton->setCheckable( true );
    swipeButton->setProperty( "compareAction", true );
    QToolButton *diffButton = new QToolButton( header );
    diffButton->setText( QStringLiteral( "差异增强" ) );
    diffButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
    diffButton->setCursor( Qt::PointingHandCursor );
    diffButton->setCheckable( true );
    diffButton->setProperty( "compareAction", true );
    headerLayout->addWidget( titleLabel );
    headerLayout->addWidget( hintLabel, 1 );
    headerLayout->addWidget( diffButton );
    headerLayout->addWidget( swapButton );
    headerLayout->addWidget( swipeButton );
    headerLayout->addWidget( exitButton );
    pageLayout->addWidget( header );

    QSplitter *splitter = new QSplitter( Qt::Horizontal, page );
    splitter->setChildrenCollapsible( false );
    splitter->setHandleWidth( 5 );
    pageLayout->addWidget( splitter, 1 );

    const auto createComparePanel = [this, page]( const QString &title, const QString &objectName ) {
      QWidget *panel = new QWidget( page );
      panel->setObjectName( objectName + QStringLiteral( "Panel" ) );
      panel->setProperty( "eco/phaseComparePanel", true );
      panel->setStyleSheet( QStringLiteral( R"(
        QWidget { background:#1e1e1e; color:#d4d4d4; }
        QLabel[compareTitle="true"] { background:#181818; color:#f1f5f9; border:1px solid #3c3c3c; border-bottom:0; padding:7px 10px; font-weight:600; }
        QgsMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; }
      )" ) );
      QVBoxLayout *layout = new QVBoxLayout( panel );
      layout->setContentsMargins( 0, 0, 0, 0 );
      layout->setSpacing( 0 );
      QLabel *titleLabel = new QLabel( title, panel );
      titleLabel->setProperty( "compareTitle", true );
      titleLabel->setCursor( Qt::OpenHandCursor );
      titleLabel->installEventFilter( this );
      layout->addWidget( titleLabel );
      QgsMapCanvas *canvas = new QgsMapCanvas( panel );
      canvas->setObjectName( objectName );
      canvas->setProject( QgsProject::instance() );
      canvas->setMessageBar( mApp->messageBar() );
      canvas->setCanvasColor( QColor( QStringLiteral( "#1e1e1e" ) ) );
      canvas->setStyleSheet( QStringLiteral( "QgsMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; }" ) );
      canvas->setMapTool( new QgsMapToolPan( canvas ) );
      applyWorkbenchSelectionColor( canvas );
      layout->addWidget( canvas, 1 );
      return std::pair<QWidget *, QgsMapCanvas *>( panel, canvas );
    };

    const auto leftPanel = createComparePanel( tr( "期次对比 路 宸?路 %1" ).arg( currentPhaseName( leftPhaseId ) ), QStringLiteral( "EcoPhaseCompareLeft" ) );
    const auto rightPanel = createComparePanel( tr( "期次对比 路 鍙?路 %1" ).arg( currentPhaseName( rightPhaseId ) ), QStringLiteral( "EcoPhaseCompareRight" ) );
    mPhaseCompareSplitter = splitter;
    mPhaseCompareLeftPanel = leftPanel.first;
    mPhaseCompareRightPanel = rightPanel.first;
    mPhaseCompareLeftCanvas = leftPanel.second;
    mPhaseCompareRightCanvas = rightPanel.second;
    mPhaseCompareLeftPhaseId = leftPhaseId;
    mPhaseCompareRightPhaseId = rightPhaseId;
    mPhaseCompareRefreshPending = false;
    QWidget *diffLegendHost = rightPanel.second->viewport() ? rightPanel.second->viewport() : rightPanel.second;
    QFrame *diffLegendOverlay = new QFrame( diffLegendHost );
    diffLegendOverlay->setObjectName( QStringLiteral( "EcoPhaseDiffLegendOverlay" ) );
    diffLegendOverlay->setAttribute( Qt::WA_StyledBackground, true );
    diffLegendOverlay->setStyleSheet( QStringLiteral( R"(
      QFrame#EcoPhaseDiffLegendOverlay {
        background:rgba(8, 13, 28, 218);
        border:1px solid rgba(56, 189, 248, 190);
        border-radius:8px;
      }
    )" ) );
    QVBoxLayout *diffLegendLayout = new QVBoxLayout( diffLegendOverlay );
    diffLegendLayout->setContentsMargins( 10, 8, 10, 8 );
    diffLegendLayout->setSpacing( 6 );
    QLabel *diffLegendTitle = new QLabel( tr( "变化图例" ), diffLegendOverlay );
    diffLegendTitle->setStyleSheet( QStringLiteral( "color:#e0f2fe; font-size:11px; font-weight:600; background:transparent;" ) );
    diffLegendLayout->addWidget( diffLegendTitle );
    const auto addDiffLegendRow = [diffLegendOverlay, diffLegendLayout]( const QString &text, const QColor &color ) {
      QWidget *row = new QWidget( diffLegendOverlay );
      row->setStyleSheet( QStringLiteral( "background:transparent;" ) );
      QHBoxLayout *rowLayout = new QHBoxLayout( row );
      rowLayout->setContentsMargins( 0, 0, 0, 0 );
      rowLayout->setSpacing( 7 );
      QFrame *swatch = new QFrame( row );
      swatch->setFixedSize( 24, 8 );
      swatch->setStyleSheet( QStringLiteral( "background:%1; border:1px solid rgba(255,255,255,175); border-radius:4px;" ).arg( color.name() ) );
      QLabel *label = new QLabel( text, row );
      label->setStyleSheet( QStringLiteral( "color:#dbeafe; font-size:11px; background:transparent;" ) );
      rowLayout->addWidget( swatch );
      rowLayout->addWidget( label );
      rowLayout->addStretch();
      diffLegendLayout->addWidget( row );
    };
    addDiffLegendRow( tr( "新增" ), QColor( QStringLiteral( "#ff2bd6" ) ) );
    addDiffLegendRow( tr( "恢复" ), QColor( QStringLiteral( "#00e5ff" ) ) );
    diffLegendOverlay->hide();
    const auto updateDiffLegendOverlay = [diffLegendHost, diffLegendOverlay] {
      if ( !diffLegendHost || !diffLegendOverlay )
        return;
      diffLegendOverlay->adjustSize();
      const QSize legendSize = diffLegendOverlay->sizeHint().expandedTo( QSize( 132, 72 ) );
      diffLegendOverlay->resize( legendSize );
      const int margin = 14;
      diffLegendOverlay->move( std::max( 6, diffLegendHost->width() - diffLegendOverlay->width() - margin ),
                               std::max( 6, diffLegendHost->height() - diffLegendOverlay->height() - margin ) );
      diffLegendOverlay->raise();
    };
    rightPanel.second->installEventFilter( this );
    if ( diffLegendHost != rightPanel.second )
      diffLegendHost->installEventFilter( this );
    splitter->addWidget( leftPanel.first );
    splitter->addWidget( rightPanel.first );
    splitter->setSizes( QList<int>() << 1 << 1 );
    if ( QWidget *splitterHandle = splitter->handle( 1 ) )
    {
      splitterHandle->setEnabled( false );
      splitterHandle->setAttribute( Qt::WA_TransparentForMouseEvents, true );
    }
    page->installEventFilter( this );

    QWidget *swipeHandle = new QWidget( page );
    swipeHandle->setObjectName( QStringLiteral( "EcoPhaseSwipeHandle" ) );
    swipeHandle->setCursor( Qt::SplitHCursor );
    swipeHandle->setFixedWidth( 5 );
    swipeHandle->hide();
    swipeHandle->raise();
    swipeHandle->installEventFilter( this );
    mPhaseSwipeHandle = swipeHandle;
    mPhaseSwipeMode = false;
    mPhaseSwipeDragging = false;
    mPhasePanelDragging = false;

    auto updateSwipeHandle = [this, page, splitter, swipeHandle] {
      if ( !page || !splitter || !swipeHandle )
        return;
      if ( !mPhaseSwipeMode )
      {
        swipeHandle->hide();
        return;
      }
      const QList<int> sizes = splitter->sizes();
      const int leftWidth = sizes.value( 0, splitter->width() / 2 );
      const QPoint topLeft = splitter->mapTo( page, QPoint( leftWidth - swipeHandle->width() / 2, 0 ) );
      swipeHandle->setGeometry( topLeft.x(), topLeft.y(), swipeHandle->width(), splitter->height() );
      swipeHandle->raise();
      swipeHandle->show();
    };
    connect( splitter, &QSplitter::splitterMoved, page, [updateSwipeHandle] { updateSwipeHandle(); } );
    QTimer *swipeResizeTimer = new QTimer( page );
    swipeResizeTimer->setObjectName( QStringLiteral( "EcoPhaseSwipeResizeTimer" ) );
    swipeResizeTimer->setSingleShot( true );
    swipeResizeTimer->setInterval( 32 );
    connect( swipeResizeTimer, &QTimer::timeout, page,
             [this, page, splitter, leftCanvas = leftPanel.second, rightCanvas = rightPanel.second] {
      if ( !mPhaseSwipeMode || !page || !splitter )
        return;
      const int width = splitter->width();
      const int requestedX = page->property( "eco/swipePendingX" ).toInt();
      const int x = std::clamp( requestedX, 120, std::max( 120, width - 120 ) );
      // Cancel the previous resize render before applying the latest position.
      // This prevents a queue of stale renders from producing a black gap or a
      // multi-second lag while the divider is being dragged.
      if ( leftCanvas )
        leftCanvas->stopRendering();
      if ( rightCanvas )
        rightCanvas->stopRendering();
      splitter->setSizes( QList<int>() << x << std::max( 1, width - x ) );
    } );
    connect( swapButton, &QToolButton::clicked, page, [this, splitter, updateSwipeHandle] {
      if ( !mPhaseCompareLeftPanel || !mPhaseCompareRightPanel || !splitter )
        return;
      const bool leftPanelCurrentlyFirst = splitter->indexOf( mPhaseCompareLeftPanel ) < splitter->indexOf( mPhaseCompareRightPanel );
      splitter->insertWidget( 0, leftPanelCurrentlyFirst ? mPhaseCompareRightPanel : mPhaseCompareLeftPanel );
      splitter->setSizes( QList<int>() << 1 << 1 );
      updateSwipeHandle();
    } );
    connect( swipeButton, &QToolButton::toggled, page, [this, splitter, updateSwipeHandle]( bool enabled ) {
      mPhaseSwipeMode = enabled;
      if ( splitter )
      {
        // Normal comparison keeps a simple 5 px neutral gap.  Swipe mode
        // removes that gap and lets the neon handle become the only divider.
        splitter->setHandleWidth( enabled ? 0 : 5 );
        if ( !enabled )
          splitter->setSizes( QList<int>() << 1 << 1 );
      }
      updateSwipeHandle();
    } );

    QList<QgsMapLayer *> leftLayers = layersForPhaseComparison( leftPhaseId );
    QList<QgsMapLayer *> rightLayers = layersForPhaseComparison( rightPhaseId );
    QgsCoordinateReferenceSystem comparisonCrs = mApp->mapCanvas()->mapSettings().destinationCrs();
    if ( !comparisonCrs.isValid() )
      comparisonCrs = QgsProject::instance()->crs();
    if ( !comparisonCrs.isValid() )
    {
      for ( QgsMapLayer *layer : leftLayers + rightLayers )
      {
        if ( layer && layer->crs().isValid() )
        {
          comparisonCrs = layer->crs();
          break;
        }
      }
    }

    const auto layersExtent = [comparisonCrs]( const QList<QgsMapLayer *> &layers ) {
      QgsRectangle extent;
      bool hasExtent = false;
      for ( QgsMapLayer *layer : layers )
      {
        if ( !layer || !layer->isSpatial() )
          continue;
        QgsRectangle layerExtent = layer->extent();
        if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
          continue;
        if ( comparisonCrs.isValid() && layer->crs().isValid() && layer->crs() != comparisonCrs )
        {
          try
          {
            QgsCoordinateTransform transform( layer->crs(), comparisonCrs, QgsProject::instance()->transformContext() );
            layerExtent = transform.transformBoundingBox( layerExtent );
          }
          catch ( const QgsCsException & )
          {
          }
        }
        if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
          continue;
        if ( hasExtent )
          extent.combineExtentWith( layerExtent );
        else
        {
          extent = layerExtent;
          hasExtent = true;
        }
      }
      return hasExtent ? extent.scaled( 1.02 ) : QgsRectangle();
    };

    const auto disturbanceExtentForPhase = [comparisonCrs]( const QString &phaseId ) {
      QgsRectangle extent;
      bool hasExtent = false;
      QgsProject *project = QgsProject::instance();
      if ( !project )
        return extent;

      for ( QgsMapLayer *layer : project->mapLayers() )
      {
        if ( !layer || !layer->isSpatial() )
          continue;
        const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
        const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
        if ( resultType != QLatin1String( "construction-disturbance" )
             && category != QLatin1String( "recognition-result" ) )
          continue;

        const QString layerPhaseId = layerPhaseIdForLayer( layer );
        if ( layerPhaseId != phaseId )
          continue;

        QgsRectangle layerExtent = layer->extent();
        if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
          continue;
        if ( comparisonCrs.isValid() && layer->crs().isValid() && layer->crs() != comparisonCrs )
        {
          try
          {
            QgsCoordinateTransform transform( layer->crs(), comparisonCrs, project->transformContext() );
            layerExtent = transform.transformBoundingBox( layerExtent );
          }
          catch ( const QgsCsException & )
          {
            continue;
          }
        }
        if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
          continue;
        if ( hasExtent )
          extent.combineExtentWith( layerExtent );
        else
        {
          extent = layerExtent;
          hasExtent = true;
        }
      }
      if ( hasExtent )
      {
        const double padding = std::max( extent.width(), extent.height() ) * 0.18;
        if ( padding > 0.0 )
          extent.grow( padding );
      }
      return extent;
    };

    QgsRectangle sharedExtent = disturbanceExtentForPhase( leftPhaseId );
    const QgsRectangle rightDisturbanceExtent = disturbanceExtentForPhase( rightPhaseId );
    const bool hasDisturbanceExtent = ( !sharedExtent.isEmpty() && sharedExtent.isFinite() )
                                      || ( !rightDisturbanceExtent.isEmpty() && rightDisturbanceExtent.isFinite() );
    if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
      sharedExtent = rightDisturbanceExtent;
    else if ( !rightDisturbanceExtent.isEmpty() && rightDisturbanceExtent.isFinite() )
      sharedExtent.combineExtentWith( rightDisturbanceExtent );

    const QgsRectangle leftExtent = layersExtent( leftLayers );
    const QgsRectangle baseRightExtent = layersExtent( rightLayers );
    if ( !hasDisturbanceExtent )
    {
      if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
        sharedExtent = leftExtent;
      if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
        sharedExtent = baseRightExtent;
    }
    if ( QgsMapLayer *activeLayer = mApp->activeLayer() )
    {
      const QString activeResultType = activeLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
      const QString activeCategory = activeLayer->customProperty( QStringLiteral( "eco/category" ) ).toString();
      if ( activeResultType == QLatin1String( "construction-disturbance" )
           || activeCategory == QLatin1String( "recognition-result" ) )
      {
        QString activePhaseId = layerPhaseIdForLayer( activeLayer );
        // Only let an explicitly phase-tagged result layer override the
        // combined extent.  An unrelated active result from another phase
        // must not silently move the comparison to the wrong place.
        if ( activePhaseId != leftPhaseId && activePhaseId != rightPhaseId )
          activePhaseId.clear();
        QgsRectangle activeExtent = activeLayer->extent();
        if ( comparisonCrs.isValid() && activeLayer->crs().isValid() && activeLayer->crs() != comparisonCrs )
        {
          try
          {
            QgsCoordinateTransform transform( activeLayer->crs(), comparisonCrs, QgsProject::instance()->transformContext() );
            activeExtent = transform.transformBoundingBox( activeExtent );
          }
          catch ( const QgsCsException & )
          {
            activeExtent = QgsRectangle();
          }
        }
        if ( !activePhaseId.isEmpty() && !activeExtent.isEmpty() && activeExtent.isFinite() )
          sharedExtent = activeExtent.scaled( 1.35 );
      }
    }
    if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
      sharedExtent = mApp->mapCanvas()->extent();

    const auto phaseDisturbanceGeometry = [comparisonCrs]( const QString &phaseId ) {
      QVector<QgsGeometry> geometries;
      QgsProject *project = QgsProject::instance();
      if ( !project )
        return QgsGeometry();

      for ( QgsMapLayer *layer : project->mapLayers() )
      {
        if ( !layer || !layer->isSpatial() )
          continue;
        const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
        const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
        if ( resultType != QLatin1String( "construction-disturbance" )
             && category != QLatin1String( "recognition-result" ) )
          continue;
        const QString layerPhaseId = layerPhaseIdForLayer( layer );
        if ( layerPhaseId != phaseId )
          continue;

        QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
        if ( !vectorLayer )
          continue;
        QgsFeature feature;
        QgsFeatureIterator iterator = vectorLayer->getFeatures();
        while ( iterator.nextFeature( feature ) )
        {
          QgsGeometry geometry = feature.geometry();
          if ( geometry.isNull() || geometry.isEmpty() )
            continue;
          if ( comparisonCrs.isValid() && vectorLayer->crs().isValid() && vectorLayer->crs() != comparisonCrs )
          {
            try
            {
              QgsCoordinateTransform transform( vectorLayer->crs(), comparisonCrs, project->transformContext() );
              geometry.transform( transform );
            }
            catch ( const QgsCsException & )
            {
              continue;
            }
          }
          if ( geometry.type() == Qgis::GeometryType::Polygon && !geometry.isGeosValid() )
            geometry = geometry.makeValid();
          if ( !geometry.isNull() && !geometry.isEmpty() )
            geometries.append( geometry );
        }
      }
      if ( geometries.isEmpty() )
        return QgsGeometry();
      QgsGeometry merged = QgsGeometry::unaryUnion( geometries );
      return ( merged.isNull() || merged.isEmpty() ) ? QgsGeometry::collectGeometry( geometries ) : merged;
    };

    const QgsGeometry leftDisturbanceGeometry = phaseDisturbanceGeometry( leftPhaseId );
    const QgsGeometry rightDisturbanceGeometry = phaseDisturbanceGeometry( rightPhaseId );
    QgsGeometry leftRecoveryGeometry;
    QgsGeometry rightAddedGeometry;
    QgsGeometry overlapGeometry;
    if ( !leftDisturbanceGeometry.isNull() && !leftDisturbanceGeometry.isEmpty()
         && !rightDisturbanceGeometry.isNull() && !rightDisturbanceGeometry.isEmpty() )
    {
      const QgsGeometry leftBase = leftDisturbanceGeometry.isGeosValid() ? leftDisturbanceGeometry : leftDisturbanceGeometry.makeValid();
      const QgsGeometry rightBase = rightDisturbanceGeometry.isGeosValid() ? rightDisturbanceGeometry : rightDisturbanceGeometry.makeValid();
      leftRecoveryGeometry = leftBase.difference( rightBase );
      rightAddedGeometry = rightBase.difference( leftBase );
      overlapGeometry = leftBase.intersection( rightBase );
    }
    else if ( !leftDisturbanceGeometry.isNull() && !leftDisturbanceGeometry.isEmpty() )
    {
      leftRecoveryGeometry = leftDisturbanceGeometry;
    }
    else if ( !rightDisturbanceGeometry.isNull() && !rightDisturbanceGeometry.isEmpty() )
    {
      rightAddedGeometry = rightDisturbanceGeometry;
    }

    const auto configureCanvas = [comparisonCrs, sharedExtent]( QgsMapCanvas *canvas, const QList<QgsMapLayer *> &layers ) {
      if ( !canvas )
        return;
      canvas->freeze( true );
      canvas->setTheme( QString() );
      if ( comparisonCrs.isValid() )
        canvas->setDestinationCrs( comparisonCrs );
      canvas->setLayers( layers );
      if ( !sharedExtent.isEmpty() && sharedExtent.isFinite() )
        canvas->setExtent( sharedExtent );
      canvas->freeze( false );
      canvas->refresh();
    };
    configureCanvas( leftPanel.second, leftLayers );
    configureCanvas( rightPanel.second, rightLayers );

    const auto diffBands = std::make_shared<QList<QPointer<QgsRubberBand>>>();
    const auto makeDifferenceBand = [comparisonCrs, diffBands]( QgsMapCanvas *canvas, const QColor &baseColor,
                                                                const QgsGeometry &geometry, int fillAlpha,
                                                                int strokeAlpha, double width,
                                                                Qt::BrushStyle brushStyle,
                                                                Qt::PenStyle lineStyle,
                                                                double zValue,
                                                                int secondaryAlpha = 0 ) {
      if ( !canvas || geometry.isNull() || geometry.isEmpty() )
        return static_cast<QgsRubberBand *>( nullptr );

      QgsGeometry polygonGeometry = geometry.convertToType( Qgis::GeometryType::Polygon, true );
      if ( polygonGeometry.isNull() || polygonGeometry.isEmpty() )
        return static_cast<QgsRubberBand *>( nullptr );

      QgsRubberBand *band = new QgsRubberBand( canvas, Qgis::GeometryType::Polygon );
      QColor fillColor = baseColor;
      fillColor.setAlpha( std::clamp( fillAlpha, 0, 255 ) );
      QColor strokeColor = baseColor;
      strokeColor.setAlpha( std::clamp( strokeAlpha, 0, 255 ) );
      QColor secondaryStrokeColor = QColor( QStringLiteral( "#ffffff" ) );
      secondaryStrokeColor.setAlpha( std::clamp( secondaryAlpha, 0, 255 ) );
      band->setFillColor( fillColor );
      band->setStrokeColor( strokeColor );
      band->setSecondaryStrokeColor( secondaryAlpha > 0 ? secondaryStrokeColor : QColor() );
      band->setWidth( width );
      band->setBrushStyle( brushStyle );
      band->setLineStyle( lineStyle );
      band->setZValue( zValue );
      band->setToGeometry( polygonGeometry, comparisonCrs );
      band->hide();
      diffBands->append( QPointer<QgsRubberBand>( band ) );
      return band;
    };
    const auto addSolidChangeEffect = [makeDifferenceBand]( QgsMapCanvas *canvas, const QColor &color, const QgsGeometry &geometry, Qt::BrushStyle brushStyle ) {
      // Wide outer glow + high-alpha patterned body + crisp white-assisted
      // contour.  Small geometry changes remain readable against noisy raster.
      makeDifferenceBand( canvas, color, geometry, 0, 160, 7.0, Qt::NoBrush, Qt::SolidLine, 870.0, 55 );
      makeDifferenceBand( canvas, color, geometry, 150, 245, 2.2, brushStyle, Qt::SolidLine, 895.0, 150 );
      makeDifferenceBand( canvas, QColor( QStringLiteral( "#ffffff" ) ), geometry, 0, 185, 0.9, Qt::NoBrush, Qt::DashLine, 905.0 );
    };
    const auto addGhostChangeEffect = [makeDifferenceBand]( QgsMapCanvas *canvas, const QColor &color, const QgsGeometry &geometry ) {
      // Opposite-side ghost contour: shows where the other phase changed
      // without filling the current phase map.
      makeDifferenceBand( canvas, color, geometry, 0, 190, 2.0, Qt::NoBrush, Qt::DashLine, 885.0, 85 );
      makeDifferenceBand( canvas, color.lighter( 135 ), geometry, 0, 115, 4.2, Qt::NoBrush, Qt::DotLine, 875.0 );
    };
    const auto addOverlapReference = [makeDifferenceBand]( QgsMapCanvas *canvas, const QgsGeometry &geometry ) {
      const QColor violet( QStringLiteral( "#8b5cf6" ) );
      // The common area is only a low-key reference frame.  It should not
      // visually compete with "新增/恢复" deltas.
      makeDifferenceBand( canvas, violet, geometry, 6, 95, 1.2, Qt::NoBrush, Qt::DashDotLine, 850.0, 28 );
    };
    const QColor recoveryColor( QStringLiteral( "#00e5ff" ) );
    const QColor addedColor( QStringLiteral( "#ff2bd6" ) );
    addOverlapReference( leftPanel.second, overlapGeometry );
    addOverlapReference( rightPanel.second, overlapGeometry );
    addSolidChangeEffect( leftPanel.second, recoveryColor, leftRecoveryGeometry, Qt::BDiagPattern );
    addSolidChangeEffect( rightPanel.second, addedColor, rightAddedGeometry, Qt::FDiagPattern );
    addGhostChangeEffect( leftPanel.second, addedColor, rightAddedGeometry );
    addGhostChangeEffect( rightPanel.second, recoveryColor, leftRecoveryGeometry );

    diffButton->setEnabled( !diffBands->isEmpty() );
    connect( diffButton, &QToolButton::toggled, page,
             [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, diffBands, diffLegendOverlay, updateDiffLegendOverlay]( bool enabled ) {
      if ( diffLegendOverlay )
      {
        diffLegendOverlay->setVisible( enabled );
        if ( enabled )
          updateDiffLegendOverlay();
      }
      if ( diffBands )
      {
        for ( const QPointer<QgsRubberBand> &band : *diffBands )
        {
          if ( band )
            band->setVisible( enabled );
        }
      }
      if ( rightCanvas )
        rightCanvas->update();
      if ( leftCanvas )
        leftCanvas->update();
    } );

    auto syncingComparisonCanvases = std::make_shared<bool>( false );
    const auto syncComparisonExtent = [syncingComparisonCanvases]( QgsMapCanvas *source, QgsMapCanvas *target ) {
      if ( !source || !target || *syncingComparisonCanvases )
        return;
      const QgsRectangle sourceExtent = source->extent();
      if ( sourceExtent.isEmpty() || !sourceExtent.isFinite() )
        return;
      *syncingComparisonCanvases = true;
      target->setRotation( source->rotation() );
      target->setCenter( sourceExtent.center() );
      target->zoomScale( source->scale() );
      target->refresh();
      *syncingComparisonCanvases = false;
    };
    connect( leftPanel.second, &QgsMapCanvas::extentsChanged, page, [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, syncComparisonExtent] {
      syncComparisonExtent( leftCanvas, rightCanvas );
    } );
    connect( rightPanel.second, &QgsMapCanvas::extentsChanged, page, [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, syncComparisonExtent] {
      syncComparisonExtent( rightCanvas, leftCanvas );
    } );

    mPhaseComparePage = page;
    centralStack->addWidget( page );
    centralStack->setCurrentWidget( page );
    QTimer::singleShot( 0, page, updateDiffLegendOverlay );
    QTimer::singleShot( 120, page, updateDiffLegendOverlay );
    QTimer::singleShot( 0, page, [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, sharedExtent] {
      if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
        return;
      const auto applyInitialExtent = [sharedExtent]( QgsMapCanvas *canvas ) {
        if ( !canvas )
          return;
        canvas->setExtent( sharedExtent );
        canvas->refresh();
      };
      applyInitialExtent( leftCanvas );
      applyInitialExtent( rightCanvas );
    } );
    QTimer::singleShot( 120, page, [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, sharedExtent] {
      if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
        return;
      leftCanvas->setExtent( sharedExtent );
      rightCanvas->setExtent( sharedExtent );
      leftCanvas->refresh();
      rightCanvas->refresh();
    } );
    // A large raster/vector layer can finish its first render after the
    // normal event-loop turn. Re-apply once after that render settles so the
    // comparison always opens on the disturbance result instead of falling
    // back to the layer's full extent.
    QTimer::singleShot( 450, page, [leftCanvas = leftPanel.second, rightCanvas = rightPanel.second, sharedExtent] {
      if ( sharedExtent.isEmpty() || !sharedExtent.isFinite() )
        return;
      leftCanvas->setExtent( sharedExtent );
      rightCanvas->setExtent( sharedExtent );
      leftCanvas->refresh();
      rightCanvas->refresh();
    } );
    connect( page, &QObject::destroyed, this, [this, comparisonSession] {
      // Opening another comparison deletes the previous page later in the
      // event loop. Ignore that stale destruction callback instead of letting
      // it clear the newly opened comparison's canvas and phase state.
      if ( comparisonSession != mPhaseCompareSession )
        return;
      mPhaseComparePage = nullptr;
      mPhaseCompareLeftDock = nullptr;
      mPhaseCompareRightDock = nullptr;
      mPhaseCompareSplitter = nullptr;
      mPhaseCompareLeftPanel = nullptr;
      mPhaseCompareRightPanel = nullptr;
      mPhaseCompareLeftCanvas = nullptr;
      mPhaseCompareRightCanvas = nullptr;
      mPhaseCompareLeftPhaseId.clear();
      mPhaseCompareRightPhaseId.clear();
      mPhaseCompareRefreshPending = false;
      mPhaseSwipeHandle = nullptr;
      mPhaseSwipeMode = false;
      mPhaseSwipeDragging = false;
      mPhasePanelDragging = false;
    } );
    connect( exitButton, &QToolButton::clicked, this, [this, centralStack] {
      QWidget *pageToRemove = mPhaseComparePage;
      if ( centralStack )
        centralStack->setCurrentIndex( 0 );
      if ( pageToRemove )
      {
        if ( centralStack )
          centralStack->removeWidget( pageToRemove );
        pageToRemove->deleteLater();
      }
      if ( mApp && mApp->mapCanvas() )
        mApp->mapCanvas()->refresh();
    } );

    if ( mApp->statusBar() )
      mApp->statusBar()->showMessage( tr( "已打开期次对比?1 / %2" ).arg( currentPhaseName( leftPhaseId ), currentPhaseName( rightPhaseId ) ), 5000 );
    return;
  }

  const auto styleComparisonDock = [this]( QgsMapCanvasDockWidget *widget, const QString &dockObjectName, const QString &title ) {
    if ( !widget )
      return;
    widget->setCanvasName( title );
    widget->setViewCenterSynchronized( true );
    widget->setViewScaleSynchronized( true );
    widget->setCursorMarkerVisible( false );
    widget->setMainCanvasExtentVisible( false );
    widget->setLabelsVisible( true );
    widget->setStyleSheet( QStringLiteral( R"(
      QgsMapCanvasDockWidget, QWidget { background:#1e1e1e; color:#d4d4d4; }
      QToolBar { background:#181818; border:0; border-bottom:1px solid #3c3c3c; spacing:3px; padding:3px 6px; }
      QToolButton { color:#cdd6e0; background:transparent; border:1px solid transparent; border-radius:3px; padding:2px 5px; }
      QToolButton:hover { background:#252a30; border-color:#3d4a55; color:#ffffff; }
      QToolButton:checked { background:#0e639c; color:white; border-color:#1177bb; }
      QLabel, QCheckBox { color:#d4d4d4; background:transparent; }
      QComboBox, QSpinBox, QgsScaleComboBox { color:#f1f5f9; background:#252526; border:1px solid #454545; border-radius:3px; padding:1px 5px; }
      QgsMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; }
    )" ) );
    if ( QgsMapCanvas *canvas = widget->mapCanvas() )
    {
      canvas->setCanvasColor( QColor( QStringLiteral( "#1e1e1e" ) ) );
      canvas->setStyleSheet( QStringLiteral( "QgsMapCanvas { background:#1e1e1e; border:1px solid #3c3c3c; }" ) );
      applyWorkbenchSelectionColor( canvas );
    }
    if ( widget->dockableWidgetHelper() )
    {
      widget->dockableWidgetHelper()->setWindowTitle( title );
      widget->dockableWidgetHelper()->setDockObjectName( dockObjectName );
      widget->dockableWidgetHelper()->setUserVisible( true );
      if ( QgsDockWidget *dock = widget->dockableWidgetHelper()->dockWidget() )
      {
        dock->setWindowTitle( title );
        dock->setObjectName( dockObjectName );
        dock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
        dock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
        dock->setMinimumSize( QSize( 360, 260 ) );
        dock->setStyleSheet( QStringLiteral( R"(
          QDockWidget, QgsDockWidget { color:#d4d4d4; background:#202020; border:1px solid #3c3c3c; margin:10px 0; }
          QDockWidget::title, QgsDockWidget::title { background:#181818; color:#f1f5f9; padding:8px 38px 8px 8px; border:0; font-weight:600; }
          QDockWidget::close-button, QgsDockWidget::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
          QDockWidget::close-button:hover, QgsDockWidget::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
          QDockWidget::close-button:pressed, QgsDockWidget::close-button:pressed { background:#8f1d14; }
        )" ) );
        installEcoDockTitleBar( dock );
        dock->show();
        dock->raise();
      }
    }
  };

  if ( !mPhaseCompareLeftDock )
    mPhaseCompareLeftDock = mApp->getMapCanvas( QStringLiteral( "EcoPhaseCompareLeft" ) );
  if ( !mPhaseCompareRightDock )
    mPhaseCompareRightDock = mApp->getMapCanvas( QStringLiteral( "EcoPhaseCompareRight" ) );
  if ( !mPhaseCompareLeftDock )
    mPhaseCompareLeftDock = mApp->createNewMapCanvasDock( QStringLiteral( "EcoPhaseCompareLeft" ), true );
  if ( !mPhaseCompareRightDock )
    mPhaseCompareRightDock = mApp->createNewMapCanvasDock( QStringLiteral( "EcoPhaseCompareRight" ), true );
  if ( !mPhaseCompareLeftDock || !mPhaseCompareRightDock )
    return;

  const QString leftTitle = tr( "期次对比 路 宸?路 %1" ).arg( currentPhaseName( leftPhaseId ) );
  const QString rightTitle = tr( "期次对比 路 鍙?路 %1" ).arg( currentPhaseName( rightPhaseId ) );
  styleComparisonDock( mPhaseCompareLeftDock, QStringLiteral( "EcoPhaseCompareLeftDock" ), leftTitle );
  styleComparisonDock( mPhaseCompareRightDock, QStringLiteral( "EcoPhaseCompareRightDock" ), rightTitle );

  QgsMapCanvas *mainCanvas = mApp->mapCanvas();
  const auto configureCanvas = [this, mainCanvas]( QgsMapCanvasDockWidget *widget, const QString &phaseId ) {
    if ( !widget || !widget->mapCanvas() || !mainCanvas )
      return;
    QgsMapCanvas *canvas = widget->mapCanvas();
    canvas->freeze( true );
    canvas->setTheme( QString() );
    canvas->setDestinationCrs( mainCanvas->mapSettings().destinationCrs() );
    canvas->setLayers( layersForPhaseComparison( phaseId ) );
    canvas->setExtent( mainCanvas->extent() );
    canvas->freeze( false );
    canvas->refresh();
  };
  configureCanvas( mPhaseCompareLeftDock, leftPhaseId );
  configureCanvas( mPhaseCompareRightDock, rightPhaseId );

  QgsDockWidget *leftDock = mPhaseCompareLeftDock->dockableWidgetHelper() ? mPhaseCompareLeftDock->dockableWidgetHelper()->dockWidget() : nullptr;
  QgsDockWidget *rightDock = mPhaseCompareRightDock->dockableWidgetHelper() ? mPhaseCompareRightDock->dockableWidgetHelper()->dockWidget() : nullptr;
  if ( leftDock && rightDock && leftDock != rightDock )
  {
    mApp->addDockWidget( Qt::BottomDockWidgetArea, leftDock );
    mApp->splitDockWidget( leftDock, rightDock, Qt::Horizontal );
    mApp->resizeDocks( QList<QDockWidget *> { leftDock, rightDock }, QList<int> { 1, 1 }, Qt::Horizontal );
  }

  if ( mApp->statusBar() )
    mApp->statusBar()->showMessage( tr( "已打开期次对比?1 / %2" ).arg( currentPhaseName( leftPhaseId ), currentPhaseName( rightPhaseId ) ), 5000 );
}

QgsLayerTreeGroup *QgsEcoRestorationController::targetBusinessGroupForLayer( QgsMapLayer *layer ) const
{
  if ( !layer )
    return nullptr;

  const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
  const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
  QString phaseId = layer->customProperty( sPhaseIdProperty ).toString();
  if ( phaseId.isEmpty() )
    phaseId = currentPhaseId();

  if ( category == QLatin1String( "recognition-result" ) || resultType == QLatin1String( "construction-disturbance" ) )
  {
    if ( !phaseId.isEmpty() )
      layer->setCustomProperty( sPhaseIdProperty, phaseId );
    return phaseResultGroup( phaseId, sRecognitionGroup );
  }
  if ( category == QLatin1String( "review-result" ) || resultType == QLatin1String( "ecological-restoration" ) )
  {
    if ( !phaseId.isEmpty() )
      layer->setCustomProperty( sPhaseIdProperty, phaseId );
    return phaseResultGroup( phaseId, sRecognitionGroup );
  }
  if ( category == QLatin1String( "smart-segmentation" ) || resultType == QLatin1String( "smart-segmentation" ) )
  {
    if ( !phaseId.isEmpty() )
      layer->setCustomProperty( sPhaseIdProperty, phaseId );
    return phaseResultGroup( phaseId, sSmartSegmentationGroup );
  }
  if ( qobject_cast<QgsRasterLayer *>( layer ) )
  {
    if ( !phaseId.isEmpty() )
      layer->setCustomProperty( sPhaseIdProperty, phaseId );
    return phaseSubGroup( phaseId, sImageryGroup );
  }
  if ( const QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
  {
    if ( vectorLayer->geometryType() == Qgis::GeometryType::Point || vectorLayer->geometryType() == Qgis::GeometryType::Line )
    {
      if ( QgsLayerTreeGroup *common = commonDataGroup( true ) )
        return directChildGroup( common, sLineGroup );
    }
    if ( vectorLayer->geometryType() == Qgis::GeometryType::Polygon )
    {
      if ( QgsLayerTreeGroup *common = commonDataGroup( true ) )
        return directChildGroup( common, sScopeGroup );
    }
  }
  return commonDataGroup( true );
}

QList<QgsMapLayer *> QgsEcoRestorationController::layersForPhaseComparison( const QString &phaseId ) const
{
  QList<QgsMapLayer *> result;
  QSet<QString> addedLayerIds;
  QgsProject *project = QgsProject::instance();
  QgsLayerTree *root = project->layerTreeRoot();
  QList<QgsMapLayer *> orderedLayers = root ? root->layerOrder() : QList<QgsMapLayer *>();
  if ( orderedLayers.isEmpty() )
    orderedLayers = project->mapLayers().values();

  for ( QgsMapLayer *layer : std::as_const( orderedLayers ) )
  {
    if ( !layer || addedLayerIds.contains( layer->id() ) )
      continue;

    // Comparison canvases are intentionally independent from the main canvas,
    // so they must explicitly honor the layer-tree checkbox state. Without
    // this guard an unchecked result layer can still be injected into one of
    // the comparison canvases because the canvas owns its own layer list.
    QgsLayerTreeLayer *layerNode = root ? root->findLayer( layer->id() ) : nullptr;
    if ( !layerNode || !layerNode->isVisible() )
      continue;

    const QString layerPhaseId = layerPhaseIdForLayer( layer );

    const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
    const QString resultType = layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString();
    const bool phaseLayer = qobject_cast<QgsRasterLayer *>( layer )
                            || category == QLatin1String( "recognition-result" )
                            || category == QLatin1String( "review-result" )
                            || category == QLatin1String( "smart-segmentation" )
                            || resultType == QLatin1String( "construction-disturbance" )
                            || resultType == QLatin1String( "ecological-restoration" )
                            || resultType == QLatin1String( "smart-segmentation" );
    // Only explicitly unassigned vector layers are common data. A result
    // layer with a missing/legacy phase tag must not leak into both canvases.
    const bool commonVector = !phaseLayer && layerPhaseId.isEmpty() && qobject_cast<QgsVectorLayer *>( layer );
    if ( commonVector || layerPhaseId == phaseId )
    {
      result.append( layer );
      addedLayerIds.insert( layer->id() );
    }
  }
  return result;
}

void QgsEcoRestorationController::refreshPhaseComparisonCanvases()
{
  if ( mPhaseCompareRefreshPending )
    return;
  if ( !mPhaseComparePage || !mPhaseCompareLeftCanvas || !mPhaseCompareRightCanvas
       || mPhaseCompareLeftPhaseId.isEmpty() || mPhaseCompareRightPhaseId.isEmpty() )
    return;

  mPhaseCompareRefreshPending = true;
  QTimer::singleShot( 0, this, [this] {
    mPhaseCompareRefreshPending = false;
    if ( !mPhaseComparePage || !mPhaseCompareLeftCanvas || !mPhaseCompareRightCanvas )
      return;

    const auto refreshCanvas = [this]( QgsMapCanvas *canvas, const QString &phaseId ) {
      if ( !canvas || phaseId.isEmpty() )
        return;
      const QgsRectangle extent = canvas->extent();
      canvas->freeze( true );
      canvas->setTheme( QString() );
      canvas->setLayers( layersForPhaseComparison( phaseId ) );
      canvas->freeze( false );
      if ( !extent.isEmpty() && extent.isFinite() )
        canvas->setExtent( extent );
      canvas->refresh();
    };

    refreshCanvas( mPhaseCompareLeftCanvas, mPhaseCompareLeftPhaseId );
    refreshCanvas( mPhaseCompareRightCanvas, mPhaseCompareRightPhaseId );
  } );
}

void QgsEcoRestorationController::locateLayers( const QStringList &layerIds )
{
  if ( !mApp || layerIds.isEmpty() )
    return;

  QList<QgsMapLayer *> layers;
  layers.reserve( layerIds.size() );
  for ( const QString &layerId : layerIds )
  {
    QgsMapLayer *layer = QgsProject::instance()->mapLayer( layerId );
    if ( layer && layer->isSpatial() )
      layers << layer;
  }
  if ( layers.isEmpty() )
    return;

  const auto extentForCanvas = [layers]( QgsMapCanvas *canvas ) -> QgsRectangle {
    if ( !canvas )
      return QgsRectangle();

    const QgsCoordinateReferenceSystem canvasCrs = canvas->mapSettings().destinationCrs();
    QgsRectangle extent;
    bool hasExtent = false;
    for ( QgsMapLayer *layer : layers )
    {
      if ( !layer )
        continue;

      QgsRectangle layerExtent = layer->extent();
      if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
        continue;

      if ( canvasCrs.isValid() && layer->crs().isValid() && layer->crs() != canvasCrs )
      {
        try
        {
          QgsCoordinateTransform transform( layer->crs(), canvasCrs, QgsProject::instance()->transformContext() );
          layerExtent = transform.transformBoundingBox( layerExtent );
        }
        catch ( const QgsCsException & )
        {
          continue;
        }
      }

      if ( layerExtent.isEmpty() || !layerExtent.isFinite() )
        continue;

      if ( hasExtent )
        extent.combineExtentWith( layerExtent );
      else
      {
        extent = layerExtent;
        hasExtent = true;
      }
    }

    if ( !hasExtent || extent.isEmpty() || !extent.isFinite() )
      return QgsRectangle();

    const double padding = std::max( extent.width(), extent.height() ) * 0.12;
    if ( padding > 0.0 )
      extent.grow( padding );
    else
      extent.grow( canvasCrs.isValid() && canvasCrs.isGeographic() ? 0.001 : 10.0 );
    return extent;
  };

  const auto zoomCanvas = [&extentForCanvas]( QgsMapCanvas *canvas ) {
    if ( !canvas )
      return;
    const QgsRectangle extent = extentForCanvas( canvas );
    if ( extent.isEmpty() || !extent.isFinite() )
      return;
    canvas->freeze( true );
    canvas->setExtent( extent );
    canvas->freeze( false );
    canvas->refresh();
  };

  if ( mPhaseCompareLeftCanvas && mPhaseCompareRightCanvas )
  {
    zoomCanvas( mPhaseCompareLeftCanvas );
    zoomCanvas( mPhaseCompareRightCanvas );
    return;
  }

  if ( mApp->mapCanvas() )
    zoomCanvas( mApp->mapCanvas() );
}

void QgsEcoRestorationController::refreshPhaseChoices()
{
  const QStringList ids = phaseIds();
  const QString current = currentPhaseId();
  if ( mCurrentPhaseCombo )
  {
    const QSignalBlocker blocker( mCurrentPhaseCombo );
    mCurrentPhaseCombo->clear();
    for ( const QString &id : ids )
      mCurrentPhaseCombo->addItem( currentPhaseName( id ), id );
    const int index = mCurrentPhaseCombo->findData( current );
    if ( index >= 0 )
      mCurrentPhaseCombo->setCurrentIndex( index );
  }
  if ( mPhaseCompareAction )
    mPhaseCompareAction->setEnabled( ids.size() >= 2 );
  refreshPhotoWorkbenchContext();
}

void QgsEcoRestorationController::refreshPhotoWorkbenchContext()
{
  if ( !mPhotoWorkbench )
    return;

  const QString phaseId = currentPhaseId();
  mPhotoWorkbench->setPhaseContext( phaseId,
                                    phaseId.isEmpty() ? QString() : currentPhaseName( phaseId ),
                                    phaseId.isEmpty() ? QString() : phaseWorkspace( phaseId ) );
}

void QgsEcoRestorationController::organizeProjectTree()
{
  QgsLayerTreeGroup *projectGroup = projectTreeRoot();
  QgsLayerTreeGroup *root = QgsProject::instance()->layerTreeRoot();
  if ( !projectGroup || !root )
    return;

  QString projectName = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "name" ) );
  if ( projectName.isEmpty() )
    projectName = QgsProject::instance()->title();
  if ( !projectName.isEmpty() )
    projectGroup->setName( tr( "工程 路 %1" ).arg( projectName ) );

  const QList<QgsLayerTreeNode *> rootChildren = root->children();
  for ( QgsLayerTreeNode *node : rootChildren )
  {
    if ( node == projectGroup )
      continue;
    if ( QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( node ) )
    {
      if ( QgsMapLayer *layer = layerNode->layer() )
        moveLayerToBusinessGroup( layer, businessGroupForLayer( layer ) );
      continue;
    }
    if ( QgsLayerTreeGroup *group = qobject_cast<QgsLayerTreeGroup *>( node ) )
      appendNodeToGroupSafely( root, group, projectGroup );
  }

  const QString phaseId = currentPhaseId().isEmpty() ? phaseIds().value( 0 ) : currentPhaseId();
  const auto moveChildrenTo = [phaseId]( QgsLayerTreeGroup *source, QgsLayerTreeGroup *target ) {
    if ( !source || !target || source == target )
      return;
    const QList<QgsLayerTreeNode *> children = source->children();
    for ( QgsLayerTreeNode *child : children )
    {
      if ( QgsLayerTreeLayer *layerNode = qobject_cast<QgsLayerTreeLayer *>( child ) )
      {
        if ( QgsMapLayer *layer = layerNode->layer(); layer && !phaseId.isEmpty()
             && ( qobject_cast<QgsRasterLayer *>( layer )
                  || !layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString().isEmpty()
                  || layer->customProperty( QStringLiteral( "eco/category" ) ).toString().contains( QLatin1String( "result" ) )
                  || layer->customProperty( QStringLiteral( "eco/category" ) ).toString() == QLatin1String( "smart-segmentation" ) ) )
        {
          layer->setCustomProperty( sPhaseIdProperty, phaseId );
        }
      }
      appendNodeToGroupSafely( source, child, target );
    }
  };
  const auto removeGroupIfEmpty = [projectGroup]( QgsLayerTreeGroup *group ) {
    if ( group && group->parent() == projectGroup && group->children().isEmpty() )
      projectGroup->removeChildNode( group );
  };

  const QString oldImageryGroup = QStringLiteral( "01 影像数据" );
  const QString oldLineGroup = QStringLiteral( "02 杆塔与线路" );
  const QString oldScopeGroup = QStringLiteral( "03 调查范围" );
  const QString oldRecognitionGroup = QStringLiteral( "04 智能识别成果" );
  const QString oldReviewGroup = QStringLiteral( "05 人工核查成果" );
  const QString oldSmartGroup = QStringLiteral( "06 智能分割成果" );

  if ( QgsLayerTreeGroup *common = commonDataGroup( true ) )
  {
    if ( QgsLayerTreeGroup *target = directChildGroup( common, sLineGroup ) )
    {
      if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldLineGroup ) )
      {
        moveChildrenTo( old, target );
        removeGroupIfEmpty( old );
      }
    }
    if ( QgsLayerTreeGroup *target = directChildGroup( common, sScopeGroup ) )
    {
      if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldScopeGroup ) )
      {
        moveChildrenTo( old, target );
        removeGroupIfEmpty( old );
      }
    }
  }

  if ( !phaseId.isEmpty() )
  {
    if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldImageryGroup ) )
    {
      moveChildrenTo( old, phaseSubGroup( phaseId, sImageryGroup ) );
      removeGroupIfEmpty( old );
    }
    if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldRecognitionGroup ) )
    {
      moveChildrenTo( old, phaseResultGroup( phaseId, sRecognitionGroup ) );
      removeGroupIfEmpty( old );
    }
    if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldReviewGroup ) )
    {
      moveChildrenTo( old, phaseResultGroup( phaseId, sRecognitionGroup ) );
      removeGroupIfEmpty( old );
    }
    if ( QgsLayerTreeGroup *old = directChildGroup( projectGroup, oldSmartGroup ) )
    {
      moveChildrenTo( old, phaseResultGroup( phaseId, sSmartSegmentationGroup ) );
      removeGroupIfEmpty( old );
    }
  }

  projectGroup->setExpanded( true );
  if ( QgsLayerTreeView *view = mApp ? mApp->layerTreeView() : nullptr )
  {
    if ( QgsLayerTreeModel *model = view->layerTreeModel() )
      model->setFilterSettings( nullptr );
  }
}

bool QgsEcoRestorationController::isNodeInsideProject( QgsLayerTreeNode *node ) const
{
  QgsLayerTreeGroup *projectGroup = projectTreeRoot();
  for ( QgsLayerTreeNode *current = node; current; current = current->parent() )
  {
    if ( current == projectGroup )
      return true;
  }
  return false;
}

void QgsEcoRestorationController::classifyAddedLayers( const QList<QgsMapLayer *> &layers )
{
  bool ok = false;
  const bool enabled = QgsProject::instance()->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &ok );
  if ( !ok || !enabled )
    return;
  ensureBusinessGroups();
  for ( QgsMapLayer *layer : layers )
  {
    if ( layer && layer->customProperty( QStringLiteral( "eco/transientPhaseDifference" ), false ).toBool() )
      continue;
    if ( layer )
      moveLayerToBusinessGroup( layer, businessGroupForLayer( layer ) );
  }
}

void QgsEcoRestorationController::moveLayerToBusinessGroup( QgsMapLayer *layer, const QString &groupName )
{
  if ( !layer )
    return;

  QgsLayerTreeGroup *root = QgsProject::instance()->layerTreeRoot();
  if ( !root )
    return;
  QgsLayerTreeGroup *group = targetBusinessGroupForLayer( layer );
  if ( !group && !groupName.isEmpty() )
    group = root->findGroup( groupName );
  if ( !group )
    return;

  // Prefer the node already inside the resolved destination group.  This is
  // important when a legacy project contains a stale node elsewhere in the
  // tree: root->findLayer() is not guaranteed to return the destination one.
  QgsLayerTreeLayer *destinationNode = group->findLayer( layer->id() );
  if ( destinationNode )
  {
    destinationNode->setExpanded( false );
    destinationNode->setItemVisibilityChecked( true );
    for ( QgsLayerTreeLayer *candidate : root->findLayers() )
    {
      if ( candidate && candidate != destinationNode && candidate->layerId() == layer->id() && candidate->parent() )
      {
        if ( QgsLayerTreeGroup *parentGroup = QgsLayerTree::toGroup( candidate->parent() ) )
          parentGroup->removeChildNode( candidate );
      }
    }
    for ( QgsLayerTreeNode *ancestor = group->parent(); ancestor; ancestor = ancestor->parent() )
    {
      if ( QgsLayerTreeGroup *ancestorGroup = QgsLayerTree::toGroup( ancestor ) )
      {
        ancestorGroup->setExpanded( true );
        ancestorGroup->setItemVisibilityChecked( true );
      }
    }
    return;
  }

  QgsLayerTreeLayer *node = root->findLayer( layer->id() );
  if ( !node )
  {
    node = group->addLayer( layer );
    if ( node )
    {
      node->setExpanded( false );
      node->setItemVisibilityChecked( true );
    }
    return;
  }
  if ( node->parent() == group )
    return;
  if ( QgsLayerTreeGroup *parent = qobject_cast<QgsLayerTreeGroup *>( node->parent() ) )
  {
    // QGIS' registry bridge removes project layers when their last layer-tree
    // node disappears. Never "take then add" while moving a layer node: during
    // that short gap the bridge can queue removal of the underlying raster or
    // vector layer. Add the business node first, then delete the old node, so
    // the layer remains continuously present in the tree.
    QgsLayerTreeLayer *businessNode = group->addLayer( layer );
    if ( businessNode )
    {
      businessNode->setExpanded( false );
      businessNode->setItemVisibilityChecked( node->itemVisibilityChecked() );
      parent->removeChildNode( node );
    }
  }
}

QString QgsEcoRestorationController::businessGroupForLayer( QgsMapLayer *layer ) const
{
  const QString category = layer->customProperty( QStringLiteral( "eco/category" ) ).toString();
  if ( category == QLatin1String( "recognition-result" ) )
    return sRecognitionGroup;
  if ( category == QLatin1String( "review-result" ) )
    return sRecognitionGroup;
  if ( category == QLatin1String( "smart-segmentation" ) )
    return sSmartSegmentationGroup;
  if ( qobject_cast<QgsRasterLayer *>( layer ) )
    return sImageryGroup;
  if ( const QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
  {
    if ( vectorLayer->geometryType() == Qgis::GeometryType::Point || vectorLayer->geometryType() == Qgis::GeometryType::Line )
      return sLineGroup;
    if ( vectorLayer->geometryType() == Qgis::GeometryType::Polygon )
      return sScopeGroup;
  }
  return sScopeGroup;
}

void QgsEcoRestorationController::scheduleProjectStateRefresh()
{
  ecoImportTrace( QStringLiteral( "scheduleProjectStateRefresh pending transition=%1" ).arg( mProjectTransitionInProgress ? 1 : 0 ) );
  mProjectStateRefreshPending = true;
  if ( mProjectTransitionInProgress )
    return;

  QTimer::singleShot( 0, this, [this] {
    ecoImportTrace( QStringLiteral( "scheduleProjectStateRefresh timer fired transition=%1 pending=%2" )
                      .arg( mProjectTransitionInProgress ? 1 : 0 )
                      .arg( mProjectStateRefreshPending ? 1 : 0 ) );
    if ( mProjectTransitionInProgress || !mProjectStateRefreshPending )
      return;
    mProjectStateRefreshPending = false;
    refreshProjectState();
  } );
}

void QgsEcoRestorationController::refreshProjectState()
{
  ecoImportTrace( QStringLiteral( "refreshProjectState entered transition=%1" ).arg( mProjectTransitionInProgress ? 1 : 0 ) );
  if ( mProjectTransitionInProgress )
  {
    mProjectStateRefreshPending = true;
    ecoImportTrace( QStringLiteral( "refreshProjectState deferred transition active" ) );
    return;
  }

  QgsProject *project = QgsProject::instance();
  bool ok = false;
  const bool enabled = project->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &ok );
  QString name = project->readEntry( sProjectGroup, QStringLiteral( "name" ) );
  if ( name.isEmpty() )
    name = project->title();
  if ( name == QStringLiteral( "生态恢复监管工程" ) )
  {
    name = tr( "遥感解译工程" );
    project->writeEntry( sProjectGroup, QStringLiteral( "name" ), name );
    if ( project->title() == QStringLiteral( "生态恢复监管工程" ) )
      project->setTitle( name );
  }
  if ( mProjectNameLabel )
    mProjectNameLabel->setText( ok && enabled ? name : tr( "当前为普?QGIS 工程" ) );
  if ( mProjectPathLabel )
    mProjectPathLabel->setText( project->fileName().isEmpty() ? tr( "工程尚未保存" ) : project->fileName() );
  if ( mProjectInfoLabel )
  {
    const QString lineName = project->readEntry( sProjectGroup, QStringLiteral( "lineName" ) );
    const QString voltageLevel = project->readEntry( sProjectGroup, QStringLiteral( "voltageLevel" ) );
    const QString province = project->readEntry( sProjectGroup, QStringLiteral( "province" ) );
    const QString ownerUnit = project->readEntry( sProjectGroup, QStringLiteral( "ownerUnit" ) );
    const QString phaseName = currentPhaseName();
    const QStringList details = {
      tr( "线路?1" ).arg( lineName.isEmpty() ? tr( "未填写" ) : lineName ),
      tr( "电压?1" ).arg( voltageLevel.isEmpty() ? tr( "未填写" ) : voltageLevel ),
      tr( "区域?1" ).arg( province.isEmpty() ? tr( "未填写" ) : province ),
      tr( "建设单位?1" ).arg( ownerUnit.isEmpty() ? tr( "未填写" ) : ownerUnit ),
      tr( "当前期次?1" ).arg( phaseName.isEmpty() || phaseName == tr( "未指定期次" ) ? tr( "未填写" ) : phaseName )
    };
    mProjectInfoLabel->setText( details.join( QStringLiteral( " 路 " ) ) );
  }
  updateWelcomeOverlay();

  if ( ok && enabled )
  {
    ecoImportTrace( QStringLiteral( "refreshProjectState before ensureCurrentPhase" ) );
    ensureCurrentPhase();
    ecoImportTrace( QStringLiteral( "refreshProjectState before ensureBusinessGroups" ) );
    ensureBusinessGroups();
    ecoImportTrace( QStringLiteral( "refreshProjectState after ensureBusinessGroups" ) );
    if ( QgsLayerTreeGroup *projectGroup = projectTreeRoot() )
      projectGroup->setName( tr( "工程 路 %1" ).arg( name ) );
    for ( QgsMapLayer *layer : project->mapLayers() )
    {
      if ( layer && layer->name() == QStringLiteral( "生态修复核查成果" ) )
        layer->setName( tr( "修复成果" ) );
      if ( QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer ) )
        applyBusinessSelectionStyle( vectorLayer );
    }
  }

  if ( mApp )
  {
    mApp->setWindowTitle( ok && enabled && !name.isEmpty() ? tr( "%1 路 江河遥感解译" ).arg( name ) : tr( "江河 路 遥感解译" ) );
    // Loading or creating a project can restore QGIS's default white canvas.
    // Re-apply the Jianghe workbench background after project state changes.
    mApp->mapCanvas()->setCanvasColor( QColor( QStringLiteral( "#1e1e1e" ) ) );
    applyWorkbenchSelectionColor( mApp->mapCanvas() );
    hideNativeQgisWidgets();
  }
  refreshLayerChoices();
  syncBusinessProjectView();
  ecoImportTrace( QStringLiteral( "refreshProjectState finished enabled=%1" ).arg( ok && enabled ? 1 : 0 ) );
}

void QgsEcoRestorationController::refreshLayerChoices()
{
  if ( mProjectTransitionInProgress )
    return;
  if ( !mResultLayerCombo || !mTowerLayerCombo )
    return;
  refreshPhaseChoices();
  const QString currentResult = mResultLayerCombo->currentData().toString();
  const QString currentTower = mTowerLayerCombo->currentData().toString();
  const QString currentRecognitionTower = mRecognitionTowerCombo ? mRecognitionTowerCombo->currentData().toString() : QString();
  const QString currentRecognitionRaster = mRecognitionRasterCombo ? mRecognitionRasterCombo->currentData().toString() : QString();
  const QString phaseId = currentPhaseId();
  std::unique_ptr<QSignalBlocker> recognitionRasterBlocker;
  if ( mRecognitionRasterCombo )
    recognitionRasterBlocker = std::make_unique<QSignalBlocker>( mRecognitionRasterCombo );
  mResultLayerCombo->clear();
  mTowerLayerCombo->clear();
  if ( mRecognitionTowerCombo )
    mRecognitionTowerCombo->clear();
  if ( mRecognitionRasterCombo )
    mRecognitionRasterCombo->clear();

  int rasterCount = 0;
  int pointCount = 0;
  int lineCount = 0;
  int polygonCount = 0;
  QString preferredRecognitionRaster;
  const auto layers = QgsProject::instance()->mapLayers();
  for ( QgsMapLayer *layer : layers )
  {
    const QString layerPhaseId = layerPhaseIdForLayer( layer );
    const bool currentPhaseLayer = layerPhaseId.isEmpty() || phaseId.isEmpty() || layerPhaseId == phaseId;
    if ( qobject_cast<QgsRasterLayer *>( layer ) )
    {
      ++rasterCount;
      if ( mRecognitionRasterCombo )
      {
        QString displayName = layer->name();
        if ( !layerPhaseId.isEmpty() )
          displayName = tr( "%1 路 %2" ).arg( currentPhaseName( layerPhaseId ), layer->name() );
        mRecognitionRasterCombo->addItem( displayName, layer->id() );
        const int itemIndex = mRecognitionRasterCombo->count() - 1;
        mRecognitionRasterCombo->setItemData( itemIndex, layer->name(), Qt::ToolTipRole );
        if ( preferredRecognitionRaster.isEmpty() || ( currentPhaseLayer && layerPhaseId == phaseId ) )
          preferredRecognitionRaster = layer->id();
      }
      continue;
    }
    QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
    if ( !vectorLayer )
      continue;
    switch ( vectorLayer->geometryType() )
    {
      case Qgis::GeometryType::Point:
        ++pointCount;
        mTowerLayerCombo->addItem( vectorLayer->name(), vectorLayer->id() );
        if ( mRecognitionTowerCombo )
          mRecognitionTowerCombo->addItem( vectorLayer->name(), vectorLayer->id() );
        break;
      case Qgis::GeometryType::Line:
        ++lineCount;
        break;
      case Qgis::GeometryType::Polygon:
        ++polygonCount;
        if ( currentPhaseLayer )
          mResultLayerCombo->addItem( vectorLayer->name(), vectorLayer->id() );
        break;
      default:
        break;
    }
  }
  if ( mDataSummaryLabel )
    mDataSummaryLabel->setText( tr( "影像 %1 涓?路 杆塔?%2 涓?路 线路 %3 涓?路 面图?%4 个" ).arg( rasterCount ).arg( pointCount ).arg( lineCount ).arg( polygonCount ) );
  int index = mResultLayerCombo->findData( currentResult );
  if ( index >= 0 )
    mResultLayerCombo->setCurrentIndex( index );
  index = mTowerLayerCombo->findData( currentTower );
  if ( index >= 0 )
    mTowerLayerCombo->setCurrentIndex( index );
  if ( mRecognitionTowerCombo )
  {
    index = mRecognitionTowerCombo->findData( currentRecognitionTower );
    if ( index >= 0 )
      mRecognitionTowerCombo->setCurrentIndex( index );
  }
  if ( mRecognitionRasterCombo )
  {
    index = mRecognitionRasterCombo->findData( currentRecognitionRaster );
    if ( index < 0 && !preferredRecognitionRaster.isEmpty() )
      index = mRecognitionRasterCombo->findData( preferredRecognitionRaster );
    if ( index >= 0 )
      mRecognitionRasterCombo->setCurrentIndex( index );
  }
  if ( mRecognitionPreviewActive )
    updateRecognitionPreview();
}

void QgsEcoRestorationController::refreshRecognitionRasterOrder()
{
  if ( !mApp || mProjectTransitionInProgress || !mRecognitionRasterCombo )
    return;

  QgsRasterLayer *selectedRaster = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) );
  if ( !selectedRaster || !selectedRaster->isSpatial() )
    return;

  QgsMapCanvas *canvas = mApp->mapCanvas();
  if ( !canvas )
    return;

  // Reorder only the layers which are already on the canvas. In particular,
  // do not touch the layer tree or invoke the layer-tree/canvas bridge here.
  // The combo-box is a view selection, not a request to rebuild the project.
  QList<QgsMapLayer *> currentLayers = canvas->layers();
  if ( !currentLayers.contains( selectedRaster ) )
    currentLayers << selectedRaster;

  QList<QgsMapLayer *> contextualLayers;
  QList<QgsMapLayer *> rasterLayers;
  contextualLayers.reserve( currentLayers.size() );
  rasterLayers.reserve( currentLayers.size() );
  for ( QgsMapLayer *layer : currentLayers )
  {
    if ( !layer || layer == selectedRaster )
      continue;
    if ( qobject_cast<QgsRasterLayer *>( layer ) )
      rasterLayers << layer;
    else
      contextualLayers << layer;
  }

  QList<QgsMapLayer *> orderedLayers;
  orderedLayers.reserve( currentLayers.size() );
  orderedLayers += contextualLayers;
  orderedLayers << selectedRaster;
  orderedLayers += rasterLayers;

  if ( orderedLayers == currentLayers )
    return;

  ecoImportTrace( QStringLiteral( "refreshRecognitionRasterOrder setLayers count=%1 selected=%2" )
                    .arg( orderedLayers.size() )
                    .arg( selectedRaster->id() ) );
  canvas->setLayers( orderedLayers );
  canvas->refresh();
}

void QgsEcoRestorationController::syncBusinessProjectView( const QList<QgsMapLayer *> &extraLayers )
{
  if ( !mApp || mProjectTransitionInProgress )
  {
    if ( mProjectTransitionInProgress )
      mProjectStateRefreshPending = true;
    return;
  }

  QgsProject *project = QgsProject::instance();
  QgsLayerTreeGroup *projectRoot = project->layerTreeRoot();
  QgsLayerTree *projectTree = qobject_cast<QgsLayerTree *>( projectRoot );
  if ( !projectRoot )
    return;

  const auto promoteSelectedRecognitionRaster = [this, project]( QList<QgsMapLayer *> &canvasLayers ) {
    if ( !mRecognitionRasterCombo )
      return;

    QgsMapLayer *selectedLayer = project->mapLayer( mRecognitionRasterCombo->currentData().toString() );
    if ( !qobject_cast<QgsRasterLayer *>( selectedLayer ) || !selectedLayer->isSpatial() || !canvasLayers.contains( selectedLayer ) )
      return;

    QList<QgsMapLayer *> contextualLayers;
    QList<QgsMapLayer *> rasterLayers;
    contextualLayers.reserve( canvasLayers.size() );
    rasterLayers.reserve( canvasLayers.size() );
    for ( QgsMapLayer *layer : std::as_const( canvasLayers ) )
    {
      if ( !layer || layer == selectedLayer )
        continue;
      if ( qobject_cast<QgsRasterLayer *>( layer ) )
        rasterLayers << layer;
      else
        contextualLayers << layer;
    }

    canvasLayers.clear();
    canvasLayers += contextualLayers;
    canvasLayers << selectedLayer;
    canvasLayers += rasterLayers;
  };

  if ( mLayerImportInProgress )
  {
    ecoImportTrace( QStringLiteral( "syncBusinessProjectView light mode during active layer operation extraLayers=%1" ).arg( extraLayers.size() ) );
    projectRoot->setItemVisibilityChecked( true );

    QList<QgsMapLayer *> canvasLayers;
    collectVisibleSpatialLayers( projectRoot, canvasLayers );
    for ( QgsMapLayer *layer : extraLayers )
    {
      if ( !layer || !layer->isSpatial() || canvasLayers.contains( layer ) )
        continue;
      canvasLayers << layer;
    }
    promoteSelectedRecognitionRaster( canvasLayers );

    if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
    {
      canvas->freeze( false );
      canvas->setTheme( QString() );
      canvas->setLayers( canvasLayers );
      canvas->refresh();
    }
    return;
  }

  bool projectEnabledOk = false;
  const bool businessProjectEnabled = project->readBoolEntry( sProjectGroup, QStringLiteral( "enabled" ), false, &projectEnabledOk );
  if ( projectEnabledOk && businessProjectEnabled )
    ensureBusinessGroups();

  projectRoot->setItemVisibilityChecked( true );
  if ( QgsLayerTreeGroup *common = commonDataGroup( false ) )
    common->setItemVisibilityChecked( true );
  for ( const QString &id : phaseIds() )
  {
    if ( QgsLayerTreeGroup *group = phaseGroup( id, false ) )
      group->setItemVisibilityChecked( true );
  }
  if ( projectTree && projectTree->hasCustomLayerOrder() )
    projectTree->setHasCustomLayerOrder( false );

  const auto projectLayers = project->mapLayers();
  for ( QgsMapLayer *layer : projectLayers )
  {
    if ( !layer )
      continue;

    QgsLayerTreeLayer *node = projectRoot->findLayer( layer->id() );
    const bool businessLayer = qobject_cast<QgsRasterLayer *>( layer )
                               || !layer->customProperty( QStringLiteral( "eco/category" ) ).toString().isEmpty()
                               || !layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString().isEmpty();
    // Existing nodes can point at a legacy top-level results group after a
    // project is reopened. Re-run the business-group resolver for these
    // layers, rather than only adding layers which have no node at all.
    if ( !node || businessLayer )
      moveLayerToBusinessGroup( layer, businessGroupForLayer( layer ) );
  }

  if ( QgsLayerTreeView *view = mApp->layerTreeView() )
  {
    if ( QgsLayerTreeModel *model = view->layerTreeModel() )
    {
      if ( model->rootGroup() != projectTree && projectTree )
        model->setRootGroup( projectTree );
      model->setFlag( QgsLayerTreeModel::ShowLegend, false );
      model->setFilterSettings( nullptr );
    }

    view->setShowPrivateLayers( true );
    view->setHideValidLayers( false );

    if ( auto *proxy = qobject_cast<QgsLayerTreeProxyModel *>( view->model() ) )
    {
      proxy->setFilterText( QString() );
      proxy->setHideValidLayers( false );
      proxy->setShowPrivateLayers( true );
    }

    view->expandAll();
    if ( QgsMapLayer *activeLayer = mApp->activeLayer() )
      view->setCurrentLayer( activeLayer );
    view->viewport()->update();
  }

  QList<QgsMapLayer *> canvasLayers;
  collectVisibleSpatialLayers( projectRoot, canvasLayers );
  for ( QgsMapLayer *layer : extraLayers )
  {
    if ( !layer || !layer->isSpatial() || canvasLayers.contains( layer ) )
      continue;
    canvasLayers << layer;
  }
  promoteSelectedRecognitionRaster( canvasLayers );

  if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
  {
    canvas->freeze( false );
    canvas->setTheme( QString() );
    canvas->setLayers( canvasLayers );
    canvas->refresh();
  }

  if ( mApp->layerTreeCanvasBridge() )
  {
    mApp->layerTreeCanvasBridge()->setAutoSetupOnFirstLayer( false );
    mApp->layerTreeCanvasBridge()->setCanvasLayers();

    // The bridge rebuilds the canvas list from the tree and can overwrite the
    // explicit list above while a result node is being moved between phase
    // groups.  Re-apply the list after the bridge has synchronized so a newly
    // created recognition layer is rendered immediately even when the bridge
    // is still processing its tree notifications.
    if ( QgsMapCanvas *canvas = mApp->mapCanvas() )
    {
      canvas->freeze( false );
      canvas->setLayers( canvasLayers );
      canvas->refresh();
    }
  }

  if ( projectEnabledOk && businessProjectEnabled )
  {
    commonDataGroup( true );
    if ( QgsLayerTreeView *view = mApp->layerTreeView() )
    {
      view->expandAll();
      view->viewport()->update();
    }
  }
}

QgsVectorLayer *QgsEcoRestorationController::selectedResultLayer() const
{
  return mResultLayerCombo ? qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mResultLayerCombo->currentData().toString() ) ) : nullptr;
}

QgsVectorLayer *QgsEcoRestorationController::ensureDisturbanceResultLayer( const QString &phaseId )
{
  QString targetPhaseId = phaseId.trimmed();
  if ( targetPhaseId.isEmpty() )
    targetPhaseId = ensureCurrentPhase();
  ecoImportTrace( QStringLiteral( "ensureDisturbanceResultLayer phaseId=%1" ).arg( targetPhaseId ) );
  QgsVectorLayer *layer = selectedResultLayer();
  if ( layer && layer->geometryType() == Qgis::GeometryType::Polygon
       && layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "construction-disturbance" )
       && ( targetPhaseId.isEmpty() || layerPhaseIdForLayer( layer ).isEmpty() || layerPhaseIdForLayer( layer ) == targetPhaseId ) )
  {
    if ( layer->customProperty( sPhaseIdProperty ).toString().isEmpty() && !targetPhaseId.isEmpty() )
      layer->setCustomProperty( sPhaseIdProperty, targetPhaseId );
    ecoImportTrace( QStringLiteral( "ensureDisturbanceResultLayer selected existing layer=%1" ).arg( layer->id() ) );
    return layer;
  }

  // Reuse an existing business result if one is already present. Otherwise
  // create the standard editable layer automatically as part of this task.
  const auto layers = QgsProject::instance()->mapLayers();
  for ( QgsMapLayer *candidate : layers )
  {
    QgsVectorLayer *candidateLayer = qobject_cast<QgsVectorLayer *>( candidate );
    if ( candidateLayer && candidateLayer->geometryType() == Qgis::GeometryType::Polygon
         && candidateLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() == QLatin1String( "construction-disturbance" )
         && ( targetPhaseId.isEmpty() || layerPhaseIdForLayer( candidateLayer ).isEmpty() || layerPhaseIdForLayer( candidateLayer ) == targetPhaseId ) )
    {
      if ( candidateLayer->customProperty( sPhaseIdProperty ).toString().isEmpty() && !targetPhaseId.isEmpty() )
        candidateLayer->setCustomProperty( sPhaseIdProperty, targetPhaseId );
      if ( mResultLayerCombo )
      {
        const int index = mResultLayerCombo->findData( candidateLayer->id() );
        if ( index >= 0 )
          mResultLayerCombo->setCurrentIndex( index );
      }
      ecoImportTrace( QStringLiteral( "ensureDisturbanceResultLayer reused candidate layer=%1" ).arg( candidateLayer->id() ) );
      return candidateLayer;
    }
  }
  ecoImportTrace( QStringLiteral( "ensureDisturbanceResultLayer creating new layer" ) );
  return createResultLayer( false, false, targetPhaseId );
}

void QgsEcoRestorationController::attachRecognitionResultLayer( QgsVectorLayer *layer )
{
  if ( !layer )
    return;

  ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer entered layerId=%1" ).arg( layer->id() ) );
  QString phaseId = layer->customProperty( sPhaseIdProperty ).toString().trimmed();
  if ( phaseId.isEmpty() )
    phaseId = currentPhaseId();
  if ( phaseId.isEmpty() )
  {
    ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer no phase, fallback move layerId=%1" ).arg( layer->id() ) );
    moveLayerToBusinessGroup( layer, sRecognitionGroup );
    return;
  }
  layer->setCustomProperty( sPhaseIdProperty, phaseId );

  ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer before phaseResultGroup phaseId=%1 layerId=%2" ).arg( phaseId, layer->id() ) );
  QgsLayerTreeGroup *destination = phaseResultGroup( phaseId, sRecognitionGroup, true );
  QgsLayerTreeGroup *root = QgsProject::instance()->layerTreeRoot();
  if ( !destination || !root )
  {
    ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer missing destination/root layerId=%1" ).arg( layer->id() ) );
    return;
  }
  ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer destination ready layerId=%1" ).arg( layer->id() ) );

  destination->setExpanded( true );
  destination->setItemVisibilityChecked( true );

  // Make all ancestors visible as well.  A phase or results group restored
  // from an older project may retain an unchecked state, which makes the
  // child layer disappear from both the tree and the canvas even though the
  // child itself is checked.
  for ( QgsLayerTreeNode *ancestor = destination->parent(); ancestor; ancestor = ancestor->parent() )
  {
    if ( QgsLayerTreeGroup *group = QgsLayerTree::toGroup( ancestor ) )
    {
      group->setExpanded( true );
      group->setItemVisibilityChecked( true );
    }
  }

  // Add the destination node before removing any stale node.  QGIS' layer
  // tree bridge treats a layer with no node as removed from the project.
  QgsLayerTreeLayer *destinationNode = destination->findLayer( layer->id() );
  if ( !destinationNode )
  {
    ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer before add destination node layerId=%1" ).arg( layer->id() ) );
    destinationNode = destination->addLayer( layer );
    ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer after add destination node layerId=%1" ).arg( layer->id() ) );
  }
  if ( destinationNode )
  {
    destinationNode->setExpanded( false );
    destinationNode->setItemVisibilityChecked( true );
  }

  // During automated imports and whole-line recognition the layer tree model
  // is already processing insertion notifications.  Avoid synchronous
  // duplicate cleanup or expandAll() here; these operations can re-enter Qt's
  // tree model with indexes from the node that has just been inserted.  A
  // later project/tree refresh can safely normalize legacy duplicates.
  if ( !mLayerImportInProgress )
  {
    const QList<QgsLayerTreeLayer *> allNodes = root->findLayers();
    for ( QgsLayerTreeLayer *node : allNodes )
    {
      if ( !node || node == destinationNode || node->layerId() != layer->id() || !node->parent() )
        continue;
      if ( QgsLayerTreeGroup *parentGroup = QgsLayerTree::toGroup( node->parent() ) )
        parentGroup->removeChildNode( node );
    }

    if ( QgsLayerTreeView *view = mApp ? mApp->layerTreeView() : nullptr )
    {
      view->expandAll();
      view->viewport()->update();
    }
  }
  ecoImportTrace( QStringLiteral( "attachRecognitionResultLayer finished layerId=%1" ).arg( layer->id() ) );
}

void QgsEcoRestorationController::setRecognitionProgress( int current, int total )
{
  if ( !mRecognitionProgressBar )
    return;
  if ( mRecognitionDock && !mRecognitionDock->isVisible() )
  {
    mRecognitionProgressBar->hide();
    return;
  }
  total = std::max( 1, total );
  if ( current >= total )
  {
    mRecognitionProgressBar->setRange( 0, total );
    mRecognitionProgressBar->setValue( total );
    mRecognitionProgressBar->setFormat( tr( "塔基扰动识别 %v / %m" ) );
    mRecognitionProgressBar->show();
    return;
  }
  mRecognitionProgressBar->setRange( 0, total );
  mRecognitionProgressBar->setValue( std::max( 0, current ) );
  mRecognitionProgressBar->setFormat( tr( "塔基扰动识别 %v / %m" ) );
  mRecognitionProgressBar->show();
}

void QgsEcoRestorationController::showRecognitionResultTable( const QString &resultLayerId )
{
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( resultLayerId ) );
  if ( !resultLayer || resultLayer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() != QLatin1String( "construction-disturbance" ) )
  {
    showMessage( tr( "扰动结果表格" ), tr( "请选择有效的塔基施工扰动成果图层。" ), true );
    return;
  }

  mRecognitionResultLayerId = resultLayerId;
  if ( !mRecognitionObservedResultLayerIds.contains( resultLayerId ) )
  {
    mRecognitionObservedResultLayerIds.append( resultLayerId );
    const auto scheduleCurrentLayerRefresh = [this, resultLayerId] {
      if ( mRecognitionResultLayerId == resultLayerId )
        scheduleRecognitionResultTableRefresh();
    };
    connect( resultLayer, &QgsVectorLayer::featureAdded, this, [scheduleCurrentLayerRefresh]( QgsFeatureId ) { scheduleCurrentLayerRefresh(); } );
    connect( resultLayer, &QgsVectorLayer::featuresDeleted, this, [scheduleCurrentLayerRefresh]( const QgsFeatureIds & ) { scheduleCurrentLayerRefresh(); } );
    connect( resultLayer, &QgsVectorLayer::attributeValueChanged, this, [scheduleCurrentLayerRefresh]( QgsFeatureId, int, const QVariant & ) { scheduleCurrentLayerRefresh(); } );
    connect( resultLayer, &QgsVectorLayer::editingStopped, this, scheduleCurrentLayerRefresh );
    connect( resultLayer, &QgsVectorLayer::afterCommitChanges, this, scheduleCurrentLayerRefresh );
    connect( resultLayer, &QgsVectorLayer::editingStopped, this, [this, resultLayer] { normalizeRecognitionResultLayerAfterEdit( resultLayer ); } );
    connect( resultLayer, &QgsVectorLayer::afterCommitChanges, this, [this, resultLayer] { normalizeRecognitionResultLayerAfterEdit( resultLayer ); } );
  }
  const bool needsVisibilityRenderer = resultLayer->fields().indexFromName( QLatin1String( sRecognitionVisibilityField ) ) < 0;
  if ( ensureRecognitionVisibilityField( resultLayer ) < 0 )
  {
    showMessage( tr( "扰动结果表格" ), tr( "无法初始化图斑显隐控制，请检查成果数据的写入权限。" ), true );
    return;
  }
  if ( needsVisibilityRenderer )
    applyBusinessStyle( resultLayer );

  if ( !mRecognitionResultDock )
  {
    mRecognitionResultDock = new QgsDockWidget( tr( "扰动识别结果" ), mApp );
    mRecognitionResultDock->setObjectName( QStringLiteral( "EcoRecognitionResultDock" ) );
    mRecognitionResultDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );
    mRecognitionResultDock->setFeatures( QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable );
    mRecognitionResultDock->setMinimumHeight( 260 );
    mRecognitionResultDock->setStyleSheet( QStringLiteral( R"(
      QDockWidget { background:#1e1e1e; color:#f1f5f9; border-top:1px solid #3c3c3c; }
      QDockWidget::title { background:#181818; color:#f1f5f9; padding:7px 38px 7px 10px; border:0; font-weight:600; }
      QDockWidget::close-button { image:url(:/images/themes/default/mIconCloseTab.svg); background:transparent; border:0; border-radius:0; width:30px; height:26px; subcontrol-origin:margin; subcontrol-position:top right; margin:0 2px 0 0; }
      QDockWidget::close-button:hover { image:url(:/images/themes/default/mIconCloseTabHover.svg); background:#c42b1c; }
      QDockWidget::close-button:pressed { background:#8f1d14; }
    )" ) );
    installEcoDockTitleBar( mRecognitionResultDock );

    QWidget *content = new QWidget( mRecognitionResultDock );
    content->setStyleSheet( QStringLiteral( R"(
      QWidget { background:#1e1e1e; color:#d4d4d4; }
      QComboBox { min-height:24px; color:#f1f5f9; background:#252526; border:1px solid #454545; border-radius:3px; padding:1px 6px; }
      QTableWidget { background:#181818; alternate-background-color:#181818; color:#d4d4d4; border:0; gridline-color:#2d2d2d; selection-background-color:#094771; selection-color:white; }
      QTableWidget::item { background:#181818; color:#d4d4d4; border-bottom:1px solid #252525; }
      QTableWidget::item:hover { background:#2a2d2e; color:#ffffff; }
      QTableWidget::item:selected { background:#094771; color:#ffffff; }
      QHeaderView::section { background:#252526; color:#cbd5e1; border:0; border-right:1px solid #3c3c3c; border-bottom:1px solid #3c3c3c; padding:5px 7px; font-weight:600; }
      QWidget#EcoRecognitionCell { background:transparent; }
      QLabel#EcoRecognitionEditingLabel { color:#fbbf24; font-weight:600; background:transparent; }
      QToolButton#EcoRecognitionFinishButton, QToolButton#EcoRecognitionDeleteButton { min-width:26px; max-width:26px; min-height:26px; max-height:26px; background:transparent; border:1px solid transparent; border-radius:3px; padding:0; }
      QToolButton#EcoRecognitionFinishButton:hover { background:#1f3b32; border-color:#3a725f; }
      QToolButton#EcoRecognitionFinishButton:pressed { background:#264f3d; }
      QToolButton#EcoRecognitionDeleteButton:hover { background:#4b1f24; border-color:#8f343d; }
      QToolButton#EcoRecognitionDeleteButton:pressed { background:#6b252d; }
      QCheckBox#EcoRecognitionVisibility { color:#cbd5e1; background:transparent; spacing:5px; }
      QScrollBar:vertical { width:12px; margin:0; background:#181818; }
      QScrollBar:horizontal { height:12px; margin:0; background:#181818; }
      QScrollBar::handle:vertical { min-height:24px; margin:2px; background:#424242; border-radius:4px; }
      QScrollBar::handle:horizontal { min-width:24px; margin:2px; background:#424242; border-radius:4px; }
      QScrollBar::handle:hover { background:#5a5a5a; }
      QScrollBar::handle:pressed { background:#6b6b6b; }
      QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { width:0; height:0; background:transparent; }
    )" ) );
    QVBoxLayout *layout = new QVBoxLayout( content );
    layout->setContentsMargins( 8, 7, 8, 8 );
    layout->setSpacing( 6 );
    QHBoxLayout *filterLayout = new QHBoxLayout;
    QLabel *filterLabel = new QLabel( tr( "筛选：" ), content );
    mRecognitionResultFilter = new QComboBox( content );
    mRecognitionResultFilter->addItem( tr( "全部杆塔" ), 0 );
    mRecognitionResultFilter->addItem( tr( "有识别结果" ), 1 );
    mRecognitionResultFilter->addItem( tr( "无识别结果" ), 2 );
    filterLayout->addWidget( filterLabel );
    filterLayout->addWidget( mRecognitionResultFilter );
    filterLayout->addStretch();
    layout->addLayout( filterLayout );

    mRecognitionResultTable = new QTableWidget( content );
    mRecognitionResultTable->setColumnCount( 8 );
    mRecognitionResultTable->setHorizontalHeaderLabels( { tr( "序号" ), tr( "杆塔编号" ), tr( "识别状态" ), tr( "类型" ), tr( "候选图斑" ), tr( "最高置信度" ), tr( "编辑状态" ), QString() } );
    mRecognitionResultTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mRecognitionResultTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mRecognitionResultTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    mRecognitionResultTable->setAlternatingRowColors( false );
    mRecognitionResultTable->setSortingEnabled( false );
    mRecognitionResultTable->setContextMenuPolicy( Qt::CustomContextMenu );
    mRecognitionResultTable->verticalHeader()->setVisible( false );
    mRecognitionResultTable->verticalHeader()->setDefaultSectionSize( 34 );
    mRecognitionResultTable->horizontalHeader()->setStretchLastSection( false );
    layout->addWidget( mRecognitionResultTable, 1 );
    mRecognitionResultDock->setWidget( content );
    mApp->addDockWidget( Qt::BottomDockWidgetArea, mRecognitionResultDock );
    connect( mRecognitionResultDock, &QDockWidget::topLevelChanged, mRecognitionResultDock, [this]( bool floating ) {
      if ( !floating && mRecognitionResultDock )
        resizeBottomAttributeDock( mRecognitionResultDock );
    } );

    connect( mRecognitionResultFilter, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this] { refreshRecognitionResultTable(); } );
    connect( mRecognitionResultTable, &QWidget::customContextMenuRequested, this, [this]( const QPoint &pos ) { showRecognitionResultContextMenu( pos ); } );
    if ( QTableWidgetItem *visibilityHeader = mRecognitionResultTable->horizontalHeaderItem( 7 ) )
      visibilityHeader->setToolTip( tr( "显示或隐藏图斑" ) );
  }

  refreshRecognitionResultTable();
  mRecognitionResultDock->show();
  mRecognitionResultDock->raise();
  resizeBottomAttributeDock( mRecognitionResultDock );
}

void QgsEcoRestorationController::scheduleRecognitionResultTableRefresh()
{
  if ( mRecognitionResultRefreshPending )
    return;

  mRecognitionResultRefreshPending = true;
  QTimer::singleShot( 0, this, [this] {
    mRecognitionResultRefreshPending = false;
    refreshRecognitionResultTable();
  } );
}

void QgsEcoRestorationController::refreshRecognitionResultTable()
{
  if ( !mRecognitionResultTable || mRecognitionResultLayerId.isEmpty() )
    return;

  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !resultLayer )
  {
    mRecognitionResultTable->setRowCount( 0 );
    return;
  }
  QString towerLayerId = resultLayer->customProperty( QStringLiteral( "eco/towerLayerId" ) ).toString();
  if ( towerLayerId.isEmpty() && mRecognitionTowerCombo )
    towerLayerId = mRecognitionTowerCombo->currentData().toString();
  QgsVectorLayer *towerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( towerLayerId ) );
  if ( !towerLayer || towerLayer->geometryType() != Qgis::GeometryType::Point )
  {
    mRecognitionResultTable->setRowCount( 0 );
    return;
  }

  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  const int confidenceIndex = resultLayer->fields().indexFromName( QStringLiteral( "confidence" ) );
  const int sourceIndex = resultLayer->fields().indexFromName( QStringLiteral( "source" ) );
  const int visibilityIndex = resultLayer->fields().indexFromName( QLatin1String( sRecognitionVisibilityField ) );
  QHash<QString, int> detectionCounts;
  QHash<QString, double> maximumConfidence;
  QHash<QString, QString> resultTypes;
  QHash<QString, bool> resultVisibility;
  QgsFeature resultFeature;
  QgsFeatureIterator resultIterator = resultLayer->getFeatures();
  while ( resultIterator.nextFeature( resultFeature ) )
  {
    const QString towerLabel = remarkIndex >= 0 ? resultFeature.attribute( remarkIndex ).toString().trimmed() : QString();
    if ( towerLabel.isEmpty() )
      continue;
    ++detectionCounts[towerLabel];
    const QVariant visibilityValue = visibilityIndex >= 0 ? resultFeature.attribute( visibilityIndex ) : QVariant();
    const bool featureVisible = !visibilityValue.isValid() || visibilityValue.isNull() || visibilityValue.toInt() != 0;
    resultVisibility.insert( towerLabel, resultVisibility.value( towerLabel, true ) && featureVisible );
    if ( confidenceIndex >= 0 )
      maximumConfidence[towerLabel] = std::max( maximumConfidence.value( towerLabel, 0.0 ), resultFeature.attribute( confidenceIndex ).toDouble() );
    const QString resultType = recognitionResultType( sourceIndex >= 0 ? resultFeature.attribute( sourceIndex ).toString() : QString() );
    if ( recognitionResultTypePriority( resultType ) >= recognitionResultTypePriority( resultTypes.value( towerLabel ) ) )
      resultTypes.insert( towerLabel, resultType );
  }

  const int filter = mRecognitionResultFilter ? mRecognitionResultFilter->currentData().toInt() : 0;
  mRecognitionResultTable->setSortingEnabled( false );
  mRecognitionResultTable->setRowCount( 0 );
  QgsFeature towerFeature;
  QgsFeatureIterator towerIterator = towerLayer->getFeatures();
  int towerIndex = 0;
  int row = 0;
  while ( towerIterator.nextFeature( towerFeature ) )
  {
    ++towerIndex;
    const QString towerLabel = featureLabel( towerFeature, towerLayer->fields(), towerIndex );
    const int detectionCount = detectionCounts.value( towerLabel );
    const bool detected = detectionCount > 0;
    if ( ( filter == 1 && !detected ) || ( filter == 2 && detected ) )
      continue;

    mRecognitionResultTable->insertRow( row );
    QTableWidgetItem *sequenceItem = new QTableWidgetItem( QString::number( towerIndex ) );
    sequenceItem->setData( Qt::UserRole, towerLayer->id() );
    sequenceItem->setData( Qt::UserRole + 1, QVariant::fromValue<qlonglong>( static_cast<qlonglong>( towerFeature.id() ) ) );
    sequenceItem->setData( Qt::UserRole + 2, detectionCount );
    mRecognitionResultTable->setItem( row, 0, sequenceItem );
    mRecognitionResultTable->setItem( row, 1, new QTableWidgetItem( towerLabel ) );
    QTableWidgetItem *statusItem = new QTableWidgetItem( detected ? tr( "有识别结果" ) : tr( "无识别结果" ) );
    statusItem->setForeground( detected ? QColor( QStringLiteral( "#ff6b6b" ) ) : QColor( QStringLiteral( "#94a3b8" ) ) );
    mRecognitionResultTable->setItem( row, 2, statusItem );
    QTableWidgetItem *typeItem = new QTableWidgetItem( detected ? resultTypes.value( towerLabel, QStringLiteral( "识别" ) ) : QString() );
    typeItem->setForeground( typeItem->text() == QStringLiteral( "新增" ) ? QColor( QStringLiteral( "#60a5fa" ) )
                              : typeItem->text() == QStringLiteral( "修正" ) ? QColor( QStringLiteral( "#fbbf24" ) )
                                                                           : QColor( QStringLiteral( "#f87171" ) ) );
    mRecognitionResultTable->setItem( row, 3, typeItem );
    mRecognitionResultTable->setItem( row, 4, new QTableWidgetItem( QString::number( detectionCount ) ) );
    mRecognitionResultTable->setItem( row, 5, new QTableWidgetItem( detected ? QString::number( maximumConfidence.value( towerLabel ), 'f', 3 ) : QStringLiteral( "鈥擻" ) ) );
    if ( detected )
    {
      QWidget *editCell = new QWidget( mRecognitionResultTable );
      editCell->setObjectName( QStringLiteral( "EcoRecognitionCell" ) );
      QHBoxLayout *editLayout = new QHBoxLayout( editCell );
      editLayout->setContentsMargins( 5, 0, 5, 0 );
      editLayout->setSpacing( 4 );
      if ( towerLabel == mRecognitionEditingTowerLabel )
      {
        QLabel *editingLabel = new QLabel( tr( "正在编辑" ), editCell );
        editingLabel->setObjectName( QStringLiteral( "EcoRecognitionEditingLabel" ) );
        QToolButton *finishButton = new QToolButton( editCell );
        finishButton->setObjectName( QStringLiteral( "EcoRecognitionFinishButton" ) );
        finishButton->setIcon( ecoToolbarIcon( EcoToolbarIcon::Confirm ) );
        finishButton->setIconSize( QSize( 18, 18 ) );
        finishButton->setToolTip( tr( "完成编辑并保存图斑" ) );
        editLayout->addWidget( editingLabel );
        editLayout->addWidget( finishButton );
        connect( finishButton, &QToolButton::clicked, this, [this, row] { finishRecognitionResultForRow( row ); } );
      }
      QToolButton *deleteButton = new QToolButton( editCell );
      deleteButton->setObjectName( QStringLiteral( "EcoRecognitionDeleteButton" ) );
      deleteButton->setIcon( ecoToolbarIcon( EcoToolbarIcon::Delete ) );
      deleteButton->setIconSize( QSize( 18, 18 ) );
      deleteButton->setToolTip( tr( "删除该杆塔的扰动图斑" ) );
      editLayout->addWidget( deleteButton );
      editLayout->addStretch();
      connect( deleteButton, &QToolButton::clicked, this, [this, row] { deleteRecognitionResultForRow( row ); } );
      mRecognitionResultTable->setCellWidget( row, 6, editCell );
    }
    else
    {
      mRecognitionResultTable->setItem( row, 6, new QTableWidgetItem( QStringLiteral( "鈥擻" ) ) );
    }
    if ( detected )
    {
      QWidget *visibilityCell = new QWidget( mRecognitionResultTable );
      visibilityCell->setObjectName( QStringLiteral( "EcoRecognitionCell" ) );
      QHBoxLayout *visibilityLayout = new QHBoxLayout( visibilityCell );
      visibilityLayout->setContentsMargins( 0, 0, 0, 0 );
      visibilityLayout->setSpacing( 0 );
      QCheckBox *visibilityCheck = new EcoVsCodeCheckBox( visibilityCell );
      visibilityCheck->setObjectName( QStringLiteral( "EcoRecognitionVisibility" ) );
      const auto updateVisibilityText = [visibilityCheck]( bool visible ) {
        visibilityCheck->setToolTip( visible ? QObject::tr( "取消勾选后隐藏该杆塔的扰动图斑" ) : QObject::tr( "勾选后显示该杆塔的扰动图斑" ) );
      };
      const bool visible = resultVisibility.value( towerLabel, true );
      visibilityCheck->setChecked( visible );
      visibilityCheck->setText( QString() );
      visibilityCheck->setFixedSize( 18, 22 );
      updateVisibilityText( visible );
      visibilityLayout->addWidget( visibilityCheck, 0, Qt::AlignCenter );
      connect( visibilityCheck, &QCheckBox::toggled, this, [this, row, updateVisibilityText]( bool value ) {
        updateVisibilityText( value );
        setRecognitionResultVisibilityForRow( row, value );
      } );
      mRecognitionResultTable->setCellWidget( row, 7, visibilityCell );
    }
    else
    {
      mRecognitionResultTable->setItem( row, 7, new QTableWidgetItem( QStringLiteral( "鈥擻" ) ) );
    }
    ++row;
  }
  mRecognitionResultTable->resizeColumnsToContents();
  mRecognitionResultTable->setColumnWidth( 6, 150 );
  mRecognitionResultTable->setColumnWidth( 7, 34 );
}

void QgsEcoRestorationController::showRecognitionResultContextMenu( const QPoint &pos )
{
  if ( !mRecognitionResultTable )
    return;
  QTableWidgetItem *item = mRecognitionResultTable->itemAt( pos );
  if ( !item )
    return;
  const int row = item->row();
  QTableWidgetItem *sequenceItem = mRecognitionResultTable->item( row, 0 );
  if ( !sequenceItem )
    return;

  mRecognitionResultTable->selectRow( row );
  QMenu menu( mRecognitionResultTable );
  applyVsCodeMenuStyle( &menu );
  QAction *focusAction = menu.addAction( ecoToolbarIcon( EcoToolbarIcon::FullExtent ), tr( "定位" ) );
  connect( focusAction, &QAction::triggered, &menu, [this, row] { focusRecognitionResultRow( row, 1250.0 ); } );
  const bool hasDetection = sequenceItem->data( Qt::UserRole + 2 ).toInt() > 0;
  QAction *editAction = menu.addAction( hasDetection ? tr( "编辑图斑" ) : tr( "新增图斑" ) );
  connect( editAction, &QAction::triggered, &menu, [this, row, hasDetection] {
    if ( !focusRecognitionResultRow( row, 1250.0 ) )
      return;
    QTimer::singleShot( 140, this, [this, row, hasDetection] {
      if ( hasDetection )
        editRecognitionResultForRow( row );
      else
        addRecognitionResultForRow( row );
    } );
  } );
  menu.exec( mRecognitionResultTable->viewport()->mapToGlobal( pos ) );
}

bool QgsEcoRestorationController::focusRecognitionResultRow( int row, double scale )
{
  if ( !mRecognitionResultTable || !mApp )
    return false;
  QTableWidgetItem *item = mRecognitionResultTable->item( row, 0 );
  if ( !item )
    return false;
  QgsVectorLayer *towerLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( item->data( Qt::UserRole ).toString() ) );
  if ( !towerLayer )
    return false;
  QgsFeature feature;
  QgsFeatureIterator iterator = towerLayer->getFeatures( QgsFeatureRequest( static_cast<QgsFeatureId>( item->data( Qt::UserRole + 1 ).toLongLong() ) ) );
  if ( !iterator.nextFeature( feature ) )
    return false;
  const QgsPointXY point = featurePoint( feature.geometry() );
  if ( point.isEmpty() )
    return false;
  try
  {
    QgsCoordinateTransform transform( towerLayer->crs(), mApp->mapCanvas()->mapSettings().destinationCrs(), QgsProject::instance()->transformContext() );
    mApp->mapCanvas()->setCenter( transform.transform( point ) );
    mApp->mapCanvas()->zoomScale( scale );
    mApp->mapCanvas()->refresh();
    mApp->setActiveLayer( towerLayer );
  }
  catch ( const QgsCsException & )
  {
    showMessage( tr( "定位杆塔" ), tr( "无法将杆塔位置转换到当前地图坐标系。" ), true );
    return false;
  }
  return true;
}

void QgsEcoRestorationController::editRecognitionResultForRow( int row )
{
  if ( !mRecognitionResultTable )
    return;
  QTableWidgetItem *towerItem = mRecognitionResultTable->item( row, 1 );
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !towerItem || !resultLayer )
    return;
  const QString towerLabel = towerItem->text();
  if ( !focusRecognitionResultRow( row, 1250.0 ) )
    return;
  if ( !ensureEditableResultLayer( resultLayer ) )
    return;
  if ( !mRecognitionEditingTowerLabel.isEmpty() && mRecognitionEditingTowerLabel != towerLabel )
  {
    showMessage( tr( "编辑扰动图斑" ), tr( "请先在结果表格中完成当前正在编辑的图斑。" ), true );
    return;
  }

  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  const int confidenceIndex = resultLayer->fields().indexFromName( QStringLiteral( "confidence" ) );
  const int sourceIndex = resultLayer->fields().indexFromName( QStringLiteral( "source" ) );
  if ( remarkIndex < 0 )
    return;

  QgsFeature selectedFeature;
  bool hasSelectedFeature = false;
  QgsFeature feature;
  QgsFeatureIterator iterator = resultLayer->getFeatures();
  while ( iterator.nextFeature( feature ) )
  {
    if ( feature.attribute( remarkIndex ).toString().trimmed() != towerLabel )
      continue;
    if ( !hasSelectedFeature || ( confidenceIndex >= 0 && feature.attribute( confidenceIndex ).toDouble() > selectedFeature.attribute( confidenceIndex ).toDouble() ) )
    {
      selectedFeature = feature;
      hasSelectedFeature = true;
    }
  }
  if ( !hasSelectedFeature )
    return;

  if ( mApp->mapCanvas() && !mRecognitionSelectionColorChanged )
  {
    mRecognitionOriginalSelectionColor = mApp->mapCanvas()->selectionColor();
    mRecognitionSelectionColorChanged = true;
  }
  if ( mApp->mapCanvas() )
  {
    QColor editSelectionColor( QStringLiteral( "#dc2626" ) );
    editSelectionColor.setAlpha( 45 );
    mApp->mapCanvas()->setSelectionColor( editSelectionColor );
  }
  resultLayer->selectByIds( QgsFeatureIds() << selectedFeature.id() );
  if ( sourceIndex >= 0 && recognitionResultType( selectedFeature.attribute( sourceIndex ).toString() ) != QStringLiteral( "新增" ) )
    resultLayer->changeAttributeValue( selectedFeature.id(), sourceIndex, tr( "修正" ) );
  resultLayer->triggerRepaint();
  mApp->setActiveLayer( resultLayer );
  mApp->actionVertexToolActiveLayer()->trigger();
  mRecognitionEditingTowerLabel = towerLabel;
  if ( mRecognitionResultFilter && mRecognitionResultFilter->currentIndex() != 0 )
    mRecognitionResultFilter->setCurrentIndex( 0 );
  refreshRecognitionResultTable();
}

void QgsEcoRestorationController::addRecognitionResultForRow( int row )
{
  if ( !mRecognitionResultTable )
    return;
  QTableWidgetItem *towerItem = mRecognitionResultTable->item( row, 1 );
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !towerItem || !resultLayer )
    return;
  const QString towerLabel = towerItem->text();
  if ( !focusRecognitionResultRow( row, 1250.0 ) )
    return;
  if ( !ensureEditableResultLayer( resultLayer ) )
    return;
  if ( !mRecognitionEditingTowerLabel.isEmpty() && mRecognitionEditingTowerLabel != towerLabel )
  {
    showMessage( tr( "新增扰动图斑" ), tr( "请先在结果表格中完成当前正在编辑的图斑。" ), true );
    return;
  }

  const int sourceIndex = resultLayer->fields().indexFromName( QStringLiteral( "source" ) );
  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  if ( sourceIndex >= 0 )
    resultLayer->setDefaultValueDefinition( sourceIndex, QgsDefaultValue( QStringLiteral( "'新增'" ) ) );
  if ( remarkIndex >= 0 )
  {
    QString escapedTowerLabel = towerLabel;
    escapedTowerLabel.replace( QLatin1Char( '\'' ), QStringLiteral( "''" ) );
    resultLayer->setDefaultValueDefinition( remarkIndex, QgsDefaultValue( QStringLiteral( "'%1'" ).arg( escapedTowerLabel ) ) );
  }
  if ( mApp->mapCanvas() && !mRecognitionSelectionColorChanged )
  {
    mRecognitionOriginalSelectionColor = mApp->mapCanvas()->selectionColor();
    mRecognitionSelectionColorChanged = true;
  }
  if ( mApp->mapCanvas() )
  {
    QColor editSelectionColor( QStringLiteral( "#dc2626" ) );
    editSelectionColor.setAlpha( 45 );
    mApp->mapCanvas()->setSelectionColor( editSelectionColor );
  }
  mRecognitionEditingTowerLabel = towerLabel;
  if ( mRecognitionResultFilter && mRecognitionResultFilter->currentIndex() != 0 )
    mRecognitionResultFilter->setCurrentIndex( 0 );
  refreshRecognitionResultTable();
  mApp->setActiveLayer( resultLayer );
  mApp->actionAddFeature()->trigger();
}

void QgsEcoRestorationController::finishRecognitionResultForRow( int row )
{
  if ( mRecognitionResultTable && !mRecognitionEditingTowerLabel.isEmpty() )
  {
    QTableWidgetItem *towerItem = mRecognitionResultTable->item( row, 1 );
    if ( !towerItem || towerItem->text() != mRecognitionEditingTowerLabel )
      return;
  }
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !mSmartSegmentationStandalone && resultLayer && mSmartSegmentationTargetLayerId == resultLayer->id()
       && !mSmartSegmentationPendingGeometry.isNull() && !mSmartSegmentationPendingGeometry.isEmpty() )
    commitSmartSegmentationPreview();
  finishRecognitionEditing( resultLayer );
}

void QgsEcoRestorationController::deleteRecognitionResultForRow( int row )
{
  if ( !mRecognitionResultTable || !mApp )
    return;

  QTableWidgetItem *towerItem = mRecognitionResultTable->item( row, 1 );
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !towerItem || !resultLayer )
    return;

  const QString towerLabel = towerItem->text();
  if ( !mRecognitionEditingTowerLabel.isEmpty() && mRecognitionEditingTowerLabel != towerLabel )
  {
    showMessage( tr( "删除扰动图斑" ), tr( "请先完成当前正在编辑的图斑，再删除其他杆塔的结果。" ), true );
    return;
  }

  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  if ( remarkIndex < 0 )
    return;

  QgsFeatureIds featureIds;
  QgsFeature feature;
  QgsFeatureIterator iterator = resultLayer->getFeatures();
  while ( iterator.nextFeature( feature ) )
  {
    if ( feature.attribute( remarkIndex ).toString().trimmed() == towerLabel )
      featureIds.insert( feature.id() );
  }
  if ( featureIds.isEmpty() )
  {
    refreshRecognitionResultTable();
    return;
  }

  QMessageBox confirmBox( mApp );
  confirmBox.setIcon( QMessageBox::Warning );
  confirmBox.setWindowTitle( tr( "删除扰动图斑" ) );
  confirmBox.setText( tr( "确定删除杆塔?1”的扰动图斑吗？" ).arg( towerLabel ) );
  confirmBox.setInformativeText( tr( "删除后该杆塔将在结果表中显示为无识别结果。" ) );
  QPushButton *deleteButton = confirmBox.addButton( tr( "删除" ), QMessageBox::DestructiveRole );
  QPushButton *cancelButton = confirmBox.addButton( tr( "取消" ), QMessageBox::RejectRole );
  confirmBox.setDefaultButton( cancelButton );
  applyVsCodeDialogStyle( &confirmBox );
  confirmBox.exec();
  if ( confirmBox.clickedButton() != deleteButton )
    return;

  if ( !resultLayer->isEditable() && !resultLayer->startEditing() )
  {
    showMessage( tr( "删除扰动图斑" ), tr( "成果图层无法进入编辑状态，请检查文件写入权限。" ), true );
    return;
  }

  resultLayer->beginEditCommand( tr( "删除杆塔扰动图斑" ) );
  if ( !resultLayer->deleteFeatures( featureIds ) )
  {
    resultLayer->destroyEditCommand();
    showMessage( tr( "删除扰动图斑" ), tr( "图斑删除失败，请检查成果数据是否可编辑。" ), true );
    return;
  }
  resultLayer->endEditCommand();
  finishRecognitionEditing( resultLayer );
  if ( resultLayer->isEditable() )
  {
    showMessage( tr( "删除扰动图斑" ), tr( "图斑已加入删除队列，但保存失败，请检查数据文件写入权限。" ), true );
    return;
  }

  QgsProject::instance()->setDirty( true );
  resultLayer->triggerRepaint();
  if ( mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
  refreshRecognitionResultTable();
}

void QgsEcoRestorationController::setRecognitionResultVisibilityForRow( int row, bool visible )
{
  if ( !mRecognitionResultTable )
    return;

  QTableWidgetItem *towerItem = mRecognitionResultTable->item( row, 1 );
  QgsVectorLayer *resultLayer = qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mRecognitionResultLayerId ) );
  if ( !towerItem || !resultLayer )
    return;

  const int remarkIndex = resultLayer->fields().indexFromName( QStringLiteral( "remark" ) );
  const int visibilityIndex = ensureRecognitionVisibilityField( resultLayer );
  if ( remarkIndex < 0 || visibilityIndex < 0 )
    return;

  QgsFeatureIds featureIds;
  QgsFeature feature;
  QgsFeatureIterator iterator = resultLayer->getFeatures();
  while ( iterator.nextFeature( feature ) )
  {
    if ( feature.attribute( remarkIndex ).toString().trimmed() == towerItem->text() )
      featureIds.insert( feature.id() );
  }
  if ( featureIds.isEmpty() )
    return;

  const bool alreadyEditable = resultLayer->isEditable();
  if ( !alreadyEditable && !resultLayer->startEditing() )
  {
    showMessage( tr( "切换图斑显示" ), tr( "成果图层无法进入编辑状态，请检查文件写入权限。" ), true );
    refreshRecognitionResultTable();
    return;
  }

  bool changed = true;
  for ( QgsFeatureId featureId : std::as_const( featureIds ) )
    changed = resultLayer->changeAttributeValue( featureId, visibilityIndex, visible ? 1 : 0 ) && changed;

  if ( !alreadyEditable )
  {
    if ( !changed || !resultLayer->commitChanges() )
    {
      resultLayer->rollBack();
      showMessage( tr( "切换图斑显示" ), tr( "无法保存图斑显隐状态。" ), true );
      refreshRecognitionResultTable();
      return;
    }
  }

  resultLayer->triggerRepaint();
  QgsProject::instance()->setDirty( true );
  if ( mApp && mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
}

void QgsEcoRestorationController::finishRecognitionEditing( QgsVectorLayer *layer )
{
  if ( !layer || !mApp )
    return;

  if ( layer->isEditable() )
    mApp->saveEdits( layer, false, true );
  if ( layer->isEditable() )
    return;

  normalizeRecognitionResultLayerAfterEdit( layer );
  if ( mApp->mapCanvas() )
    mApp->actionPan()->trigger();
  if ( QWidget *vertexEditor = mApp->findChild<QWidget *>( QStringLiteral( "VertexEditor" ) ) )
    vertexEditor->hide();
}

void QgsEcoRestorationController::normalizeRecognitionResultLayerAfterEdit( QgsVectorLayer *layer )
{
  if ( !layer || !mApp
       || layer->customProperty( QStringLiteral( "eco/resultType" ) ).toString() != QLatin1String( "construction-disturbance" ) )
    return;

  mRecognitionEditingTowerLabel.clear();
  layer->removeSelection();
  if ( mRecognitionSelectionColorChanged && mApp->mapCanvas() )
  {
    mApp->mapCanvas()->setSelectionColor( mRecognitionOriginalSelectionColor );
    mRecognitionSelectionColorChanged = false;
  }
  // QGIS keeps a temporary yellow edit/selection renderer after committing a
  // feature from an attribute form. Restore the dedicated red translucent
  // result renderer once the edit transaction has completed.
  applyBusinessStyle( layer );
  layer->triggerRepaint();
  if ( mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
  if ( mRecognitionResultLayerId == layer->id() )
    scheduleRecognitionResultTableRefresh();
}

void QgsEcoRestorationController::showTowerStyleDialog( QgsVectorLayer *layer )
{
  if ( !layer || layer->geometryType() != Qgis::GeometryType::Point || !mApp )
    return;

  EcoTowerIconSizeDialog dialog( layer, mApp, projectWorkspace() );
  dialog.exec();
}

void QgsEcoRestorationController::showRecognitionPanel()
{
  if ( !mRecognitionDock )
    return;

  mRecognitionPreviewActive = true;
  if ( mRecognitionPanelAction && !mRecognitionPanelAction->isChecked() )
  {
    const QSignalBlocker blocker( mRecognitionPanelAction );
    mRecognitionPanelAction->setChecked( true );
  }
  mRecognitionDock->show();
  mRecognitionDock->raise();
  updateRecognitionPreview();
}

void QgsEcoRestorationController::toggleTowerDisplayMode( QgsVectorLayer *layer )
{
  if ( !layer || layer->geometryType() != Qgis::GeometryType::Point )
    return;

  const bool useTowerIcon = !shouldUseTowerIconRenderer( layer );
  layer->setCustomProperty( sTowerDisplayModeProperty, useTowerIcon ? QStringLiteral( "icon" ) : QStringLiteral( "vector" ) );
  applyBusinessStyle( layer );
  QgsProject::instance()->setDirty( true );
  if ( mApp && mApp->mapCanvas() )
    mApp->mapCanvas()->refresh();
}

QgsRasterLayer *QgsEcoRestorationController::selectedRecognitionRasterLayer() const
{
  QgsRasterLayer *rasterLayer = mRecognitionRasterCombo
                                  ? qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( mRecognitionRasterCombo->currentData().toString() ) )
                                  : nullptr;
  if ( rasterLayer )
    return rasterLayer;

  const QString phaseId = currentPhaseId();
  const auto layers = QgsProject::instance()->mapLayers();
  for ( QgsMapLayer *layer : layers )
  {
    if ( QgsRasterLayer *candidate = qobject_cast<QgsRasterLayer *>( layer ) )
    {
      const QString layerPhaseId = candidate->customProperty( sPhaseIdProperty ).toString();
      if ( phaseId.isEmpty() || layerPhaseId.isEmpty() || layerPhaseId == phaseId )
        return candidate;
    }
  }
  for ( QgsMapLayer *layer : layers )
  {
    if ( QgsRasterLayer *candidate = qobject_cast<QgsRasterLayer *>( layer ) )
      return candidate;
  }
  return nullptr;
}

QString QgsEcoRestorationController::askSmartSegmentationLayerName() const
{
  QDialog dialog( mApp );
  dialog.setWindowTitle( tr( "保存智能分割成果" ) );
  dialog.setModal( true );
  dialog.resize( 365, 180 );

  QVBoxLayout *layout = new QVBoxLayout( &dialog );
  layout->setContentsMargins( 16, 14, 16, 14 );
  layout->setSpacing( 10 );

  QLabel *hint = new QLabel( tr( "请输入本次智能分割成果名称。该成果将作为独立图层保存，不写入扰动识别结果表。" ), &dialog );
  hint->setWordWrap( true );
  layout->addWidget( hint );

  QLineEdit *nameEdit = new QLineEdit( &dialog );
  nameEdit->setPlaceholderText( tr( "例如：临建区分割、道路边界分割" ) );
  nameEdit->setText( tr( "智能分割成果_%1" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) ) ) );
  nameEdit->selectAll();
  layout->addWidget( nameEdit );

  QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
  if ( QPushButton *okButton = buttons->button( QDialogButtonBox::Ok ) )
    okButton->setText( tr( "保存" ) );
  if ( QPushButton *cancelButton = buttons->button( QDialogButtonBox::Cancel ) )
    cancelButton->setText( tr( "取消" ) );
  layout->addWidget( buttons );

  connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
  applyVsCodeDialogStyle( &dialog );

  if ( dialog.exec() != QDialog::Accepted )
    return QString();

  QString name = nameEdit->text().trimmed();
  if ( name.isEmpty() )
    name = tr( "智能分割成果_%1" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) ) );
  return sanitizedName( name );
}

QgsVectorLayer *QgsEcoRestorationController::selectedTowerLayer() const
{
  return mTowerLayerCombo ? qobject_cast<QgsVectorLayer *>( QgsProject::instance()->mapLayer( mTowerLayerCombo->currentData().toString() ) ) : nullptr;
}

QString QgsEcoRestorationController::projectWorkspace() const
{
  QString path = QgsProject::instance()->readEntry( sProjectGroup, QStringLiteral( "workspace" ) );
  if ( path.isEmpty() && !QgsProject::instance()->fileName().isEmpty() )
    path = QFileInfo( QgsProject::instance()->fileName() ).absolutePath();
  return path;
}

void QgsEcoRestorationController::showMessage( const QString &title, const QString &message, bool warning ) const
{
  const bool useToast = title.contains( tr( "识别" ) ) || title.contains( tr( "扰动" ) );
  if ( warning && useToast )
  {
    showEcoToast( mApp, title, message, true, 3600 );
    return;
  }
  if ( useToast )
  {
    showEcoToast( mApp, title, message, false, 3000 );
    return;
  }
  QMessageBox dialog( warning ? QMessageBox::Warning : QMessageBox::Information, title, message, QMessageBox::Ok, mApp );
  applyVsCodeDialogStyle( &dialog );
  dialog.exec();
}
