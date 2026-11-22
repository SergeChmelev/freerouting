#include "jrt/bignum.h"

#include <cmath>
#include <vector>

#include "jrt/collections.h"

namespace jrt {

// ---------------------------------------------------------------------------------------------
// BigInteger
// ---------------------------------------------------------------------------------------------
BigInteger::Rep* BigInteger::newRep() {
  Rep* r = new Rep;  // collected; limbs are GC memory too (see gc.cpp), so never mpz_clear
  mpz_init(r->v);
  return r;
}

BigInteger::BigInteger(int64_t v) : rep_(newRep()) {
  static_assert(sizeof(long) == 8, "LP64 expected");
  mpz_set_si(rep_->v, static_cast<long>(v));
}

BigInteger::BigInteger(const std::string& s) : rep_(newRep()) {
  std::string t = s;
  if (!t.empty() && t[0] == '+') t.erase(0, 1);
  if (t.empty() || mpz_set_str(rep_->v, t.c_str(), 10) != 0) throw NumberFormatException("For input string: \"" + s + "\"");
}

BigInteger BigInteger::fromRaw(const mpz_t v) {
  Rep* r = newRep();
  mpz_set(r->v, v);
  return BigInteger(r);
}

const BigInteger& BigInteger::ZERO() {
  static const BigInteger* z = new BigInteger(int64_t{0});
  return *z;
}
const BigInteger& BigInteger::ONE() {
  static const BigInteger* z = new BigInteger(int64_t{1});
  return *z;
}
const BigInteger& BigInteger::TWO() {
  static const BigInteger* z = new BigInteger(int64_t{2});
  return *z;
}
const BigInteger& BigInteger::TEN() {
  static const BigInteger* z = new BigInteger(int64_t{10});
  return *z;
}

BigInteger BigInteger::add(const BigInteger& o) const {
  Rep* r = newRep();
  mpz_add(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::subtract(const BigInteger& o) const {
  Rep* r = newRep();
  mpz_sub(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::multiply(const BigInteger& o) const {
  Rep* r = newRep();
  mpz_mul(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::divide(const BigInteger& o) const {
  if (o.signum() == 0) throw ArithmeticException("BigInteger divide by zero");
  Rep* r = newRep();
  mpz_tdiv_q(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::remainder(const BigInteger& o) const {
  if (o.signum() == 0) throw ArithmeticException("BigInteger divide by zero");
  Rep* r = newRep();
  mpz_tdiv_r(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::mod(const BigInteger& m) const {
  if (m.signum() <= 0) throw ArithmeticException("BigInteger: modulus not positive");
  Rep* r = newRep();
  mpz_mod(r->v, rep_->v, m.rep_->v);
  return BigInteger(r);
}
Array<BigInteger>* BigInteger::divideAndRemainder(const BigInteger& o) const {
  auto* a = new Array<BigInteger>(2);
  (*a)[0] = divide(o);
  (*a)[1] = remainder(o);
  return a;
}
BigInteger BigInteger::negate() const {
  Rep* r = newRep();
  mpz_neg(r->v, rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::abs() const {
  if (signum() >= 0) return *this;
  return negate();
}
BigInteger BigInteger::gcd(const BigInteger& o) const {
  Rep* r = newRep();
  mpz_gcd(r->v, rep_->v, o.rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::pow(int32_t e) const {
  if (e < 0) throw ArithmeticException("Negative exponent");
  Rep* r = newRep();
  mpz_pow_ui(r->v, rep_->v, static_cast<unsigned long>(e));
  return BigInteger(r);
}
BigInteger BigInteger::sqrt() const {
  if (signum() < 0) throw ArithmeticException("Negative BigInteger");
  Rep* r = newRep();
  mpz_sqrt(r->v, rep_->v);
  return BigInteger(r);
}
BigInteger BigInteger::shiftLeft(int32_t n) const {
  if (n < 0) return shiftRight(-n);
  Rep* r = newRep();
  mpz_mul_2exp(r->v, rep_->v, static_cast<mp_bitcnt_t>(n));
  return BigInteger(r);
}
BigInteger BigInteger::shiftRight(int32_t n) const {
  if (n < 0) return shiftLeft(-n);
  Rep* r = newRep();
  mpz_fdiv_q_2exp(r->v, rep_->v, static_cast<mp_bitcnt_t>(n));
  return BigInteger(r);
}
int32_t BigInteger::compareTo(const BigInteger& o) const {
  int c = mpz_cmp(rep_->v, o.rep_->v);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}
int32_t BigInteger::hashCode() const {
  // Java: hash over the big-endian int[] magnitude, times signum.
  int sgn = signum();
  if (sgn == 0) return 0;
  std::size_t count = 0;
  std::vector<uint32_t> words((mpz_sizeinbase(rep_->v, 2) + 31) / 32 + 1);
  mpz_export(words.data(), &count, 1, sizeof(uint32_t), 0, 0, rep_->v);
  uint32_t h = 0;
  for (std::size_t i = 0; i < count; ++i) h = 31 * h + words[i];
  return static_cast<int32_t>(h * static_cast<uint32_t>(sgn));
}
int32_t BigInteger::bitLength() const {
  if (signum() == 0) return 0;
  if (signum() > 0) return static_cast<int32_t>(mpz_sizeinbase(rep_->v, 2));
  // negative: bitLength of (-x - 1)
  mpz_t t;
  mpz_init(t);
  mpz_neg(t, rep_->v);
  mpz_sub_ui(t, t, 1);
  int32_t r = mpz_sgn(t) == 0 ? 0 : static_cast<int32_t>(mpz_sizeinbase(t, 2));
  return r;
}
double BigInteger::doubleValue() const {
  int sgn = signum();
  if (sgn == 0) return 0.0;
  mpz_t a;
  mpz_init(a);
  mpz_abs(a, rep_->v);
  std::size_t bits = mpz_sizeinbase(a, 2);
  double r;
  if (bits <= 63) {
    r = static_cast<double>(static_cast<int64_t>(mpz_get_ui(a)));
  } else {
    std::size_t shift = bits - 63;
    // sticky bit: any of the shifted-out bits set
    bool sticky = mpz_scan1(a, 0) < shift;
    mpz_t top;
    mpz_init(top);
    mpz_fdiv_q_2exp(top, a, shift);
    uint64_t t = mpz_get_ui(top);
    if (sticky) t |= 1;
    r = std::ldexp(static_cast<double>(static_cast<int64_t>(t)), static_cast<int>(shift));
  }
  return sgn < 0 ? -r : r;
}
int64_t BigInteger::longValue() const {
  // low 64 bits of the two's complement representation
  mpz_t t;
  mpz_init(t);
  mpz_fdiv_r_2exp(t, rep_->v, 64);  // t in [0, 2^64)
  uint64_t u = mpz_get_ui(t);
  return static_cast<int64_t>(u);
}
int32_t BigInteger::intValue() const { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(longValue()))); }
int32_t BigInteger::intValueExact() const {
  if (!mpz_fits_sint_p(rep_->v)) throw ArithmeticException("BigInteger out of int range");
  return static_cast<int32_t>(mpz_get_si(rep_->v));
}
int64_t BigInteger::longValueExact() const {
  if (!mpz_fits_slong_p(rep_->v)) throw ArithmeticException("BigInteger out of long range");
  return static_cast<int64_t>(mpz_get_si(rep_->v));
}
std::string BigInteger::toString() const {
  std::string s(mpz_sizeinbase(rep_->v, 10) + 2, '\0');
  mpz_get_str(s.data(), 10, rep_->v);
  s.resize(std::strlen(s.c_str()));
  return s;
}

// ---------------------------------------------------------------------------------------------
// BigDecimal
// ---------------------------------------------------------------------------------------------
BigDecimal::BigDecimal(const std::string& s0) : scale_(0) {
  std::string s = s0;
  if (s.empty()) throw NumberFormatException("empty BigDecimal");
  int32_t exp = 0;
  auto epos = s.find_first_of("eE");
  if (epos != std::string::npos) {
    exp = Integer::parseInt(s.substr(epos + 1));
    s = s.substr(0, epos);
  }
  bool neg = false;
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
    neg = s[0] == '-';
    s.erase(0, 1);
  }
  auto dot = s.find('.');
  std::string digits;
  int32_t scale = 0;
  if (dot == std::string::npos) {
    digits = s;
  } else {
    digits = s.substr(0, dot) + s.substr(dot + 1);
    scale = static_cast<int32_t>(s.size() - dot - 1);
  }
  if (digits.empty()) throw NumberFormatException("For input string: \"" + s0 + "\"");
  for (char c : digits)
    if (c < '0' || c > '9') throw NumberFormatException("For input string: \"" + s0 + "\"");
  unscaled_ = BigInteger(digits);
  if (neg) unscaled_ = unscaled_.negate();
  scale_ = scale - exp;
}

double BigDecimal::doubleValue() const { return Double::parseDouble(toPlainString()); }

int32_t BigDecimal::compareTo(const BigDecimal& o) const {
  BigInteger a = unscaled_, b = o.unscaled_;
  if (scale_ < o.scale_) a = a.multiply(BigInteger::TEN().pow(o.scale_ - scale_));
  else if (o.scale_ < scale_) b = b.multiply(BigInteger::TEN().pow(scale_ - o.scale_));
  return a.compareTo(b);
}

std::string BigDecimal::toPlainString() const {
  std::string digits = unscaled_.abs().toString();
  std::string sign = unscaled_.signum() < 0 ? "-" : "";
  if (scale_ <= 0) return sign + digits + std::string(-scale_, '0');
  if (static_cast<int32_t>(digits.size()) <= scale_) digits = std::string(scale_ - digits.size() + 1, '0') + digits;
  return sign + digits.substr(0, digits.size() - scale_) + "." + digits.substr(digits.size() - scale_);
}

// ---------------------------------------------------------------------------------------------
// BigFraction
// ---------------------------------------------------------------------------------------------
BigFraction BigFraction::of(const BigInteger& num, const BigInteger& den) {
  if (den.signum() == 0) throw ArithmeticException("BigFraction: zero denominator");
  BigInteger n = num, d = den;
  if (d.signum() < 0) {
    n = n.negate();
    d = d.negate();
  }
  BigInteger g = n.gcd(d);
  if (g.signum() != 0 && !g.equals(BigInteger::ONE())) {
    n = n.divide(g);
    d = d.divide(g);
  }
  if (n.signum() == 0) d = BigInteger::ONE();
  return BigFraction(n, d);
}

BigFraction BigFraction::from(double value) {
  if (!std::isfinite(value)) throw IllegalArgumentException("BigFraction.from: non-finite value");
  if (value == 0) return ZERO();
  int exp = 0;
  double m = std::frexp(value, &exp);  // value = m * 2^exp, 0.5 <= |m| < 1
  int64_t mant = static_cast<int64_t>(std::ldexp(m, 53));
  exp -= 53;
  BigInteger n(mant);
  if (exp >= 0) return of(n.shiftLeft(exp), BigInteger::ONE());
  return of(n, BigInteger::ONE().shiftLeft(-exp));
}

const BigFraction& BigFraction::ZERO() {
  static const BigFraction* z = new BigFraction(BigInteger::ZERO(), BigInteger::ONE());
  return *z;
}
const BigFraction& BigFraction::ONE() {
  static const BigFraction* z = new BigFraction(BigInteger::ONE(), BigInteger::ONE());
  return *z;
}

BigFraction BigFraction::add(const BigFraction& o) const {
  return of(num_.multiply(o.den_).add(o.num_.multiply(den_)), den_.multiply(o.den_));
}
BigFraction BigFraction::subtract(const BigFraction& o) const {
  return of(num_.multiply(o.den_).subtract(o.num_.multiply(den_)), den_.multiply(o.den_));
}
BigFraction BigFraction::multiply(const BigFraction& o) const {
  return of(num_.multiply(o.num_), den_.multiply(o.den_));
}
BigFraction BigFraction::divide(const BigFraction& o) const {
  if (o.num_.signum() == 0) throw ArithmeticException("BigFraction: division by zero");
  return of(num_.multiply(o.den_), den_.multiply(o.num_));
}
BigFraction BigFraction::reciprocal() const { return of(den_, num_); }
int32_t BigFraction::compareTo(const BigFraction& o) const {
  return num_.multiply(o.den_).compareTo(o.num_.multiply(den_));
}
double BigFraction::doubleValue() const {
  // Correctly rounded num/den: scale to ~64 significant bits, fold the sticky bit in, and let
  // BigInteger::doubleValue (correctly rounded) do the final rounding.
  int sgn = num_.signum();
  if (sgn == 0) return 0.0;
  BigInteger n = num_.abs();
  int32_t shift = 64 - (n.bitLength() - den_.bitLength());
  BigInteger scaled = shift >= 0 ? n.shiftLeft(shift) : n;
  BigInteger d = shift >= 0 ? den_ : den_.shiftLeft(-shift);
  BigInteger qt = scaled.divide(d);
  bool sticky = scaled.remainder(d).signum() != 0;
  // qt has ~64-65 bits; fold sticky into lowest bit, then let doubleValue round.
  BigInteger folded = sticky ? qt.shiftLeft(1).add(BigInteger::ONE()) : qt.shiftLeft(1);
  double r = std::ldexp(folded.doubleValue(), -shift - 1);
  return sgn < 0 ? -r : r;
}
std::string BigFraction::toString() const {
  if (den_.equals(BigInteger::ONE())) return num_.toString();
  return num_.toString() + " / " + den_.toString();
}

}  // namespace jrt
