#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Defines a line in the plane by to FloatPoints. Calculations with FloatLines are generally not
 * exact. For that reason collinearity for example is not defined for FloatLines. If exactness is
 * needed, use the class Line instead.
 */
class FloatLine : public virtual jrt::Object {
 public:
  FloatPoint* a;
  FloatPoint* b;

  /** Creates a line from two FloatPoints. */
  FloatLine(FloatPoint* a, FloatPoint* b);

  /** Returns the FloatLine with swapped end points. */
  virtual FloatLine* opposite();

  /** Adjusts this line's direction to match the orientation of another line. */
  virtual FloatLine* adjustDirection(FloatLine* other);

  /** Calculates the intersection of this line with other. Returns null, if the lines are
   * parallel. */
  virtual FloatPoint* intersection(FloatLine* other);

  /** Translates the line perpendicular at about dist. If dist > 0, the line will be translated to
   * the left, else to the right. */
  virtual FloatLine* translate(double dist);

  /** Returns the signed distance of this line from point. The result will be positive, if the
   * line is on the left of point, else negative. */
  virtual double signedDistance(FloatPoint* point);

  /** Returns an approximation of the perpendicular projection of point onto this line. */
  virtual FloatPoint* perpendicularProjection(FloatPoint* point);

  /** Returns the distance of point to the nearest point of this line between this.a and
   * this.b. */
  virtual double segmentDistance(FloatPoint* point);

  /** Returns the perpendicular projection of lineSegment onto this oriented line segment. Returns
   * null, if the projection is empty. */
  virtual FloatLine* segmentProjection(FloatLine* lineSegment);

  /** Returns the projection of lineSegment onto this oriented line segment by moving lineSegment
   * perpendicular into the direction of this line segment. Returns null, if the projection is
   * empty. */
  virtual FloatLine* segmentProjection2(FloatLine* lineSegment);

  /** Shrinks this line on both sides by value. The result will contain at least the midpoint of
   * the line. */
  virtual FloatLine* shrinkSegment(double offset);

  /** Calculates the nearest point on this line to fromPoint between this.a and this.b. */
  virtual FloatPoint* nearestSegmentPoint(FloatPoint* fromPoint);

  /** Divides this line segment into count line segments of nearly equal length and at most
   * maxSectionLength. */
  virtual jrt::Array<FloatLine*>* divideSegmentIntoSections(int32_t count);
};

}  // namespace fr
