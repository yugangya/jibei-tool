/***************************************************************************
  qgsecosam2onnxinference.h
 ***************************************************************************/

#ifndef QGSECOSAM2ONNXINFERENCE_H
#define QGSECOSAM2ONNXINFERENCE_H

#include "qgsgeometry.h"
#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <QImage>
#include <QString>
#include <QVector>

class QgsRasterLayer;

/**
 * Native SAM2 ONNX prompt segmentation for the business disturbance workflow.
 *
 * The model is intentionally used as an assisted delineation tool: an
 * operator supplies a positive point or a rectangular prompt and this class
 * returns the best mask as a polygon in the raster layer CRS.
 */
class QgsEcoSam2OnnxInference
{
  public:
    enum class PromptMode
    {
      Point,
      Box,
    };

    struct Parameters
    {
      QString encoderModelPath;
      QString decoderModelPath;
      QImage inputImage;
      QgsRectangle imageExtent;
      QgsRectangle cropExtent;
      QgsPointXY promptPoint;
      QVector<QgsPointXY> promptPoints;
      QVector<int> promptLabels;
      QgsRectangle promptBox;
      PromptMode promptMode = PromptMode::Point;
    };

    struct Result
    {
      bool success = false;
      QString error;
      QgsGeometry geometry;
      double score = 0.0;
      QgsRectangle cropExtent;
    };

    static bool isRuntimeAvailable();
    static Result run( QgsRasterLayer *rasterLayer, const Parameters &parameters );
};

#endif // QGSECOSAM2ONNXINFERENCE_H
