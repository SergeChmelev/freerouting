#include "geometry/planar/Direction.h"

#include <typeinfo>

#include "datastructures/Signum.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"

namespace fr {

IntDirection* Direction::NULL_() {
  static IntDirection* v = new IntDirection(0, 0);
  return v;
}
IntDirection* Direction::RIGHT() {
  static IntDirection* v = new IntDirection(1, 0);
  return v;
}
IntDirection* Direction::RIGHT45() {
  static IntDirection* v = new IntDirection(1, 1);
  return v;
}
IntDirection* Direction::UP() {
  static IntDirection* v = new IntDirection(0, 1);
  return v;
}
IntDirection* Direction::UP45() {
  static IntDirection* v = new IntDirection(-1, 1);
  return v;
}
IntDirection* Direction::LEFT() {
  static IntDirection* v = new IntDirection(-1, 0);
  return v;
}
IntDirection* Direction::LEFT45() {
  static IntDirection* v = new IntDirection(-1, -1);
  return v;
}
IntDirection* Direction::DOWN() {
  static IntDirection* v = new IntDirection(0, -1);
  return v;
}
IntDirection* Direction::DOWN45() {
  static IntDirection* v = new IntDirection(1, -1);
  return v;
}

Direction* Direction::getInstance(Vector* vector) { return vector->toNormalizedDirection(); }

Direction* Direction::getInstance(Point* from, Point* to) {
  if (from->equals(to)) {
    return nullptr;
  }
  return getInstance(to->differenceBy(from));
}

Direction* Direction::getInstanceApprox(double angle) {
  const double scaleFactor = 10000;
  int32_t x = jrt::l2i(jrt::Math::round(jrt::Math::cos(angle) * scaleFactor));
  int32_t y = jrt::l2i(jrt::Math::round(jrt::Math::sin(angle) * scaleFactor));
  return getInstance(new IntVector(x, y));
}

bool Direction::isMultipleOf45Degree() { return isOrthogonal() || isDiagonal(); }

bool Direction::equals(jrt::Object* other) {
  if (other == this) {
    return true;
  }
  if (other == nullptr || typeid(*this) != typeid(*other)) {
    return false;
  }
  Direction* otherDirection = jrt::fast_cast<Direction>(other);
  if (this == otherDirection) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }

  if (this->sideOf(otherDirection) != Side::COLLINEAR()) {
    return false;
  }
  Vector* thisVector = getVector();
  Vector* otherVector = otherDirection->getVector();
  return thisVector->projection(otherVector) == Signum::POSITIVE();
}

Side* Direction::sideOf(Direction* other) { return this->getVector()->sideOf(other->getVector()); }

Signum* Direction::projection(Direction* other) {
  return this->getVector()->projection(other->getVector());
}

Direction* Direction::middleApprox(Direction* other) {
  FloatPoint* v1 = getVector()->toFloat();
  FloatPoint* v2 = other->getVector()->toFloat();
  double length1 = v1->size();
  double length2 = v2->size();
  double x = v1->x / length1 + v2->x / length2;
  double y = v1->y / length1 + v2->y / length2;
  const double scaleFactor = 1000;
  Vector* vm = new IntVector(jrt::l2i(jrt::Math::round(x * scaleFactor)),
                             jrt::l2i(jrt::Math::round(y * scaleFactor)));
  return Direction::getInstance(vm);
}

int32_t Direction::compareFrom(Direction* p1, Direction* p2) {
  int32_t result;
  if (p1->compareTo(this) >= 0) {
    if (p2->compareTo(this) >= 0) {
      result = p1->compareTo(p2);
    } else {
      result = -1;
    }
  } else {
    if (p2->compareTo(this) >= 0) {
      result = 1;
    } else {
      result = p1->compareTo(p2);
    }
  }
  return result;
}

double Direction::angleApprox() { return this->getVector()->angleApprox(); }

std::string Direction::toString() {
  if (this->compareTo(RIGHT()) == 0) {
    return "RIGHT";
  } else if (this->compareTo(RIGHT45()) == 0) {
    return "UP-RIGHT";
  } else if (this->compareTo(UP()) == 0) {
    return "UP";
  } else if (this->compareTo(UP45()) == 0) {
    return "UP-LEFT";
  } else if (this->compareTo(LEFT()) == 0) {
    return "LEFT";
  } else if (this->compareTo(LEFT45()) == 0) {
    return "DOWN-LEFT";
  } else if (this->compareTo(DOWN()) == 0) {
    return "DOWN";
  } else if (this->compareTo(DOWN45()) == 0) {
    return "DOWN-RIGHT";
  } else if (this->compareTo(NULL_()) == 0) {
    return "NULL";
  } else {
    return "UNKNOWN";
  }
}

}  // namespace fr
