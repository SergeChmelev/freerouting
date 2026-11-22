#include "geometry/planar/RationalVector.h"

#include <typeinfo>

#include "datastructures/BigIntAux.h"
#include "datastructures/Signum.h"
#include "geometry/planar/BigIntDirection.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/Side.h"

namespace fr {

RationalVector::RationalVector(const jrt::BigInteger& x, const jrt::BigInteger& y,
                               const jrt::BigInteger& z)
    : x(z.signum() >= 0 ? x : x.negate()),
      y(z.signum() >= 0 ? y : y.negate()),
      z(z.signum() >= 0 ? z : z.negate()) {}

RationalVector::RationalVector(IntVector* vector)
    : x(jrt::BigInteger::valueOf(vector->x)),
      y(jrt::BigInteger::valueOf(vector->y)),
      z(jrt::BigInteger::ONE()) {}

bool RationalVector::isZero() { return x.signum() == 0 && y.signum() == 0; }

bool RationalVector::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }
  if (typeid(*this) != typeid(*other)) {
    return false;
  }
  RationalVector* otherVector = dynamic_cast<RationalVector*>(other);
  jrt::BigInteger det = BigIntAux::determinant(x, otherVector->x, z, otherVector->z);
  if (det.signum() != 0) {
    return false;
  }
  det = BigIntAux::determinant(y, otherVector->y, z, otherVector->z);
  return det.signum() == 0;
}

Vector* RationalVector::negate() { return new RationalVector(x.negate(), y.negate(), z); }

Vector* RationalVector::add(Vector* other) { return other->add(this); }

Vector* RationalVector::add(IntVector* other) {
  BigIntAux_RationalCoordinates* result = BigIntAux::addRationalCoordinates(
      x, y, z, jrt::BigInteger::valueOf(other->x), jrt::BigInteger::valueOf(other->y),
      jrt::BigInteger::ONE());
  return new RationalVector(result->x(), result->y(), result->z());
}

Vector* RationalVector::add(RationalVector* other) {
  BigIntAux_RationalCoordinates* result =
      BigIntAux::addRationalCoordinates(x, y, z, other->x, other->y, other->z);
  return new RationalVector(result->x(), result->y(), result->z());
}

Side* RationalVector::sideOf(Vector* other) {
  Side* tmp = other->sideOf(this);
  return tmp->negate();
}

Side* RationalVector::sideOf(IntVector* other) {
  return Side::of(y.multiply(jrt::BigInteger::valueOf(other->x))
                      .subtract(x.multiply(jrt::BigInteger::valueOf(other->y)))
                      .signum());
}

Side* RationalVector::sideOf(RationalVector* other) {
  jrt::BigInteger tmp1 = y.multiply(other->x);
  jrt::BigInteger tmp2 = x.multiply(other->y);
  jrt::BigInteger determinant = tmp1.subtract(tmp2);
  int32_t signum = determinant.signum();
  return Side::of(signum);
}

bool RationalVector::isOrthogonal() { return x.signum() == 0 || y.signum() == 0; }

bool RationalVector::isDiagonal() { return x.abs().equals(y.abs()); }

Signum* RationalVector::projection(Vector* other) { return other->projection(this); }

Signum* RationalVector::projection(IntVector* other) {
  return Signum::of(x.multiply(jrt::BigInteger::valueOf(other->x))
                        .add(y.multiply(jrt::BigInteger::valueOf(other->y)))
                        .signum());
}

Signum* RationalVector::projection(RationalVector* other) {
  jrt::BigInteger tmp1 = x.multiply(other->x);
  jrt::BigInteger tmp2 = y.multiply(other->y);
  jrt::BigInteger tmp3 = tmp1.add(tmp2);
  int32_t result = tmp3.signum();
  return Signum::of(result);
}

double RationalVector::scalarProduct(Vector* other) { return other->scalarProduct(this); }

double RationalVector::scalarProduct(IntVector* other) {
  Vector* vector = new RationalVector(other);
  return vector->scalarProduct(this);
}

double RationalVector::scalarProduct(RationalVector* other) {
  FloatPoint* v1 = toFloat();
  FloatPoint* v2 = other->toFloat();
  return v1->x * v2->x + v1->y * v2->y;
}

FloatPoint* RationalVector::toFloat() {
  double xd = x.doubleValue();
  double yd = y.doubleValue();
  double zd = z.doubleValue();
  return new FloatPoint(xd / zd, yd / zd);
}

Vector* RationalVector::changeLengthApprox(double length) {
  FloatPoint* scaled = toFloat()->changeSize(length);
  if (!jrt::Double::isFinite(scaled->x) || !jrt::Double::isFinite(scaled->y) ||
      jrt::Math::abs(scaled->x) > Limits::CRIT_INT || jrt::Math::abs(scaled->y) > Limits::CRIT_INT) {
    throw jrt::IllegalArgumentException("The scaled vector exceeds the integer coordinate range");
  }
  return scaled->round()->differenceBy(Point::ZERO());
}

Vector* RationalVector::turn90Degree(int32_t factor) {
  int32_t n = factor;
  while (n < 0) {
    n += 4;
  }
  while (n >= 4) {
    n -= 4;
  }
  switch (n) {
    case 0:
      return this;  // 0 degrees
    case 1:
      return new RationalVector(y.negate(), x, this->z);  // 90 degrees
    case 2:
      return new RationalVector(x.negate(), y.negate(), this->z);  // 180 degrees
    case 3:
      return new RationalVector(y, x.negate(), this->z);  // 270 degrees
    default:
      return this;
  }
}

Vector* RationalVector::mirrorAtYAxis() {
  return new RationalVector(this->x.negate(), this->y, this->z);
}

Vector* RationalVector::mirrorAtXAxis() {
  return new RationalVector(this->x, this->y.negate(), this->z);
}

Direction* RationalVector::toNormalizedDirection() {
  jrt::BigInteger dx = x;
  jrt::BigInteger dy = y;
  jrt::BigInteger gcd = dx.gcd(y);
  if (gcd.signum() == 0) {
    return Direction::NULL_();
  }
  dx = dx.divide(gcd);
  dy = dy.divide(gcd);
  if (dx.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0 &&
      dy.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0) {
    return new IntDirection(dx.intValue(), dy.intValue());
  }
  return new BigIntDirection(dx, dy);
}

Point* RationalVector::addTo(IntPoint* point) {
  jrt::BigInteger newX = z.multiply(jrt::BigInteger::valueOf(point->x));
  newX = newX.add(x);
  jrt::BigInteger newY = z.multiply(jrt::BigInteger::valueOf(point->y));
  newY = newY.add(y);
  return new RationalPoint(newX, newY, z);
}

Point* RationalVector::addTo(RationalPoint* point) {
  BigIntAux_RationalCoordinates* result =
      BigIntAux::addRationalCoordinates(x, y, z, point->x, point->y, point->z);
  return new RationalPoint(result->x(), result->y(), result->z());
}

}  // namespace fr
