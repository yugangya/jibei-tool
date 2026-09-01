/***************************************************************************
  qgsecorestorationcontroller.h
  --------------------------------------
  Native environmental restoration workflow for the desktop application.
 ***************************************************************************/

#ifndef QGSECORESTORATIONCONTROLLER_H
#define QGSECORESTORATIONCONTROLLER_H

#include "qgsgeometry.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsrectangle.h"

#include <QColor>
#include <QObject>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QLabel;
class QProgressBar;
class QPoint;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTableWidget;
class QToolBar;
class QToolButton;
class QWidget;

class QgisApp;
class QgsCoordinateReferenceSystem;
class QgsDockWidget;
class QgsLayerTreeGroup;
class QgsLayerTreeNode;
class QgsLayerTreeView;
class QgsMapCanvas;
class QgsMapLayer;
class QgsMapCanvasDockWidget;
class QgsMapTool;
class QgsMapToolExtent;
class QgsPointXY;
class QgsEcoPhotoWorkbench;
class QgsRasterLayer;
class QgsRubberBand;
class QgsVectorLayer;

/**
 * Owns the C++ environmental protection and ecological restoration workspace.
 *
 * The controller deliberately builds on QGIS project, layer tree, digitizing
 * and rendering APIs. This keeps the business workflow isolated while the
 * mature GIS implementation remains available underneath it.
 */
class QgsEcoRestorationController final : public QObject
{
  public:
    explicit QgsEcoRestorationController( QgisApp *app );
    void activateBusinessStartupLayout();

  private:
    QWidget *createDockContents();
    void createWelcomeOverlay();
    void updateWelcomeOverlay();
    QWidget *createProjectPage();
    QWidget *createDataPage();
    QWidget *createRecognitionPage();
    QWidget *createReviewPage();
    QWidget *createPhotoPage();
    QWidget *createScreenshotPage();
    QWidget *createWorkflowCard( const QString &number, const QString &title, const QString &description );
    void createToolbar();
    bool eventFilter( QObject *watched, QEvent *event ) override;

    void createBusinessProject();
    void createPhase();
    QString createPhaseInternal( const QString &phaseName, bool makeCurrent = true );
    void deletePhase( const QString &phaseId );
    void setCurrentPhase( const QString &phaseId );
    void showPhaseComparisonDialog();
    void openPhaseComparison( const QString &leftPhaseId, const QString &rightPhaseId );
    void importImagery( const QString &targetPhaseId = QString() );
    void importVectors();
    void importTowerVectors();
    void removeProjectLayersSafely( const QStringList &layerIds );
    void createManualVectorLayer( const QString &kind );
    void finishManualTowerDrawing();
    void startDrawingOnActiveLayer( const QString &geometryKind );
    void showRecognitionPanel();
    void runTowerRecognition();
    void startExtentRecognition();
    void cancelExtentRecognition();
    void runRecognition( const QVector<QgsRectangle> &targetExtents, const QStringList &targetLabels, const QString &towerLayerId = QString() );
    QgsVectorLayer *createResultLayer( bool restoration = false, bool notify = true, const QString &phaseId = QString() );
    QgsVectorLayer *createSmartSegmentationLayer( const QString &layerName, const QgsCoordinateReferenceSystem &crs, bool notify = false );
    void startAddingPolygon();
    void startVertexEditing();
    void selectResultFeatures();
    void deleteSelectedFeatures();
    void saveResultEdits();
    void exportTowerScreenshots();
    void exportDisturbanceResultYoloSamples( const QString &resultLayerId );
    void setCompactMode( bool enabled );
    void setThreeDMode( bool enabled );
    void hideNativeQgisWidgets();
    void toggleTowerDisplayMode( QgsVectorLayer *layer );
    QVector<QgsRectangle> towerRecognitionExtents( QgsVectorLayer *towerLayer, const QgsCoordinateReferenceSystem &targetCrs, QStringList *labels = nullptr ) const;
    void updateRecognitionPreview();
    void clearRecognitionPreview();
    QgsVectorLayer *ensureDisturbanceResultLayer( const QString &phaseId = QString() );
    void attachRecognitionResultLayer( QgsVectorLayer *layer );
    void setRecognitionProgress( int current, int total );
    void showRecognitionResultTable( const QString &resultLayerId );
    void scheduleRecognitionResultTableRefresh();
    void refreshRecognitionResultTable();
    void showRecognitionResultContextMenu( const QPoint &pos );
    bool focusRecognitionResultRow( int row, double scale = 1250.0 );
    void editRecognitionResultForRow( int row );
    void addRecognitionResultForRow( int row );
    void finishRecognitionResultForRow( int row );
    void deleteRecognitionResultForRow( int row );
    void setRecognitionResultVisibilityForRow( int row, bool visible );
    void finishRecognitionEditing( QgsVectorLayer *layer );
    void normalizeRecognitionResultLayerAfterEdit( QgsVectorLayer *layer );
    void showTowerStyleDialog( QgsVectorLayer *layer );
    QgsRasterLayer *selectedRecognitionRasterLayer() const;
    QString askSmartSegmentationLayerName() const;
    void startSmartSegmentation();
    void startSmartSegmentationForLayer( const QString &layerId );
    void previewSmartSegmentationPrompt( const QVector<QgsPointXY> &canvasPoints, const QVector<int> &promptLabels );
    void commitSmartSegmentationPreview();
    void leaveSmartSegmentationTool();

    void ensureBusinessGroups();
    QString ensureCurrentPhase();
    QString currentPhaseId() const;
    QString currentPhaseName( const QString &phaseId = QString() ) const;
    QStringList phaseIds() const;
    QString phaseWorkspace( const QString &phaseId = QString() ) const;
    QgsLayerTreeGroup *commonDataGroup( bool create = true ) const;
    QgsLayerTreeGroup *phaseGroup( const QString &phaseId, bool create = true ) const;
    QgsLayerTreeGroup *phaseSubGroup( const QString &phaseId, const QString &groupName, bool create = true ) const;
    QgsLayerTreeGroup *phaseResultGroup( const QString &phaseId, const QString &groupName, bool create = true ) const;
    QgsLayerTreeGroup *targetBusinessGroupForLayer( QgsMapLayer *layer ) const;
    QList<QgsMapLayer *> layersForPhaseComparison( const QString &phaseId ) const;
    void refreshPhaseChoices();
    void organizeProjectTree();
    QgsLayerTreeGroup *projectTreeRoot() const;
    bool isNodeInsideProject( QgsLayerTreeNode *node ) const;
    void classifyAddedLayers( const QList<QgsMapLayer *> &layers );
    void moveLayerToBusinessGroup( QgsMapLayer *layer, const QString &groupName );
    QString businessGroupForLayer( QgsMapLayer *layer ) const;
    void refreshProjectState();
    void scheduleProjectStateRefresh();
    void refreshLayerChoices();
    void refreshRecognitionRasterOrder();
    void refreshPhaseComparisonCanvases();
    void refreshPhotoWorkbenchContext();
    void locateLayers( const QStringList &layerIds );
    void syncBusinessProjectView( const QList<QgsMapLayer *> &extraLayers = QList<QgsMapLayer *>() );
    QgsVectorLayer *selectedResultLayer() const;
    QgsVectorLayer *selectedTowerLayer() const;
    bool ensureEditableResultLayer( QgsVectorLayer *layer );
    QString projectWorkspace() const;
    void showMessage( const QString &title, const QString &message, bool warning = false ) const;

    QgisApp *mApp = nullptr;
    QPointer<QgsDockWidget> mDock;
    QPointer<QWidget> mWelcomeOverlay;
    QPointer<QToolBar> mToolbar;
    QPointer<QLabel> mProjectNameLabel;
    QPointer<QLabel> mProjectPathLabel;
    QPointer<QLabel> mProjectInfoLabel;
    QPointer<QLabel> mDataSummaryLabel;
    QPointer<QLabel> mDrawingStatusLabel;
    QPointer<QComboBox> mCurrentPhaseCombo;
    QPointer<QComboBox> mRecognitionRasterCombo;
    QPointer<QDoubleSpinBox> mConfidenceSpin;
    QPointer<QComboBox> mRecognitionTowerCombo;
    QPointer<QSpinBox> mRecognitionRangeSpin;
    QPointer<QToolButton> mRecognitionPreviewSwitch;
    QPointer<QPushButton> mRecognitionRunButton;
    QPointer<QWidget> mRecognitionScanPreview;
    int mRecognitionScanTileIndex = 0;
    QPointer<QLabel> mRecognitionSelectionLabel;
    QPointer<QgsMapToolExtent> mRecognitionExtentTool;
    QPointer<QgsMapTool> mRecognitionPreviousMapTool;
    QPointer<QgsDockWidget> mRecognitionDock;
    QPointer<QgsDockWidget> mPhotoDock;
    QPointer<QAction> mRecognitionPanelAction;
    QPointer<QAction> mExtentRecognitionAction;
    QPointer<QAction> mPhaseCompareAction;
    QPointer<QgsRubberBand> mRecognitionPreview;
    bool mRecognitionPreviewActive = false;
    bool mRecognitionRasterRefreshPending = false;
    bool mRecognitionRunning = false;
    QPointer<QProgressBar> mRecognitionProgressBar;
    QPointer<QgsDockWidget> mRecognitionResultDock;
    QPointer<QTableWidget> mRecognitionResultTable;
    QPointer<QComboBox> mRecognitionResultFilter;
    QString mRecognitionResultLayerId;
    QStringList mRecognitionObservedResultLayerIds;
    bool mRecognitionResultRefreshPending = false;
    QColor mRecognitionOriginalSelectionColor;
    bool mRecognitionSelectionColorChanged = false;
    // Project clearing and writing emit synchronous QGIS signals. Defer the
    // workbench refresh until the project transition has completely finished.
    bool mProjectTransitionInProgress = false;
    bool mProjectStateRefreshPending = false;
    bool mLayerImportInProgress = false;
    QString mRecognitionEditingTowerLabel;
    QString mManualTowerDrawingLayerId;
    QPointer<QAction> mSmartSegmentationAction;
    QPointer<QgsMapTool> mSmartSegmentationTool;
    QPointer<QgsMapTool> mSmartSegmentationPreviousMapTool;
    QPointer<QgsRubberBand> mSmartSegmentationPreview;
    QgsGeometry mSmartSegmentationPendingGeometry;
    double mSmartSegmentationPendingScore = 0.0;
    QString mSmartSegmentationRequestedLayerId;
    QString mSmartSegmentationTargetLayerId;
    qint64 mSmartSegmentationFeatureId = -1;
    bool mSmartSegmentationCommitInProgress = false;
    bool mSmartSegmentationStandalone = true;
    QgsCoordinateReferenceSystem mSmartSegmentationOutputCrs;
    QPointer<QToolButton> mThreeDSwitch;
    QString mThreeDViewName;
    QPointer<QComboBox> mResultLayerCombo;
    QPointer<QComboBox> mTowerLayerCombo;
    QPointer<QSpinBox> mScreenshotRangeSpin;
    QPointer<QSpinBox> mScreenshotSizeSpin;
    QList<QPointer<QToolBar>> mHiddenToolbars;
    QList<QPointer<QDockWidget>> mHiddenDocks;
    QPointer<QgsMapCanvasDockWidget> mPhaseCompareLeftDock;
    QPointer<QgsMapCanvasDockWidget> mPhaseCompareRightDock;
    QPointer<QWidget> mPhaseComparePage;
    QPointer<QSplitter> mPhaseCompareSplitter;
    QPointer<QWidget> mPhaseCompareLeftPanel;
    QPointer<QWidget> mPhaseCompareRightPanel;
    QPointer<QgsMapCanvas> mPhaseCompareLeftCanvas;
    QPointer<QgsMapCanvas> mPhaseCompareRightCanvas;
    QString mPhaseCompareLeftPhaseId;
    QString mPhaseCompareRightPhaseId;
    bool mPhaseCompareRefreshPending = false;
    quint64 mPhaseCompareSession = 0;
    QPointer<QWidget> mPhaseSwipeHandle;
    bool mPhaseSwipeMode = false;
    bool mPhaseSwipeDragging = false;
    bool mPhasePanelDragging = false;
    QPoint mPhasePanelDragStart;
    QPointer<QgsEcoPhotoWorkbench> mPhotoWorkbench;
};

#endif // QGSECORESTORATIONCONTROLLER_H
