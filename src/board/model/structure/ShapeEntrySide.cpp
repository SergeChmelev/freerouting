// Port of app.freerouting.board.model.structure.ShapeEntrySide
#include "board/model/structure/ShapeEntrySide.h"

#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

ShapeEntrySide* ShapeEntrySide::NOT_CALCULATED() {
  static ShapeEntrySide* v = new (jrt::kBoard) ShapeEntrySide(-1, nullptr);
  return v;
}

ShapeEntrySide::ShapeEntrySide(Polyline* polyline, int32_t no, TileShape* shape) {
  int32_t fromsideNo = -1;
  FloatPoint* intersection = nullptr;
  bool borderIntersectionFound = false;
  // calculate the edgeIndex of shape, where polyline enters
  for (int32_t currentNo = no; currentNo > 0; currentNo--) {
    auto* currentSeg = new (jrt::kGeom) LineSegment(polyline, currentNo);
    jrt::Array<int32_t>* intersections = currentSeg->borderIntersections(shape);
    if (intersections->length > 0) {
      fromsideNo = (*intersections)[0];
      intersection = currentSeg->getLine()->intersectionApprox(shape->borderLine(fromsideNo));
      borderIntersectionFound = true;
      break;
    }
  }
  if (!borderIntersectionFound) {
    // The first corner of polyline is inside shape.
    // Calculate the nearest intersection point of polyline.lines[1]
    // with the border of shape to the first corner of polyline
    FloatPoint* fromPoint = polyline->cornerApprox(0);
    Line* checkLine = (*polyline->lines)[1];
    double minDist = jrt::Double::MAX_VALUE;
    int32_t edgeCount = shape->borderLineCount();
    for (int32_t i = 0; i < edgeCount; i++) {
      Line* currentLine = shape->borderLine(i);
      FloatPoint* currentIntersection = checkLine->intersectionApprox(currentLine);
      double currentDistance = jrt::Math::abs(currentIntersection->distance(fromPoint));
      if (currentDistance < minDist) {
        fromsideNo = i;
        intersection = currentIntersection;
        minDist = currentDistance;
      }
    }
  }
  this->no = fromsideNo;
  this->borderIntersection = intersection;
}

ShapeEntrySide::ShapeEntrySide(Point* fromPoint, TileShape* shape) {
  Point* borderProjection = shape->nearestBorderPoint(fromPoint);
  this->no = shape->containsOnBorderLineNo(borderProjection);
  if (this->no < 0) {
    FRLogger::warn("CalcFromSide: this.no >= 0 expected");
  }
  this->borderIntersection = borderProjection->toFloat();
}

ShapeEntrySide::ShapeEntrySide(LineSegment* lineSegment, TileShape* shape, bool shoveToTheLeft) {
  FloatPoint* startCorner = lineSegment->startPointApprox();
  FloatPoint* endCorner = lineSegment->endPointApprox();
  int32_t borderLineCount = shape->borderLineCount();
  Line* checkLine = lineSegment->getLine();
  FloatPoint* firstCorner = shape->cornerApprox(0);
  Side* prevSide = checkLine->sideOf(firstCorner);
  int32_t frontSideNo = -1;

  for (int32_t i = 1; i <= borderLineCount; i++) {
    FloatPoint* nextCorner;
    if (i == borderLineCount) {
      nextCorner = firstCorner;
    } else {
      nextCorner = shape->cornerApprox(i);
    }
    Side* nextSide = checkLine->sideOf(nextCorner);
    if (prevSide != nextSide) {
      FloatPoint* currentIntersection = shape->borderLine(i - 1)->intersectionApprox(checkLine);
      if (currentIntersection->distanceSquare(startCorner) <
          currentIntersection->distanceSquare(endCorner)) {
        frontSideNo = i - 1;
        break;
      }
    }
    prevSide = nextSide;
  }
  if (frontSideNo < 0) {
    // Fallback: find the nearest side of the shape to the start point of the line segment
    startCorner = lineSegment->startPointApprox();
    double minDistance = jrt::Double::MAX_VALUE;
    int32_t nearestSide = 0;

    // Check each side of the shape
    for (int32_t i = 0; i < borderLineCount; i++) {
      auto* borderLine =
          new (jrt::kGeom) FloatLine(shape->borderLine(i)->a->toFloat(), shape->borderLine(i)->b->toFloat());
      FloatPoint* projection = borderLine->perpendicularProjection(startCorner);

      // Only consider if projection is on the line segment
      FloatPoint* sideStart = shape->cornerApprox(i);
      FloatPoint* sideEnd = shape->cornerApprox((i + 1) % borderLineCount);
      if (projection->isContainedInBox(sideStart, sideEnd, 0.01)) {
        double distance = startCorner->distance(projection);
        if (distance < minDistance) {
          minDistance = distance;
          nearestSide = i;
          this->borderIntersection = projection;
        }
      }
    }

    // Apply the same shove direction logic as the original code
    if (shoveToTheLeft) {
      this->no = (nearestSide + 2) % borderLineCount;
    } else {
      this->no = (nearestSide + borderLineCount - 2) % borderLineCount;
    }

    // Update border intersection to be the middle of the chosen side
    FloatPoint* prevCorner = shape->cornerApprox(this->no);
    FloatPoint* nextCorner = shape->cornerApprox((this->no + 1) % borderLineCount);
    this->borderIntersection = prevCorner->middlePoint(nextCorner);
    return;
  }
  if (shoveToTheLeft) {
    this->no = (frontSideNo + 2) % borderLineCount;
  } else {
    this->no = (frontSideNo + borderLineCount - 2) % borderLineCount;
  }
  FloatPoint* prevCorner = shape->cornerApprox(this->no);
  FloatPoint* nextCorner = shape->cornerApprox((this->no + 1) % borderLineCount);
  this->borderIntersection = prevCorner->middlePoint(nextCorner);
}

}  // namespace fr
