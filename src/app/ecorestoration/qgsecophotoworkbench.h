/***************************************************************************
  qgsecophotoworkbench.h
  --------------------------------------
  Phase-bound photo interpretation workbench. Photo records deliberately
  remain independent from the QGIS map-layer tree.
 ***************************************************************************/

#ifndef QGSECOPHOTOWORKBENCH_H
#define QGSECOPHOTOWORKBENCH_H

#include <QPolygonF>
#include <QImage>
#include <QPointer>
#include <QQueue>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QVariantAnimation;
class QTabWidget;
class QCheckBox;
template<class T> class QFutureWatcher;

class EcoPhotoAnnotationView;

/**
 * Lightweight workbench for large batches of field photographs.
 *
 * Each phase owns a small JSON index in its workspace. Imported source
 * photographs are referenced in place (never edited or moved), while fused
 * renderings and YOLO deliverables are written as derived outputs.
 */
class QgsEcoPhotoWorkbench final : public QWidget
{
  public:
    using MessageCallback = std::function<void( const QString &title, const QString &message, bool warning )>;

    struct Annotation
    {
      int classId = 0;
      QString className;
      QString source = QStringLiteral( "manual" );
      QPolygonF normalizedPolygon;
    };

    struct PhotoRecord
    {
      QString sourcePath;
      QString fusedPath;
      QVector<Annotation> annotations;
    };

    explicit QgsEcoPhotoWorkbench( MessageCallback messageCallback, QWidget *parent = nullptr );

    //! Rebinds the isolated photo workspace to the active project phase.
    void setPhaseContext( const QString &phaseId, const QString &phaseName, const QString &workspacePath );
    bool isProcessing() const { return mBusy; }

  private:
    void reloadIndex();
    bool saveIndex() const;
    void scheduleIndexSave();
    void rebuildPhotoList();
    void queueThumbnail( const QString &path, QWidget *entry );
    void loadNextThumbnail();
    void loadPreviewImage( bool fused );
    void cleanupObsoleteFusedPhotos();
    void showSelectedPhoto();
    void showPhotoAt( int index );
    void showSelectedFusedPhoto();
    void rebuildFusedList();
    void updatePhotoListItem( int index, bool loadThumbnail = false );
    void updateFusedListItem( int index, bool loadThumbnail = false );
    void updateSummary();
    void setBusy( bool busy, const QString &status = QString() );
    void setTaskProgress( int current, int total, const QString &status );
    void notify( const QString &title, const QString &message, bool warning = false ) const;

    void importPhotos();
    void removeSelectedPhotos();
    void setManualDrawingEnabled( bool enabled );
    void setPhotoSmartSegmentationEnabled( bool enabled );
    void runPhotoSmartSegmentation( const QVector<QPointF> &normalizedPoints, const QVector<int> &promptLabels );
    void batchRecognizePhotos();
    void setDeleteAnnotationEnabled( bool enabled );
    void setAnnotationEditingEnabled( bool enabled );
    void updateAnnotation( int annotationIndex, const QPolygonF &normalizedPolygon );
    void deleteAnnotationAt( int annotationIndex );
    void addAnnotation( int classId, const QString &className, const QPolygonF &normalizedPolygon, const QString &source = QStringLiteral( "manual" ) );
    void updateSelectAllCheckBoxState();
    void exportOriginalPhotos();
    void createFusedPhotos();
    void exportFusedPhotos();
    void exportYoloSamples();

    QString mPhaseId;
    QString mPhaseName;
    QString mWorkspacePath;
    bool mBusy = false;
    bool mCreatingFusion = false;
    bool mIndexSavePending = false;
    bool mPendingPhaseContext = false;
    quint64 mContextGeneration = 0;
    QQueue<QPair<QString, QPointer<QWidget>>> mThumbnailRequests;
    QPointer<QFutureWatcher<QImage>> mThumbnailWatcher;
    QPointer<QFutureWatcher<QImage>> mPhotoPreviewWatcher;
    QPointer<QFutureWatcher<QImage>> mFusedPreviewWatcher;
    quint64 mPhotoPreviewRequest = 0;
    quint64 mFusedPreviewRequest = 0;
    int mRequestedPhotoIndex = -1;
    int mRequestedFusedIndex = -1;
    QString mDisplayedPhotoPath;
    QString mPendingPhaseId;
    QString mPendingPhaseName;
    QString mPendingWorkspacePath;
    MessageCallback mMessageCallback;

    QLabel *mPhaseLabel = nullptr;
    QLabel *mSummaryLabel = nullptr;
    QLabel *mMetadataLabel = nullptr;
    QLabel *mStatusLabel = nullptr;
    QProgressBar *mProgressBar = nullptr;
    QPointer<QVariantAnimation> mProgressAnimation;
    QListWidget *mPhotoList = nullptr;
    EcoPhotoAnnotationView *mPreview = nullptr;
    QTabWidget *mPhotoTabs = nullptr;
    QListWidget *mFusedList = nullptr;
    EcoPhotoAnnotationView *mFusedPreview = nullptr;
    QLabel *mFusedMetadataLabel = nullptr;
    QComboBox *mAnnotationClassCombo = nullptr;
    QPushButton *mImportButton = nullptr;
    QPushButton *mRemoveButton = nullptr;
    QPushButton *mManualDrawButton = nullptr;
    QPushButton *mPhotoSmartSegmentationButton = nullptr;
    QPushButton *mPhotoRecognitionButton = nullptr;
    QPushButton *mDeleteAnnotationButton = nullptr;
    QPushButton *mEditAnnotationButton = nullptr;
    QPushButton *mCreateFusionButton = nullptr;
    QPushButton *mExportOriginalButton = nullptr;
    QPushButton *mExportFusedButton = nullptr;
    QPushButton *mExportYoloButton = nullptr;
    QCheckBox *mSelectAllCheckBox = nullptr;
    bool mDeleteMode = false;
    QSet<QString> mCheckedPhotoPaths;
    QSet<QString> mObsoleteFusedPaths;
    QVector<PhotoRecord> mPhotos;
};

#endif // QGSECOPHOTOWORKBENCH_H
