#pragma once

#include "fwd.h"
#include "geometry/planar/Vector.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Analog RationalPoint, but implementing the functionality of a Vector instead of the
 * functionality of a Point.
 */
class RationalVector : public Vector {
 public:
  jrt::BigInteger x;
  jrt::BigInteger y;
  jrt::BigInteger z;

  /** Creates a RationalVector from 3 BigIntegers x, y and z. They represent the 2-dimensional
   * Vector with the rational number coordinates x/z and y/z. */
  RationalVector(const jrt::BigInteger& x, const jrt::BigInteger& y, const jrt::BigInteger& z);

  /** Creates a RationalVector from an IntVector. */
  explicit RationalVector(IntVector* vector);

  /** Returns true, if the x and y coordinates of this vector are 0. */
  bool isZero() override;

  /** Returns true, if this RationalVector is equal to other. PORT NOTE: Java does not override
   * hashCode here (identity hash is kept). */
  bool equals(jrt::Object* other) override;

  /** Returns the Vector such that this plus this.negate() is zero. */
  Vector* negate() override;

  /** Adds other to this vector. */
  Vector* add(Vector* other) override;
  Vector* add(IntVector* other) override;
  Vector* add(RationalVector* other) override;

  /** Let L be the line from the Zero Vector to other. Returns Side.ON_THE_LEFT, if this Vector is
   * on the left of L, Side.ON_THE_RIGHT, if on the right, and Side.COLLINEAR otherwise. */
  Side* sideOf(Vector* other) override;
  Side* sideOf(IntVector* other) override;
  Side* sideOf(RationalVector* other) override;

  bool isOrthogonal() override;
  bool isDiagonal() override;

  /** Returns Signum.POSITIVE/NEGATIVE/ZERO according to the sign of the scalar product. */
  Signum* projection(Vector* other) override;
  Signum* projection(IntVector* other) override;
  Signum* projection(RationalVector* other) override;

  /** Calculates the scalar product of this vector and other. */
  double scalarProduct(Vector* other) override;
  double scalarProduct(IntVector* other) override;
  double scalarProduct(RationalVector* other) override;

  /** Approximates the coordinates of this vector by float coordinates. */
  FloatPoint* toFloat() override;

  Vector* changeLengthApprox(double length) override;
  Vector* turn90Degree(int32_t factor) override;
  Vector* mirrorAtYAxis() override;
  Vector* mirrorAtXAxis() override;

  Direction* toNormalizedDirection() override;

  Point* addTo(IntPoint* point) override;
  Point* addTo(RationalPoint* point) override;
};

}  // namespace fr
