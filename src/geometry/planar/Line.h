#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements functionality for lines in the plane. Implements Comparable<Line>.
 */
class Line : public virtual jrt::Object {
 public:
  Point* a;
  Point* b;

 private:
  Direction* dir;  // should only be accessed from direction() (lazily cached).
  /**
   * PORT NOTE: not in Java. directionInts() of a line without dir, once computed (dirIntsState 1:
   * dirX / dirY hold it, -1: not integer, 0: not computed); a and b never change.
   */
  int32_t dirX = 0;
  int32_t dirY = 0;
  int8_t dirIntsState = 0;

 public:
  /** Creates a directed Line from two points. */
  Line(Point* a, Point* b);

  /** Creates a directed Line from four integer coordinates. */
  Line(int32_t ax, int32_t ay, int32_t bx, int32_t by);

  /** Creates a directed Line from a point and a direction. */
  Line(Point* a, Direction* dir);

  /** Same as new Line(ax, ay, bx, by), but the line and its two IntPoints are one allocation
   * (PORT NOTE, MEMORY_MODEL.md stage D: the line owns its defining points). */
  static Line* ofInts(int32_t ax, int32_t ay, int32_t bx, int32_t by);

  /** Finds a bounded, nonparallel cap at an exact point without rounding it. */
  virtual Line* capAt(Point* point);

 private:
  /** Resolves exact points to the bounded integer supports required by line operations. */
  static jrt::Array<IntPoint*>* integerSupport(Point* first, Point* second);
  static jrt::Array<jrt::BigInteger>* rationalCoordinates(Point* point);
  /** PORT NOTE: the Java BigInteger[] interval starts as {null, null} -> optional elements. */
  static void restrictSupportParameter(jrt::Array<std::optional<jrt::BigInteger>>* interval,
                                       const jrt::BigInteger& origin,
                                       const jrt::BigInteger& direction);
  static jrt::BigInteger floorDivide(const jrt::BigInteger& numerator,
                                     const jrt::BigInteger& denominator);

 public:
  /** Creates a directed line from an IntPoint and an IntDirection. */
  static Line* getInstance(Point* a, Direction* dir);

  virtual int32_t getId();

  /** Returns true if this and other define the same line. PORT NOTE: Java does not override
   * hashCode here (identity hash is kept). */
  bool equals(jrt::Object* other) final;

  /** Returns true, if this and other define the same line. Is designed for good performance, but
   * works only for lines consisting of IntPoints. */
  bool fastEquals(Line* other);

  /** Gets the direction of this directed line. */
  virtual Direction* direction();
  /** PORT NOTE: the coordinates of direction() if it is an IntDirection, without creating (or
   * caching) the direction object when it is not cached yet. False: use direction(). */
  bool directionInts(int32_t& x, int32_t& y);

  /** Returns Side.ON_THE_LEFT, if this Line is on the left of point, Side.ON_THE_RIGHT, if this
   * Line is on the right of point, Side.COLLINEAR, if this Line contains point. */
  virtual Side* sideOf(Point* point);

  /** Returns Side.COLLINEAR, if point is on the line with tolerance tolerance. Otherwise,
   * Side.ON_THE_LEFT, if this line is on the left of point, or Side.ON_THE_RIGHT. */
  virtual Side* sideOf(FloatPoint* point, double tolerance);

  /** Returns Side.ON_THE_LEFT, if this line is on the left of point, Side.ON_THE_RIGHT, if this
   * line is on the right of point, Side.COLLINEAR otherwise. */
  virtual Side* sideOf(FloatPoint* point);

  /** Returns Side.ON_THE_LEFT, if this line is on the left of the intersection of p1 and p2,
   * Side.ON_THE_RIGHT, if on the right, and Side.COLLINEAR if it contains the intersection. */
  virtual Side* sideOfIntersection(Line* p1, Line* p2);

  /** Looks, if all interior points of tile are on the right side of this line. */
  virtual bool isOnTheLeft(TileShape* tile);

  /** Looks, if all interior points of tile are on the left side of this line. */
  virtual bool isOnTheRight(TileShape* tile);

  /** Returns the signed distance of this line from point. The result will be positive, if the
   * line is on the left of point, else negative. */
  virtual double signedDistance(FloatPoint* point);

  /** Returns true if the two lines define the same set of points, but may have opposite
   * directions. */
  virtual bool overlaps(Line* other);

  /** Returns the line defining the same set of points, but with opposite direction. */
  virtual Line* opposite();

  /** Returns the intersection point of the 2 lines. If the lines are parallel result.isInfinite()
   * will be true. */
  virtual Point* intersection(Line* other);

  /** Returns an approximation of the intersection of the 2 lines by a FloatPoint. If the lines
   * are parallel the result coordinates will be Integer.MAX_VALUE. */
  virtual FloatPoint* intersectionApprox(Line* other);
  /** intersectionApprox() without creating the FloatPoint. */
  void intersectionApprox(Line* other, double& isX, double& isY);
  /** sideOf(FloatPoint, tolerance) for the point (x, y). */
  Side* sideOfApprox(double x, double y, double tolerance);

  /** Returns the perpendicular projection of point onto this line. */
  virtual Point* perpendicularProjection(Point* point);

  /** Translates the line perpendicular by dist. If dist > 0, the line is translated to the left,
   * else to the right. */
  virtual Line* translate(double dist);

  /** Translates the line by vector. */
  virtual Line* translateBy(Vector* vector);

  /** Returns true if the line is axis-parallel. */
  virtual bool isOrthogonal();

  /** Returns true if this line is diagonal. */
  virtual bool isDiagonal();

  /** Returns true if the direction of this line is a multiple of 45 degrees. */
  virtual bool isMultipleOf45Degree();

  /** Checks if this line and other are parallel. */
  virtual bool isParallel(Line* other);

  /** Checks if this line and other are perpendicular. */
  virtual bool isPerpendicular(Line* other);

  /** Returns true if this and other define the same line (possibly opposite direction). */
  virtual bool isEqualOrOpposite(Line* other);

  /** Calculates the cosine of the angle between this line and other. */
  virtual double cosAngle(Line* other);

  /** A line l_1 is defined bigger than a line l_2, if the direction of l_1 is bigger than the
   * direction of l_2. Implements the comparable interface. Throws a ClassCastException, if other
   * is not a Line. Fast implementation only for lines consisting of IntPoints. */
  virtual int32_t compareTo(Line* other);

  /** Calculates an approximation of the function value of this line at x, if the line is not
   * vertical. */
  virtual double functionValueApprox(double x);

  /** Calculates an approximation of the function value in y of this line at y, if the line is not
   * horizontal. */
  virtual double functionInYValueApprox(double y);

  /** Calculates the direction from fromPoint to the nearest point on this line to fromPoint.
   * Returns null, if fromPoint is contained in this line. */
  virtual Direction* perpendicularDirection(Point* fromPoint);

  /** Turns this line by factor times 90 degree around pole. */
  virtual Line* turn90Degree(int32_t factor, IntPoint* pole);

  /** Mirrors this line at the vertical line through pole. */
  virtual Line* mirrorVertical(IntPoint* pole);

  /** Mirrors this line at the horizontal line through pole. */
  virtual Line* mirrorHorizontal(IntPoint* pole);

  /** Returns the Euclidean length of this line. */
  virtual float length();
};

}  // namespace fr
