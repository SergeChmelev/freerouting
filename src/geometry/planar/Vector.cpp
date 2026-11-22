#include "geometry/planar/Vector.h"

#include "datastructures/Signum.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RationalVector.h"
#include "geometry/planar/Side.h"

namespace fr {

IntVector* Vector::toIntegerExact() {
  if (auto* integer = dynamic_cast<IntVector*>(this)) {
    return integer;
  }
  IntPoint* integer = Point::ZERO()->translateBy(this)->toIntegerExact();
  return new IntVector(integer->x, integer->y);
}

IntVector* Vector::ZERO() {
  static IntVector* v = new IntVector(0, 0);
  return v;
}

Vector* Vector::getInstance(int32_t x, int32_t y) {
  IntVector* result = new IntVector(x, y);
  if (jrt::Math::abs(x) > Limits::CRIT_INT || jrt::Math::abs(y) > Limits::CRIT_INT) {
    return new RationalVector(result);
  }
  return result;
}

Vector* Vector::getInstance(const jrt::BigInteger& px, const jrt::BigInteger& py,
                            const jrt::BigInteger& pz) {
  jrt::BigInteger x = px;
  jrt::BigInteger y = py;
  jrt::BigInteger z = pz;
  if (z.signum() < 0) {
    x = x.negate();
    y = y.negate();
    z = z.negate();
  }
  if (x.mod(z).signum() == 0 && y.mod(z).signum() == 0) {
    x = x.divide(z);
    y = y.divide(z);
    z = jrt::BigInteger::ONE();
  }
  if (z.equals(jrt::BigInteger::ONE())) {
    if (x.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0 &&
        y.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0) {
      return new IntVector(x.intValue(), y.intValue());
    }
  }
  return new RationalVector(x, y, z);
}

bool Vector::isMultipleOf45Degree() { return isOrthogonal() || isDiagonal(); }

double Vector::lengthApprox() { return this->toFloat()->size(); }

double Vector::cosAngle(Vector* other) {
  double result = this->scalarProduct(other);
  result /= this->toFloat()->size() * other->toFloat()->size();
  return result;
}

double Vector::angleApprox(Vector* other) {
  double result = jrt::Math::acos(cosAngle(other));
  if (this->sideOf(other) == Side::ON_THE_LEFT()) {
    result = -result;
  }
  return result;
}

double Vector::angleApprox() {
  Vector* other = new IntVector(1, 0);
  return other->angleApprox(this);
}

}  // namespace fr
