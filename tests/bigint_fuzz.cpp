// Differential test: jrt::BigInteger (int64 fast paths) vs plain GMP on values around the
// int64 boundaries. Prints mismatches and a summary.
#include <gmp.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "jrt/jrt.h"

using jrt::BigInteger;

static std::string mpzStr(const mpz_t v) {
  char* s = mpz_get_str(nullptr, 10, v);
  std::string r(s);
  return r;
}

static int fails = 0;
static long checks = 0;
static void check(const char* op, const std::string& a, const std::string& b, const std::string& got,
                  const std::string& want) {
  ++checks;
  if (got != want) {
    if (++fails < 30) std::printf("MISMATCH %s(%s, %s): got %s want %s\n", op, a.c_str(), b.c_str(), got.c_str(), want.c_str());
  }
}

int main() {
  std::mt19937_64 rng(12345);
  std::vector<std::string> vals = {"0", "1", "-1", "2", "-2", "9223372036854775807", "-9223372036854775808",
                                   "9223372036854775806", "-9223372036854775807", "4294967296", "-4294967296",
                                   "4294967295", "2147483647", "-2147483648", "2147483648",
                                   "18446744073709551616", "-18446744073709551616", "123456789012345678901234567890",
                                   "-98765432109876543210", "3037000499", "3037000500", "-3037000500"};
  for (int i = 0; i < 400; ++i) {
    int64_t v = static_cast<int64_t>(rng());
    int shift = static_cast<int>(rng() % 64);
    vals.push_back(std::to_string(v >> shift));
  }
  long jhashMismatch = 0;
  for (const auto& sa : vals) {
    BigInteger a(sa);
    mpz_t ma;
    mpz_init_set_str(ma, sa.c_str(), 10);
    // unary
    {
      mpz_t r; mpz_init(r);
      mpz_neg(r, ma); check("negate", sa, "", a.negate().toString(), mpzStr(r));
      mpz_abs(r, ma); check("abs", sa, "", a.abs().toString(), mpzStr(r));
      check("signum", sa, "", std::to_string(a.signum()), std::to_string(mpz_sgn(ma)));
      check("toString", sa, "", a.toString(), sa);
      // bitLength (Java)
      int32_t bl;
      if (mpz_sgn(ma) >= 0) bl = mpz_sgn(ma) == 0 ? 0 : (int32_t)mpz_sizeinbase(ma, 2);
      else { mpz_t t; mpz_init(t); mpz_neg(t, ma); mpz_sub_ui(t, t, 1); bl = mpz_sgn(t) == 0 ? 0 : (int32_t)mpz_sizeinbase(t, 2); }
      check("bitLength", sa, "", std::to_string(a.bitLength()), std::to_string(bl));
      // longValue: low 64 bits
      mpz_t t; mpz_init(t); mpz_fdiv_r_2exp(t, ma, 64);
      check("longValue", sa, "", std::to_string(a.longValue()), std::to_string((int64_t)mpz_get_ui(t)));
      check("fitsLong", sa, "", std::to_string(a.fitsLong()), std::to_string(mpz_fits_slong_p(ma) != 0));
      // doubleValue vs mpz_get_d with correct rounding: compare against a GMP-only BigInteger
      // built from a value outside int64 range is not possible; compare with long double path.
      if (mpz_fits_slong_p(ma)) {
        double want = (double)(int64_t)mpz_get_si(ma);
        char buf1[64], buf2[64];
        std::snprintf(buf1, 64, "%a", a.doubleValue()); std::snprintf(buf2, 64, "%a", want);
        check("doubleValue", sa, "", buf1, buf2);
      }
      // hashCode (Java magnitude words)
      {
        int sgn = mpz_sgn(ma);
        uint32_t h = 0;
        if (sgn != 0) {
          size_t count = 0; std::vector<uint32_t> w((mpz_sizeinbase(ma, 2) + 31) / 32 + 1);
          mpz_export(w.data(), &count, 1, 4, 0, 0, ma);
          for (size_t k = 0; k < count; ++k) h = 31 * h + w[k];
          h *= (uint32_t)sgn;
        }
        check("hashCode", sa, "", std::to_string(a.hashCode()), std::to_string((int32_t)h));
      }
      for (int n : {0, 1, 2, 31, 32, 33, 62, 63, 64, 65, 100}) {
        mpz_mul_2exp(r, ma, n); check("shl", sa, std::to_string(n), a.shiftLeft(n).toString(), mpzStr(r));
        mpz_fdiv_q_2exp(r, ma, n); check("shr", sa, std::to_string(n), a.shiftRight(n).toString(), mpzStr(r));
      }
    }
    for (const auto& sb : vals) {
      BigInteger b(sb);
      mpz_t mb, r;
      mpz_init_set_str(mb, sb.c_str(), 10);
      mpz_init(r);
      mpz_add(r, ma, mb); check("add", sa, sb, a.add(b).toString(), mpzStr(r));
      mpz_sub(r, ma, mb); check("sub", sa, sb, a.subtract(b).toString(), mpzStr(r));
      mpz_mul(r, ma, mb); check("mul", sa, sb, a.multiply(b).toString(), mpzStr(r));
      int c = mpz_cmp(ma, mb); c = c < 0 ? -1 : (c > 0 ? 1 : 0);
      check("cmp", sa, sb, std::to_string(a.compareTo(b)), std::to_string(c));
      mpz_gcd(r, ma, mb); check("gcd", sa, sb, a.gcd(b).toString(), mpzStr(r));
      if (mpz_sgn(mb) != 0) {
        mpz_tdiv_q(r, ma, mb); check("div", sa, sb, a.divide(b).toString(), mpzStr(r));
        mpz_tdiv_r(r, ma, mb); check("rem", sa, sb, a.remainder(b).toString(), mpzStr(r));
      }
      if (mpz_sgn(mb) > 0) {
        mpz_mod(r, ma, mb); check("mod", sa, sb, a.mod(b).toString(), mpzStr(r));
      }
      // results must be normalised: inline iff the value fits in int64
      BigInteger s = a.add(b).subtract(b);
      check("norm", sa, sb, std::to_string(s.isSmall()), std::to_string(mpz_fits_slong_p(ma) != 0));
    }
  }
  std::printf("checks %ld, mismatches %d\n", checks, fails);
  return fails == 0 ? 0 : 1;
}
