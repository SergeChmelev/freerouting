#include "geometry/planar/PolylineShape.h"

#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "logger/FRLogger.h"

namespace fr {

jrt::Array<Point*>* PolylineShape::boundedCorners() {
  int32_t cornerCount = this->borderLineCount();
  jrt::Collection<Point*>* resultList = new jrt::LinkedList<Point*>();
  for (int32_t i = 0; i < cornerCount; i++) {
    if (this->cornerIsBounded(i)) {
      resultList->add(this->corner(i));
    }
  }
  auto* result = new jrt::Array<Point*>(resultList->size());
  jrt::Iterator<Point*>* it = resultList->iterator();
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = it->next();
  }
  return result;
}

FloatPoint* PolylineShape::cornerApprox(int32_t no) { return corner(no)->toFloat(); }

jrt::Array<FloatPoint*>* PolylineShape::cornerApproxArr() {
  int32_t cornerCount = this->borderLineCount();
  auto* result = new jrt::Array<FloatPoint*>(cornerCount);
  for (int32_t i = 0; i < cornerCount; i++) {
    (*result)[i] = this->cornerApprox(i);
  }
  return result;
}

int32_t PolylineShape::equalsCorner(Point* point) {
  for (int32_t i = 0; i < borderLineCount(); i++) {
    if (point->equals(corner(i))) {
      return i;
    }
  }
  return -1;
}

double PolylineShape::circumference() {
  if (!isBounded()) {
    return jrt::Integer::MAX_VALUE;
  }
  int32_t cornerCount = borderLineCount();
  double result = 0;
  FloatPoint* prevCorner = cornerApprox(cornerCount - 1);
  for (int32_t i = 0; i < cornerCount; i++) {
    FloatPoint* currentCorner = cornerApprox(i);
    result += currentCorner->distance(prevCorner);
    prevCorner = currentCorner;
  }
  return result;
}

FloatPoint* PolylineShape::centreOfGravity() {
  int32_t cornerCount = borderLineCount();
  double x = 0;
  double y = 0;
  for (int32_t i = 0; i < cornerCount; i++) {
    FloatPoint* currentPoint = cornerApprox(i);
    x += currentPoint->x;
    y += currentPoint->y;
  }
  x /= cornerCount;
  y /= cornerCount;
  return new FloatPoint(x, y);
}

bool PolylineShape::isContainedIn(IntBox* box) { return box->contains(boundingBox()); }

int32_t PolylineShape::indexOfLeftMostCorner(FloatPoint* fromPoint) {
  FloatPoint* leftMostCorner = cornerApprox(0);
  int32_t cornerCount = borderLineCount();
  int32_t result = 0;
  for (int32_t i = 1; i < cornerCount; i++) {
    FloatPoint* currentCorner = cornerApprox(i);
    if (currentCorner->sideOf(fromPoint, leftMostCorner) == Side::ON_THE_LEFT()) {
      leftMostCorner = currentCorner;
      result = i;
    }
  }
  return result;
}

int32_t PolylineShape::indexOfRightMostCorner(FloatPoint* fromPoint) {
  FloatPoint* rightMostCorner = cornerApprox(0);
  int32_t cornerCount = borderLineCount();
  int32_t result = 0;
  for (int32_t i = 1; i < cornerCount; i++) {
    FloatPoint* currentCorner = cornerApprox(i);
    if (currentCorner->sideOf(fromPoint, rightMostCorner) == Side::ON_THE_RIGHT()) {
      rightMostCorner = currentCorner;
      result = i;
    }
  }
  return result;
}

FloatLine* PolylineShape::polarLineSegment(FloatPoint* fromPoint) {
  if (this->isEmpty()) {
    FRLogger::warn("PolylineShape.polarLineSegment: shape is empty");
    return nullptr;
  }
  FloatPoint* leftMostCorner = cornerApprox(0);
  FloatPoint* rightMostCorner = cornerApprox(0);
  int32_t cornerCount = borderLineCount();
  for (int32_t i = 1; i < cornerCount; i++) {
    FloatPoint* currentCorner = cornerApprox(i);
    if (currentCorner->sideOf(fromPoint, rightMostCorner) == Side::ON_THE_RIGHT()) {
      rightMostCorner = currentCorner;
    }
    if (currentCorner->sideOf(fromPoint, leftMostCorner) == Side::ON_THE_LEFT()) {
      leftMostCorner = currentCorner;
    }
  }
  return new FloatLine(leftMostCorner, rightMostCorner);
}

int32_t PolylineShape::prevNo(int32_t no) {
  int32_t result;
  if (no == 0) {
    result = borderLineCount() - 1;
  } else {
    result = no - 1;
  }
  return result;
}

int32_t PolylineShape::nextNo(int32_t no) { return (no + 1) % borderLineCount(); }

jrt::Array<Shape*>* PolylineShape::getHoles() { return new jrt::Array<Shape*>(0); }

bool PolylineShape::intersects(Line* line) {
  Side* sideOfFirstCorner = line->sideOf(corner(0));
  if (sideOfFirstCorner == Side::COLLINEAR()) {
    return true;
  }
  for (int32_t i = 1; i < this->borderLineCount(); i++) {
    if (line->sideOf(corner(i)) != sideOfFirstCorner) {
      return true;
    }
  }
  return false;
}

Point* PolylineShape::leftMostCorner(Point* fromPoint) {
  if (this->isEmpty()) {
    return fromPoint;
  }
  Point* result = this->corner(0);
  int32_t cornerCount = this->borderLineCount();
  for (int32_t i = 1; i < cornerCount; i++) {
    Point* currentCorner = this->corner(i);
    if (currentCorner->sideOf(fromPoint, result) == Side::ON_THE_LEFT()) {
      result = currentCorner;
    }
  }
  return result;
}

Point* PolylineShape::rightMostCorner(Point* fromPoint) {
  if (this->isEmpty()) {
    return fromPoint;
  }
  Point* result = this->corner(0);
  int32_t cornerCount = this->borderLineCount();
  for (int32_t i = 1; i < cornerCount; i++) {
    Point* currentCorner = this->corner(i);
    if (currentCorner->sideOf(fromPoint, result) == Side::ON_THE_RIGHT()) {
      result = currentCorner;
    }
  }
  return result;
}

}  // namespace fr
