#include "geometry/planar/BigIntDirection.h"

#include "geometry/planar/IntDirection.h"
#include "geometry/planar/RationalVector.h"

namespace fr {

BigIntDirection::BigIntDirection(IntDirection* dir)
    : x(jrt::BigInteger::valueOf(dir->x)), y(jrt::BigInteger::valueOf(dir->y)) {}

bool BigIntDirection::isOrthogonal() { return x.signum() == 0 || y.signum() == 0; }

bool BigIntDirection::isDiagonal() { return x.abs().equals(y.abs()); }

Vector* BigIntDirection::getVector() { return new (jrt::kGeom) RationalVector(x, y, jrt::BigInteger::ONE()); }

Direction* BigIntDirection::turn45Degree(int32_t factor) {
  jrt::BigInteger r0, r1;
  switch (jrt::Math::floorMod(factor, 8)) {
    case 0:
      r0 = x;
      r1 = y;
      break;
    case 1:
      r0 = x.subtract(y);
      r1 = x.add(y);
      break;
    case 2:
      r0 = y.negate();
      r1 = x;
      break;
    case 3:
      r0 = x.negate().subtract(y);
      r1 = x.subtract(y);
      break;
    case 4:
      r0 = x.negate();
      r1 = y.negate();
      break;
    case 5:
      r0 = y.subtract(x);
      r1 = x.negate().subtract(y);
      break;
    case 6:
      r0 = y;
      r1 = x.negate();
      break;
    default:
      r0 = x.add(y);
      r1 = y.subtract(x);
      break;
  }
  return Direction::getInstance(new (jrt::kGeom) RationalVector(r0, r1, jrt::BigInteger::ONE()));
}

Direction* BigIntDirection::opposite() { return new (jrt::kGeom) BigIntDirection(x.negate(), y.negate()); }

int32_t BigIntDirection::compareTo(Direction* otherDirection) {
  return -otherDirection->compareTo(this);
}

int32_t BigIntDirection::compareTo(IntDirection* otherDirection) {
  BigIntDirection* other = new (jrt::kGeom) BigIntDirection(otherDirection);
  return compareTo(other);
}

int32_t BigIntDirection::compareTo(BigIntDirection* other) {
  int32_t x1 = x.signum();
  int32_t y1 = y.signum();
  int32_t x2 = other->x.signum();
  int32_t y2 = other->y.signum();
  if (y1 > 0) {
    if (y2 < 0) {
      return -1;
    }
    if (y2 == 0) {
      if (x2 > 0) {
        return 1;
      }
      return -1;
    }
  } else if (y1 < 0) {
    if (y2 >= 0) {
      return 1;
    }
  } else {  // y1 == 0
    if (x1 > 0) {
      if (y2 != 0 || x2 < 0) {
        return -1;
      }
      return 0;
    }
    if (y2 > 0 || (y2 == 0 && x2 > 0)) {
      return 1;
    }
    if (y2 < 0) {
      return -1;
    }
    return 0;
  }

  jrt::BigInteger tmp1 = y.multiply(other->x);
  jrt::BigInteger tmp2 = x.multiply(other->y);
  jrt::BigInteger determinant = tmp1.subtract(tmp2);
  return determinant.signum();
}

}  // namespace fr
