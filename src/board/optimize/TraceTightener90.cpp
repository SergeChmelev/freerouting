#include "board/optimize/TraceTightener90.h"

#include "board/facade/RoutingBoard.h"
#include "board/state/ChangedArea.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"

namespace fr {

Polyline* TraceTightener90::pullTight(Polyline* polyline) {
  Polyline* newResult = avoidAcidTraps(polyline);
  Polyline* prevResult = nullptr;
  for (int32_t rounds = 0; newResult != prevResult && !mustStopAfter(rounds); ++rounds) {
    prevResult = newResult;
    Polyline* tmp1 = trySkipSecondCorner(prevResult);
    Polyline* tmp2 = trySkipCorners(tmp1);
    newResult = repositionLines(tmp2);
  }
  return newResult;
}

Polyline* TraceTightener90::trySkipSecondCorner(Polyline* polyline) {
  if (polyline->lines->length < 5) {
    return polyline;
  }
  jrt::Array<Line*>* checkLines = new (jrt::kGeom) jrt::Array<Line*>(4);
  (*checkLines)[0] = (*polyline->lines)[1];
  (*checkLines)[1] = (*polyline->lines)[0];
  (*checkLines)[2] = (*polyline->lines)[3];
  (*checkLines)[3] = (*polyline->lines)[4];
  Polyline* checkPolyline = new (jrt::kGeom) Polyline(checkLines);
  if (checkPolyline->lines->length != 4 ||
      (currentClipShape != nullptr &&
       !currentClipShape->contains(checkPolyline->cornerApprox(1)))) {
    return polyline;
  }
  for (int32_t i = 0; i < 2; i++) {
    TileShape* shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, i);
    if (!board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                currentClearanceClassIndex, this->contactPins)) {
      return polyline;
    }
  }
  // now the second corner can be skipped.
  jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(polyline->lines->length - 1);
  (*newLines)[0] = (*polyline->lines)[1];
  (*newLines)[1] = (*polyline->lines)[0];
  jrt::arraycopy(polyline->lines, 3, newLines, 2, newLines->length - 2);
  return new (jrt::kGeom) Polyline(newLines);
}

Polyline* TraceTightener90::trySkipCorners(Polyline* polyline) {
  if (polyline->lines->length < 5) {
    return polyline;
  }
  jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(polyline->lines->length);
  (*newLines)[0] = (*polyline->lines)[0];
  (*newLines)[1] = (*polyline->lines)[1];
  int32_t newLineIndex = 1;
  bool polylineChanged = false;
  jrt::Array<Line*>* checkLines = new (jrt::kGeom) jrt::Array<Line*>(4);
  bool secondLastCornerSkipped = false;
  for (int32_t i = 5; i <= polyline->lines->length; i++) {
    bool skipLines = false;
    bool inClipShape =
        currentClipShape == nullptr || currentClipShape->contains(polyline->cornerApprox(i - 3));
    if (inClipShape) {
      (*checkLines)[0] = (*newLines)[newLineIndex - 1];
      (*checkLines)[1] = (*newLines)[newLineIndex];
      (*checkLines)[2] = (*polyline->lines)[i - 1];
      if (i < polyline->lines->length) {
        (*checkLines)[3] = (*polyline->lines)[i];
      } else {
        // use as concluding line the second last line
        (*checkLines)[3] = (*polyline->lines)[i - 2];
      }
      Polyline* checkPolyline = new (jrt::kGeom) Polyline(checkLines);
      skipLines = checkPolyline->lines->length == 4 &&
                  (currentClipShape == nullptr ||
                   currentClipShape->contains(checkPolyline->cornerApprox(1)));
      if (skipLines) {
        TileShape* shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 0);
        skipLines = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                           currentClearanceClassIndex, this->contactPins);
      }
      if (skipLines) {
        TileShape* shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 1);
        skipLines = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                           currentClearanceClassIndex, this->contactPins);
      }
    }
    if (skipLines) {
      if (i == polyline->lines->length) {
        secondLastCornerSkipped = true;
      }
      if (board->changedArea != nullptr) {
        FloatPoint* newCorner = (*checkLines)[1]->intersectionApprox((*checkLines)[2]);
        board->changedArea->join(newCorner, currentLayer);
        FloatPoint* skippedCorner =
            (*polyline->lines)[i - 2]->intersectionApprox((*polyline->lines)[i - 3]);
        board->changedArea->join(skippedCorner, currentLayer);
      }
      polylineChanged = true;
      ++i;
    } else {
      ++newLineIndex;
      (*newLines)[newLineIndex] = (*polyline->lines)[i - 3];
    }
  }
  if (!polylineChanged) {
    return polyline;
  }
  if (secondLastCornerSkipped) {
    // The second last corner of polyline was skipped
    ++newLineIndex;
    (*newLines)[newLineIndex] = (*polyline->lines)[polyline->lines->length - 1];
    ++newLineIndex;
    (*newLines)[newLineIndex] = (*polyline->lines)[polyline->lines->length - 2];
  } else {
    for (int32_t i = 3; i > 0; i--) {
      ++newLineIndex;
      (*newLines)[newLineIndex] = (*polyline->lines)[polyline->lines->length - i];
    }
  }

  jrt::Array<Line*>* cleanedNewLines = new (jrt::kGeom) jrt::Array<Line*>(newLineIndex + 1);
  jrt::arraycopy(newLines, 0, cleanedNewLines, 0, cleanedNewLines->length);
  return new (jrt::kGeom) Polyline(cleanedNewLines);
}

Polyline* TraceTightener90::smoothenStartCornerAtTrace(PolylineTrace* trace) { return nullptr; }

Polyline* TraceTightener90::smoothenEndCornerAtTrace(PolylineTrace* trace) { return nullptr; }

}  // namespace fr
