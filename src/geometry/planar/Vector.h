#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Abstract class describing functionality of Vectors. Vectors are used for translating Points in
 * the plane. PORT NOTE: package-private (double dispatch) methods are public in C++.
 */
class Vector : public virtual jrt::Object {
 public:
  /** Returns the identical bounded integer displacement without rounding fractional vectors. */
  IntVector* toIntegerExact();

  /** Standard implementation of the zero vector. PORT NOTE: static field -> accessor. */
  static IntVector* ZERO();

  /** Creates a Vector (x, y) in the plane. */
  static Vector* getInstance(int32_t x, int32_t y);

  /** Creates a 2-dimensional Vector from the 3 input values. If z != 0 it correspondents to the
   * Vector in the plane with rational number coordinates (x / z, y / z). */
  static Vector* getInstance(const jrt::BigInteger& x, const jrt::BigInteger& y,
                             const jrt::BigInteger& z);

  /** Returns true, if this vector is equal to the zero vector. */
  virtual bool isZero() = 0;

  /** Returns the Vector such that this plus this.negate() is zero. */
  virtual Vector* negate() = 0;

  /** Adds other to this vector. */
  virtual Vector* add(Vector* other) = 0;
  virtual Vector* add(IntVector* other) = 0;
  virtual Vector* add(RationalVector* other) = 0;

  /** Let L be the line from the Zero Vector to other. Returns Side.ON_THE_LEFT, if this Vector is
   * on the left of L, Side.ON_THE_RIGHT, if on the right, and Side.COLLINEAR otherwise. */
  virtual Side* sideOf(Vector* other) = 0;
  virtual Side* sideOf(IntVector* other) = 0;
  virtual Side* sideOf(RationalVector* other) = 0;

  /** Returns true, if the vector is horizontal or vertical. */
  virtual bool isOrthogonal() = 0;

  /** Returns true, if the vector is diagonal. */
  virtual bool isDiagonal() = 0;

  /** Returns true, if the vector is orthogonal or diagonal. */
  virtual bool isMultipleOf45Degree();

  /** Returns Signum.POSITIVE, if the scalar product of this vector and other > 0, Signum.NEGATIVE,
   * if it is < 0, and Signum.ZERO, if it is equal to 0. */
  virtual Signum* projection(Vector* other) = 0;
  virtual Signum* projection(IntVector* other) = 0;
  virtual Signum* projection(RationalVector* other) = 0;

  /** Returns an approximation of the scalar product of this vector with other by a double. */
  virtual double scalarProduct(Vector* other) = 0;
  virtual double scalarProduct(IntVector* other) = 0;
  virtual double scalarProduct(RationalVector* other) = 0;

  /** Approximates the coordinates of this vector by float coordinates. */
  virtual FloatPoint* toFloat() = 0;

  /** Turns this vector by factor times 90 degree. */
  virtual Vector* turn90Degree(int32_t factor) = 0;

  /** Mirrors this vector at the x axis. */
  virtual Vector* mirrorAtXAxis() = 0;

  /** Mirrors this vector at the y axis. */
  virtual Vector* mirrorAtYAxis() = 0;

  /** Returns an approximation of the Euclidean length of this vector. */
  virtual double lengthApprox();

  /** Returns an approximation of the cosinus of the angle between this vector and other. */
  virtual double cosAngle(Vector* other);

  /** Returns an approximation of the signed angle between this vector and other. */
  virtual double angleApprox(Vector* other);

  /** Returns an approximation of the signed angle between this vector and the x axis. */
  virtual double angleApprox();

  /** Returns an approximation vector of this vector with the same direction and length length. */
  virtual Vector* changeLengthApprox(double length) = 0;

  virtual Direction* toNormalizedDirection() = 0;

  virtual Point* addTo(IntPoint* point) = 0;
  virtual Point* addTo(RationalPoint* point) = 0;
};

}  // namespace fr
