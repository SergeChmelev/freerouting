#pragma once

#include "fwd.h"
#include "geometry/planar/Vector.h"
#include "jrt/jrt.h"

namespace fr {

/** Implementation of the interface Vector via a tuple of integers. */
class IntVector : public Vector {
 public:
  /** The x coordinate of this vector. */
  int32_t x;

  /** The y coordinate of this vector. */
  int32_t y;

  /** Creates an IntVector from two integer coordinates. */
  IntVector(int32_t x, int32_t y) : x(x), y(y) {}

  /** Returns true, if this IntVector is equal to other. PORT NOTE: Java does not override
   * hashCode here (identity hash is kept). */
  bool equals(jrt::Object* other) override;

  /** Returns true, if both coordinates of this vector are 0. */
  bool isZero() override { return x == 0 && y == 0; }

  /** Returns the Vector such that this plus this.negate() is zero. */
  Vector* negate() override;

  bool isOrthogonal() override;
  bool isDiagonal() override;

  /** Calculates the determinant of the matrix consisting of this Vector and other. */
  int64_t determinant(IntVector* other);

  Vector* turn90Degree(int32_t factor) override;
  Vector* mirrorAtYAxis() override;
  Vector* mirrorAtXAxis() override;

  /** Adds other to this vector. */
  Vector* add(Vector* other) override;
  Vector* add(IntVector* other) override;
  Vector* add(RationalVector* other) override;

  /** Returns the Point, which results from adding this vector to point. */
  Point* addTo(IntPoint* point) override;
  Point* addTo(RationalPoint* point) override;

  /** Let L be the line from the Zero Vector to other. Returns Side.ON_THE_LEFT, if this Vector is
   * on the left of L, Side.ON_THE_RIGHT, if on the right, and Side.COLLINEAR otherwise. */
  Side* sideOf(Vector* other) override;
  Side* sideOf(IntVector* other) override;
  Side* sideOf(RationalVector* other) override;

  /** Returns Signum.POSITIVE/NEGATIVE/ZERO according to the sign of the scalar product. */
  Signum* projection(Vector* other) override;
  Signum* projection(IntVector* other) override;
  Signum* projection(RationalVector* other) override;

  double scalarProduct(Vector* other) override;
  double scalarProduct(IntVector* other) override;
  double scalarProduct(RationalVector* other) override;

  /** Converts this vector to a FloatPoint. */
  FloatPoint* toFloat() override;

  Vector* changeLengthApprox(double length) override;

  Direction* toNormalizedDirection() override;
  /** toNormalizedDirection() of the vector (x, y), without creating the vector. */
  static Direction* normalizedDirection(int32_t x, int32_t y);
  /** The coordinates of normalizedDirection(x, y), in place. */
  static void normalizeDirectionCoordinates(int32_t& x, int32_t& y);
};

}  // namespace fr
