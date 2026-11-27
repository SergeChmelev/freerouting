#include "geometry/planar/Line.h"

#include "datastructures/Signum.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

void Line::cacheCoordinates() {
  auto* ia = jrt::fast_cast<IntPoint>(a);
  auto* ib = jrt::fast_cast<IntPoint>(b);
  ax = ia->x;
  ay = ia->y;
  bx = ib->x;
  by = ib->y;
}

Line::Line(Point* a, Point* b) {
  if (jrt::fast_cast<IntPoint>(a) != nullptr && jrt::fast_cast<IntPoint>(b) != nullptr) {
    this->a = a;
    this->b = b;
    this->dir = nullptr;
    cacheCoordinates();
    return;
  }
  jrt::Array<IntPoint*>* support = integerSupport(a, b);
  this->a = (*support)[0];
  this->b = (*support)[1];
  this->dir = nullptr;
  cacheCoordinates();
}

Line::Line(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  this->a = new (jrt::kGeom) IntPoint(ax, ay);
  this->b = new (jrt::kGeom) IntPoint(bx, by);
  this->dir = nullptr;
  this->ax = ax;
  this->ay = ay;
  this->bx = bx;
  this->by = by;
}

Line* Line::ofInts(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  // One block: [Line][IntPoint a][IntPoint b]. The collector recognises interior pointers, so a
  // reference to one of the points alone keeps the block alive.
  static_assert(sizeof(Line) % alignof(IntPoint) == 0);
  constexpr std::size_t kLine = sizeof(Line);
  char* mem = static_cast<char*>(::operator new(kLine + 2 * sizeof(IntPoint), jrt::kGeom));
  IntPoint* pa = new (mem + kLine) IntPoint(ax, ay);
  IntPoint* pb = new (mem + kLine + sizeof(IntPoint)) IntPoint(bx, by);
  return new (mem) Line(static_cast<Point*>(pa), static_cast<Point*>(pb));
}

Line::Line(Point* a, Direction* dir) {
  Point* b = a->translateBy(dir->getVector());
  if (jrt::fast_cast<IntPoint>(a) != nullptr && jrt::fast_cast<IntPoint>(b) != nullptr) {
    this->a = a;
    this->b = b;
    this->dir = dir;
    cacheCoordinates();
    return;
  }
  jrt::Array<IntPoint*>* support = integerSupport(a, b);
  this->a = (*support)[0];
  this->b = (*support)[1];
  this->dir = nullptr;
  cacheCoordinates();
}

Line* Line::capAt(Point* point) {
  try {
    return new (jrt::kGeom) Line(point, direction()->turn45Degree(2));
  } catch (const jrt::IllegalArgumentException& unsupportedPerpendicular) {
    if (point == nullptr || point->isInfinite()) {
      return nullptr;
    }
    jrt::Array<jrt::BigInteger>* coordinates = rationalCoordinates(point);
    jrt::BigInteger x = floorDivide((*coordinates)[0], (*coordinates)[2]);
    jrt::BigInteger y = floorDivide((*coordinates)[1], (*coordinates)[2]);
    static const int32_t offsets[5][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (const auto& offset : offsets) {
      jrt::BigInteger ax = x.add(jrt::BigInteger::valueOf(offset[0]));
      jrt::BigInteger ay = y.add(jrt::BigInteger::valueOf(offset[1]));
      if (ax.abs().compareTo(Limits::CRIT_INT_BIG()) > 0 ||
          ay.abs().compareTo(Limits::CRIT_INT_BIG()) > 0) {
        continue;
      }
      try {
        Line* cap = new (jrt::kGeom) Line(point, static_cast<Point*>(
                                        new (jrt::kGeom) IntPoint(ax.intValueExact(), ay.intValueExact())));
        if (!isParallel(cap)) {
          return cap;
        }
      } catch (const jrt::IllegalArgumentException& unsupportedCap) {
      }
    }
    return nullptr;
  }
}

jrt::Array<IntPoint*>* Line::integerSupport(Point* first, Point* second) {
  {
    auto* ia = jrt::fast_cast<IntPoint>(first);
    auto* ib = jrt::fast_cast<IntPoint>(second);
    if (ia != nullptr && ib != nullptr) {
      return jrt::Array<IntPoint*>::of({ia, ib});
    }
  }
  if (first == nullptr || second == nullptr || first->isInfinite() || second->isInfinite()) {
    throw jrt::IllegalArgumentException("A line requires finite points");
  }
  jrt::Array<jrt::BigInteger>* a = rationalCoordinates(first);
  jrt::Array<jrt::BigInteger>* b = rationalCoordinates(second);
  jrt::BigInteger dx = (*b)[0].multiply((*a)[2]).subtract((*a)[0].multiply((*b)[2]));
  jrt::BigInteger dy = (*b)[1].multiply((*a)[2]).subtract((*a)[1].multiply((*b)[2]));
  jrt::BigInteger gcd = dx.gcd(dy);
  if (gcd.signum() == 0) {
    throw jrt::IllegalArgumentException("A line requires distinct points");
  }
  dx = dx.divide(gcd);
  dy = dy.divide(gcd);
  jrt::Array<jrt::BigInteger>* constant =
      (*a)[0].multiply(dy).subtract((*a)[1].multiply(dx)).divideAndRemainder((*a)[2]);
  if ((*constant)[1].signum() != 0) {
    throw jrt::IllegalArgumentException("The line has no integer defining points");
  }
  jrt::BigInteger x;
  jrt::BigInteger y;
  if (dx.signum() == 0) {
    x = (*constant)[0].divide(dy);
    y = jrt::BigInteger::ZERO();
  } else {
    jrt::BigInteger modulus = dx.abs();
    x = (*constant)[0].multiply(dy.mod(modulus).modInverse(modulus)).mod(modulus);
    y = dy.multiply(x).subtract((*constant)[0]).divide(dx);
  }
  auto* interval = new (jrt::kOp) jrt::Array<std::optional<jrt::BigInteger>>(2);
  restrictSupportParameter(interval, x, dx);
  restrictSupportParameter(interval, y, dy);
  if ((*interval)[0].value().compareTo((*interval)[1].value()) > 0) {
    throw jrt::IllegalArgumentException("The line has no bounded integer defining points");
  }
  jrt::BigInteger step =
      dx.signum() == 0 ? floorDivide((*a)[1].subtract(y.multiply((*a)[2])), dy.multiply((*a)[2]))
                       : floorDivide((*a)[0].subtract(x.multiply((*a)[2])), dx.multiply((*a)[2]));
  step = step.max((*interval)[0].value()).min((*interval)[1].value());
  x = x.add(step.multiply(dx));
  y = y.add(step.multiply(dy));
  IntPoint* p0 = new (jrt::kGeom) IntPoint(x.intValueExact(), y.intValueExact());
  IntPoint* p1 = new (jrt::kGeom) IntPoint(x.add(dx).intValueExact(), y.add(dy).intValueExact());
  return jrt::Array<IntPoint*>::of({p0, p1});
}

jrt::Array<jrt::BigInteger>* Line::rationalCoordinates(Point* point) {
  if (auto* integer = jrt::fast_cast<IntPoint>(point)) {
    return jrt::Array<jrt::BigInteger>::of({jrt::BigInteger::valueOf(integer->x),
                                            jrt::BigInteger::valueOf(integer->y),
                                            jrt::BigInteger::ONE()});
  }
  RationalPoint* rational = jrt::fast_cast<RationalPoint>(point);
  if (rational == nullptr) {
    throw jrt::ClassCastException("Point is not a RationalPoint");
  }
  return jrt::Array<jrt::BigInteger>::of({rational->x, rational->y, rational->z});
}

void Line::restrictSupportParameter(jrt::Array<std::optional<jrt::BigInteger>>* interval,
                                    const jrt::BigInteger& origin,
                                    const jrt::BigInteger& direction) {
  jrt::BigInteger limit = Limits::CRIT_INT_BIG();
  jrt::BigInteger low = limit.negate().max(limit.negate().subtract(direction));
  jrt::BigInteger high = limit.min(limit.subtract(direction));
  if (direction.signum() == 0) {
    if (origin.compareTo(low) < 0 || origin.compareTo(high) > 0) {
      throw jrt::IllegalArgumentException("The line is outside the integer coordinate range");
    }
    return;
  }
  jrt::BigInteger start = direction.signum() > 0 ? low : high;
  jrt::BigInteger end = direction.signum() > 0 ? high : low;
  jrt::BigInteger lower = floorDivide(start.subtract(origin).negate(), direction).negate();
  jrt::BigInteger upper = floorDivide(end.subtract(origin), direction);
  (*interval)[0] = !(*interval)[0].has_value() ? lower : (*interval)[0].value().max(lower);
  (*interval)[1] = !(*interval)[1].has_value() ? upper : (*interval)[1].value().min(upper);
}

jrt::BigInteger Line::floorDivide(const jrt::BigInteger& pNumerator,
                                  const jrt::BigInteger& pDenominator) {
  jrt::BigInteger numerator = pNumerator;
  jrt::BigInteger denominator = pDenominator;
  if (denominator.signum() < 0) {
    numerator = numerator.negate();
    denominator = denominator.negate();
  }
  jrt::Array<jrt::BigInteger>* quotient = numerator.divideAndRemainder(denominator);
  return (*quotient)[1].signum() < 0 ? (*quotient)[0].subtract(jrt::BigInteger::ONE())
                                     : (*quotient)[0];
}

Line* Line::getInstance(Point* a, Direction* dir) {
  // PORT NOTE: allocation-free fast path for the direction vector (IntVector.addTo(IntPoint)).
  if (auto* ai = jrt::fast_cast<IntPoint>(a)) {
    if (auto* d = jrt::fast_cast<IntDirection>(dir)) {
      return new (jrt::kGeom) Line(a, new (jrt::kGeom) IntPoint(ai->x + d->x, ai->y + d->y));
    }
  }
  Point* b = a->translateBy(dir->getVector());
  return new (jrt::kGeom) Line(a, b);
}

int32_t Line::getId() { return 31 * a->getId() + b->getId(); }

bool Line::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }
  auto* otherLine = jrt::fast_cast<Line>(other);
  if (otherLine == nullptr) {
    return false;
  }
  if (sideOf(otherLine->a) != Side::COLLINEAR()) {
    return false;
  }
  if (sideOf(otherLine->b) != Side::COLLINEAR()) {
    return false;
  }
  Vector* dir1 = b->differenceBy(a);
  Vector* dir2 = otherLine->b->differenceBy(otherLine->a);
  return dir1->projection(dir2) == Signum::POSITIVE();
}

bool Line::fastEquals(Line* other) {
  double dx1 = other->ax - ax;
  double dy1 = other->ay - ay;
  double dx2 = bx - ax;
  double dy2 = by - ay;
  double det = dx1 * dy2 - dx2 * dy1;
  if (det != 0) {
    return false;
  }
  return direction()->equals(other->direction());
}

Direction* Line::direction() {
  if (dir == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // PORT NOTE: fast path without the temporary difference vector (same arithmetic).
    dir = IntVector::normalizedDirection(bx - ax, by - ay);
  }
  return dir;
}

bool Line::directionInts(int32_t& x, int32_t& y) {
  if (dir != nullptr) {
    auto* d = jrt::fast_cast<IntDirection>(dir);
    if (d == nullptr) return false;
    x = d->x;
    y = d->y;
    return true;
  }
  if (dirIntsState != 0) {
    x = dirX;
    y = dirY;
    return dirIntsState > 0;
  }
  x = bx - ax;
  y = by - ay;
  IntVector::normalizeDirectionCoordinates(x, y);
  dirX = x;
  dirY = y;
  dirIntsState = 1;
  return true;
}

Side* Line::sideOf(Point* point) {
  Side* result = point->sideOf(this);
  return result->negate();
}

Side* Line::sideOf(FloatPoint* point, double tolerance) {
  return sideOfApprox(point->x, point->y, tolerance);
}

Side* Line::sideOfApprox(double px, double py, double tolerance) {
  double det = (by - ay) * (px - ax) - (bx - ax) * (py - ay);
  Side* result;
  if (det - tolerance > 0) {
    result = Side::ON_THE_LEFT();
  } else if (det + tolerance < 0) {
    result = Side::ON_THE_RIGHT();
  } else {
    result = Side::COLLINEAR();
  }
  return result;
}

Side* Line::sideOf(FloatPoint* point) { return sideOf(point, 0); }

Side* Line::sideOfIntersection(Line* p1, Line* p2) {
  double isX;
  double isY;
  p1->intersectionApprox(p2, isX, isY);
  Side* result = this->sideOfApprox(isX, isY, 1.0);
  if (result == Side::COLLINEAR()) {
    Point* intersection = p1->intersection(p2);
    result = this->sideOf(intersection);
  }
  return result;
}

bool Line::isOnTheLeft(TileShape* tile) {
  for (int32_t i = 0; i < tile->borderLineCount(); i++) {
    if (this->sideOf(tile->corner(i)) == Side::ON_THE_RIGHT()) {
      return false;
    }
  }
  return true;
}

bool Line::isOnTheRight(TileShape* tile) {
  for (int32_t i = 0; i < tile->borderLineCount(); i++) {
    if (this->sideOf(tile->corner(i)) == Side::ON_THE_LEFT()) {
      return false;
    }
  }
  return true;
}

double Line::signedDistance(FloatPoint* point) {
  double dx = bx - ax;
  double dy = by - ay;
  double det = dy * (point->x - ax) - dx * (point->y - ay);
  double length = jrt::Math::sqrt(dx * dx + dy * dy);
  return det / length;
}

bool Line::overlaps(Line* other) {
  return sideOf(other->a) == Side::COLLINEAR() && sideOf(other->b) == Side::COLLINEAR();
}

Line* Line::opposite() { return new (jrt::kGeom) Line(b, a); }

Point* Line::intersection(Line* other) {
  // PORT NOTE: the difference vectors of integer lines are local values (b - a, as
  // IntPoint.differenceBy); they are only read below.
  std::optional<IntVector> localDelta1;
  std::optional<IntVector> localDelta2;
  IntVector* delta1;
  IntVector* delta2;
  {
    auto* ia = jrt::fast_cast<IntPoint>(a);
    auto* ib = ia != nullptr ? jrt::fast_cast<IntPoint>(b) : nullptr;
    if (ib != nullptr) {
      localDelta1.emplace(ib->x - ia->x, ib->y - ia->y);
      delta1 = &*localDelta1;
    } else {
      delta1 = jrt::fast_cast<IntVector>(b->differenceBy(a));
    }
    auto* oa = jrt::fast_cast<IntPoint>(other->a);
    auto* ob = oa != nullptr ? jrt::fast_cast<IntPoint>(other->b) : nullptr;
    if (ob != nullptr) {
      localDelta2.emplace(ob->x - oa->x, ob->y - oa->y);
      delta2 = &*localDelta2;
    } else {
      delta2 = jrt::fast_cast<IntVector>(other->b->differenceBy(other->a));
    }
  }
  if (delta1->x == 0) {  // this line is vertical
    if (delta2->y == 0) {  // other line is horizontal
      return new (jrt::kGeom) IntPoint(jrt::fast_cast<IntPoint>(this->a)->x,
                          jrt::fast_cast<IntPoint>(other->a)->y);
    }
    if (delta2->x == delta2->y) {  // other line is right diagonal
      int32_t thisX = jrt::fast_cast<IntPoint>(this->a)->x;
      IntPoint* otherA = jrt::fast_cast<IntPoint>(other->a);
      return new (jrt::kGeom) IntPoint(thisX, otherA->y + thisX - otherA->x);
    }
    if (delta2->x == -delta2->y) {  // other line is left diagonal
      int32_t thisX = jrt::fast_cast<IntPoint>(this->a)->x;
      IntPoint* otherA = jrt::fast_cast<IntPoint>(other->a);
      return new (jrt::kGeom) IntPoint(thisX, otherA->y + otherA->x - thisX);
    }
  } else if (delta1->y == 0) {  // this line is horizontal
    if (delta2->x == 0) {  // other line is vertical
      return new (jrt::kGeom) IntPoint(jrt::fast_cast<IntPoint>(other->a)->x,
                          jrt::fast_cast<IntPoint>(this->a)->y);
    }
    if (delta2->x == delta2->y) {  // other line is right diagonal
      int32_t thisYcoordinate = jrt::fast_cast<IntPoint>(this->a)->y;
      IntPoint* otherA = jrt::fast_cast<IntPoint>(other->a);
      return new (jrt::kGeom) IntPoint(otherA->x + thisYcoordinate - otherA->y, thisYcoordinate);
    }
    if (delta2->x == -delta2->y) {  // other line is left diagonal
      int32_t thisYcoordinate = jrt::fast_cast<IntPoint>(this->a)->y;
      IntPoint* otherA = jrt::fast_cast<IntPoint>(other->a);
      return new (jrt::kGeom) IntPoint(otherA->x + otherA->y - thisYcoordinate, thisYcoordinate);
    }
  } else if (delta1->x == delta1->y) {  // this line is right diagonal
    if (delta2->x == 0) {  // other line is vertical
      int32_t otherX = jrt::fast_cast<IntPoint>(other->a)->x;
      IntPoint* thisA = jrt::fast_cast<IntPoint>(this->a);
      return new (jrt::kGeom) IntPoint(otherX, thisA->y + otherX - thisA->x);
    }
    if (delta2->y == 0) {  // other line is horizontal
      int32_t otherY = jrt::fast_cast<IntPoint>(other->a)->y;
      IntPoint* thisA = jrt::fast_cast<IntPoint>(this->a);
      return new (jrt::kGeom) IntPoint(thisA->x + otherY - thisA->y, otherY);
    }
  } else if (delta1->x == -delta1->y) {  // this line is left diagonal
    if (delta2->x == 0) {  // other line is vertical
      int32_t otherX = jrt::fast_cast<IntPoint>(other->a)->x;
      IntPoint* thisA = jrt::fast_cast<IntPoint>(this->a);
      return new (jrt::kGeom) IntPoint(otherX, thisA->y + thisA->x - otherX);
    }
    if (delta2->y == 0) {  // other line is horizontal
      int32_t otherY = jrt::fast_cast<IntPoint>(other->a)->y;
      IntPoint* thisA = jrt::fast_cast<IntPoint>(this->a);
      return new (jrt::kGeom) IntPoint(thisA->x + thisA->y - otherY, otherY);
    }
  }
  jrt::BigInteger det1 = jrt::BigInteger::valueOf(
      jrt::fast_cast<IntPoint>(a)->determinant(jrt::fast_cast<IntPoint>(b)));
  jrt::BigInteger det2 = jrt::BigInteger::valueOf(
      jrt::fast_cast<IntPoint>(other->a)->determinant(jrt::fast_cast<IntPoint>(other->b)));
  jrt::BigInteger det = jrt::BigInteger::valueOf(delta2->determinant(delta1));
  jrt::BigInteger tmp1 = det1.multiply(jrt::BigInteger::valueOf(delta2->x));
  jrt::BigInteger tmp2 = det2.multiply(jrt::BigInteger::valueOf(delta1->x));
  jrt::BigInteger isX = tmp1.subtract(tmp2);
  tmp1 = det1.multiply(jrt::BigInteger::valueOf(delta2->y));
  tmp2 = det2.multiply(jrt::BigInteger::valueOf(delta1->y));
  jrt::BigInteger isY = tmp1.subtract(tmp2);
  int32_t signum = det.signum();
  if (signum != 0) {
    if (signum < 0) {
      det = det.negate();
      isX = isX.negate();
      isY = isY.negate();
    }
    if (isX.mod(det).signum() == 0 && isY.mod(det).signum() == 0) {
      isX = isX.divide(det);
      isY = isY.divide(det);
      if (jrt::Math::abs(isX.doubleValue()) <= Limits::CRIT_INT &&
          jrt::Math::abs(isY.doubleValue()) <= Limits::CRIT_INT) {
        return new (jrt::kGeom) IntPoint(isX.intValue(), isY.intValue());
      }
      det = jrt::BigInteger::ONE();
    }
  }
  return new (jrt::kGeom) RationalPoint(isX, isY, det);
}

FloatPoint* Line::intersectionApprox(Line* other) {
  double isX;
  double isY;
  intersectionApprox(other, isX, isY);
  return new (jrt::kGeom) FloatPoint(isX, isY);
}

void Line::intersectionApprox(Line* other, double& isX, double& isY) {
  double d1x = bx - ax;
  double d1y = by - ay;
  double d2x = other->bx - other->ax;
  double d2y = other->by - other->ay;
  double det1 = static_cast<double>(ax) * by - static_cast<double>(ay) * bx;
  double det2 = static_cast<double>(other->ax) * other->by - static_cast<double>(other->ay) * other->bx;
  double det = d2x * d1y - d2y * d1x;
  if (det == 0) {
    isX = jrt::Integer::MAX_VALUE;
    isY = jrt::Integer::MAX_VALUE;
  } else {
    isX = (d2x * det1 - d1x * det2) / det;
    isY = (d2y * det1 - d1y * det2) / det;
  }
}

Point* Line::perpendicularProjection(Point* point) { return point->perpendicularProjection(this); }

Line* Line::translate(double dist) {
  IntPoint* ai = jrt::fast_cast<IntPoint>(a);
  // PORT NOTE: the direction coordinates directly instead of a temporary getVector().
  int32_t vx;
  int32_t vy;
  bool intDirection = directionInts(vx, vy);
  if (!intDirection) {
    IntVector* v = jrt::fast_cast<IntVector>(direction()->getVector());
    vx = v->x;
    vy = v->y;
  }
  double vxvx = static_cast<double>(vx) * vx;
  double vyvy = static_cast<double>(vy) * vy;
  double length = jrt::Math::sqrt(vxvx + vyvy);
  int32_t newAx;
  int32_t newAy;
  if (vxvx <= vyvy) {
    int32_t relX = jrt::l2i(jrt::Math::round((dist * length) / vy));
    newAx = ai->x - relX;
    newAy = ai->y;
  } else {
    int32_t relY = jrt::l2i(jrt::Math::round((dist * length) / vx));
    newAx = ai->x;
    newAy = ai->y + relY;
  }
  if (intDirection) {
    // getInstance(newA, direction()) for an IntDirection: the line through newA and
    // newA + (vx, vy), in one allocation.
    return Line::ofInts(newAx, newAy, newAx + vx, newAy + vy);
  }
  return Line::getInstance(new (jrt::kGeom) IntPoint(newAx, newAy), direction());
}

Line* Line::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  Point* newA = a->translateBy(vector);
  Point* newB = b->translateBy(vector);
  return new (jrt::kGeom) Line(newA, newB);
}

bool Line::isOrthogonal() { return direction()->isOrthogonal(); }

bool Line::isDiagonal() { return direction()->isDiagonal(); }

bool Line::isMultipleOf45Degree() { return direction()->isMultipleOf45Degree(); }

bool Line::isParallel(Line* other) {
  return this->direction()->sideOf(other->direction()) == Side::COLLINEAR();
}

bool Line::isPerpendicular(Line* other) {
  Vector* v1 = direction()->getVector();
  Vector* v2 = other->direction()->getVector();
  return v1->projection(v2) == Signum::ZERO();
}

bool Line::isEqualOrOpposite(Line* other) {
  return sideOf(other->a) == Side::COLLINEAR() && sideOf(other->b) == Side::COLLINEAR();
}

double Line::cosAngle(Line* other) {
  Vector* v1 = b->differenceBy(a);
  Vector* v2 = other->b->differenceBy(other->a);
  return v1->cosAngle(v2);
}

int32_t Line::compareTo(Line* other) {
  int32_t dx1 = bx - ax;
  int32_t dy1 = by - ay;
  int32_t dx2 = other->bx - other->ax;
  int32_t dy2 = other->by - other->ay;
  if (dy1 > 0) {
    if (dy2 < 0) {
      return -1;
    }
    if (dy2 == 0) {
      if (dx2 > 0) {
        return 1;
      }
      return -1;
    }
  } else if (dy1 < 0) {
    if (dy2 >= 0) {
      return 1;
    }
  } else {  // dy1 == 0
    if (dx1 > 0) {
      if (dy2 != 0 || dx2 < 0) {
        return -1;
      }
      return 0;
    }
    if (dy2 > 0 || (dy2 == 0 && dx2 > 0)) {
      return 1;
    }
    if (dy2 < 0) {
      return -1;
    }
    return 0;
  }
  double determinant = static_cast<double>(dx2) * dy1 - static_cast<double>(dy2) * dx1;
  return Signum::asInt(determinant);
}

double Line::functionValueApprox(double x) {
  FloatPoint* p1 = a->toFloat();
  FloatPoint* p2 = b->toFloat();
  double dx = p2->x - p1->x;
  if (dx == 0) {
    FRLogger::warn("function_value_approx: line is vertical");
    return 0;
  }
  double dy = p2->y - p1->y;
  double det = p1->x * p2->y - p2->x * p1->y;
  return (dy * x - det) / dx;
}

double Line::functionInYValueApprox(double y) {
  FloatPoint* p1 = a->toFloat();
  FloatPoint* p2 = b->toFloat();
  double dy = p2->y - p1->y;
  if (dy == 0) {
    FRLogger::warn("function_in_y_value_approx: line is horizontal");
    return 0;
  }
  double dx = p2->x - p1->x;
  double det = p1->x * p2->y - p2->x * p1->y;
  return (dx * y + det) / dy;
}

Direction* Line::perpendicularDirection(Point* fromPoint) {
  Side* lineSide = this->sideOf(fromPoint);
  if (lineSide == Side::COLLINEAR()) {
    return nullptr;
  }
  Direction* dir1 = this->direction()->turn45Degree(2);
  Direction* dir2 = this->direction()->turn45Degree(6);
  Point* checkPoint1 = fromPoint->translateBy(dir1->getVector());
  if (this->sideOf(checkPoint1) != lineSide) {
    return dir1;
  }
  Point* checkPoint2 = fromPoint->translateBy(dir2->getVector());
  if (this->sideOf(checkPoint2) != lineSide) {
    return dir2;
  }
  FloatPoint* nearestLinePoint = fromPoint->toFloat()->projectionApprox(this);
  Direction* result;
  if (nearestLinePoint->distanceSquare(checkPoint1->toFloat()) <=
      nearestLinePoint->distanceSquare(checkPoint2->toFloat())) {
    result = dir1;
  } else {
    result = dir2;
  }
  return result;
}

Line* Line::turn90Degree(int32_t factor, IntPoint* pole) {
  Point* newA = a->turn90Degree(factor, pole);
  Point* newB = b->turn90Degree(factor, pole);
  return new (jrt::kGeom) Line(newA, newB);
}

Line* Line::mirrorVertical(IntPoint* pole) {
  Point* newA = b->mirrorVertical(pole);
  Point* newB = a->mirrorVertical(pole);
  return new (jrt::kGeom) Line(newA, newB);
}

Line* Line::mirrorHorizontal(IntPoint* pole) {
  Point* newA = b->mirrorHorizontal(pole);
  Point* newB = a->mirrorHorizontal(pole);
  return new (jrt::kGeom) Line(newA, newB);
}

float Line::length() {
  double dx = static_cast<double>(bx) - ax;
  double dy = static_cast<double>(by) - ay;
  return static_cast<float>(jrt::Math::sqrt(dx * dx + dy * dy));
}

}  // namespace fr
