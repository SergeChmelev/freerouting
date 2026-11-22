// Port of app.freerouting.datastructures.BigIntAux
#include "datastructures/BigIntAux.h"

namespace fr {

jrt::Array<int8_t>* BigIntAux::trailingZeroTable() {
  static jrt::Array<int8_t>* v = jrt::Array<int8_t>::of({
      -25, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1,
      0});
  return v;
}

jrt::BigInteger BigIntAux::determinant(const jrt::BigInteger& x1, const jrt::BigInteger& y1,
                                       const jrt::BigInteger& x2, const jrt::BigInteger& y2) {
  jrt::BigInteger tmp1 = x1.multiply(y2);
  jrt::BigInteger tmp2 = x2.multiply(y1);
  return tmp1.subtract(tmp2);
}

BigIntAux_RationalCoordinates* BigIntAux::addRationalCoordinates(
    const jrt::BigInteger& ax, const jrt::BigInteger& ay, const jrt::BigInteger& az,
    const jrt::BigInteger& bx, const jrt::BigInteger& by, const jrt::BigInteger& bz) {
  if (az.equals(bz)) {
    return new (jrt::kOp) BigIntAux_RationalCoordinates(ax.add(bx), ay.add(by), az);
  }
  return new (jrt::kOp) BigIntAux_RationalCoordinates(ax.multiply(bz).add(bx.multiply(az)),
                                           ay.multiply(bz).add(by.multiply(az)), az.multiply(bz));
}

jrt::Array<jrt::BigInteger>* BigIntAux::addRationalCoordinates(
    jrt::Array<jrt::BigInteger>* first, jrt::Array<jrt::BigInteger>* second) {
  auto* result = new (jrt::kOp) jrt::Array<jrt::BigInteger>(3);
  if ((*first)[2].equals((*second)[2])) {
    // both rational numbers have the same denominator
    (*result)[2] = (*first)[2];
    (*result)[0] = (*first)[0].add((*second)[0]);
    (*result)[1] = (*first)[1].add((*second)[1]);
  } else {
    // multiply both denominators for the new denominator
    (*result)[2] = (*first)[2].multiply((*second)[2]);
    jrt::BigInteger tmp1 = (*first)[0].multiply((*second)[2]);
    jrt::BigInteger tmp2 = (*second)[0].multiply((*first)[2]);
    (*result)[0] = tmp1.add(tmp2);
    tmp1 = (*first)[1].multiply((*second)[2]);
    tmp2 = (*second)[1].multiply((*first)[2]);
    (*result)[1] = tmp1.add(tmp2);
  }
  return result;
}

int32_t BigIntAux::binaryGcd(int32_t a, int32_t b) {
  if (b == 0) {
    return a;
  }
  if (a == 0) {
    return b;
  }
  jrt::Array<int8_t>* table = trailingZeroTable();

  int32_t x;
  int32_t leadingZeroCountA = 0;
  while ((x = a & 0xff) == 0) {
    a = jrt::ushr(a, 8);
    leadingZeroCountA += 8;
  }
  int32_t y = (*table)[x];
  leadingZeroCountA += y;
  a = jrt::ushr(a, y);

  int32_t leadingZeroCountB = 0;
  while ((x = b & 0xff) == 0) {
    b = jrt::ushr(b, 8);
    leadingZeroCountB += 8;
  }
  y = (*table)[x];
  leadingZeroCountB += y;
  b = jrt::ushr(b, y);

  int32_t t = jrt::Math::min(leadingZeroCountA, leadingZeroCountB);

  while (a != b) {
    if (static_cast<int32_t>(static_cast<uint32_t>(a) + 0x80000000u) >
        static_cast<int32_t>(static_cast<uint32_t>(b) + 0x80000000u)) {  // a > b as unsigned
      a -= b;

      while ((x = a & 0xff) == 0) {
        a = jrt::ushr(a, 8);
      }
      a = jrt::ushr(a, static_cast<int32_t>((*table)[x]));
    } else {
      b -= a;

      while ((x = b & 0xff) == 0) {
        b = jrt::ushr(b, 8);
      }
      b = jrt::ushr(b, static_cast<int32_t>((*table)[x]));
    }
  }
  return jrt::shl(a, t);
}

}  // namespace fr
