#pragma once

#include "fwd.h"
#include "geometry/planar/Direction.h"
#include "jrt/jrt.h"

namespace fr {

/** Implements an abstract class Direction as an equivalence class of IntVector's. */
class IntDirection : public Direction {
 public:
  int32_t x;
  int32_t y;

  IntDirection(int32_t x, int32_t y) : x(x), y(y) {}
  explicit IntDirection(IntVector* vector);

  bool isOrthogonal() override;
  bool isDiagonal() override;
  Vector* getVector() override;

  int32_t compareTo(IntDirection* other) override;

  /** Implements the Comparable interface. Returns 1, if this direction has a strict bigger angle
   * with the positive x-axis than other, 0, if this direction is equal to other, and -1 otherwise.
   * Throws an IllegalArgumentException, if other is not a Direction. */
  int32_t compareTo(Direction* otherDirection) override;
  int32_t compareTo(BigIntDirection* other) override;

  Direction* opposite() override;
  Direction* turn45Degree(int32_t factor) override;

  double determinant(IntDirection* other);
};

}  // namespace fr
