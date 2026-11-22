// java.math.BigInteger / BigDecimal and commons-numbers BigFraction on top of GMP.
// All three are immutable value types (cheap to copy: they share an immutable GC'd rep).
#pragma once

#include <gmp.h>

#include <cstdint>
#include <string>

#include "jrt/lang.h"

namespace jrt {

template <class T>
class Array;

class BigInteger {
 public:
  BigInteger() : BigInteger(int64_t{0}) {}
  BigInteger(int64_t v);  // NOLINT(google-explicit-constructor): mirrors BigInteger.valueOf
  BigInteger(int32_t v) : BigInteger(static_cast<int64_t>(v)) {}  // NOLINT
  explicit BigInteger(const std::string& decimal);
  static BigInteger valueOf(int64_t v) { return BigInteger(v); }
  static const BigInteger& ZERO();
  static const BigInteger& ONE();
  static const BigInteger& TWO();
  static const BigInteger& TEN();

  BigInteger add(const BigInteger& o) const;
  BigInteger subtract(const BigInteger& o) const;
  BigInteger multiply(const BigInteger& o) const;
  BigInteger divide(const BigInteger& o) const;     // truncates toward zero
  BigInteger remainder(const BigInteger& o) const;  // sign follows dividend
  BigInteger mod(const BigInteger& m) const;        // result in [0, m)
  Array<BigInteger>* divideAndRemainder(const BigInteger& o) const;
  BigInteger negate() const;
  BigInteger abs() const;
  BigInteger gcd(const BigInteger& o) const;
  BigInteger pow(int32_t e) const;
  BigInteger sqrt() const;  // floor(sqrt(this)), throws for negative
  BigInteger shiftLeft(int32_t n) const;
  BigInteger shiftRight(int32_t n) const;  // floor
  BigInteger min(const BigInteger& o) const { return compareTo(o) <= 0 ? *this : o; }
  BigInteger max(const BigInteger& o) const { return compareTo(o) >= 0 ? *this : o; }
  int32_t signum() const { return mpz_sgn(rep_->v); }
  int32_t compareTo(const BigInteger& o) const;
  bool equals(const BigInteger& o) const { return compareTo(o) == 0; }
  int32_t hashCode() const;
  int32_t bitLength() const;  // Java semantics (two's complement, excluding sign bit)
  double doubleValue() const;  // correctly rounded like Java
  int32_t intValue() const;    // low 32 bits (two's complement)
  int64_t longValue() const;   // low 64 bits (two's complement)
  int32_t intValueExact() const;
  int64_t longValueExact() const;
  bool fitsLong() const { return mpz_fits_slong_p(rep_->v) != 0; }
  std::string toString() const;

  bool operator==(const BigInteger& o) const { return compareTo(o) == 0; }
  bool operator!=(const BigInteger& o) const { return compareTo(o) != 0; }
  bool operator<(const BigInteger& o) const { return compareTo(o) < 0; }

  const __mpz_struct* raw() const { return rep_->v; }
  static BigInteger fromRaw(const mpz_t v);

 private:
  struct Rep {
    mpz_t v;
  };
  explicit BigInteger(Rep* r) : rep_(r) {}
  static Rep* newRep();
  Rep* rep_;
};

inline std::string str(const BigInteger& b) { return b.toString(); }

// Minimal java.math.BigDecimal: unscaled * 10^-scale.
class BigDecimal {
 public:
  BigDecimal() : unscaled_(int64_t{0}), scale_(0) {}
  BigDecimal(BigInteger unscaled, int32_t scale) : unscaled_(std::move(unscaled)), scale_(scale) {}
  explicit BigDecimal(const std::string& s);  // parses like new BigDecimal(String)
  static BigDecimal valueOf(int64_t v) { return BigDecimal(BigInteger(v), 0); }
  const BigInteger& unscaledValue() const { return unscaled_; }
  int32_t scale() const { return scale_; }
  int32_t signum() const { return unscaled_.signum(); }
  BigDecimal ulp() const { return BigDecimal(BigInteger::ONE(), scale_); }
  BigDecimal negate() const { return BigDecimal(unscaled_.negate(), scale_); }
  BigDecimal abs() const { return BigDecimal(unscaled_.abs(), scale_); }
  double doubleValue() const;
  int32_t compareTo(const BigDecimal& o) const;
  std::string toPlainString() const;
  std::string toString() const { return toPlainString(); }

 private:
  BigInteger unscaled_;
  int32_t scale_;
};

// org.apache.commons.numbers.fraction.BigFraction (always reduced, denominator > 0).
class BigFraction {
 public:
  BigFraction() : BigFraction(BigInteger::ZERO(), BigInteger::ONE()) {}
  static BigFraction of(const BigInteger& num) { return BigFraction(num, BigInteger::ONE()); }
  static BigFraction of(int64_t num) { return of(BigInteger(num)); }
  static BigFraction of(const BigInteger& num, const BigInteger& den);
  static BigFraction of(int64_t num, int64_t den) { return of(BigInteger(num), BigInteger(den)); }
  static BigFraction from(double value);  // exact binary value of the double
  static const BigFraction& ZERO();
  static const BigFraction& ONE();

  const BigInteger& getNumerator() const { return num_; }
  const BigInteger& getDenominator() const { return den_; }
  int32_t signum() const { return num_.signum(); }
  BigFraction add(const BigFraction& o) const;
  BigFraction add(int64_t v) const { return add(of(v)); }
  BigFraction subtract(const BigFraction& o) const;
  BigFraction subtract(int64_t v) const { return subtract(of(v)); }
  BigFraction multiply(const BigFraction& o) const;
  BigFraction multiply(int64_t v) const { return multiply(of(v)); }
  BigFraction divide(const BigFraction& o) const;
  BigFraction divide(int64_t v) const { return divide(of(v)); }
  BigFraction negate() const { return BigFraction(num_.negate(), den_); }
  BigFraction abs() const { return BigFraction(num_.abs(), den_); }
  BigFraction reciprocal() const;
  int32_t compareTo(const BigFraction& o) const;
  bool equals(const BigFraction& o) const { return num_.equals(o.num_) && den_.equals(o.den_); }
  int32_t hashCode() const { return hashCombine(num_.hashCode(), den_.hashCode()); }
  double doubleValue() const;
  BigInteger bigIntegerValue() const { return num_.divide(den_); }  // truncation
  int64_t longValue() const { return bigIntegerValue().longValue(); }
  int32_t intValue() const { return bigIntegerValue().intValue(); }
  std::string toString() const;

 private:
  BigFraction(BigInteger n, BigInteger d) : num_(std::move(n)), den_(std::move(d)) {}
  BigInteger num_, den_;
};

}  // namespace jrt
