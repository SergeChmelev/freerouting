#include "geometry/planar/FloatPoint.h"

#include <cmath>
#include <cstdio>

#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {

// Emulates java.text.NumberFormat.getInstance(Locale.ENGLISH) with the given minimum/maximum
// fraction digits: grouping separator ',' every 3 integer digits, decimal separator '.',
// RoundingMode.HALF_EVEN on the exact binary value (what glibc printf does), "-" prefix for
// negative values (also when they round to zero, like DecimalFormat).
std::string formatEnglishNumber(double value, int32_t minFractionDigits,
                                int32_t maxFractionDigits) {
  if (std::isnan(value)) {
    return "NaN";
  }
  bool negative = std::signbit(value);
  std::string body;
  if (std::isinf(value)) {
    body = "\xE2\x88\x9E";  // U+221E
  } else {
    int32_t maxFrac = maxFractionDigits < 0 ? 0 : maxFractionDigits;
    int32_t minFrac = minFractionDigits < 0 ? 0 : minFractionDigits;
    if (minFrac > maxFrac) {
      minFrac = maxFrac;
    }
    int len = std::snprintf(nullptr, 0, "%.*f", maxFrac, std::fabs(value));
    std::string digits(static_cast<size_t>(len) + 1, '\0');
    std::snprintf(digits.data(), digits.size(), "%.*f", maxFrac, std::fabs(value));
    digits.resize(static_cast<size_t>(len));
    std::string intPart = digits;
    std::string fracPart;
    size_t dot = digits.find('.');
    if (dot != std::string::npos) {
      intPart = digits.substr(0, dot);
      fracPart = digits.substr(dot + 1);
    }
    while (static_cast<int32_t>(fracPart.size()) > minFrac && !fracPart.empty() &&
           fracPart.back() == '0') {
      fracPart.pop_back();
    }
    std::string grouped;
    int32_t count = 0;
    for (int32_t i = static_cast<int32_t>(intPart.size()) - 1; i >= 0; --i) {
      grouped.insert(grouped.begin(), intPart[i]);
      if (++count % 3 == 0 && i > 0) {
        grouped.insert(grouped.begin(), ',');
      }
    }
    body = fracPart.empty() ? grouped : grouped + "." + fracPart;
  }
  return negative ? "-" + body : body;
}

// String.format("%<padding>s", s): right aligned in a field of width padding.
std::string padLeft(const std::string& s, int32_t padding) {
  if (static_cast<int32_t>(s.size()) >= padding) {
    return s;
  }
  return std::string(static_cast<size_t>(padding) - s.size(), ' ') + s;
}

}  // namespace

FloatPoint* FloatPoint::ZERO() {
  static FloatPoint* v = new (jrt::kGeom) FloatPoint(0, 0);
  return v;
}

FloatPoint::FloatPoint(IntPoint* pt) {
  x = pt->x;
  y = pt->y;
}

IntOctagon* FloatPoint::boundingOctagon(jrt::Array<FloatPoint*>* points) {
  double minX = jrt::Integer::MAX_VALUE;
  double minY = jrt::Integer::MAX_VALUE;
  double maxX = jrt::Integer::MIN_VALUE;
  double maxY = jrt::Integer::MIN_VALUE;
  double minUpperLeftDiagonalX = jrt::Integer::MAX_VALUE;
  double maxLowerRightDiagonalX = jrt::Integer::MIN_VALUE;
  double minLowerLeftDiagonalX = jrt::Integer::MAX_VALUE;
  double maxUpperRightDiagonalX = jrt::Integer::MIN_VALUE;
  for (int32_t i = 0; i < points->length; i++) {
    FloatPoint* current = (*points)[i];
    minX = jrt::Math::min(minX, current->x);
    minY = jrt::Math::min(minY, current->y);
    maxX = jrt::Math::max(maxX, current->x);
    maxY = jrt::Math::max(maxY, current->y);
    double tmp = current->x - current->y;
    minUpperLeftDiagonalX = jrt::Math::min(minUpperLeftDiagonalX, tmp);
    maxLowerRightDiagonalX = jrt::Math::max(maxLowerRightDiagonalX, tmp);
    tmp = current->x + current->y;
    minLowerLeftDiagonalX = jrt::Math::min(minLowerLeftDiagonalX, tmp);
    maxUpperRightDiagonalX = jrt::Math::max(maxUpperRightDiagonalX, tmp);
  }
  return new (jrt::kGeom) IntOctagon(jrt::d2i(jrt::Math::floor(minX)), jrt::d2i(jrt::Math::floor(minY)),
                        jrt::d2i(jrt::Math::ceil(maxX)), jrt::d2i(jrt::Math::ceil(maxY)),
                        jrt::d2i(jrt::Math::floor(minUpperLeftDiagonalX)),
                        jrt::d2i(jrt::Math::ceil(maxLowerRightDiagonalX)),
                        jrt::d2i(jrt::Math::floor(minLowerLeftDiagonalX)),
                        jrt::d2i(jrt::Math::ceil(maxUpperRightDiagonalX)));
}

double FloatPoint::size() { return jrt::Math::sqrt(sizeSquare()); }

double FloatPoint::distanceSquare(FloatPoint* other) {
  double dx = other->x - x;
  double dy = other->y - y;
  return dx * dx + dy * dy;
}

double FloatPoint::distance(FloatPoint* other) { return jrt::Math::sqrt(distanceSquare(other)); }

double FloatPoint::weightedDistance(FloatPoint* other, double horizontalWeight,
                                    double verticalWeight) {
  double deltaX = this->x - other->x;
  double deltaY = this->y - other->y;
  deltaX *= horizontalWeight;
  deltaY *= verticalWeight;
  return jrt::Math::sqrt(deltaX * deltaX + deltaY * deltaY);
}

IntPoint* FloatPoint::round() {
  return new (jrt::kGeom) IntPoint(jrt::l2i(jrt::Math::round(x)), jrt::l2i(jrt::Math::round(y)));
}

IntPoint* FloatPoint::roundToTheRight(Direction* dir) {
  FloatPoint* directionVector = dir->getVector()->toFloat();
  int32_t roundedX;

  if (directionVector->y > 0) {
    roundedX = jrt::d2i(jrt::Math::ceil(x));
  } else if (directionVector->y < 0) {
    roundedX = jrt::d2i(jrt::Math::floor(x));
  } else {
    roundedX = jrt::l2i(jrt::Math::round(x));
  }

  int32_t roundedY;

  if (directionVector->x > 0) {
    roundedY = jrt::d2i(jrt::Math::floor(y));
  } else if (directionVector->x < 0) {
    roundedY = jrt::d2i(jrt::Math::ceil(y));
  } else {
    roundedY = jrt::l2i(jrt::Math::round(y));
  }
  return new (jrt::kGeom) IntPoint(roundedX, roundedY);
}

IntPoint* FloatPoint::roundToGrid(int32_t horizontalGrid, int32_t verticalGrid) {
  double roundedX;
  if (horizontalGrid > 0) {
    roundedX = jrt::Math::rint(this->x / horizontalGrid) * horizontalGrid;
  } else {
    roundedX = this->x;
  }
  double roundedY;
  if (verticalGrid > 0) {
    roundedY = jrt::Math::rint(this->y / verticalGrid) * verticalGrid;
  } else {
    roundedY = this->y;
  }
  return new (jrt::kGeom) IntPoint(jrt::d2i(roundedX), jrt::d2i(roundedY));
}

IntPoint* FloatPoint::roundToTheLeft(Direction* dir) {
  FloatPoint* directionVector = dir->getVector()->toFloat();
  int32_t roundedX;

  if (directionVector->y > 0) {
    roundedX = jrt::d2i(jrt::Math::floor(x));
  } else if (directionVector->y < 0) {
    roundedX = jrt::d2i(jrt::Math::ceil(x));
  } else {
    roundedX = jrt::l2i(jrt::Math::round(x));
  }

  int32_t roundedY;

  if (directionVector->x > 0) {
    roundedY = jrt::d2i(jrt::Math::ceil(y));
  } else if (directionVector->x < 0) {
    roundedY = jrt::d2i(jrt::Math::floor(y));
  } else {
    roundedY = jrt::l2i(jrt::Math::round(y));
  }
  return new (jrt::kGeom) IntPoint(roundedX, roundedY);
}

FloatPoint* FloatPoint::add(FloatPoint* other) {
  return new (jrt::kGeom) FloatPoint(this->x + other->x, this->y + other->y);
}

FloatPoint* FloatPoint::subtract(FloatPoint* other) {
  return new (jrt::kGeom) FloatPoint(this->x - other->x, this->y - other->y);
}

FloatPoint* FloatPoint::projectionApprox(Line* line) {
  // PORT NOTE: allocation-free fast path for integer lines: FloatLine.perpendicularProjection
  // on (a.toFloat(), b.toFloat()) with the same arithmetic.
  auto* ia = jrt::fast_cast<IntPoint>(line->a);
  auto* ib = ia != nullptr ? jrt::fast_cast<IntPoint>(line->b) : nullptr;
  if (ib != nullptr) {
    double ax = ia->x;
    double ay = ia->y;
    double bx = ib->x;
    double by = ib->y;
    double dx = bx - ax;
    double dy = by - ay;
    if (dx == 0 && dy == 0) {
      return new (jrt::kGeom) FloatPoint(ax, ay);
    }
    double dxdx = dx * dx;
    double dydy = dy * dy;
    double dxdy = dx * dy;
    double denominator = dxdx + dydy;
    double det = ax * by - bx * ay;
    double px = (this->x * dxdx + this->y * dxdy + det * dy) / denominator;
    double py = (this->x * dxdy + this->y * dydy - det * dx) / denominator;
    return new (jrt::kGeom) FloatPoint(px, py);
  }
  FloatLine* floatLine = new (jrt::kGeom) FloatLine(line->a->toFloat(), line->b->toFloat());
  return floatLine->perpendicularProjection(this);
}

double FloatPoint::scalarProduct(FloatPoint* p1, FloatPoint* p2) {
  if (p1 == nullptr || p2 == nullptr) {
    FRLogger::warn("FloatPoint.scalarProduct: parameter point is null");
    return 0;
  }
  double dx1 = p1->x - this->x;
  double dx2 = p2->x - this->x;
  double dy1 = p1->y - this->y;
  double dy2 = p2->y - this->y;
  return dx1 * dx2 + dy1 * dy2;
}

FloatPoint* FloatPoint::changeSize(double newSize) {
  if (x == 0 && y == 0) {
    // the size of the zero point cannot be changed
    return this;
  }
  double length = jrt::Math::sqrt(x * x + y * y);
  double newX = (x * newSize) / length;
  double newY = (y * newSize) / length;
  return new (jrt::kGeom) FloatPoint(newX, newY);
}

FloatPoint* FloatPoint::changeLength(FloatPoint* toPoint, double newLength) {
  double dx = toPoint->x - this->x;
  double dy = toPoint->y - this->y;
  if (dx == 0 && dy == 0) {
    FRLogger::warn("IntPoint.change_length: Points are equal");
    return toPoint;
  }
  double length = jrt::Math::sqrt(dx * dx + dy * dy);
  double newX = this->x + (dx * newLength) / length;
  double newY = this->y + (dy * newLength) / length;
  return new (jrt::kGeom) FloatPoint(newX, newY);
}

FloatPoint* FloatPoint::middlePoint(FloatPoint* toPoint) {
  if (toPoint == this) {
    return this;
  }
  double middleX = 0.5 * (this->x + toPoint->x);
  double middleY = 0.5 * (this->y + toPoint->y);
  return new (jrt::kGeom) FloatPoint(middleX, middleY);
}

Side* FloatPoint::sideOf(FloatPoint* p1, FloatPoint* p2) {
  double d21X = p2->x - p1->x;
  double d21Y = p2->y - p1->y;
  double d01X = this->x - p1->x;
  double d01Y = this->y - p1->y;
  double determinant = d21X * d01Y - d21Y * d01X;
  return Side::of(determinant);
}

FloatPoint* FloatPoint::rotate(double angle, FloatPoint* pole) {
  if (angle == 0) {
    return this;
  }

  double dx = x - pole->x;
  double dy = y - pole->y;
  double sinAngle = jrt::Math::sin(angle);
  double cosAngle = jrt::Math::cos(angle);
  double newDx = dx * cosAngle - dy * sinAngle;
  double newDy = dx * sinAngle + dy * cosAngle;
  return new (jrt::kGeom) FloatPoint(pole->x + newDx, pole->y + newDy);
}

FloatPoint* FloatPoint::turn90Degree(int32_t factor) {
  int32_t n = factor;
  while (n < 0) {
    n += 4;
  }
  while (n >= 4) {
    n -= 4;
  }
  switch (n) {
    case 0: return new (jrt::kGeom) FloatPoint(x, y);    // 0 degrees
    case 1: return new (jrt::kGeom) FloatPoint(-y, x);   // 90 degrees
    case 2: return new (jrt::kGeom) FloatPoint(-x, -y);  // 180 degrees
    case 3: return new (jrt::kGeom) FloatPoint(y, -x);   // 270 degrees
    default: return ZERO();
  }
}

FloatPoint* FloatPoint::turn90Degree(int32_t factor, FloatPoint* pole) {
  FloatPoint* v = this->subtract(pole);
  v = v->turn90Degree(factor);
  return pole->add(v);
}

bool FloatPoint::isContainedInBox(FloatPoint* p1, FloatPoint* p2, double tolerance) {
  double minX;
  double maxX;
  if (p1->x < p2->x) {
    minX = p1->x;
    maxX = p2->x;
  } else {
    minX = p2->x;
    maxX = p1->x;
  }
  if (this->x < minX - tolerance || this->x > maxX + tolerance) {
    return false;
  }
  double minY;
  double maxY;
  if (p1->y < p2->y) {
    minY = p1->y;
    maxY = p2->y;
  } else {
    minY = p2->y;
    maxY = p1->y;
  }
  return this->y >= minY - tolerance && this->y <= maxY + tolerance;
}

IntBox* FloatPoint::boundingBox() {
  IntPoint* lowerLeft =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::floor(this->x)), jrt::d2i(jrt::Math::floor(this->y)));
  IntPoint* upperRight =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::ceil(this->x)), jrt::d2i(jrt::Math::ceil(this->y)));
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

jrt::Array<FloatPoint*>* FloatPoint::tangentialPoints(FloatPoint* toPoint, double distance) {
  // turn the situation 90 degree if the x difference is smaller
  // than the y difference for better numerical stability

  double dx = jrt::Math::abs(this->x - toPoint->x);
  double dy = jrt::Math::abs(this->y - toPoint->y);
  bool situationTurned = dy > dx;
  FloatPoint* pole;
  FloatPoint* circleCenter;

  if (situationTurned) {
    // turn the situation by 90 degree
    pole = new (jrt::kGeom) FloatPoint(-this->y, this->x);
    circleCenter = new (jrt::kGeom) FloatPoint(-toPoint->y, toPoint->x);
  } else {
    pole = this;
    circleCenter = toPoint;
  }

  dx = pole->x - circleCenter->x;
  dy = pole->y - circleCenter->y;
  double dxSquare = dx * dx;
  double dySquare = dy * dy;
  double distSquare = dxSquare + dySquare;
  double radiusSquare = distance * distance;
  double discriminant = radiusSquare * dySquare - (radiusSquare - dxSquare) * distSquare;

  if (discriminant <= 0) {
    // pole is inside the circle.
    return new (jrt::kGeom) jrt::Array<FloatPoint*>(0);
  }
  double squareRoot = jrt::Math::sqrt(discriminant);

  auto* result = new (jrt::kGeom) jrt::Array<FloatPoint*>(2);

  double a1 = radiusSquare * dy;
  double dy1 = (a1 + distance * squareRoot) / distSquare;
  double dy2 = (a1 - distance * squareRoot) / distSquare;

  double firstPointY = dy1 + circleCenter->y;
  double firstPointX = (radiusSquare - dy * dy1) / dx + circleCenter->x;
  double secondPointY = dy2 + circleCenter->y;
  double secondPointX = (radiusSquare - dy * dy2) / dx + circleCenter->x;

  if (situationTurned) {
    // turn the result by 270 degree
    (*result)[0] = new (jrt::kGeom) FloatPoint(firstPointY, -firstPointX);
    (*result)[1] = new (jrt::kGeom) FloatPoint(secondPointY, -secondPointX);
  } else {
    (*result)[0] = new (jrt::kGeom) FloatPoint(firstPointX, firstPointY);
    (*result)[1] = new (jrt::kGeom) FloatPoint(secondPointX, secondPointY);
  }
  return result;
}

FloatPoint* FloatPoint::leftTangentialPoint(FloatPoint* toPoint, double distance) {
  if (toPoint == nullptr) {
    return nullptr;
  }
  jrt::Array<FloatPoint*>* tangentPoints = tangentialPoints(toPoint, distance);
  if (tangentPoints->length < 2) {
    return nullptr;
  }
  FloatPoint* result;
  if (toPoint->sideOf(this, (*tangentPoints)[0]) == Side::ON_THE_RIGHT()) {
    result = (*tangentPoints)[0];
  } else {
    result = (*tangentPoints)[1];
  }
  return result;
}

FloatPoint* FloatPoint::rightTangentialPoint(FloatPoint* toPoint, double distance) {
  if (toPoint == nullptr) {
    return nullptr;
  }
  jrt::Array<FloatPoint*>* tangentPoints = tangentialPoints(toPoint, distance);
  if (tangentPoints->length < 2) {
    return nullptr;
  }
  FloatPoint* result;
  if (toPoint->sideOf(this, (*tangentPoints)[0]) == Side::ON_THE_LEFT()) {
    result = (*tangentPoints)[0];
  } else {
    result = (*tangentPoints)[1];
  }
  return result;
}

FloatPoint* FloatPoint::circleCenter(FloatPoint* p1, FloatPoint* p2) {
  double slope1 = (p1->y - this->y) / (p1->x - this->x);
  double slope2 = (p2->y - p1->y) / (p2->x - p1->x);
  double centerX =
      (slope1 * slope2 * (this->y - p2->y) + slope2 * (this->x + p1->x) -
       slope1 * (p1->x + p2->x)) /
      (2 * (slope2 - slope1));
  double centerY = (0.5 * (this->x + p1->x) - centerX) / slope1 + 0.5 * (this->y + p1->y);
  return new (jrt::kGeom) FloatPoint(centerX, centerY);
}

bool FloatPoint::insideCircle(FloatPoint* p1, FloatPoint* p2, FloatPoint* p3) {
  FloatPoint* center = p1->circleCenter(p2, p3);
  double radiusSquare = center->distanceSquare(p1);
  return this->distanceSquare(center) < radiusSquare - 1;  // - 1 is a tolerance for numerical
                                                           // stability.
}

std::string FloatPoint::toString(int32_t fractionDigits, int32_t padding) {
  // PORT NOTE: NumberFormat.getInstance(locale) emulated for Locale.ENGLISH (see header).
  return "X " + padLeft(formatEnglishNumber(x, fractionDigits, fractionDigits), padding) +
         "   Y " + padLeft(formatEnglishNumber(-y, fractionDigits, fractionDigits), padding);
}

std::string FloatPoint::toString() {
  // Java toString(Locale.ENGLISH): NumberFormat with maximum 4 fraction digits.
  return "(" + formatEnglishNumber(x, 0, 4) + " , " + formatEnglishNumber(y, 0, 4) + ")";
}

}  // namespace fr
