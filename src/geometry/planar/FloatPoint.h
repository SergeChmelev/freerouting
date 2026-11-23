#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements a point in the plane as a tuple of double's. Because arithmetic calculations with
 * double's are in general not exact, FloatPoint is not derived from the abstract class Point.
 */
class FloatPoint : public virtual jrt::Object {
 public:
  /** PORT NOTE: static field -> accessor. */
  static FloatPoint* ZERO();

  /** The x coordinate of this point. */
  double x;

  /** The y coordinate of this point. */
  double y;

  /** Creates an instance of class FloatPoint from two doubles. */
  FloatPoint(double x, double y) : x(x), y(y) {}

  /** PORT NOTE: coordinates of projectionApprox(line) for a line through integer points, without
   * creating the point; false (nothing stored) for other lines. */
  bool projectionApproxInts(Line* line, double& px, double& py);

  /** Creates a FloatPoint from an IntPoint. */
  explicit FloatPoint(IntPoint* pt);

  /** Calculates the smallest IntOctagon containing all the input points. */
  static IntOctagon* boundingOctagon(jrt::Array<FloatPoint*>* points);

  /** Returns the square of the distance from this point to the zero point. */
  double sizeSquare() { return x * x + y * y; }

  /** Returns the distance from this point to the zero point. */
  double size();

  /** Returns the square of the distance from this Point to the Point other. */
  double distanceSquare(FloatPoint* other);

  /** Returns the distance from this point to the point other. */
  double distance(FloatPoint* other);

  /** Computes the weighted distance to other. */
  virtual double weightedDistance(FloatPoint* other, double horizontalWeight,
                                  double verticalWeight);

  /** Rounds the coordinates from an object of class Point_double to an object of class
   * IntPoint. */
  virtual IntPoint* round();

  /** Rounds this point, so that if this point is on the right side of any directed line with
   * direction dir, the result point will also be on the right side. */
  virtual IntPoint* roundToTheRight(Direction* dir);

  /** Round this Point so the x coordinate of the result will be a multiple of horizontalGrid and
   * the y coordinate a multiple of verticalGrid. */
  virtual IntPoint* roundToGrid(int32_t horizontalGrid, int32_t verticalGrid);

  /** Rounds this point, so that if this point is on the left side of any directed line with
   * direction dir, the result point will also be on the left side. */
  virtual IntPoint* roundToTheLeft(Direction* dir);

  /** Adds the coordinates of this FloatPoint and other. */
  virtual FloatPoint* add(FloatPoint* other);

  /** Subtracts the coordinates of other from this FloatPoint. */
  virtual FloatPoint* subtract(FloatPoint* other);

  /** Returns an approximation of the perpendicular projection of this point onto line. */
  virtual FloatPoint* projectionApprox(Line* line);

  /** Calculates the scalar product of (p1 - this). with (p2 - this). */
  virtual double scalarProduct(FloatPoint* p1, FloatPoint* p2);

  /** Approximates a FloatPoint on the line from zero to this point with distance newSize from
   * zero. */
  virtual FloatPoint* changeSize(double newSize);

  /** Approximates a FloatPoint on the line from this point to toPoint with distance newLength
   * from this point. */
  virtual FloatPoint* changeLength(FloatPoint* toPoint, double newLength);

  /** Returns the middle point between this point and toPoint. */
  virtual FloatPoint* middlePoint(FloatPoint* toPoint);

  /** Returns Side.ON_THE_LEFT, if this Point is on the left of the line from p1 to p2;
   * Side.ON_THE_RIGHT, if on the right; Side.COLLINEAR otherwise. */
  virtual Side* sideOf(FloatPoint* p1, FloatPoint* p2);

  /** Rotates this FloatPoint by angle (in radians) around the pole. */
  virtual FloatPoint* rotate(double angle, FloatPoint* pole);

  /** Turns this FloatPoint by factor times 90 degrees around ZERO. */
  virtual FloatPoint* turn90Degree(int32_t factor);

  /** Turns this FloatPoint by factor times 90 degrees around pole. */
  virtual FloatPoint* turn90Degree(int32_t factor, FloatPoint* pole);

  /** Checks, if this point is contained in the box spanned by p1 and p2 with the input
   * tolerance. */
  virtual bool isContainedInBox(FloatPoint* p1, FloatPoint* p2, double tolerance);

  /** Creates the smallest IntBox containing this point. */
  virtual IntBox* boundingBox();

  /** Calculates the touching points of the tangents from this point to a circle around toPoint
   * with radius distance. */
  virtual jrt::Array<FloatPoint*>* tangentialPoints(FloatPoint* toPoint, double distance);

  /** Calculates the left tangential point of the line from this point to a circle around toPoint
   * with radius distance. Returns null, if this point is inside this circle. */
  virtual FloatPoint* leftTangentialPoint(FloatPoint* toPoint, double distance);

  /** Calculates the right tangential point of the line from this point to a circle around toPoint
   * with radius distance. Returns null, if this point is inside this circle. */
  virtual FloatPoint* rightTangentialPoint(FloatPoint* toPoint, double distance);

  /** Calculates the center of the circle through this point, p1 and p2 by calculating the
   * intersection of the two lines perpendicular to and passing through the midpoints. */
  virtual FloatPoint* circleCenter(FloatPoint* p1, FloatPoint* p2);

  /** Returns true, if this point is contained in the circle through p1, p2 and p3. */
  virtual bool insideCircle(FloatPoint* p1, FloatPoint* p2, FloatPoint* p3);

  /** Java toString(Locale locale, int fractionDigits, int padding): padded string
   * "X <x>   Y <-y>". PORT NOTE: java.util.Locale is not ported; the Locale parameter is dropped
   * and formatting always uses Locale.ENGLISH (Java toString(Locale) == toString()). */
  virtual std::string toString(int32_t fractionDigits, int32_t padding);

  /** Java: toString(Locale.ENGLISH), "(x , y)" with at most 4 fraction digits. */
  std::string toString() override;
};

}  // namespace fr
