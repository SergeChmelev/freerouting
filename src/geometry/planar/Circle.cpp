#include "geometry/planar/Circle.h"

#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {
// NumberFormat.getInstance(Locale.ENGLISH).format(int): decimal digits with ',' grouping.
std::string formatGroupedInt(int32_t value) {
  std::string digits = std::to_string(static_cast<int64_t>(value) < 0
                                          ? -static_cast<int64_t>(value)
                                          : static_cast<int64_t>(value));
  std::string result;
  int32_t count = 0;
  for (int32_t i = static_cast<int32_t>(digits.size()) - 1; i >= 0; --i) {
    if (count > 0 && count % 3 == 0) {
      result.insert(result.begin(), ',');
    }
    result.insert(result.begin(), digits[i]);
    ++count;
  }
  if (value < 0) {
    result.insert(result.begin(), '-');
  }
  return result;
}
}  // namespace

Circle::Circle(IntPoint* center, int32_t radius) {
  this->center = center;
  if (radius < 0) {
    FRLogger::warn("Circle: unexpected negative radius");
    this->radius = -radius;
  } else {
    this->radius = radius;
  }
}

bool Circle::isEmpty() { return false; }

bool Circle::isBounded() { return true; }

int32_t Circle::dimension() {
  if (radius == 0) {
    return 0;
  }
  return 2;
}

double Circle::circumference() { return 2.0 * jrt::Math::PI * radius; }

double Circle::area() { return (jrt::Math::PI * radius) * radius; }

FloatPoint* Circle::centreOfGravity() { return center->toFloat(); }

bool Circle::isOutside(Point* point) {
  FloatPoint* fp = point->toFloat();
  return fp->distanceSquare(center->toFloat()) > static_cast<double>(radius) * radius;
}

bool Circle::contains(Point* point) { return !isOutside(point); }

bool Circle::contains(FloatPoint* point) {
  return point->distanceSquare(center->toFloat()) <= static_cast<double>(radius) * radius;
}

bool Circle::containsInside(Point* point) {
  FloatPoint* fp = point->toFloat();
  return fp->distanceSquare(center->toFloat()) < static_cast<double>(radius) * radius;
}

bool Circle::containsOnBorder(Point* point) {
  FloatPoint* fp = point->toFloat();
  return fp->distanceSquare(center->toFloat()) == static_cast<double>(radius) * radius;
}

double Circle::distance(FloatPoint* point) {
  double d = point->distance(center->toFloat()) - radius;
  return jrt::Math::max(d, 0.0);
}

double Circle::smallestRadius() { return radius; }

IntBox* Circle::boundingBox() {
  int32_t lowerLeftX = center->x - radius;
  int32_t upperRightX = center->x + radius;
  int32_t lowerLeftY = center->y - radius;
  int32_t upperRightY = center->y + radius;
  return new IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
}

IntOctagon* Circle::boundingOctagon() {
  int32_t leftX = center->x - radius;
  int32_t rightX = center->x + radius;
  int32_t bottomY = center->y - radius;
  int32_t topY = center->y + radius;
  const double sqrt2Minus1 = jrt::Math::sqrt(2) - 1;
  const int32_t ceilCornerValue = jrt::d2i(jrt::Math::ceil(sqrt2Minus1 * radius));
  const int32_t floorCornerValue = jrt::d2i(jrt::Math::floor(sqrt2Minus1 * radius));
  int32_t upperLeftDiagonalX = leftX - (center->y + floorCornerValue);
  int32_t lowerRightDiagonalX = rightX - (center->y - ceilCornerValue);
  int32_t lowerLeftDiagonalX = leftX + (center->y - floorCornerValue);
  int32_t upperRightDiagonalX = rightX + (center->y + ceilCornerValue);
  return new IntOctagon(leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                        lowerLeftDiagonalX, upperRightDiagonalX);
}

TileShape* Circle::boundingTile() { return boundingOctagon(); }

TileShape* Circle::boundingTile(int32_t maxSegmentLength) {
  if (maxSegmentLength <= 0) {
    return this->boundingOctagon();
  }
  int32_t quadrantDivisionCount = this->radius / maxSegmentLength + 1;
  return boundingTileWithDivisions(quadrantDivisionCount);
}

TileShape* Circle::boundingTileWithDivisions(int32_t quadrantDivisionCount) {
  if (this->radius <= 0 || quadrantDivisionCount <= 2) {
    return this->boundingOctagon();
  }
  int32_t divisions = quadrantDivisionCount;
  auto* tangentLineArr = new jrt::Array<Line*>(divisions * 4);
  for (int32_t i = 0; i < divisions; i++) {
    Vector* borderDelta;
    if (i == 0) {
      borderDelta = new IntVector(this->radius, 0);
    } else {
      double currentAngle = i * jrt::Math::PI / (2.0 * divisions);
      int32_t currentX = jrt::d2i(jrt::Math::ceil(jrt::Math::cos(currentAngle) * this->radius));
      int32_t currentY = jrt::d2i(jrt::Math::ceil(jrt::Math::sin(currentAngle) * this->radius));
      borderDelta = new IntVector(currentX, currentY);
    }
    Point* currentA = this->center->translateBy(borderDelta);
    Direction* currentDirection;
    if (i == 0) {
      currentDirection = Direction::UP();
    } else {
      double currentAngle = i * jrt::Math::PI / (2.0 * divisions);
      currentDirection = Direction::getInstanceApprox(currentAngle + jrt::Math::PI / 2.0);
    }
    Line* currentTangent = new Line(currentA, currentDirection);
    (*tangentLineArr)[divisions + i] = currentTangent;
    (*tangentLineArr)[2 * divisions + i] = currentTangent->turn90Degree(1, this->center);
    (*tangentLineArr)[3 * divisions + i] = currentTangent->turn90Degree(2, this->center);
    (*tangentLineArr)[i] = currentTangent->turn90Degree(3, this->center);
  }
  return TileShape::getInstance(tangentLineArr);
}

bool Circle::isContainedIn(IntBox* box) {
  if (box->ll->x > center->x - radius) {
    return false;
  }
  if (box->ll->y > center->y - radius) {
    return false;
  }
  if (box->ur->x < center->x + radius) {
    return false;
  }
  return box->ur->y >= center->y + radius;
}

Circle* Circle::turn90Degree(int32_t factor, IntPoint* pole) {
  IntPoint* newCenter = jrt::fast_cast<IntPoint>(center->turn90Degree(factor, pole));
  return new Circle(newCenter, radius);
}

Circle* Circle::rotateApprox(double angle, FloatPoint* pole) {
  IntPoint* newCenter = center->toFloat()->rotate(angle, pole)->round();
  return new Circle(newCenter, radius);
}

Circle* Circle::mirrorVertical(IntPoint* pole) {
  IntPoint* newCenter = jrt::fast_cast<IntPoint>(center->mirrorVertical(pole));
  return new Circle(newCenter, radius);
}

Circle* Circle::mirrorHorizontal(IntPoint* pole) {
  IntPoint* newCenter = jrt::fast_cast<IntPoint>(center->mirrorHorizontal(pole));
  return new Circle(newCenter, radius);
}

double Circle::maxWidth() { return 2 * this->radius; }

double Circle::minWidth() { return 2 * this->radius; }

RegularTileShape* Circle::boundingShape(ShapeBoundingDirections* dirs) { return dirs->bounds(this); }

Circle* Circle::offset(double offset) {
  double newRadius = this->radius + offset;
  int32_t r = jrt::l2i(jrt::Math::round(newRadius));
  return new Circle(this->center, r);
}

Circle* Circle::shrink(double offset) {
  double newRadius = this->radius - offset;
  int32_t r = jrt::Math::max(jrt::l2i(jrt::Math::round(newRadius)), 1);
  return new Circle(this->center, r);
}

Circle* Circle::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  IntPoint* newCenter = center->translateBy(vector->toIntegerExact())->toIntegerExact();
  return new Circle(newCenter, radius);
}

FloatPoint* Circle::nearestPointApprox(FloatPoint* point) {
  FRLogger::warn("Circle.nearest_point_approx not yet implemented");
  return nullptr;
}

double Circle::borderDistance(FloatPoint* point) {
  double d = point->distance(center->toFloat()) - radius;
  return jrt::Math::abs(d);
}

Circle* Circle::enlarge(double offset) {
  if (offset == 0) {
    return this;
  }
  int32_t newRadius = radius + jrt::l2i(jrt::Math::round(offset));
  return new Circle(center, newRadius);
}

bool Circle::intersects(Shape* other) { return other->intersects(this); }

bool Circle::intersects(Circle* other) {
  double radiusSumSquare = radius + other->radius;
  radiusSumSquare *= radiusSumSquare;
  return center->distanceSquare(other->center) <= radiusSumSquare;
}

bool Circle::intersects(IntBox* box) { return box->distance(center->toFloat()) <= radius; }

bool Circle::intersects(IntOctagon* oct) { return oct->distance(center->toFloat()) <= radius; }

bool Circle::intersects(Simplex* simplex) {
  return simplex->distance(center->toFloat()) <= radius;
}

jrt::Array<Polyline*>* Circle::cutout(Polyline* polyline) {
  FRLogger::warn("Circle.cutout not yet implemented");
  return nullptr;
}

jrt::Array<TileShape*>* Circle::splitToConvex() {
  auto* result = new jrt::Array<TileShape*>(1);
  if (this->radius >= 16) {
    (*result)[0] = this->boundingTileWithDivisions(16);
  } else {
    (*result)[0] = this->boundingOctagon();
  }
  return result;
}

jrt::Array<Shape*>* Circle::getHoles() { return new jrt::Array<Shape*>(0); }

jrt::Array<FloatPoint*>* Circle::cornerApproxArr() { return new jrt::Array<FloatPoint*>(0); }

std::string Circle::toString() {
  // PORT NOTE: toString(Locale.ENGLISH) inlined (Locale dropped).
  std::string result = "Circle: ";
  if (!center->equals(Point::ZERO())) {
    std::string centerString = "center " + jrt::str(center);
    result += centerString;
  }
  std::string radiusString = "radius " + formatGroupedInt(radius);
  result += radiusString;
  return result;
}

}  // namespace fr
