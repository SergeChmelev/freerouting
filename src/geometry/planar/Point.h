#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Abstract class describing functionality for Points in the plane (sealed: IntPoint,
 * RationalPoint). PORT NOTE: package-private (double dispatch) methods are public in C++.
 */
class Point : public virtual jrt::Object {
 public:
  /** Immutable exact coordinates for interchange; affine coordinates are x/z and y/z. */
  BigIntAux_RationalCoordinates* exactCoordinates();

  /** Returns the identical integer coordinate, or rejects a fractional/infinite/out-of-range
   * point (IllegalArgumentException). */
  IntPoint* toIntegerExact();

  /** Standard implementation of the zero point. PORT NOTE: static field -> accessor. */
  static IntPoint* ZERO();

  /** Creates an IntPoint from x and y. If x or y is too big for an IntPoint, a RationalPoint is
   * created. */
  static Point* getInstance(int32_t x, int32_t y);

  /** Factory method for creating a Point from 3 BigIntegers. */
  static Point* getInstance(const jrt::BigInteger& x, const jrt::BigInteger& y,
                            const jrt::BigInteger& z);

  /** Returns the translation of this point by vector. */
  virtual Point* translateBy(Vector* vector) = 0;
  virtual Point* translateBy(IntVector* vector) = 0;
  virtual Point* translateBy(RationalVector* vector) = 0;

  /** Returns the difference vector of this point and other. */
  virtual Vector* differenceBy(Point* other) = 0;
  virtual Vector* differenceBy(IntPoint* other) = 0;
  virtual Vector* differenceBy(RationalPoint* other) = 0;

  /** Approximates the coordinates of this point by float coordinates. */
  virtual FloatPoint* toFloat() = 0;

  /** Returns a unique ID for this point for deterministic tie-breaking. */
  virtual int32_t getId() = 0;

  /** Returns true, if this Point is a RationalPoint with denominator z = 0. */
  virtual bool isInfinite() = 0;

  /** Creates the smallest Box with integer coordinates containing this point. */
  virtual IntBox* surroundingBox() = 0;

  /** Creates the smallest Octagon with integer coordinates containing this point. */
  virtual IntOctagon* surroundingOctagon() = 0;

  /** Returns true, if this point lies in the interior or on the border of box. */
  virtual bool isContainedIn(IntBox* box) = 0;

  /** Returns the side of a line on which this point lies. */
  virtual Side* sideOf(Line* line) = 0;

  /** Returns Side.ON_THE_LEFT, if this Point is on the left of the line from p1 to p2;
   * Side.ON_THE_RIGHT, if on the right; Side.COLLINEAR otherwise. */
  virtual Side* sideOf(Point* p1, Point* p2);

  /** Returns the nearest point to this point on line. */
  virtual Point* perpendicularProjection(Line* line) = 0;

  /** Calculates the perpendicular direction from this point to line. Returns Direction.NULL, if
   * this point lies on line. */
  virtual Direction* perpendicularDirection(Line* line);

  /** Returns 1, if this Point has a strict bigger x coordinate than other, 0, if the x
   * coordinates are equal, and -1 otherwise. */
  virtual int32_t compareX(Point* other) = 0;
  virtual int32_t compareX(IntPoint* other) = 0;
  virtual int32_t compareX(RationalPoint* other) = 0;

  /** Returns 1, if this Point has a strict bigger y coordinate than other, 0, if the y
   * coordinates are equal, and -1 otherwise. */
  virtual int32_t compareY(Point* other) = 0;
  virtual int32_t compareY(IntPoint* other) = 0;
  virtual int32_t compareY(RationalPoint* other) = 0;

  /** Returns compareX(other), if the result is not 0. Otherwise, it returns compareY(other). */
  virtual int32_t compareXY(Point* other);

  /** Turns this point by factor times 90 degree around pole. */
  virtual Point* turn90Degree(int32_t factor, Point* pole);

  /** Mirrors this point at the vertical line through pole. */
  virtual Point* mirrorVertical(Point* pole);

  /** Mirrors this point at the horizontal line through pole. */
  virtual Point* mirrorHorizontal(Point* pole);
};

}  // namespace fr
