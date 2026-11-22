#pragma once

#include "fwd.h"
#include "geometry/planar/Direction.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements the abstract class Direction as a tuple of infinite precision integers.
 */
class BigIntDirection : public Direction {
 public:
  // PORT NOTE: package-private fields -> public.
  jrt::BigInteger x;
  jrt::BigInteger y;

  BigIntDirection(const jrt::BigInteger& x, const jrt::BigInteger& y) : x(x), y(y) {}

  /** Creates a BigIntDirection from an IntDirection. */
  explicit BigIntDirection(IntDirection* dir);

  bool isOrthogonal() override;
  bool isDiagonal() override;
  Vector* getVector() override;
  Direction* turn45Degree(int32_t factor) override;
  Direction* opposite() override;

  /** Implements the Comparable interface. Returns 1, if this direction has a strict bigger angle
   * with the positive x-axis than other, 0, if this direction is equal to other, and -1 otherwise.
   * Throws an IllegalArgumentException, if other is not a Direction. */
  int32_t compareTo(Direction* otherDirection) override;
  int32_t compareTo(IntDirection* otherDirection) override;
  int32_t compareTo(BigIntDirection* other) override;
};

}  // namespace fr
