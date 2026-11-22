#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Abstract class defining functionality of directions in the plane. A Direction is an equivalence
 * class of vectors. Two vectors define the same object of class Direction, if they point into the
 * same direction. We prefer using directions instead of angles, because with angles the
 * arithmetic calculations are in general not exact. Implements Comparable<Direction>.
 * PORT NOTE: static IntDirection constants -> accessors; Java `NULL` is renamed `NULL_` (macro).
 */
class Direction : public virtual jrt::Object {
 public:
  static IntDirection* NULL_();
  /** The direction to the east. */
  static IntDirection* RIGHT();
  /** The direction to the northeast. */
  static IntDirection* RIGHT45();
  /** The direction to the north. */
  static IntDirection* UP();
  /** The direction to the northwest. */
  static IntDirection* UP45();
  /** The direction to the west. */
  static IntDirection* LEFT();
  /** The direction to the southwest. */
  static IntDirection* LEFT45();
  /** The direction to the south. */
  static IntDirection* DOWN();
  /** The direction to the southeast. */
  static IntDirection* DOWN45();

  /** Creates a Direction from the input Vector. */
  static Direction* getInstance(Vector* vector);

  /** Calculates the direction from from to to. If from and to are equal, null is returned. */
  static Direction* getInstance(Point* from, Point* to);

  /** Creates a Direction whose angle with the x-axis is nearly equal to angle. */
  static Direction* getInstanceApprox(double angle);

  /** Returns any Vector pointing into this direction. */
  virtual Vector* getVector() = 0;

  /** Returns true, if the direction is horizontal or vertical. */
  virtual bool isOrthogonal() = 0;

  /** Returns true, if the direction is diagonal. */
  virtual bool isDiagonal() = 0;

  /** Returns true, if the direction is orthogonal or diagonal. */
  virtual bool isMultipleOf45Degree();

  /** Turns the direction by factor times 45 degree. */
  virtual Direction* turn45Degree(int32_t factor) = 0;

  /** Returns the opposite direction of this direction. */
  virtual Direction* opposite() = 0;

  /** Returns true, if other is a Direction and this Direction and other point into the same
   * direction. PORT NOTE: Java does not override hashCode here (identity hash is kept). */
  bool equals(jrt::Object* other) final;

  /** Let L be the line from the Zero Vector to other.getVector(). Returns Side.ON_THE_LEFT, if
   * this.getVector() is on the left of L, Side.ON_THE_RIGHT, if on the right, else COLLINEAR. */
  virtual Side* sideOf(Direction* other);

  /** Returns Signum.POSITIVE, if the scalar product of a vector representing this direction and a
   * vector representing other is > 0, Signum.NEGATIVE if < 0, and Signum.ZERO if 0. */
  virtual Signum* projection(Direction* other);

  /** Calculates an approximation of the direction in the middle of this direction and other. */
  virtual Direction* middleApprox(Direction* other);

  /** Returns 1, if the angle between p1 and this direction is bigger the angle between p2 and
   * this direction, 0, if p1 is equal to p2, and -1 otherwise. */
  virtual int32_t compareFrom(Direction* p1, Direction* p2);

  /** Returns an approximation of the signed angle corresponding to this direction. */
  virtual double angleApprox();

  /** Comparable<Direction>.compareTo: 1 if this direction has a strict bigger angle with the
   * positive x-axis than other, 0 if equal, -1 otherwise. */
  virtual int32_t compareTo(Direction* other) = 0;
  virtual int32_t compareTo(IntDirection* other) = 0;
  virtual int32_t compareTo(BigIntDirection* other) = 0;

  std::string toString() override;
};

}  // namespace fr
