// Port of app.freerouting.datastructures.BigIntAux
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Record representing homogeneous 3D rational coordinates (x, y, z). */
class BigIntAux_RationalCoordinates : public virtual jrt::Object {
 public:
  BigIntAux_RationalCoordinates(const jrt::BigInteger& x, const jrt::BigInteger& y,
                                const jrt::BigInteger& z)
      : x_(x), y_(y), z_(z) {}

  jrt::BigInteger x() { return x_; }
  jrt::BigInteger y() { return y_; }
  jrt::BigInteger z() { return z_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = jrt::fast_cast<BigIntAux_RationalCoordinates>(o);
    return other != nullptr && x_.equals(other->x_) && y_.equals(other->y_) &&
           z_.equals(other->z_);
  }
  int32_t hashCode() override {
    int32_t h = x_.hashCode();
    h = jrt::hashCombine(h, y_.hashCode());
    return jrt::hashCombine(h, z_.hashCode());
  }
  std::string toString() override {
    return "RationalCoordinates[x=" + x_.toString() + ", y=" + y_.toString() +
           ", z=" + z_.toString() + "]";
  }

 private:
  jrt::BigInteger x_;
  jrt::BigInteger y_;
  jrt::BigInteger z_;
};

/** Auxiliary functions with BigInteger parameters. */
class BigIntAux final : public virtual jrt::Object {
 public:
  /** trailingZeroTable[i] is the number of trailing zero bits in the binary representation of i. */
  static jrt::Array<int8_t>* trailingZeroTable();

  /** Calculates the determinant of the vectors (x1, y1) and (x2, y2). */
  static jrt::BigInteger determinant(const jrt::BigInteger& x1, const jrt::BigInteger& y1,
                                     const jrt::BigInteger& x2, const jrt::BigInteger& y2);

  /** Adds homogeneous coordinates without allocating temporary three-element arrays. */
  static BigIntAux_RationalCoordinates* addRationalCoordinates(
      const jrt::BigInteger& ax, const jrt::BigInteger& ay, const jrt::BigInteger& az,
      const jrt::BigInteger& bx, const jrt::BigInteger& by, const jrt::BigInteger& bz);

  /**
   * Auxiliary function to implement addition and translation in the classes RationalVector and
   * RationalPoint.
   */
  static jrt::Array<jrt::BigInteger>* addRationalCoordinates(jrt::Array<jrt::BigInteger>* first,
                                                             jrt::Array<jrt::BigInteger>* second);

  /** Calculate GCD of a and b interpreted as unsigned integers. */
  static int32_t binaryGcd(int32_t a, int32_t b);

 private:
  BigIntAux() = default;  // disallow instantiation
};

}  // namespace fr
