#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Closed rational interval [low, high] (Java record SesNumbers.Interval). */
class SesNumbers_Interval : public virtual jrt::Object {
 public:
  SesNumbers_Interval(jrt::BigFraction low, jrt::BigFraction high)
      : low_(std::move(low)), high_(std::move(high)) {}

  jrt::BigFraction low() { return low_; }
  jrt::BigFraction high() { return high_; }

  bool contains(const jrt::BigFraction& value) {
    return value.compareTo(low_) >= 0 && value.compareTo(high_) <= 0;
  }

  /** Throws jrt::IOException if the intervals do not intersect. */
  SesNumbers_Interval* intersect(SesNumbers_Interval* other);

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = dynamic_cast<SesNumbers_Interval*>(o);
    return other != nullptr && low_.equals(other->low_) && high_.equals(other->high_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, low_.hashCode());
    h = jrt::hashCombine(h, high_.hashCode());
    return h;
  }
  std::string toString() override {
    return "Interval[low=" + low_.toString() + ", high=" + high_.toString() + "]";
  }

 private:
  friend class SesNumbers;
  jrt::BigFraction low_;
  jrt::BigFraction high_;
};

/**
 * Exact decimal interchange and bounded rational recovery; never an electrical contact tolerance.
 * All "throws IOException" methods throw jrt::IOException.
 */
class SesNumbers final : public virtual jrt::Object {
 public:
  static jrt::BigFraction sessionScale(fr::BasicBoard* board, fr::Unit unit, int32_t resolution);

  /**
   * PORT NOTE: needs BigDecimal.precision() (number of decimal digits of the unscaled value),
   * which jrt::BigDecimal does not provide yet (compute from unscaledValue().abs().toString()).
   */
  static jrt::BigFraction exact(const jrt::BigDecimal& value);

  /** PORT NOTE: BigDecimal.valueOf(double) == new BigDecimal(Double.toString(value)). */
  static jrt::BigFraction fromDouble(double value);

  static SesNumbers_Interval* legacyInterval(double value);

  static jrt::BigFraction recover(const jrt::BigDecimal& token, const jrt::BigFraction& scale);

  static jrt::BigFraction recoverInteger(const jrt::BigDecimal& token,
                                         const jrt::BigFraction& scale);

  static jrt::BigFraction simplest(SesNumbers_Interval* interval);

  /**
   * PORT NOTE: Java uses new BigDecimal(num).divide(new BigDecimal(den), scale,
   * RoundingMode.HALF_EVEN).toPlainString(); jrt::BigDecimal has no divide(scale, rounding) yet, so
   * phase 2 must implement the HALF_EVEN rounded division on BigInteger (num * 10^scale / den).
   */
  static std::string format(const jrt::BigFraction& value);

 private:
  SesNumbers() = default;

  static jrt::BigFraction micrometers(fr::Unit unit);

  static jrt::BigFraction simplest(const jrt::BigFraction& low, const jrt::BigFraction& high,
                                   int32_t depth);
};

}  // namespace fr::dsn
