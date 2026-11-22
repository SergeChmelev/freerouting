#include "io/specctra/SesNumbers.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "io/CoordinateTransform.h"

namespace fr::dsn {

SesNumbers_Interval* SesNumbers_Interval::intersect(SesNumbers_Interval* other) {
  jrt::BigFraction lower = low_.compareTo(other->low_) > 0 ? low_ : other->low_;
  jrt::BigFraction upper = high_.compareTo(other->high_) < 0 ? high_ : other->high_;
  if (lower.compareTo(upper) > 0) {
    throw jrt::IOException("SES numeric recovery intervals do not intersect");
  }
  return new (jrt::kOp) SesNumbers_Interval(lower, upper);
}

jrt::BigFraction SesNumbers::sessionScale(fr::BasicBoard* board, fr::Unit unit,
                                          int32_t resolution) {
  jrt::BigFraction base =
      board->communication->coordinateTransform == nullptr
          ? jrt::BigFraction::of(static_cast<int64_t>(board->communication->resolution))
          : fromDouble(board->communication->coordinateTransform->dsnToBoard(1.0));
  return base.multiply(micrometers(unit))
      .divide(micrometers(board->communication->unit))
      .divide(static_cast<int64_t>(resolution));
}

jrt::BigFraction SesNumbers::micrometers(fr::Unit unit) {
  switch (unit) {
    case fr::Unit::UM:
      return jrt::BigFraction::ONE();
    case fr::Unit::MM:
      return jrt::BigFraction::of(static_cast<int64_t>(1000));
    case fr::Unit::MIL:
      return jrt::BigFraction::of(static_cast<int64_t>(254), static_cast<int64_t>(10));
    case fr::Unit::INCH:
      return jrt::BigFraction::of(static_cast<int64_t>(25400));
  }
  throw jrt::IllegalArgumentException("unknown unit");
}

jrt::BigFraction SesNumbers::exact(const jrt::BigDecimal& value) {
  int32_t scale = value.scale();
  if (jrt::Math::abs(static_cast<int64_t>(scale)) > 4096 || value.precision() > 4096) {
    throw jrt::IOException("SES number exceeds the supported decimal precision");
  }
  return scale >= 0
             ? jrt::BigFraction::of(value.unscaledValue(), jrt::BigInteger::TEN().pow(scale))
             : jrt::BigFraction::of(
                   value.unscaledValue().multiply(jrt::BigInteger::TEN().pow(-scale)));
}

jrt::BigFraction SesNumbers::fromDouble(double value) {
  if (!jrt::Double::isFinite(value)) {
    throw jrt::IOException("Non-finite SES number");
  }
  return exact(jrt::BigDecimal::valueOf(value));
}

SesNumbers_Interval* SesNumbers::legacyInterval(double value) {
  if (!jrt::Double::isFinite(value)) {
    throw jrt::IOException("Non-finite SES coordinate");
  }
  jrt::BigFraction center = jrt::BigFraction::from(value);
  jrt::BigFraction tolerance = jrt::BigFraction::from(4 * jrt::Math::ulp(value));
  return new (jrt::kOp) SesNumbers_Interval(center.subtract(tolerance), center.add(tolerance));
}

jrt::BigFraction SesNumbers::recover(const jrt::BigDecimal& token, const jrt::BigFraction& scale) {
  jrt::BigFraction external = exact(token);
  jrt::BigFraction recovered = external;
  if (token.scale() > 0) {
    jrt::BigFraction halfPlace = exact(token.ulp()).divide(static_cast<int64_t>(2));
    recovered =
        simplest(new (jrt::kOp) SesNumbers_Interval(external.subtract(halfPlace), external.add(halfPlace)));
  }
  jrt::BigFraction value = recovered.multiply(scale);
  if (!legacyInterval(token.doubleValue() * scale.doubleValue())->contains(value)) {
    throw jrt::IOException("SES rational recovery exceeds the numeric fidelity envelope");
  }
  return value;
}

jrt::BigFraction SesNumbers::recoverInteger(const jrt::BigDecimal& token,
                                            const jrt::BigFraction& scale) {
  // Old writers did not certify their decimal rounding interval. Preserve only integer values
  // within the original floating-point envelope; never round a genuinely fractional position.
  if (token.scale() < 24) {
    double approximate = token.doubleValue() * scale.doubleValue();
    jrt::BigFraction integer = jrt::BigFraction::of(jrt::Math::round(approximate));
    if (legacyInterval(approximate)->contains(integer)) {
      return integer;
    }
  }
  jrt::BigFraction value = recover(token, scale);
  if (!value.getDenominator().equals(jrt::BigInteger::ONE())) {
    throw jrt::IOException("The SES value is not an integer board position");
  }
  return value;
}

jrt::BigFraction SesNumbers::simplest(SesNumbers_Interval* interval) {
  return simplest(interval->low_, interval->high_, 0);
}

jrt::BigFraction SesNumbers::simplest(const jrt::BigFraction& low, const jrt::BigFraction& high,
                                      int32_t depth) {
  if (depth > 512 || low.compareTo(high) > 0) {
    throw jrt::IOException("SES rational interval cannot be reconstructed");
  }
  if (low.equals(high)) {
    return low;
  }
  if (low.signum() <= 0) {
    if (high.signum() >= 0) {
      if (low.compareTo(jrt::BigFraction::of(static_cast<int64_t>(-1))) <= 0 ||
          high.compareTo(jrt::BigFraction::ONE()) >= 0) {
        throw jrt::IOException("Ambiguous SES integer recovery");
      }
      return jrt::BigFraction::ZERO();
    }
    return simplest(high.negate(), low.negate(), depth + 1).negate();
  }
  jrt::BigInteger integer = low.getNumerator().divide(low.getDenominator());
  jrt::BigFraction whole = jrt::BigFraction::of(integer);
  if (low.equals(whole)) {
    if (high.compareTo(whole.add(static_cast<int64_t>(1))) >= 0) {
      throw jrt::IOException("Ambiguous SES integer recovery");
    }
    return low;
  }
  jrt::BigFraction next = whole.add(static_cast<int64_t>(1));
  if (high.compareTo(next) >= 0) {
    if (high.compareTo(next.add(static_cast<int64_t>(1))) >= 0) {
      throw jrt::IOException("Ambiguous SES integer recovery");
    }
    return next;
  }
  return whole.add(simplest(high.subtract(whole).reciprocal(), low.subtract(whole).reciprocal(),
                            depth + 1)
                       .reciprocal());
}

std::string SesNumbers::format(const jrt::BigFraction& value) {
  if (value.getDenominator().equals(jrt::BigInteger::ONE())) {
    return value.getNumerator().toString();
  }
  // Fixed scale certifies a rounding interval narrow enough to identify the source denominator.
  // Retain trailing zeros even for terminating fractions; stripping them destroys that interval.
  int32_t scale = jrt::Math::max(
      24, 2 * static_cast<int32_t>(value.getDenominator().toString().length()) + 8);
  return jrt::BigDecimal(value.getNumerator())
      .divide(jrt::BigDecimal(value.getDenominator()), scale, jrt::RoundingMode::HALF_EVEN)
      .toPlainString();
}

}  // namespace fr::dsn
