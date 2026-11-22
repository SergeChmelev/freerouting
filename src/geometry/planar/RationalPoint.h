#pragma once

#include "fwd.h"
#include "geometry/planar/Point.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implementation of points in the projective plane represented by 3 coordinates x, y, z, which
 * are infinite precision integers. Two projective points (x1, y1, z1) and (x2, y2, z2) are equal,
 * if they are located on the same line through the zero point, that means, if there exists a
 * number r with x2 = r*x1, y2 = r*y1 and z2 = r*z1. The affine Point with rational coordinates
 * represented by the projective Point (x, y, z) is (x/z, y/z). The projective plane with integer
 * coordinates contains in addition to the affine plane with rational coordinates the so-called
 * line at infinity, which consist of all projective points (x, y, z) with z = 0.
 */
class RationalPoint final : public Point {
 public:
  // PORT NOTE: package-private fields -> public.
  jrt::BigInteger x;
  jrt::BigInteger y;
  jrt::BigInteger z;

  /** Creates a RationalPoint from 3 BigIntegers x, y and z. They represent the 2-dimensional
   * point with the rational coordinates x/z and y/z. */
  RationalPoint(const jrt::BigInteger& x, const jrt::BigInteger& y, const jrt::BigInteger& z);

  /** Creates a RationalPoint from an IntPoint. */
  explicit RationalPoint(IntPoint* point);

  /** Approximates the coordinates of this point by float coordinates. */
  FloatPoint* toFloat() override;
  int32_t getId() override;

  bool equals(jrt::Object* other) override;
  int32_t hashCode() override;

  bool isInfinite() override;
  IntBox* surroundingBox() override;
  IntOctagon* surroundingOctagon() override;

 private:
  int32_t floorCoordinate(const jrt::BigInteger& numerator);
  int32_t ceilCoordinate(const jrt::BigInteger& numerator);
  static int32_t boundedInteger(const jrt::BigInteger& value);

 public:
  bool isContainedIn(IntBox* box) override;

  /** Returns the translation of this point by vector. */
  Point* translateBy(Vector* vector) override;
  Point* translateBy(IntVector* vector) override;
  Point* translateBy(RationalVector* vector) override;

  /** Returns the difference vector of this point and other. */
  Vector* differenceBy(Point* other) override;
  Vector* differenceBy(IntPoint* other) override;
  Vector* differenceBy(RationalPoint* other) override;

  /** Returns Side.ON_THE_LEFT, if this Point is on the left of the line from p1 to p2;
   * Side.ON_THE_RIGHT, if on the right; Side.COLLINEAR otherwise (exact rational arithmetic). */
  Side* sideOf(Point* p1, Point* p2) override;

 private:
  static BigIntAux_RationalCoordinates* coordinates(Point* point);

 public:
  Side* sideOf(Line* line) override;
  Point* perpendicularProjection(Line* line) override;

  int32_t compareX(Point* other) override;
  int32_t compareX(RationalPoint* other) override;
  int32_t compareX(IntPoint* other) override;
  int32_t compareY(Point* other) override;
  int32_t compareY(RationalPoint* other) override;
  int32_t compareY(IntPoint* other) override;
};

}  // namespace fr
