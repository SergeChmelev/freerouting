#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Stores numerical limits and values used by planar geometry. */
class Limits final : public virtual jrt::Object {
 public:
  /**
   * An upper bound (2^25) so that the product of two integers with absolute value at most
   * CRIT_INT is contained in the mantissa of a double with some safety distance.
   */
  static constexpr int32_t CRIT_INT = 33554432;

  /**
   * The biggest double value (2^53), so that all integers smaller than this value are represented
   * exactly as double value.
   */
  static constexpr double CRIT_DOUBLE = 9007199254740992.0;

  /** BigInteger.valueOf(CRIT_INT). PORT NOTE: reference-typed static -> accessor. */
  static const jrt::BigInteger& CRIT_INT_BIG() {
    static const jrt::BigInteger v(static_cast<int64_t>(CRIT_INT));
    return v;
  }

  static inline const double sqrt2 = jrt::Math::sqrt(2);

 private:
  /** Prevents instantiation. */
  Limits() {}
};

}  // namespace fr
