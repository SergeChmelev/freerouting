#include "geometry/planar/IntVector.h"

#include <typeinfo>

#include "datastructures/BigIntAux.h"
#include "datastructures/Signum.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/RationalVector.h"
#include "geometry/planar/Side.h"

namespace fr {

bool IntVector::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }
  if (typeid(*this) != typeid(*other)) {
    return false;
  }
  IntVector* otherVector = jrt::fast_cast<IntVector>(other);
  return x == otherVector->x && y == otherVector->y;
}

Vector* IntVector::negate() { return new (jrt::kGeom) IntVector(-x, -y); }

bool IntVector::isOrthogonal() { return x == 0 || y == 0; }

bool IntVector::isDiagonal() { return jrt::Math::abs(x) == jrt::Math::abs(y); }

int64_t IntVector::determinant(IntVector* other) {
  return static_cast<int64_t>(x) * other->y - static_cast<int64_t>(y) * other->x;
}

Vector* IntVector::turn90Degree(int32_t factor) {
  int32_t n = factor;
  while (n < 0) {
    n += 4;
  }
  while (n >= 4) {
    n -= 4;
  }
  switch (n) {
    case 0:
      return new (jrt::kGeom) IntVector(x, y);  // 0 degrees
    case 1:
      return new (jrt::kGeom) IntVector(-y, x);  // 90 degrees
    case 2:
      return new (jrt::kGeom) IntVector(-x, -y);  // 180 degrees
    case 3:
      return new (jrt::kGeom) IntVector(y, -x);  // 270 degrees
    default:
      return IntVector::ZERO();
  }
}

Vector* IntVector::mirrorAtYAxis() { return new (jrt::kGeom) IntVector(-this->x, this->y); }

Vector* IntVector::mirrorAtXAxis() { return new (jrt::kGeom) IntVector(this->x, -this->y); }

Vector* IntVector::add(Vector* other) { return other->add(this); }

Vector* IntVector::add(IntVector* other) { return new (jrt::kGeom) IntVector(x + other->x, y + other->y); }

Vector* IntVector::add(RationalVector* other) { return other->add(this); }

Point* IntVector::addTo(IntPoint* point) { return new (jrt::kGeom) IntPoint(point->x + x, point->y + y); }

Point* IntVector::addTo(RationalPoint* point) { return point->translateBy(this); }

Side* IntVector::sideOf(Vector* other) {
  Side* tmp = other->sideOf(this);
  return tmp->negate();
}

Side* IntVector::sideOf(IntVector* other) {
  double determinant = static_cast<double>(other->x) * y - static_cast<double>(other->y) * x;
  return Side::of(determinant);
}

Side* IntVector::sideOf(RationalVector* other) {
  Side* tmp = other->sideOf(this);
  return tmp->negate();
}

Signum* IntVector::projection(Vector* other) { return other->projection(this); }

Signum* IntVector::projection(IntVector* other) {
  double tmp = static_cast<double>(x) * other->x + static_cast<double>(y) * other->y;
  return Signum::of(tmp);
}

Signum* IntVector::projection(RationalVector* other) { return other->projection(this); }

double IntVector::scalarProduct(Vector* other) { return other->scalarProduct(this); }

double IntVector::scalarProduct(IntVector* other) {
  return static_cast<double>(x) * other->x + static_cast<double>(y) * other->y;
}

double IntVector::scalarProduct(RationalVector* other) { return other->scalarProduct(this); }

FloatPoint* IntVector::toFloat() { return new (jrt::kGeom) FloatPoint(x, y); }

Vector* IntVector::changeLengthApprox(double length) {
  FloatPoint* newPoint = this->toFloat()->changeSize(length);
  return newPoint->round()->differenceBy(Point::ZERO());
}

Direction* IntVector::toNormalizedDirection() {
  int32_t dx = x;
  int32_t dy = y;
  int32_t gcd = BigIntAux::binaryGcd(jrt::Math::abs(dx), jrt::Math::abs(dy));
  if (gcd > 1) {
    dx /= gcd;
    dy /= gcd;
  }
  return new (jrt::kGeom) IntDirection(dx, dy);
}

}  // namespace fr
