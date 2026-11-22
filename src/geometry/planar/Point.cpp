#include "geometry/planar/Point.h"

#include "datastructures/BigIntAux.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"

namespace fr {

BigIntAux_RationalCoordinates* Point::exactCoordinates() {
  if (auto* integer = jrt::fast_cast<IntPoint>(this)) {
    return new (jrt::kOp) BigIntAux_RationalCoordinates(jrt::BigInteger::valueOf(integer->x),
                                             jrt::BigInteger::valueOf(integer->y),
                                             jrt::BigInteger::ONE());
  }
  auto* rational = jrt::fast_cast<RationalPoint>(this);
  return new (jrt::kOp) BigIntAux_RationalCoordinates(rational->x, rational->y, rational->z);
}

IntPoint* Point::toIntegerExact() {
  if (auto* integer = jrt::fast_cast<IntPoint>(this)) {
    return integer;
  }
  auto* rational = jrt::fast_cast<RationalPoint>(this);
  if (rational->isInfinite()) {
    throw jrt::IllegalArgumentException("An infinite point is not an integer board position");
  }
  jrt::Array<jrt::BigInteger>* x = rational->x.divideAndRemainder(rational->z);
  jrt::Array<jrt::BigInteger>* y = rational->y.divideAndRemainder(rational->z);
  if ((*x)[1].signum() != 0 || (*y)[1].signum() != 0 ||
      (*x)[0].abs().compareTo(Limits::CRIT_INT_BIG()) > 0 ||
      (*y)[0].abs().compareTo(Limits::CRIT_INT_BIG()) > 0) {
    throw jrt::IllegalArgumentException("The point is not a bounded integer board position");
  }
  return new (jrt::kGeom) IntPoint((*x)[0].intValueExact(), (*y)[0].intValueExact());
}

IntPoint* Point::ZERO() {
  static IntPoint* v = new (jrt::kGeom) IntPoint(0, 0);
  return v;
}

Point* Point::getInstance(int32_t x, int32_t y) {
  IntPoint* result = new (jrt::kGeom) IntPoint(x, y);
  if (jrt::Math::abs(x) > Limits::CRIT_INT || jrt::Math::abs(y) > Limits::CRIT_INT) {
    return new (jrt::kGeom) RationalPoint(result);
  }
  return result;
}

Point* Point::getInstance(const jrt::BigInteger& px, const jrt::BigInteger& py,
                          const jrt::BigInteger& pz) {
  jrt::BigInteger x = px;
  jrt::BigInteger y = py;
  jrt::BigInteger z = pz;
  if (z.signum() < 0) {
    // the dominator z of a RationalPoint is expected to be positive
    x = x.negate();
    y = y.negate();
    z = z.negate();
  }
  if (x.mod(z).signum() == 0 && y.mod(z).signum() == 0) {
    // x and y can be divided by z
    x = x.divide(z);
    y = y.divide(z);
    z = jrt::BigInteger::ONE();
  }
  if (z.equals(jrt::BigInteger::ONE())) {
    if (x.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0 &&
        y.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0) {
      // the Point fits into an IntPoint
      return new (jrt::kGeom) IntPoint(x.intValue(), y.intValue());
    }
  }
  return new (jrt::kGeom) RationalPoint(x, y, z);
}

Side* Point::sideOf(Point* p1, Point* p2) {
  Vector* v1 = differenceBy(p1);
  Vector* v2 = p2->differenceBy(p1);
  return v1->sideOf(v2);
}

Direction* Point::perpendicularDirection(Line* line) {
  Side* side = this->sideOf(line);
  if (side == Side::COLLINEAR()) {
    return Direction::NULL_();
  }
  Direction* result;
  if (side == Side::ON_THE_RIGHT()) {
    result = line->direction()->turn45Degree(2);
  } else {
    result = line->direction()->turn45Degree(6);
  }
  return result;
}

int32_t Point::compareXY(Point* other) {
  int32_t result = compareX(other);
  if (result == 0) {
    result = compareY(other);
  }
  return result;
}

Point* Point::turn90Degree(int32_t factor, Point* pole) {
  Vector* v = this->differenceBy(pole);
  v = v->turn90Degree(factor);
  return pole->translateBy(v);
}

Point* Point::mirrorVertical(Point* pole) {
  Vector* v = this->differenceBy(pole);
  v = v->mirrorAtYAxis();
  return pole->translateBy(v);
}

Point* Point::mirrorHorizontal(Point* pole) {
  Vector* v = this->differenceBy(pole);
  v = v->mirrorAtXAxis();
  return pole->translateBy(v);
}

}  // namespace fr
