/***************************************************************************
  qgsecoonnxinference.h
 ***************************************************************************/

#ifndef QGSECOONNXINFERENCE_H
#define QGSECOONNXINFERENCE_H

#include "qgsgeometry.h"
#include "qgsrectangle.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QgsRasterLayer;
class QImage;

/** Lightweight result returned by the native construction disturbance model. */
struct QgsEcoOnnxDetection
{
  QgsGeometry geometry;
  double confidence = 0;
  int classId = 0;
  QString className;
  QString sourceName;
  int tileIndex = 0;
};

/**
 * Executes a YOLO-style segmentation ONNX model against a georeferenced raster.
 *
 * Raster tiles are rendered through QGIS so the exact layer stretch visible to
 * an operator is supplied to the model. Mask prototypes are converted back to
 * source CRS polygons and overlap detections are suppressed across tile seams.
 */
class QgsEcoOnnxInference
{
  public:
    struct Parameters
    {
      QString modelPath;
      int fallbackImageSize = 640;
      int overlapPercent = 20;
      double confidence = 0.35;
      double iouThreshold = 0.35;
      int maximumTiles = 300;
      QVector<QgsRectangle> targetExtents;
      QStringList targetLabels;
    };

    struct Result
    {
      bool success = false;
      QString error;
      QString modelDescription;
      int processedTiles = 0;
      QVector<QgsEcoOnnxDetection> detections;
    };

    using ProgressCallback = std::function<bool( int current, int total, const QString &message, const QString &sourceName, const QImage &previewImage )>;

    static bool isRuntimeAvailable();
    static Result run( QgsRasterLayer *rasterLayer, const Parameters &parameters, const ProgressCallback &progress );
    static Result run( const QImage &inputImage, const Parameters &parameters, const ProgressCallback &progress = ProgressCallback() );
};

#endif // QGSECOONNXINFERENCE_H
