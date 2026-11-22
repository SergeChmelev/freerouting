#include "geometry/planar/RationalPoint.h"

#include "datastructures/BigIntAux.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/RationalVector.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"

namespace fr {

RationalPoint::RationalPoint(const jrt::BigInteger& x, const jrt::BigInteger& y,
                             const jrt::BigInteger& z)
    : x(x), y(y), z(z) {
  if (z.signum() < 0) {
    throw jrt::IllegalArgumentException("RationalPoint: z is expected to be >= 0");
  }
}

RationalPoint::RationalPoint(IntPoint* point)
    : x(jrt::BigInteger::valueOf(point->x)),
      y(jrt::BigInteger::valueOf(point->y)),
      z(jrt::BigInteger::ONE()) {}

FloatPoint* RationalPoint::toFloat() {
  double xd = x.doubleValue();
  double yd = y.doubleValue();
  double zd = z.doubleValue();
  if (zd == 0) {
    xd = jrt::Float::MAX_VALUE;
    yd = jrt::Float::MAX_VALUE;
  } else {
    xd /= zd;
    yd /= zd;
  }

  return new FloatPoint(xd, yd);
}

int32_t RationalPoint::getId() {
  int32_t result = x.hashCode();
  result = 31 * result + y.hashCode();
  return 31 * result + z.hashCode();
}

bool RationalPoint::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }
  if (typeid(*this) != typeid(*other)) {
    return false;
  }
  RationalPoint* otherPoint = dynamic_cast<RationalPoint*>(other);
  jrt::BigInteger det = BigIntAux::determinant(x, otherPoint->x, z, otherPoint->z);
  if (det.signum() != 0) {
    return false;
  }
  det = BigIntAux::determinant(y, otherPoint->y, z, otherPoint->z);
  return det.signum() == 0;
}

int32_t RationalPoint::hashCode() {
  if (z.signum() == 0) {
    return 0;
  }
  jrt::BigInteger gcd = x.abs().gcd(y.abs()).gcd(z);
  jrt::BigInteger rx = x;
  jrt::BigInteger ry = y;
  jrt::BigInteger rz = z;
  if (gcd.compareTo(jrt::BigInteger::ONE()) > 0) {
    rx = x.divide(gcd);
    ry = y.divide(gcd);
    rz = z.divide(gcd);
  }
  int32_t result = rx.hashCode();
  result = 31 * result + ry.hashCode();
  return 31 * result + rz.hashCode();
}

bool RationalPoint::isInfinite() { return z.signum() == 0; }

IntBox* RationalPoint::surroundingBox() {
  int32_t llx = floorCoordinate(x);
  int32_t lly = floorCoordinate(y);
  int32_t urx = ceilCoordinate(x);
  int32_t ury = ceilCoordinate(y);
  return new IntBox(llx, lly, urx, ury);
}

IntOctagon* RationalPoint::surroundingOctagon() {
  int32_t lx = floorCoordinate(x);
  int32_t ly = floorCoordinate(y);
  int32_t rx = ceilCoordinate(x);
  int32_t uy = ceilCoordinate(y);
  int32_t ulx = floorCoordinate(x.subtract(y));
  int32_t lrx = ceilCoordinate(x.subtract(y));
  int32_t llx = floorCoordinate(x.add(y));
  int32_t urx = ceilCoordinate(x.add(y));
  return new IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
}

int32_t RationalPoint::floorCoordinate(const jrt::BigInteger& numerator) {
  jrt::Array<jrt::BigInteger>* quotient = numerator.divideAndRemainder(z);
  return boundedInteger((*quotient)[1].signum() < 0
                            ? (*quotient)[0].subtract(jrt::BigInteger::ONE())
                            : (*quotient)[0]);
}

int32_t RationalPoint::ceilCoordinate(const jrt::BigInteger& numerator) {
  jrt::Array<jrt::BigInteger>* quotient = numerator.divideAndRemainder(z);
  return boundedInteger((*quotient)[1].signum() > 0 ? (*quotient)[0].add(jrt::BigInteger::ONE())
                                                    : (*quotient)[0]);
}

int32_t RationalPoint::boundedInteger(const jrt::BigInteger& value) {
  return value.max(jrt::BigInteger::valueOf(jrt::Integer::MIN_VALUE))
      .min(jrt::BigInteger::valueOf(jrt::Integer::MAX_VALUE))
      .intValue();
}

bool RationalPoint::isContainedIn(IntBox* box) {
  jrt::BigInteger tmp = jrt::BigInteger::valueOf(box->ll->x).multiply(z);
  if (x.compareTo(tmp) < 0) {
    return false;
  }
  tmp = jrt::BigInteger::valueOf(box->ll->y).multiply(z);
  if (y.compareTo(tmp) < 0) {
    return false;
  }
  tmp = jrt::BigInteger::valueOf(box->ur->x).multiply(z);
  if (x.compareTo(tmp) > 0) {
    return false;
  }
  tmp = jrt::BigInteger::valueOf(box->ur->y).multiply(z);
  return y.compareTo(tmp) <= 0;
}

Point* RationalPoint::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  return vector->addTo(this);
}

Point* RationalPoint::translateBy(IntVector* vector) {
  BigIntAux_RationalCoordinates* result = BigIntAux::addRationalCoordinates(
      x, y, z, jrt::BigInteger::valueOf(vector->x), jrt::BigInteger::valueOf(vector->y),
      jrt::BigInteger::ONE());
  return new RationalPoint(result->x(), result->y(), result->z());
}

Point* RationalPoint::translateBy(RationalVector* vector) {
  BigIntAux_RationalCoordinates* result =
      BigIntAux::addRationalCoordinates(x, y, z, vector->x, vector->y, vector->z);
  return new RationalPoint(result->x(), result->y(), result->z());
}

Vector* RationalPoint::differenceBy(Point* other) {
  if (auto* integer = dynamic_cast<IntPoint*>(other)) {
    return differenceBy(integer);
  }
  return differenceBy(dynamic_cast<RationalPoint*>(other));
}

Vector* RationalPoint::differenceBy(IntPoint* other) {
  BigIntAux_RationalCoordinates* result = BigIntAux::addRationalCoordinates(
      x, y, z, jrt::BigInteger::valueOf(-other->x), jrt::BigInteger::valueOf(-other->y),
      jrt::BigInteger::ONE());
  return new RationalVector(result->x(), result->y(), result->z());
}

Vector* RationalPoint::differenceBy(RationalPoint* other) {
  BigIntAux_RationalCoordinates* result = BigIntAux::addRationalCoordinates(
      x, y, z, other->x.negate(), other->y.negate(), other->z);
  return new RationalVector(result->x(), result->y(), result->z());
}

Side* RationalPoint::sideOf(Point* p1, Point* p2) {
  BigIntAux_RationalCoordinates* a = coordinates(p1);
  BigIntAux_RationalCoordinates* b = coordinates(p2);
  BigIntAux_RationalCoordinates* v1 =
      BigIntAux::addRationalCoordinates(x, y, z, a->x().negate(), a->y().negate(), a->z());
  auto* i1 = dynamic_cast<IntPoint*>(p1);
  auto* i2 = dynamic_cast<IntPoint*>(p2);
  if (i1 != nullptr && i2 != nullptr) {
    jrt::BigInteger crossProduct =
        v1->y()
            .multiply(jrt::BigInteger::valueOf(static_cast<int64_t>(i2->x) - i1->x))
            .subtract(
                v1->x().multiply(jrt::BigInteger::valueOf(static_cast<int64_t>(i2->y) - i1->y)));
    return Side::of(crossProduct.signum());
  }
  BigIntAux_RationalCoordinates* v2 = BigIntAux::addRationalCoordinates(
      b->x(), b->y(), b->z(), a->x().negate(), a->y().negate(), a->z());
  jrt::BigInteger crossProduct = v1->y().multiply(v2->x()).subtract(v1->x().multiply(v2->y()));
  return Side::of(crossProduct.signum());
}

BigIntAux_RationalCoordinates* RationalPoint::coordinates(Point* point) {
  if (auto* p = dynamic_cast<IntPoint*>(point)) {
    return new BigIntAux_RationalCoordinates(jrt::BigInteger::valueOf(p->x),
                                             jrt::BigInteger::valueOf(p->y),
                                             jrt::BigInteger::ONE());
  }
  if (auto* p = dynamic_cast<RationalPoint*>(point)) {
    return new BigIntAux_RationalCoordinates(p->x, p->y, p->z);
  }
  throw jrt::IllegalArgumentException("Unsupported point type: " + jrt::className(point));
}

Side* RationalPoint::sideOf(Line* line) { return sideOf(line->a, line->b); }

Point* RationalPoint::perpendicularProjection(Line* line) {
  // this function is at the moment only implemented for lines
  // consisting of IntPoints.
  // The general implementation is still missing.
  IntVector* v = static_cast<IntVector*>(line->b->differenceBy(line->a));
  jrt::BigInteger vxvx = jrt::BigInteger::valueOf(static_cast<int64_t>(v->x) * v->x);
  jrt::BigInteger vyvy = jrt::BigInteger::valueOf(static_cast<int64_t>(v->y) * v->y);
  jrt::BigInteger vxvy = jrt::BigInteger::valueOf(static_cast<int64_t>(v->x) * v->y);
  jrt::BigInteger denominator = vxvx.add(vyvy).multiply(z);
  jrt::BigInteger det = jrt::BigInteger::valueOf(
      static_cast<IntPoint*>(line->a)->determinant(static_cast<IntPoint*>(line->b)));

  jrt::BigInteger tmp1 = vxvx.multiply(x);
  jrt::BigInteger tmp2 = vxvy.multiply(y);
  tmp1 = tmp1.add(tmp2);
  tmp2 = det.multiply(jrt::BigInteger::valueOf(v->y));
  tmp2 = tmp2.multiply(z);
  jrt::BigInteger projX = tmp1.add(tmp2);

  tmp1 = vxvy.multiply(x);
  tmp2 = vyvy.multiply(y);
  tmp1 = tmp1.add(tmp2);
  tmp2 = det.multiply(jrt::BigInteger::valueOf(v->x));
  tmp2 = tmp2.multiply(z);
  jrt::BigInteger projY = tmp1.subtract(tmp2);

  int32_t signum = denominator.signum();
  if (signum != 0) {
    if (signum < 0) {
      denominator = denominator.negate();
      projX = projX.negate();
      projY = projY.negate();
    }
    if (projX.mod(denominator).signum() == 0 && projY.mod(denominator).signum() == 0) {
      projX = projX.divide(denominator);
      projY = projY.divide(denominator);
      if (projX.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0 &&
          projY.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0) {
        return new IntPoint(projX.intValue(), projY.intValue());
      }
      denominator = jrt::BigInteger::ONE();
    }
  }
  return new RationalPoint(projX, projY, denominator);
}

int32_t RationalPoint::compareX(Point* other) { return -other->compareX(this); }

int32_t RationalPoint::compareX(RationalPoint* other) {
  jrt::BigInteger tmp1 = this->x.multiply(other->z);
  jrt::BigInteger tmp2 = other->x.multiply(this->z);
  return tmp1.compareTo(tmp2);
}

int32_t RationalPoint::compareX(IntPoint* other) {
  jrt::BigInteger tmp1 = this->z.multiply(jrt::BigInteger::valueOf(other->x));
  return this->x.compareTo(tmp1);
}

int32_t RationalPoint::compareY(Point* other) { return -other->compareY(this); }

int32_t RationalPoint::compareY(RationalPoint* other) {
  jrt::BigInteger tmp1 = this->y.multiply(other->z);
  jrt::BigInteger tmp2 = other->y.multiply(this->z);
  return tmp1.compareTo(tmp2);
}

int32_t RationalPoint::compareY(IntPoint* other) {
  jrt::BigInteger tmp1 = this->z.multiply(jrt::BigInteger::valueOf(other->y));
  return this->y.compareTo(tmp1);
}

}  // namespace fr
