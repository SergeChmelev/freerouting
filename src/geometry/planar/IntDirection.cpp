#include "geometry/planar/IntDirection.h"

#include "datastructures/Signum.h"
#include "geometry/planar/BigIntDirection.h"
#include "geometry/planar/IntVector.h"

namespace fr {

IntDirection::IntDirection(IntVector* vector) {
  x = vector->x;
  y = vector->y;
}

bool IntDirection::isOrthogonal() { return x == 0 || y == 0; }

bool IntDirection::isDiagonal() { return jrt::Math::abs(x) == jrt::Math::abs(y); }

Vector* IntDirection::getVector() { return new (jrt::kGeom) IntVector(x, y); }

int32_t IntDirection::compareTo(IntDirection* other) {
  if (y > 0) {
    if (other->y < 0) {
      return -1;
    }
    if (other->y == 0) {
      if (other->x > 0) {
        return 1;
      }
      return -1;
    }
  } else if (y < 0) {
    if (other->y >= 0) {
      return 1;
    }
  } else {  // y == 0
    if (x > 0) {
      if (other->y != 0 || other->x < 0) {
        return -1;
      }
      return 0;
    }
    // x < 0
    if (other->y > 0 || (other->y == 0 && other->x > 0)) {
      return 1;
    }
    if (other->y < 0) {
      return -1;
    }
    return 0;
  }

  // now this direction and other are located in the same
  // open horizontal half plane

  double determinant = static_cast<double>(other->x) * y - static_cast<double>(other->y) * x;
  return Signum::asInt(determinant);
}

int32_t IntDirection::compareTo(Direction* otherDirection) {
  return -otherDirection->compareTo(this);
}

int32_t IntDirection::compareTo(BigIntDirection* other) { return -other->compareTo(this); }

Direction* IntDirection::opposite() { return new (jrt::kGeom) IntDirection(-x, -y); }

Direction* IntDirection::turn45Degree(int32_t factor) {
  int32_t n = jrt::Math::floorMod(factor, 8);
  switch (n) {
    case 0: return new (jrt::kGeom) IntDirection(x, y);           // 0 degrees
    case 1: return new (jrt::kGeom) IntDirection(x - y, x + y);   // 45 degrees
    case 2: return new (jrt::kGeom) IntDirection(-y, x);          // 90 degrees
    case 3: return new (jrt::kGeom) IntDirection(-x - y, x - y);  // 135 degrees
    case 4: return new (jrt::kGeom) IntDirection(-x, -y);         // 180 degrees
    case 5: return new (jrt::kGeom) IntDirection(y - x, -x - y);  // 225 degrees
    case 6: return new (jrt::kGeom) IntDirection(y, -x);          // 270 degrees
    case 7: return new (jrt::kGeom) IntDirection(x + y, y - x);   // 315 degrees
    default: return new (jrt::kGeom) IntDirection(0, 0);
  }
}

double IntDirection::determinant(IntDirection* other) {
  return static_cast<double>(x) * other->y - static_cast<double>(y) * other->x;
}

}  // namespace fr
