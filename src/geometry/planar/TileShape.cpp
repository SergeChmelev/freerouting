#include "geometry/planar/TileShape.h"

#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polygon.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "logger/FRLogger.h"

namespace fr {

TileShape* TileShape::getInstance(jrt::Array<Line*>* lines) {
  Simplex* result = Simplex::getInstance(lines);
  return result->simplify();
}

TileShape* TileShape::getInstance(jrt::Array<Point*>* convexPolygon) {
  auto* lines = new jrt::Array<Line*>(convexPolygon->length);
  for (int32_t j = 0; j < lines->length - 1; j++) {
    (*lines)[j] = new Line((*convexPolygon)[j], (*convexPolygon)[j + 1]);
  }
  (*lines)[lines->length - 1] = new Line((*convexPolygon)[lines->length - 1], (*convexPolygon)[0]);
  return getInstance(lines);
}

TileShape* TileShape::getInstance(Line* line) {
  auto* lines = new jrt::Array<Line*>(1);
  (*lines)[0] = line;
  return Simplex::getInstance(lines);
}

IntOctagon* TileShape::getInstance(int32_t lx, int32_t ly, int32_t rx, int32_t uy, int32_t ulx,
                                   int32_t lrx, int32_t llx, int32_t urx) {
  IntOctagon* oct = new IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
  return oct->normalize();
}

IntOctagon* TileShape::getInstance(int32_t lowerLeftX, int32_t lowerLeftY, int32_t upperRightX,
                                   int32_t upperRightY) {
  IntBox* box = new IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
  return box->toIntOctagon();
}

IntBox* TileShape::getInstance(Point* point) { return point->surroundingBox(); }

TileShape* TileShape::intersectionWithSimplify(TileShape* other) {
  TileShape* result = this->intersection(other);
  return result->simplify();
}

double TileShape::area() {
  if (!isBounded()) {
    return jrt::Double::MAX_VALUE;
  }

  if (dimension() < 2) {
    return 0;
  }
  // calculate half of the absolute value of
  // x0 (y1 - yn-1) + x1 (y2 - y0) + x2 (y3 - y1) + ...+ xn-1( y0 - yn-2)
  // where xi, yi are the coordinates of the i-th corner of this TileShape.

  double result = 0;
  int32_t cornerCount = borderLineCount();
  FloatPoint* prevCorner = cornerApprox(cornerCount - 2);
  FloatPoint* currentCorner = cornerApprox(cornerCount - 1);
  for (int32_t i = 0; i < cornerCount; i++) {
    FloatPoint* nextCorner = cornerApprox(i);
    result += currentCorner->x * (nextCorner->y - prevCorner->y);
    prevCorner = currentCorner;
    currentCorner = nextCorner;
  }
  return 0.5 * jrt::Math::abs(result);
}

bool TileShape::isOutside(Point* point) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return true;
  }
  for (int32_t i = 0; i < lineCount; i++) {
    if (borderLine(i)->sideOf(point) == Side::ON_THE_LEFT()) {
      return true;
    }
  }
  return false;
}

bool TileShape::contains(Point* point) { return !isOutside(point); }

bool TileShape::contains(FloatPoint* point) { return contains(point, 0); }

bool TileShape::contains(FloatPoint* point, double tolerance) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return false;
  }
  for (int32_t i = 0; i < lineCount; i++) {
    if (borderLine(i)->sideOf(point, tolerance) != Side::ON_THE_RIGHT()) {
      return false;
    }
  }
  return true;
}

bool TileShape::contains(TileShape* other) {
  for (int32_t i = 0; i < other->borderLineCount(); i++) {
    if (!this->contains(other->corner(i))) {
      return false;
    }
  }
  return true;
}

bool TileShape::containsInside(Point* point) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return false;
  }
  for (int32_t i = 0; i < lineCount; i++) {
    if (borderLine(i)->sideOf(point) != Side::ON_THE_RIGHT()) {
      return false;
    }
  }
  return true;
}

Side* TileShape::sideOfBorder(FloatPoint* point, double tolerance) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return Side::COLLINEAR();
  }
  Side* result = Side::ON_THE_RIGHT();  // point is inside
  for (int32_t i = 0; i < lineCount; i++) {
    Side* currentSide = borderLine(i)->sideOf(point, tolerance);
    if (currentSide == Side::ON_THE_LEFT()) {
      return Side::ON_THE_LEFT();  // point is outside
    } else if (currentSide == Side::COLLINEAR()) {
      result = currentSide;
    }
  }
  return result;
}

int32_t TileShape::containsOnBorderLineNo(Point* point) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return -1;
  }
  int32_t containingLineNo = -1;
  for (int32_t i = 0; i < lineCount; i++) {
    Side* sideOf = borderLine(i)->sideOf(point);
    if (sideOf == Side::ON_THE_LEFT()) {
      // point outside the convex shape
      return -1;
    }
    if (sideOf == Side::COLLINEAR()) {
      containingLineNo = i;
    }
  }
  return containingLineNo;
}

bool TileShape::containsOnBorder(Point* point) { return containsOnBorderLineNo(point) >= 0; }

bool TileShape::containsApprox(TileShape* other) {
  jrt::Array<FloatPoint*>* corners = other->cornerApproxArr();
  for (FloatPoint* currentCorner : *corners) {
    if (!this->contains(currentCorner)) {
      return false;
    }
  }
  return true;
}

double TileShape::distance(FloatPoint* point) {
  FloatPoint* nearestPoint = nearestPointApprox(point);
  return nearestPoint->distance(point);
}

double TileShape::borderDistance(FloatPoint* point) {
  FloatPoint* nearestPoint = nearestBorderPointApprox(point);
  return nearestPoint->distance(point);
}

double TileShape::smallestRadius() { return borderDistance(centreOfGravity()); }

Point* TileShape::nearestPoint(Point* fromPoint) {
  if (!isOutside(fromPoint)) {
    return fromPoint;
  }
  return nearestBorderPoint(fromPoint);
}

FloatPoint* TileShape::nearestPointApprox(FloatPoint* fromPoint) {
  if (this->contains(fromPoint)) {
    return fromPoint;
  }
  return nearestBorderPointApprox(fromPoint);
}

Point* TileShape::nearestBorderPoint(Point* fromPoint) {
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return nullptr;
  }
  FloatPoint* fromPointF = fromPoint->toFloat();
  if (lineCount == 1) {
    return borderLine(0)->perpendicularProjection(fromPoint);
  }
  double minDist = jrt::Double::MAX_VALUE;
  int32_t minDistInd = 0;

  // calculate the distance to the nearest corner first
  for (int32_t i = 0; i < lineCount; i++) {
    FloatPoint* currentCornerF = cornerApprox(i);
    double currentDistance = currentCornerF->distanceSquare(fromPointF);
    if (currentDistance < minDist) {
      minDist = currentDistance;
      minDistInd = i;
    }
  }

  Point* nearestPoint = corner(minDistInd);

  int32_t prevInd = lineCount - 2;
  int32_t currentInd = lineCount - 1;

  for (int32_t nextInd = 0; nextInd < lineCount; nextInd++) {
    Point* projection = borderLine(currentInd)->perpendicularProjection(fromPoint);
    if ((!cornerIsBounded(currentInd) ||
         borderLine(prevInd)->sideOf(projection) == Side::ON_THE_RIGHT()) &&
        (!cornerIsBounded(nextInd) ||
         borderLine(nextInd)->sideOf(projection) == Side::ON_THE_RIGHT())) {
      FloatPoint* projectionF = projection->toFloat();
      double currentDistance = projectionF->distanceSquare(fromPointF);
      if (currentDistance < minDist) {
        minDist = currentDistance;
        nearestPoint = projection;
      }
    }
    prevInd = currentInd;
    currentInd = nextInd;
  }
  return nearestPoint;
}

FloatPoint* TileShape::nearestBorderPointApprox(FloatPoint* fromPoint) {
  jrt::Array<FloatPoint*>* nearestPoints = nearestBorderPointsApprox(fromPoint, 1);
  if (nearestPoints->length == 0) {
    return nullptr;
  }
  return (*nearestPoints)[0];
}

jrt::Array<FloatPoint*>* TileShape::nearestBorderPointsApprox(FloatPoint* fromPoint,
                                                              int32_t count) {
  if (count <= 0) {
    return new jrt::Array<FloatPoint*>(0);
  }
  int32_t lineCount = borderLineCount();
  if (lineCount == 0) {
    return new jrt::Array<FloatPoint*>(0);
  }
  if (lineCount == 1) {
    auto* result = new jrt::Array<FloatPoint*>(1);
    (*result)[0] = fromPoint->projectionApprox(borderLine(0));
    return result;
  }
  if (this->dimension() == 0) {
    auto* result = new jrt::Array<FloatPoint*>(1);
    (*result)[0] = cornerApprox(0);
    return result;
  }
  int32_t resultCount = jrt::Math::min(count, lineCount);
  auto* nearestPoints = new jrt::Array<FloatPoint*>(resultCount);
  auto* minDists = new jrt::Array<double>(resultCount);
  jrt::Arrays::fill(minDists, jrt::Double::MAX_VALUE);

  // calculate the distances to the nearest corners first
  for (int32_t i = 0; i < lineCount; i++) {
    if (cornerIsBounded(i)) {
      FloatPoint* currentCorner = cornerApprox(i);
      double currentDistance = currentCorner->distanceSquare(fromPoint);
      for (int32_t j = 0; j < resultCount; j++) {
        if (currentDistance < (*minDists)[j]) {
          for (int32_t k = j + 1; k < resultCount; k++) {
            (*minDists)[k] = (*minDists)[k - 1];
            (*nearestPoints)[k] = (*nearestPoints)[k - 1];
          }
          (*minDists)[j] = currentDistance;
          (*nearestPoints)[j] = currentCorner;
          break;
        }
      }
    }
  }

  int32_t prevInd = lineCount - 2;
  int32_t currentInd = lineCount - 1;

  for (int32_t nextInd = 0; nextInd < lineCount; nextInd++) {
    FloatPoint* projection = fromPoint->projectionApprox(borderLine(currentInd));
    if ((!cornerIsBounded(currentInd) ||
         borderLine(prevInd)->sideOf(projection) == Side::ON_THE_RIGHT()) &&
        (!cornerIsBounded(nextInd) ||
         borderLine(nextInd)->sideOf(projection) == Side::ON_THE_RIGHT())) {
      double currentDistance = projection->distanceSquare(fromPoint);
      for (int32_t j = 0; j < resultCount; j++) {
        if (currentDistance < (*minDists)[j]) {
          for (int32_t k = j + 1; k < resultCount; k++) {
            (*minDists)[k] = (*minDists)[k - 1];
            (*nearestPoints)[k] = (*nearestPoints)[k - 1];
          }
          (*minDists)[j] = currentDistance;
          (*nearestPoints)[j] = projection;
          break;
        }
      }
    }
    prevInd = currentInd;
    currentInd = nextInd;
  }
  return nearestPoints;
}

int32_t TileShape::indexOfNearestCorner(Point* fromPoint) {
  FloatPoint* fromPointF = fromPoint->toFloat();
  int32_t result = 0;
  int32_t cornerCount = borderLineCount();
  double minDist = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < cornerCount; i++) {
    double currentDistance = cornerApprox(i)->distance(fromPointF);
    if (currentDistance < minDist) {
      minDist = currentDistance;
      result = i;
    }
  }
  return result;
}

FloatLine* TileShape::diagonalCornerSegment() {
  if (this->isEmpty()) {
    return nullptr;
  }
  FloatPoint* firstCorner = this->cornerApprox(0);
  FloatPoint* lastCorner = this->cornerApprox(this->borderLineCount() / 2);
  return new FloatLine(firstCorner, lastCorner);
}

jrt::Array<FloatPoint*>* TileShape::nearestRelativeOutsideLocations(TileShape* shape,
                                                                    int32_t count) {
  int32_t lineCount = borderLineCount();
  if (count <= 0 || lineCount < 3 || !this->intersects(static_cast<Shape*>(shape))) {
    return new jrt::Array<FloatPoint*>(0);
  }

  int32_t resultCount = jrt::Math::min(count, lineCount);

  auto* translateCoors = new jrt::Array<FloatPoint*>(resultCount);
  auto* minDists = new jrt::Array<double>(resultCount);
  jrt::Arrays::fill(minDists, jrt::Double::MAX_VALUE);

  int32_t currentInd = lineCount - 1;

  int32_t otherLineCount = shape->borderLineCount();

  for (int32_t nextInd = 0; nextInd < lineCount; nextInd++) {
    double currentMaxDist = 0;
    FloatPoint* currentTranslateCoor = FloatPoint::ZERO();
    for (int32_t cornerIndex = 0; cornerIndex < otherLineCount; cornerIndex++) {
      FloatPoint* currentCorner = shape->cornerApprox(cornerIndex);
      if (borderLine(currentInd)->sideOf(currentCorner) == Side::ON_THE_RIGHT()) {
        FloatPoint* projection = currentCorner->projectionApprox(borderLine(currentInd));
        double currentDistance = projection->distanceSquare(currentCorner);
        if (currentDistance > currentMaxDist) {
          currentMaxDist = currentDistance;
          currentTranslateCoor = projection->subtract(currentCorner);
        }
      }
    }

    for (int32_t j = 0; j < resultCount; j++) {
      if (currentMaxDist < (*minDists)[j]) {
        for (int32_t k = j + 1; k < resultCount; k++) {
          (*minDists)[k] = (*minDists)[k - 1];
          (*translateCoors)[k] = (*translateCoors)[k - 1];
        }
        (*minDists)[j] = currentMaxDist;
        (*translateCoors)[j] = currentTranslateCoor;
        break;
      }
    }
    currentInd = nextInd;
  }
  return translateCoors;
}

ConvexShape* TileShape::shrink(double offset) {
  ConvexShape* result = this->offset(-offset);
  if (result->isEmpty()) {
    IntBox* centreBox = this->centreOfGravity()->boundingBox();
    result = this->intersection(centreBox);
  }
  return result;
}

double TileShape::length() {
  if (!this->isBounded()) {
    return jrt::Integer::MAX_VALUE;
  }
  int32_t dimension = this->dimension();
  if (dimension <= 0) {
    return 0;
  }
  if (dimension == 1) {
    return this->circumference() / 2;
  }
  // now the shape is 2-dimensional
  double maxDistance = -1;
  double maxDistance2 = -1;
  FloatPoint* gravityPoint = this->centreOfGravity();
  for (int32_t i = 0; i < borderLineCount(); i++) {
    double currentDistance = jrt::Math::abs(borderLine(i)->signedDistance(gravityPoint));
    if (currentDistance > maxDistance) {
      maxDistance2 = maxDistance;
      maxDistance = currentDistance;
    } else if (currentDistance > maxDistance2) {
      maxDistance2 = currentDistance;
    }
  }
  return maxDistance + maxDistance2;
}

jrt::Array<int32_t>* TileShape::touchingSides(TileShape* other) {
  // search the first edge line of other with reverse direction >= right

  int32_t sideNo2 = -1;
  Direction* dir2 = nullptr;
  for (int32_t i = 0; i < other->borderLineCount(); i++) {
    Direction* currentDirection = other->borderLine(i)->direction();
    if (currentDirection->compareTo(Direction::LEFT()) >= 0) {
      sideNo2 = i;
      dir2 = currentDirection->opposite();
      break;
    }
  }
  if (dir2 == nullptr) {
    FRLogger::warn("touching_side : dir2 not found");
    return new jrt::Array<int32_t>(0);
  }
  int32_t sideNo1 = 0;
  Direction* dir1 = this->borderLine(0)->direction();
  const int32_t maxInd = this->borderLineCount() + other->borderLineCount();

  for (int32_t i = 0; i < maxInd; i++) {
    int32_t compare = dir2->compareTo(dir1);
    if (compare == 0) {
      if (this->borderLine(sideNo1)->isEqualOrOpposite(other->borderLine(sideNo2))) {
        auto* result = new jrt::Array<int32_t>(2);
        (*result)[0] = sideNo1;
        (*result)[1] = sideNo2;
        return result;
      }
    }
    if (compare >= 0) {  // dir2 is bigger than dir1
      sideNo1 = (sideNo1 + 1) % this->borderLineCount();
      dir1 = this->borderLine(sideNo1)->direction();
    } else {  // dir1 is bigger than dir2
      sideNo2 = (sideNo2 + 1) % other->borderLineCount();
      dir2 = other->borderLine(sideNo2)->direction()->opposite();
    }
  }
  return new jrt::Array<int32_t>(0);
}

double TileShape::distanceToTheLeft(Line* line) {
  double result = jrt::Integer::MAX_VALUE;
  for (int32_t i = 0; i < this->borderLineCount(); i++) {
    FloatPoint* currentCorner = this->cornerApprox(i);
    Side* lineSide = line->sideOf(currentCorner, 1);
    if (lineSide == Side::COLLINEAR()) {
      lineSide = line->sideOf(this->corner(i));
    }
    if (lineSide == Side::ON_THE_RIGHT()) {
      // currentPoint would be outside the result shape
      result = -1;
      break;
    }
    result = jrt::Math::min(result, line->signedDistance(currentCorner));
  }
  return result;
}

Side* TileShape::sideOf(Line* line) {
  bool onTheLeft = false;
  bool onTheRight = false;
  for (int32_t i = 0; i < this->borderLineCount(); i++) {
    Side* currentSide = line->sideOf(this->corner(i));
    if (currentSide == Side::ON_THE_LEFT()) {
      onTheRight = true;
    } else if (currentSide == Side::ON_THE_RIGHT()) {
      onTheLeft = true;
    }
    if (onTheLeft && onTheRight) {
      return Side::COLLINEAR();
    }
  }
  Side* result;
  if (onTheLeft) {
    result = Side::ON_THE_LEFT();
  } else {
    result = Side::ON_THE_RIGHT();
  }
  return result;
}

TileShape* TileShape::turn90Degree(int32_t factor, IntPoint* pole) {
  auto* newLines = new jrt::Array<Line*>(borderLineCount());
  for (int32_t i = 0; i < newLines->length; i++) {
    (*newLines)[i] = this->borderLine(i)->turn90Degree(factor, pole);
  }
  return getInstance(newLines);
}

TileShape* TileShape::rotateApprox(double angle, FloatPoint* pole) {
  if (angle == 0) {
    return this;
  }
  // PORT NOTE: Java IntPoint[] passed covariantly as Point[] -> Array<Point*> of IntPoints.
  auto* newCorners = new jrt::Array<Point*>(borderLineCount());
  for (int32_t i = 0; i < newCorners->length; i++) {
    (*newCorners)[i] = this->cornerApprox(i)->rotate(angle, pole)->round();
  }
  Polygon* cornerPolygon = new Polygon(newCorners);
  jrt::Array<Point*>* polygonCorners = cornerPolygon->cornerArray();
  TileShape* result;
  if (polygonCorners->length >= 3) {
    result = getInstance(polygonCorners);
  } else if (polygonCorners->length == 2) {
    Polyline* currentPolyline = new Polyline(polygonCorners);
    LineSegment* currentSegment = new LineSegment(currentPolyline, 0);
    result = currentSegment->toSimplex();
  } else if (polygonCorners->length == 1) {
    result = getInstance((*polygonCorners)[0]);
  } else {
    result = Simplex::EMPTY();
  }
  return result;
}

TileShape* TileShape::mirrorVertical(IntPoint* pole) {
  auto* newLines = new jrt::Array<Line*>(borderLineCount());
  for (int32_t i = 0; i < newLines->length; i++) {
    (*newLines)[i] = this->borderLine(i)->mirrorVertical(pole);
  }
  return getInstance(newLines);
}

TileShape* TileShape::mirrorHorizontal(IntPoint* pole) {
  auto* newLines = new jrt::Array<Line*>(borderLineCount());
  for (int32_t i = 0; i < newLines->length; i++) {
    (*newLines)[i] = this->borderLine(i)->mirrorHorizontal(pole);
  }
  return getInstance(newLines);
}

int32_t TileShape::intersectingBorderLineNo(Point* point, Direction* direction) {
  if (!this->contains(point)) {
    return -1;
  }
  FloatPoint* fromPoint = point->toFloat();
  Line* intersectionLine = new Line(point, direction);
  FloatPoint* secondLinePoint = intersectionLine->b->toFloat();
  int32_t result = -1;
  double minDistance = jrt::Float::MAX_VALUE;
  for (int32_t i = 0; i < this->borderLineCount(); i++) {
    Line* currentBorderLine = this->borderLine(i);
    FloatPoint* currentIntersection = currentBorderLine->intersectionApprox(intersectionLine);
    if (currentIntersection->x >= jrt::Integer::MAX_VALUE) {
      continue;  // lines are parallel
    }
    double currentDistance = currentIntersection->distanceSquare(fromPoint);
    if (currentDistance < minDistance) {
      bool directionOk = currentBorderLine->sideOf(secondLinePoint) == Side::ON_THE_LEFT() ||
                         secondLinePoint->distanceSquare(currentIntersection) < currentDistance;
      if (directionOk) {
        result = i;
        minDistance = currentDistance;
      }
    }
  }
  return result;
}

jrt::Array<Polyline*>* TileShape::cutout(Polyline* polyline) {
  jrt::Array<jrt::Array<int32_t>*>* intersectionNo = this->entrancePoints(polyline);
  Point* firstCorner = polyline->firstCorner();
  bool firstCornerIsInside = this->containsInside(firstCorner);
  if (intersectionNo->length == 0) {
    // no intersections
    if (firstCornerIsInside) {
      // polyline is contained completely in this shape
      return new jrt::Array<Polyline*>(0);
    }
    // polyline is completely outside
    auto* result = new jrt::Array<Polyline*>(1);
    (*result)[0] = polyline;
    return result;
  }
  jrt::Collection<Polyline*>* pieces = new jrt::LinkedList<Polyline*>();
  int32_t currentIntersectionNo = 0;
  jrt::Array<int32_t>* currentIntersectionTuple = (*intersectionNo)[currentIntersectionNo];
  Point* firstIntersection = (*polyline->lines)[(*currentIntersectionTuple)[0]]->intersection(
      this->borderLine((*currentIntersectionTuple)[1]));
  if (!firstCornerIsInside) {
    // calculate outside piece at start
    if (!firstCorner->equals(firstIntersection)) {
      // otherwise skip 1 point outside polyline at the start
      int32_t currentPolylineIntersectionNo = (*currentIntersectionTuple)[0];
      auto* currentLines = new jrt::Array<Line*>(currentPolylineIntersectionNo + 2);
      jrt::arraycopy(polyline->lines, 0, currentLines, 0, currentPolylineIntersectionNo + 1);
      // close the polyline piece with the intersected edge line.
      (*currentLines)[currentPolylineIntersectionNo + 1] =
          this->borderLine((*currentIntersectionTuple)[1]);
      Polyline* currentPiece = new Polyline(currentLines);
      if (!currentPiece->isEmpty()) {
        pieces->add(currentPiece);
      }
    }
    ++currentIntersectionNo;
  }
  while (currentIntersectionNo < intersectionNo->length - 1) {
    // calculate the next outside polyline piece
    currentIntersectionTuple = (*intersectionNo)[currentIntersectionNo];
    jrt::Array<int32_t>* nextIntersectionTuple = (*intersectionNo)[currentIntersectionNo + 1];
    int32_t currentIntersectionNoOfPolyline = (*currentIntersectionTuple)[0];
    int32_t nextIntersectionNoOfPolyline = (*nextIntersectionTuple)[0];
    // check that at least 1 corner of polyline with number
    // between currentIntersectionNoOfPolyline and
    // nextIntersectionNoOfPolyline
    // is not contained in this shape. Otherwise, the part of polyline
    // between this intersections is completely contained in the border
    // and can be ignored
    bool insertPiece = false;
    for (int32_t i = currentIntersectionNoOfPolyline + 1; i < nextIntersectionNoOfPolyline; i++) {
      if (this->isOutside(polyline->corner(i))) {
        insertPiece = true;
        break;
      }
    }

    if (insertPiece) {
      auto* currentLines = new jrt::Array<Line*>(nextIntersectionNoOfPolyline -
                                                 currentIntersectionNoOfPolyline + 3);
      (*currentLines)[0] = this->borderLine((*currentIntersectionTuple)[1]);
      jrt::arraycopy(polyline->lines, currentIntersectionNoOfPolyline, currentLines, 1,
                     currentLines->length - 2);
      (*currentLines)[currentLines->length - 1] = this->borderLine((*nextIntersectionTuple)[1]);
      Polyline* currentPiece = new Polyline(currentLines);
      if (!currentPiece->isEmpty()) {
        pieces->add(currentPiece);
      }
    }
    currentIntersectionNo += 2;
  }
  if (currentIntersectionNo <= intersectionNo->length - 1) {
    // calculate outside piece at end
    currentIntersectionTuple = (*intersectionNo)[currentIntersectionNo];
    int32_t currentPolylineIntersectionNo = (*currentIntersectionTuple)[0];
    auto* currentLines =
        new jrt::Array<Line*>(polyline->lines->length - currentPolylineIntersectionNo + 1);
    (*currentLines)[0] = this->borderLine((*currentIntersectionTuple)[1]);
    jrt::arraycopy(polyline->lines, currentPolylineIntersectionNo, currentLines, 1,
                   currentLines->length - 1);
    Polyline* currentPiece = new Polyline(currentLines);
    if (!currentPiece->isEmpty()) {
      pieces->add(currentPiece);
    }
  }
  auto* result = new jrt::Array<Polyline*>(pieces->size());
  jrt::Iterator<Polyline*>* it = pieces->iterator();
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = it->next();
  }
  return result;
}

jrt::Array<jrt::Array<int32_t>*>* TileShape::entrancePoints(Polyline* polyline) {
  jrt::Array<jrt::Array<int32_t>*>* result =
      jrt::newArray2<int32_t>(2 * polyline->lines->length, 2);
  int32_t intersectionCount = 0;
  int32_t prevIntersectionLineNo = -1;
  int32_t prevIntersectionEdgeNo = -1;
  for (int32_t lineIndex = 1; lineIndex < polyline->lines->length - 1; lineIndex++) {
    LineSegment* currentLineSeg = new LineSegment(polyline, lineIndex);
    jrt::Array<int32_t>* currentIntersections = currentLineSeg->borderIntersections(this);
    for (int32_t i = 0; i < currentIntersections->length; i++) {
      int32_t edgeIndex = (*currentIntersections)[i];
      if (lineIndex != prevIntersectionLineNo || edgeIndex != prevIntersectionEdgeNo) {
        (*(*result)[intersectionCount])[0] = lineIndex;
        (*(*result)[intersectionCount])[1] = edgeIndex;
        ++intersectionCount;
        prevIntersectionLineNo = lineIndex;
        prevIntersectionEdgeNo = edgeIndex;
      }
    }
  }
  return jrt::Arrays::copyOf(result, intersectionCount);
}

jrt::Array<TileShape*>* TileShape::splitToConvex() {
  auto* result = new jrt::Array<TileShape*>(1);
  (*result)[0] = this;
  return result;
}

jrt::Array<TileShape*>* TileShape::splitIntoSlices(int32_t count) {
  auto* result = new jrt::Array<TileShape*>(jrt::Math::max(1, count));
  if (result->length == 1) {
    (*result)[0] = this;
    return result;
  }
  IntBox* box = this->boundingBox();
  bool alongX = box->width() >= box->height();
  int64_t low = alongX ? box->ll->x : box->ll->y;
  int64_t extent = alongX ? box->width() : box->height();
  for (int32_t i = 0; i < result->length; i++) {
    int32_t from = jrt::l2i(low + extent * i / result->length);
    int32_t to = jrt::l2i(low + extent * (i + 1) / result->length);
    IntBox* slab = alongX ? new IntBox(from, box->ll->y, to, box->ur->y)
                          : new IntBox(box->ll->x, from, box->ur->x, to);
    TileShape* slice = to > from ? this->intersectionWithSimplify(slab) : nullptr;
    (*result)[i] =
        slice == nullptr || slice->isEmpty() || slice->dimension() < 2 ? this : slice;
  }
  return result;
}

jrt::Array<TileShape*>* TileShape::splitToTightOctagons(jrt::Array<TileShape*>* convexShapes,
                                                        double tolerance) {
  jrt::Collection<TileShape*>* pieces = new jrt::LinkedList<TileShape*>();
  for (TileShape* shape : *convexShapes) {
    addTightOctagonPieces(shape, tolerance, 0, pieces);
  }
  return new jrt::Array<TileShape*>(pieces->toVector());
}

void TileShape::addTightOctagonPieces(TileShape* shape, double tolerance, int32_t depth,
                                      jrt::Collection<TileShape*>* pieces) {
  if (shape->dimension() < 2 || depth >= MAX_TIGHT_OCTAGON_SPLIT_DEPTH ||
      shape->boundingOctagonExcess() <= tolerance) {
    pieces->add(shape);
    return;
  }
  IntBox* box = shape->boundingBox();
  if (jrt::Math::max(box->width(), box->height()) < 2) {
    pieces->add(shape);
    return;
  }
  IntBox* firstHalf;
  IntBox* secondHalf;
  if (box->width() >= box->height()) {
    int32_t middle = box->ll->x + box->width() / 2;
    firstHalf = new IntBox(box->ll->x, box->ll->y, middle, box->ur->y);
    secondHalf = new IntBox(middle, box->ll->y, box->ur->x, box->ur->y);
  } else {
    int32_t middle = box->ll->y + box->height() / 2;
    firstHalf = new IntBox(box->ll->x, box->ll->y, box->ur->x, middle);
    secondHalf = new IntBox(box->ll->x, middle, box->ur->x, box->ur->y);
  }
  for (IntBox* half : {firstHalf, secondHalf}) {
    TileShape* piece = shape->intersectionWithSimplify(half);
    if (!piece->isEmpty() && piece->dimension() == 2) {
      addTightOctagonPieces(piece, tolerance, depth + 1, pieces);
    }
  }
}

double TileShape::boundingOctagonExcess() {
  IntOctagon* octagon = this->boundingOctagon();
  double result = 0;
  for (int32_t i = 0; i < octagon->borderLineCount(); i++) {
    result = jrt::Math::max(result, this->distance(octagon->cornerApprox(i)));
  }
  return result;
}

jrt::Array<TileShape*>* TileShape::divideIntoSections(double maxSectionWidth) {
  if (this->isEmpty()) {
    auto* result = new jrt::Array<TileShape*>(1);
    (*result)[0] = this;
    return result;
  }
  jrt::Array<TileShape*>* sectionBoxes = this->boundingBox()->divideIntoSections(maxSectionWidth);
  jrt::Collection<TileShape*>* sectionList = new jrt::LinkedList<TileShape*>();
  for (int32_t i = 0; i < sectionBoxes->length; i++) {
    TileShape* currentSection = this->intersectionWithSimplify((*sectionBoxes)[i]);
    if (currentSection->dimension() == 2) {
      sectionList->add(currentSection);
    }
  }
  auto* result = new jrt::Array<TileShape*>(sectionList->size());
  jrt::Iterator<TileShape*>* it = sectionList->iterator();
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = it->next();
  }
  return result;
}

bool TileShape::isIntersectedInteriorBy(LineSegment* lineSegment) {
  return isIntersectedInteriorBy(lineSegment->startPoint(), lineSegment->endPoint(),
                                 lineSegment->getLine());
}

bool TileShape::isIntersectedInteriorBy(Point* startPoint, Point* endPoint, Line* line) {
  FloatPoint* floatStartPoint = startPoint->toFloat();
  FloatPoint* floatEndPoint = endPoint->toFloat();

  auto* borderLineSideOfStartPointArr = new jrt::Array<Side*>(this->borderLineCount());
  auto* borderLineSideOfEndPointArr = new jrt::Array<Side*>(borderLineSideOfStartPointArr->length);
  for (int32_t i = 0; i < borderLineSideOfStartPointArr->length; i++) {
    Line* currentBorderLine = this->borderLine(i);
    Side* borderLineSideOfStartPoint = currentBorderLine->sideOf(floatStartPoint, 1);
    if (borderLineSideOfStartPoint == Side::COLLINEAR()) {
      borderLineSideOfStartPoint = currentBorderLine->sideOf(startPoint);
    }
    Side* borderLineSideOfEndPoint = currentBorderLine->sideOf(floatEndPoint, 1);
    if (borderLineSideOfEndPoint == Side::COLLINEAR()) {
      borderLineSideOfEndPoint = currentBorderLine->sideOf(endPoint);
    }
    if (borderLineSideOfStartPoint != Side::ON_THE_RIGHT() &&
        borderLineSideOfEndPoint != Side::ON_THE_RIGHT()) {
      // both endpoints are outside the borderLine,
      // no intersection possible
      return false;
    }
    (*borderLineSideOfStartPointArr)[i] = borderLineSideOfStartPoint;
    (*borderLineSideOfEndPointArr)[i] = borderLineSideOfEndPoint;
  }
  bool startPointIsInside = true;
  for (int32_t i = 0; i < borderLineSideOfStartPointArr->length; i++) {
    if ((*borderLineSideOfStartPointArr)[i] != Side::ON_THE_RIGHT()) {
      startPointIsInside = false;
      break;
    }
  }
  if (startPointIsInside) {
    return true;
  }
  bool endPointIsInside = true;
  for (int32_t i = 0; i < borderLineSideOfEndPointArr->length; i++) {
    if ((*borderLineSideOfEndPointArr)[i] != Side::ON_THE_RIGHT()) {
      endPointIsInside = false;
      break;
    }
  }
  if (endPointIsInside) {
    return true;
  }
  Line* segmentLine = line;
  // Check, if this line segments intersect a border line of shape.
  for (int32_t i = 0; i < borderLineSideOfStartPointArr->length; i++) {
    Side* borderLineSideOfStartPoint = (*borderLineSideOfStartPointArr)[i];
    Side* borderLineSideOfEndPoint = (*borderLineSideOfEndPointArr)[i];
    if (borderLineSideOfStartPoint != borderLineSideOfEndPoint) {
      if ((borderLineSideOfStartPoint == Side::COLLINEAR() &&
           borderLineSideOfEndPoint == Side::ON_THE_LEFT()) ||
          (borderLineSideOfEndPoint == Side::COLLINEAR() &&
           borderLineSideOfStartPoint == Side::ON_THE_LEFT())) {
        // the interior of shape is not intersected.
        continue;
      }
      Side* prevCornerSide = segmentLine->sideOf(this->cornerApprox(i), 1);
      if (prevCornerSide == Side::COLLINEAR()) {
        prevCornerSide = segmentLine->sideOf(this->corner(i));
      }
      int32_t nextCornerIndex;
      if (i == borderLineSideOfStartPointArr->length - 1) {
        nextCornerIndex = 0;
      } else {
        nextCornerIndex = i + 1;
      }
      Side* nextCornerSide = segmentLine->sideOf(this->cornerApprox(nextCornerIndex), 1);
      if (nextCornerSide == Side::COLLINEAR()) {
        nextCornerSide = segmentLine->sideOf(this->corner(nextCornerIndex));
      }
      if ((prevCornerSide == Side::ON_THE_LEFT() && nextCornerSide == Side::ON_THE_RIGHT()) ||
          (prevCornerSide == Side::ON_THE_RIGHT() && nextCornerSide == Side::ON_THE_LEFT())) {
        // this line segment crosses a border line of shape
        return true;
      }
    }
  }
  return false;
}

}  // namespace fr
