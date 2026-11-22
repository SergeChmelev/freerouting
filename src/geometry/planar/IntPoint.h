#pragma once

#include "fwd.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Point.h"
#include "jrt/jrt.h"

namespace fr {

/** Implementation of the abstract class Point as a tuple of integers. */
class IntPoint final : public Point {
 public:
  /** The x coordinate of this point. */
  int32_t x;

  /** The y coordinate of this point. */
  int32_t y;

  /** Creates an IntPoint from two integer coordinates. */
  IntPoint(int32_t x, int32_t y) : x(x), y(y) {}

  using Point::sideOf;

  /** Returns true, if this IntPoint is equal to other. */
  bool equals(jrt::Object* other) override;
  int32_t hashCode() override;

  bool isInfinite() override { return false; }
  IntBox* surroundingBox() override;
  IntOctagon* surroundingOctagon() override;
  bool isContainedIn(IntBox* box) override;

  /** Returns the translation of this point by vector. */
  Point* translateBy(Vector* vector) override;
  Point* translateBy(IntVector* vector) override;
  Point* translateBy(RationalVector* vector) override;

  /** Returns the difference vector of this point and other. */
  Vector* differenceBy(Point* other) override;
  Vector* differenceBy(RationalPoint* other) override;
  IntVector* differenceBy(IntPoint* other) override;

  Side* sideOf(Line* line) override;

  /** Converts this point to a FloatPoint. */
  FloatPoint* toFloat() override;
  int32_t getId() override;

  /** Returns the determinant of the vectors (x, y) and (other.x, other.y). */
  int64_t determinant(IntPoint* other);

  Point* perpendicularProjection(Line* line) override;

  /** Returns the signed area of the parallelogramm spanned by the vectors p2 - p1 and this - p1. */
  double signedArea(IntPoint* p1, IntPoint* p2);

  /** Calculates the square of the distance between this point and toPoint. */
  double distanceSquare(IntPoint* toPoint);

  /** Calculates the distance between this point and toPoint. */
  double distance(IntPoint* toPoint);

  /** Calculates the nearest point to this point on the horizontal or vertical line through other
   * (Snaps this point to the nearest orthogonal line through other). */
  IntPoint* orthogonalProjection(IntPoint* other);

  /** Calculates the nearest point to this point on an orthogonal or diagonal line through other
   * (Snaps this point to the nearest 45 degree line through other). */
  IntPoint* fortyfiveDegreeProjection(IntPoint* other);

  /** Calculates a corner point p so that the lines through this point and p and from p to toPoint
   * are multiples of 45 degree, and that the angle at p will be 45 degree. If leftTurn, toPoint
   * will be on the left of the line from this point to p, else on the right. Returns null, if the
   * line from this point to toPoint is already a multiple of 45 degree. */
  IntPoint* fortyfiveDegreeCorner(IntPoint* toPoint, bool leftTurn);

  /** Calculates a corner point p so that the lines through this point and p and from p to toPoint
   * are orthogonal (90 degree corner). Returns null, if the line is already orthogonal. */
  IntPoint* ninetyDegreeCorner(IntPoint* toPoint, bool leftTurn);

  int32_t compareX(Point* other) override;
  int32_t compareX(IntPoint* other) override;
  int32_t compareX(RationalPoint* other) override;
  int32_t compareY(Point* other) override;
  int32_t compareY(IntPoint* other) override;
  int32_t compareY(RationalPoint* other) override;

  std::string toString() override;
};

}  // namespace fr
