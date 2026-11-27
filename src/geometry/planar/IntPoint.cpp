#include "geometry/planar/IntPoint.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/RationalVector.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

IntPoint::IntPoint(int32_t x, int32_t y) {
  if (jrt::Math::abs(x) > Limits::CRIT_INT) {
    FRLogger::debug("IntPoint: x is out of range");
  }
  if (jrt::Math::abs(y) > Limits::CRIT_INT) {
    FRLogger::debug("IntPoint: y is out of range");
  }

  this->x = x;
  this->y = y;
}

bool IntPoint::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr) {
    return false;
  }
  if (typeid(*this) != typeid(*other)) {
    return false;
  }
  IntPoint* otherPoint = jrt::fast_cast<IntPoint>(other);
  return x == otherPoint->x && y == otherPoint->y;
}

int32_t IntPoint::hashCode() { return 31 * x + y; }

IntBox* IntPoint::surroundingBox() { return new (jrt::kGeom) IntBox(this, this); }

IntOctagon* IntPoint::surroundingOctagon() {
  int32_t tmp1 = x - y;
  int32_t tmp2 = x + y;

  return new (jrt::kGeom) IntOctagon(x, y, x, y, tmp1, tmp1, tmp2, tmp2);
}

bool IntPoint::isContainedIn(IntBox* box) {
  return x >= box->ll->x && y >= box->ll->y && x <= box->ur->x && y <= box->ur->y;
}

Point* IntPoint::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  return vector->addTo(this);
}

Point* IntPoint::translateBy(IntVector* vector) {
  return new (jrt::kGeom) IntPoint(x + vector->x, y + vector->y);
}

Point* IntPoint::translateBy(RationalVector* vector) { return vector->addTo(this); }

Vector* IntPoint::differenceBy(Point* other) {
  // PORT NOTE: fast path for an integer point: -(other - this) == this - other bit for bit
  // (-fwrapv), one new vector instead of two.
  if (auto* otherInt = jrt::fast_cast<IntPoint>(other)) {
    return new (jrt::kGeom) IntVector(x - otherInt->x, y - otherInt->y);
  }
  Vector* tmp = other->differenceBy(this);
  return tmp->negate();
}

Vector* IntPoint::differenceBy(RationalPoint* other) {
  Vector* tmp = other->differenceBy(this);
  return tmp->negate();
}

IntVector* IntPoint::differenceBy(IntPoint* other) {
  return new (jrt::kGeom) IntVector(x - other->x, y - other->y);
}

Side* IntPoint::sideOf(Line* line) {
  // PORT NOTE: allocation-free fast path for a line through integer points. Same arithmetic as
  // the generic path below: v1 = this - a, v2 = b - a, IntVector::sideOf dispatches to
  // v2.sideOf(v1) = Side::of(v1.x * v2.y - v1.y * v2.x) and negates it.
  // (line->a and line->b are IntPoints: Line keeps their coordinates.)
  int32_t v1x = x - line->ax;
  int32_t v1y = y - line->ay;
  int32_t v2x = line->bx - line->ax;
  int32_t v2y = line->by - line->ay;
  double determinant = static_cast<double>(v1x) * v2y - static_cast<double>(v1y) * v2x;
  return Side::of(determinant)->negate();
}

FloatPoint* IntPoint::toFloat() { return new (jrt::kGeom) FloatPoint(x, y); }

int32_t IntPoint::getId() { return 31 * x + y; }

int64_t IntPoint::determinant(IntPoint* other) {
  return static_cast<int64_t>(x) * other->y - static_cast<int64_t>(y) * other->x;
}

Point* IntPoint::perpendicularProjection(Line* line) {
  // this function is at the moment only implemented for lines
  // consisting of IntPoints.
  // The general implementation is still missing.
  IntVector* v = static_cast<IntVector*>(line->b->differenceBy(line->a));
  jrt::BigInteger vxvx = jrt::BigInteger::valueOf(static_cast<int64_t>(v->x) * v->x);
  jrt::BigInteger vyvy = jrt::BigInteger::valueOf(static_cast<int64_t>(v->y) * v->y);
  jrt::BigInteger vxvy = jrt::BigInteger::valueOf(static_cast<int64_t>(v->x) * v->y);
  jrt::BigInteger denominator = vxvx.add(vyvy);
  jrt::BigInteger det = jrt::BigInteger::valueOf(
      static_cast<IntPoint*>(line->a)->determinant(static_cast<IntPoint*>(line->b)));
  jrt::BigInteger pointX = jrt::BigInteger::valueOf(x);
  jrt::BigInteger pointY = jrt::BigInteger::valueOf(y);

  jrt::BigInteger tmp1 = vxvx.multiply(pointX);
  jrt::BigInteger tmp2 = vxvy.multiply(pointY);
  tmp1 = tmp1.add(tmp2);
  tmp2 = det.multiply(jrt::BigInteger::valueOf(v->y));
  jrt::BigInteger projX = tmp1.add(tmp2);

  tmp1 = vxvy.multiply(pointX);
  tmp2 = vyvy.multiply(pointY);
  tmp1 = tmp1.add(tmp2);
  tmp2 = det.multiply(jrt::BigInteger::valueOf(v->x));
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
      return new (jrt::kGeom) IntPoint(projX.intValue(), projY.intValue());
    }
  }
  return new (jrt::kGeom) RationalPoint(projX, projY, denominator);
}

double IntPoint::signedArea(IntPoint* p1, IntPoint* p2) {
  IntVector* d21 = p2->differenceBy(p1);
  IntVector* d01 = this->differenceBy(p1);
  return static_cast<double>(d21->determinant(d01));
}

double IntPoint::distanceSquare(IntPoint* toPoint) {
  double dx = toPoint->x - this->x;
  double dy = toPoint->y - this->y;
  return dx * dx + dy * dy;
}

double IntPoint::distance(IntPoint* toPoint) { return jrt::Math::sqrt(distanceSquare(toPoint)); }

IntPoint* IntPoint::orthogonalProjection(IntPoint* other) {
  IntPoint* result;
  int32_t horizontalDistance = jrt::Math::abs(this->x - other->x);
  int32_t verticalDistance = jrt::Math::abs(this->y - other->y);
  if (horizontalDistance <= verticalDistance) {
    // projection onto the vertical line through other
    result = new (jrt::kGeom) IntPoint(other->x, this->y);
  } else {
    // projection onto the horizontal line through other
    result = new (jrt::kGeom) IntPoint(this->x, other->y);
  }
  return result;
}

IntPoint* IntPoint::fortyfiveDegreeProjection(IntPoint* other) {
  int32_t dx = this->x - other->x;
  int32_t dy = this->y - other->y;
  auto* distArr = new (jrt::kOp) jrt::Array<double>(4);
  (*distArr)[0] = jrt::Math::abs(dx);
  (*distArr)[1] = jrt::Math::abs(dy);
  double diagonal1 = (static_cast<double>(dy) - static_cast<double>(dx)) / 2;
  double diagonal2 = (static_cast<double>(dy) + static_cast<double>(dx)) / 2;
  (*distArr)[2] = jrt::Math::abs(diagonal1);
  (*distArr)[3] = jrt::Math::abs(diagonal2);
  double minDist = (*distArr)[0];
  for (int32_t i = 1; i < 4; i++) {
    if ((*distArr)[i] < minDist) {
      minDist = (*distArr)[i];
    }
  }
  IntPoint* result;
  if (minDist == (*distArr)[0]) {
    // projection onto the vertical line through other
    result = new (jrt::kGeom) IntPoint(other->x, this->y);
  } else if (minDist == (*distArr)[1]) {
    // projection onto the horizontal line through other
    result = new (jrt::kGeom) IntPoint(this->x, other->y);
  } else if (minDist == (*distArr)[2]) {
    // projection onto the right diagonal line through other
    int32_t diagonalValue = jrt::d2i(diagonal2);
    result = new (jrt::kGeom) IntPoint(other->x + diagonalValue, other->y + diagonalValue);
  } else {
    // projection onto the left diagonal line through other
    int32_t diagonalValue = jrt::d2i(diagonal1);
    result = new (jrt::kGeom) IntPoint(other->x - diagonalValue, other->y + diagonalValue);
  }
  return result;
}

IntPoint* IntPoint::fortyfiveDegreeCorner(IntPoint* toPoint, bool leftTurn) {
  int32_t dx = toPoint->x - this->x;
  int32_t dy = toPoint->y - this->y;
  IntPoint* result;

  // handle the 8 sections between the 45 degree lines

  if (dy > 0 && dy < dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(toPoint->x - dy, this->y);
    } else {
      result = new (jrt::kGeom) IntPoint(this->x + dy, toPoint->y);
    }
  } else if (dx > 0 && dy > dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y + dx);
    } else {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y - dx);
    }
  } else if (dx < 0 && dy > -dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y + dx);
    } else {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y - dx);
    }
  } else if (dy > 0 && dy < -dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(this->x - dy, toPoint->y);
    } else {
      result = new (jrt::kGeom) IntPoint(toPoint->x + dy, this->y);
    }
  } else if (dy < 0 && dy > dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(toPoint->x - dy, this->y);
    } else {
      result = new (jrt::kGeom) IntPoint(this->x + dy, toPoint->y);
    }
  } else if (dx < 0 && dy < dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y + dx);
    } else {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y - dx);
    }
  } else if (dx > 0 && dy < -dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y + dx);
    } else {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y - dx);
    }
  } else if (dy < 0 && dy > -dx) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(this->x - dy, toPoint->y);
    } else {
      result = new (jrt::kGeom) IntPoint(toPoint->x + dy, this->y);
    }
  } else {
    // the line from this point to toPoint is already a multiple of 45 degree
    result = nullptr;
  }
  return result;
}

IntPoint* IntPoint::ninetyDegreeCorner(IntPoint* toPoint, bool leftTurn) {
  int32_t dx = toPoint->x - this->x;
  int32_t dy = toPoint->y - this->y;
  IntPoint* result;

  // handle the 4 quadrants

  if ((dx > 0 && dy > 0) || (dx < 0 && dy < 0)) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y);
    } else {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y);
    }
  } else if ((dx < 0 && dy > 0) || (dx > 0 && dy < 0)) {
    if (leftTurn) {
      result = new (jrt::kGeom) IntPoint(this->x, toPoint->y);
    } else {
      result = new (jrt::kGeom) IntPoint(toPoint->x, this->y);
    }
  } else {
    // the line from this point to toPoint is already orthogonal
    result = nullptr;
  }
  return result;
}

int32_t IntPoint::compareX(Point* other) { return -other->compareX(this); }

int32_t IntPoint::compareX(IntPoint* other) {
  int32_t result;
  if (this->x > other->x) {
    result = 1;
  } else if (this->x == other->x) {
    result = 0;
  } else {
    result = -1;
  }
  return result;
}

int32_t IntPoint::compareX(RationalPoint* other) { return -other->compareX(this); }

int32_t IntPoint::compareY(Point* other) { return -other->compareY(this); }

int32_t IntPoint::compareY(IntPoint* other) {
  int32_t result;
  if (this->y > other->y) {
    result = 1;
  } else if (this->y == other->y) {
    result = 0;
  } else {
    result = -1;
  }
  return result;
}

int32_t IntPoint::compareY(RationalPoint* other) { return -other->compareY(this); }

std::string IntPoint::toString() {
  return "(" + jrt::str(this->x) + "," + jrt::str(this->y) + ")";
}

}  // namespace fr
