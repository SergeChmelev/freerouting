#include "jrt/bignum.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "jrt/collections.h"

namespace jrt {

// ---------------------------------------------------------------------------------------------
// BigInteger
//
// PORT NOTE (MEMORY_MODEL.md stage D): values that fit in int64 are stored inline (rep_ ==
// nullptr, no allocation); GMP is used only for larger values and for rare operations (pow,
// sqrt, modInverse, parsing). Every result is normalised: it is inline whenever it fits, so
// the representation of a value is unique.
// ---------------------------------------------------------------------------------------------
namespace {

// Temporary mpz holding a BigInteger's value (rep_ shared or a copy of the inline value).
struct MpzArg {
  explicit MpzArg(const BigInteger& b) {
    if (b.isSmall()) {
      mpz_init_set_si(own, static_cast<long>(b.smallValue()));
      p = own;
      owned = true;
    } else {
      p = b.raw();
    }
  }
  ~MpzArg() {
    if (owned) mpz_clear(own);
  }
  MpzArg(const MpzArg&) = delete;
  MpzArg& operator=(const MpzArg&) = delete;
  mpz_t own;
  const __mpz_struct* p;
  bool owned = false;
};

}  // namespace

BigInteger::Rep* BigInteger::newRep() {
  // Never mpz_clear'ed: a rep is shared by every copy of the value and has no owner. Collected
  // in the collector's build (limbs are GC memory, see gc.cpp); without it the rep and its limbs
  // belong to the job (job heap, never an operation region: copies of the value outlive it).
  void* mem = job_region() != nullptr ? job_heap_alloc(sizeof(Rep)) : ::operator new(sizeof(Rep));
  Rep* r = new (mem) Rep;
  mpz_init(r->v);
  return r;
}

BigInteger BigInteger::fromRep(Rep* r) {
  if (mpz_fits_slong_p(r->v)) return BigInteger(static_cast<int64_t>(mpz_get_si(r->v)));
  return BigInteger(r);
}

BigInteger::BigInteger(const std::string& s) {
  std::string t = s;
  if (!t.empty() && t[0] == '+') t.erase(0, 1);
  Rep* r = newRep();
  if (t.empty() || mpz_set_str(r->v, t.c_str(), 10) != 0) throw NumberFormatException("For input string: \"" + s + "\"");
  *this = fromRep(r);
}

BigInteger BigInteger::fromRaw(const mpz_t v) {
  Rep* r = newRep();
  mpz_set(r->v, v);
  return fromRep(r);
}

const __mpz_struct* BigInteger::raw() const {
  if (rep_ == nullptr) {
    // GMP form of an inline value (not cached: rep_ == nullptr marks the inline form).
    Rep* r = newRep();
    static_assert(sizeof(long) == 8, "LP64 expected");
    mpz_set_si(r->v, static_cast<long>(small_));
    return r->v;
  }
  return rep_->v;
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
  int64_t r;
  if (isSmall() && o.isSmall() && !__builtin_add_overflow(small_, o.small_, &r)) return BigInteger(r);
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_add(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::subtract(const BigInteger& o) const {
  int64_t r;
  if (isSmall() && o.isSmall() && !__builtin_sub_overflow(small_, o.small_, &r)) return BigInteger(r);
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_sub(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::multiply(const BigInteger& o) const {
  int64_t r;
  if (isSmall() && o.isSmall() && !__builtin_mul_overflow(small_, o.small_, &r)) return BigInteger(r);
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_mul(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::divide(const BigInteger& o) const {
  if (o.signum() == 0) throw ArithmeticException("BigInteger divide by zero");
  if (isSmall() && o.isSmall() && !(small_ == INT64_MIN && o.small_ == -1)) {
    return BigInteger(small_ / o.small_);  // C++ truncates toward zero like mpz_tdiv_q
  }
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_tdiv_q(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::remainder(const BigInteger& o) const {
  if (o.signum() == 0) throw ArithmeticException("BigInteger divide by zero");
  if (isSmall() && o.isSmall()) {
    if (o.small_ == -1) return BigInteger(int64_t{0});
    return BigInteger(small_ % o.small_);  // sign of the dividend like mpz_tdiv_r
  }
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_tdiv_r(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::mod(const BigInteger& m) const {
  if (m.signum() <= 0) throw ArithmeticException("BigInteger: modulus not positive");
  if (isSmall() && m.isSmall()) {
    int64_t r = small_ % m.small_;  // m > 0
    if (r < 0) r += m.small_;
    return BigInteger(r);
  }
  MpzArg a(*this), b(m);
  Rep* rr = newRep();
  mpz_mod(rr->v, a.p, b.p);
  return fromRep(rr);
}
Array<BigInteger>* BigInteger::divideAndRemainder(const BigInteger& o) const {
  auto* a = new Array<BigInteger>(2);
  (*a)[0] = divide(o);
  (*a)[1] = remainder(o);
  return a;
}
BigInteger BigInteger::negate() const {
  if (isSmall() && small_ != INT64_MIN) return BigInteger(-small_);
  MpzArg a(*this);
  Rep* rr = newRep();
  mpz_neg(rr->v, a.p);
  return fromRep(rr);
}
BigInteger BigInteger::abs() const {
  if (signum() >= 0) return *this;
  return negate();
}
BigInteger BigInteger::gcd(const BigInteger& o) const {
  if (isSmall() && o.isSmall() && small_ != INT64_MIN && o.small_ != INT64_MIN) {
    uint64_t a = static_cast<uint64_t>(small_ < 0 ? -small_ : small_);
    uint64_t b = static_cast<uint64_t>(o.small_ < 0 ? -o.small_ : o.small_);
    while (b != 0) {
      uint64_t t = a % b;
      a = b;
      b = t;
    }
    return BigInteger(static_cast<int64_t>(a));
  }
  MpzArg a(*this), b(o);
  Rep* rr = newRep();
  mpz_gcd(rr->v, a.p, b.p);
  return fromRep(rr);
}
BigInteger BigInteger::pow(int32_t e) const {
  if (e < 0) throw ArithmeticException("Negative exponent");
  MpzArg a(*this);
  Rep* rr = newRep();
  mpz_pow_ui(rr->v, a.p, static_cast<unsigned long>(e));
  return fromRep(rr);
}
BigInteger BigInteger::modInverse(const BigInteger& m) const {
  if (m.signum() <= 0) throw ArithmeticException("BigInteger: modulus not positive");
  MpzArg a(*this), b(m);
  Rep* rr = newRep();
  if (mpz_invert(rr->v, a.p, b.p) == 0) throw ArithmeticException("BigInteger not invertible.");
  if (mpz_sgn(rr->v) < 0) mpz_add(rr->v, rr->v, b.p);  // Java returns a value in [0, m)
  return fromRep(rr);
}
BigInteger BigInteger::sqrt() const {
  if (signum() < 0) throw ArithmeticException("Negative BigInteger");
  MpzArg a(*this);
  Rep* rr = newRep();
  mpz_sqrt(rr->v, a.p);
  return fromRep(rr);
}
BigInteger BigInteger::shiftLeft(int32_t n) const {
  if (n < 0) return shiftRight(-n);
  if (isSmall()) {
    if (small_ == 0) return *this;
    int64_t r;
    if (n < 63 && !__builtin_mul_overflow(small_, int64_t{1} << n, &r)) return BigInteger(r);
  }
  MpzArg a(*this);
  Rep* rr = newRep();
  mpz_mul_2exp(rr->v, a.p, static_cast<mp_bitcnt_t>(n));
  return fromRep(rr);
}
BigInteger BigInteger::shiftRight(int32_t n) const {
  if (n < 0) return shiftLeft(-n);
  if (isSmall()) {
    // floor division by 2^n (arithmetic shift)
    if (n >= 63) return BigInteger(int64_t{small_ < 0 ? -1 : 0});
    return BigInteger(small_ >> n);
  }
  MpzArg a(*this);
  Rep* rr = newRep();
  mpz_fdiv_q_2exp(rr->v, a.p, static_cast<mp_bitcnt_t>(n));
  return fromRep(rr);
}
int32_t BigInteger::compareTo(const BigInteger& o) const {
  if (isSmall() && o.isSmall()) return small_ < o.small_ ? -1 : (small_ > o.small_ ? 1 : 0);
  MpzArg a(*this), b(o);
  int c = mpz_cmp(a.p, b.p);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}
int32_t BigInteger::hashCode() const {
  // Java: hash over the big-endian int[] magnitude, times signum.
  int sgn = signum();
  if (sgn == 0) return 0;
  if (isSmall()) {
    uint64_t m = small_ < 0 ? 0 - static_cast<uint64_t>(small_) : static_cast<uint64_t>(small_);
    uint32_t hi = static_cast<uint32_t>(m >> 32);
    uint32_t lo = static_cast<uint32_t>(m);
    uint32_t h = hi != 0 ? 31 * hi + lo : lo;
    return static_cast<int32_t>(h * static_cast<uint32_t>(sgn));
  }
  std::size_t count = 0;
  std::vector<uint32_t> words((mpz_sizeinbase(rep_->v, 2) + 31) / 32 + 1);
  mpz_export(words.data(), &count, 1, sizeof(uint32_t), 0, 0, rep_->v);
  uint32_t h = 0;
  for (std::size_t i = 0; i < count; ++i) h = 31 * h + words[i];
  return static_cast<int32_t>(h * static_cast<uint32_t>(sgn));
}
int32_t BigInteger::bitLength() const {
  if (isSmall()) {
    uint64_t m = static_cast<uint64_t>(small_ < 0 ? ~small_ : small_);  // negative: -x - 1
    return m == 0 ? 0 : 64 - __builtin_clzll(m);
  }
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
  // int64 -> double conversion rounds to nearest even, like Java (long) -> double and like the
  // correctly rounded GMP path below.
  if (isSmall()) return static_cast<double>(small_);
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
  if (isSmall()) return small_;
  // low 64 bits of the two's complement representation
  mpz_t t;
  mpz_init(t);
  mpz_fdiv_r_2exp(t, rep_->v, 64);  // t in [0, 2^64)
  uint64_t u = mpz_get_ui(t);
  return static_cast<int64_t>(u);
}
int32_t BigInteger::intValue() const { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(longValue()))); }
int32_t BigInteger::intValueExact() const {
  if (isSmall()) {
    if (small_ < INT32_MIN || small_ > INT32_MAX) throw ArithmeticException("BigInteger out of int range");
    return static_cast<int32_t>(small_);
  }
  throw ArithmeticException("BigInteger out of int range");
}
int64_t BigInteger::longValueExact() const {
  if (isSmall()) return small_;
  throw ArithmeticException("BigInteger out of long range");
}
std::string BigInteger::toString() const {
  if (isSmall()) return std::to_string(small_);
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

BigInteger roundedDivide(const BigInteger& n, const BigInteger& d, RoundingMode mode) {
  if (d.signum() == 0) throw ArithmeticException("Division by zero");
  BigInteger q = n.divide(d);      // truncated toward zero
  BigInteger r = n.remainder(d);   // sign of n
  if (r.signum() == 0) return q;
  int32_t sign = n.signum() * d.signum();  // sign of the exact quotient
  int32_t cmpHalf = r.abs().shiftLeft(1).compareTo(d.abs());  // |r|*2 vs |d|
  bool up;  // increment magnitude
  switch (mode) {
    case RoundingMode::UP: up = true; break;
    case RoundingMode::DOWN: up = false; break;
    case RoundingMode::CEILING: up = sign > 0; break;
    case RoundingMode::FLOOR: up = sign < 0; break;
    case RoundingMode::HALF_UP: up = cmpHalf >= 0; break;
    case RoundingMode::HALF_DOWN: up = cmpHalf > 0; break;
    case RoundingMode::HALF_EVEN:
      up = cmpHalf > 0 || (cmpHalf == 0 && q.remainder(BigInteger::TWO()).signum() != 0);
      break;
    default: throw ArithmeticException("Rounding necessary");
  }
  return up ? q.add(BigInteger(static_cast<int64_t>(sign))) : q;
}

int32_t BigDecimal::precision() const {
  if (unscaled_.signum() == 0) return 1;
  return static_cast<int32_t>(unscaled_.abs().toString().size());
}

BigDecimal BigDecimal::divide(const BigDecimal& divisor, int32_t scale, RoundingMode mode) const {
  // (ua*10^-sa) / (ub*10^-sb) * 10^scale = ua * 10^(sb - sa + scale) / ub
  int64_t e = static_cast<int64_t>(divisor.scale_) - scale_ + scale;
  BigInteger n = unscaled_, d = divisor.unscaled_;
  if (e >= 0) n = n.multiply(BigInteger::TEN().pow(static_cast<int32_t>(e)));
  else d = d.multiply(BigInteger::TEN().pow(static_cast<int32_t>(-e)));
  return BigDecimal(roundedDivide(n, d, mode), scale);
}

BigDecimal BigDecimal::setScale(int32_t scale, RoundingMode mode) const {
  if (scale >= scale_) return BigDecimal(unscaled_.multiply(BigInteger::TEN().pow(scale - scale_)), scale);
  return BigDecimal(roundedDivide(unscaled_, BigInteger::TEN().pow(scale_ - scale), mode), scale);
}

BigDecimal BigDecimal::stripTrailingZeros() const {
  if (unscaled_.signum() == 0) return BigDecimal(BigInteger::ZERO(), 0);
  BigInteger u = unscaled_;
  int32_t s = scale_;
  while (u.remainder(BigInteger::TEN()).signum() == 0) {
    u = u.divide(BigInteger::TEN());
    --s;
  }
  return BigDecimal(u, s);
}

BigDecimal BigFraction::bigDecimalValue(int32_t scale, RoundingMode mode) const {
  BigInteger n = num_, d = den_;
  if (scale >= 0) n = n.multiply(BigInteger::TEN().pow(scale));
  else d = d.multiply(BigInteger::TEN().pow(-scale));
  return BigDecimal(roundedDivide(n, d, mode), scale);
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
