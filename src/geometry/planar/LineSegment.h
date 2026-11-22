#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements functionality for line segments. The difference between a LineSegment and a Line is,
 * that a Line is infinite and a LineSegment has a start and an endpoint.
 */
class LineSegment : public virtual jrt::Object {
 private:
  Line* start;
  Line* middle;
  Line* end;
  Point* precalculatedStartPoint = nullptr;
  Point* precalculatedEndPoint = nullptr;

 public:
  /** Creates a line segment from the 3 input lines. It starts at the intersection of startLine
   * and middleLine and ends at the intersection of middleLine and endLine. */
  LineSegment(Line* startLine, Line* middleLine, Line* endLine);

  /** Creates the no-th line segment of polyline for no between 1 and polyline.lineCount - 2. */
  LineSegment(Polyline* polyline, int32_t no);

  /** Creates the no-th line segment of shape for no between 0 and shape.lineCount - 1. */
  LineSegment(PolylineShape* shape, int32_t no);

  /** Returns the intersection of the first 2 lines of this segment. */
  virtual Point* startPoint();

  /** Returns the intersection of the last 2 lines of this segment. */
  virtual Point* endPoint();

  /** Returns an approximation of the intersection of the first 2 lines of this segment. */
  virtual FloatPoint* startPointApprox();

  /** Returns an approximation of the intersection of the last 2 lines of this segment. */
  virtual FloatPoint* endPointApprox();

  /** Returns the (infinite) line of this segment. */
  virtual Line* getLine() { return middle; }

  /** Returns the start closing line of this segment. */
  virtual Line* getStartClosingLine() { return start; }

  /** Returns the end closing line of this segment. */
  virtual Line* getEndClosingLine() { return end; }

  /** Returns the line segment with the opposite direction. */
  virtual LineSegment* opposite();

  /** Transforms this LineSegment into a polyline of length 3. */
  virtual Polyline* toPolyline();

  /** Creates a 1 dimensional simplex rom this line segment, which has the same shape as the line
   * segment. */
  virtual Simplex* toSimplex();

  /** Returns an integer-defined cap through point, nonparallel to this segment, or null when no
   * such cap exists. */
  virtual Line* splitLine(Point* point);

 private:
  static bool withinIntegerBounds(const jrt::BigInteger& coordinate);

 public:
  /** Checks exact membership in the closed finite segment, including rational coordinates. */
  virtual bool contains(Point* point);

  /** Calculates the smallest surrounding box of this line segment. */
  virtual IntBox* boundingBox();

  /** Calculates the smallest surrounding octagon of this line segment. */
  virtual IntOctagon* boundingOctagon();

  /** Creates a new line segment with the same start and middle line and an end line, so that the
   * length of the new line segment is about newLength. */
  virtual LineSegment* changeLengthApprox(double newLength);

  /** Looks up the intersections of this line segment with other. The result array may have
   * length 0, 1 or 2. If the segments do not intersect the result array will have length 0. The
   * result lines are so that the intersections of the result lines with this line segment will
   * deliver the intersection points. */
  virtual jrt::Array<Line*>* intersection(LineSegment* other);

  /** Checks if this LineSegment and other contain a common point. */
  virtual bool intersects(LineSegment* other);

  /** Checks if this LineSegment and other contain a common LineSegment, which is not reduced to a
   * point. */
  virtual bool overlaps(LineSegment* other);

  /** Constructs an approximation of this line segment by orthogonal stairs with integer
   * coordinates. If toTheRight, the approximation is to the right of this line, else to the
   * left. */
  virtual jrt::Array<IntPoint*>* stairApproximation(double width, bool toTheRight);

  /** Constructs an approximation of this line segment by 45 degree stairs with integer
   * coordinates. If toTheRight, the approximation is to the right of this line, else to the
   * left. */
  virtual jrt::Array<IntPoint*>* stairApproximation45(double width, bool toTheRight);

  /** Returns an array with the borderline numbers of shape, which are intersected by this line
   * segment. Intersections at an endpoint of this line segment are only counted, if the line
   * segment intersects with the interior of shape. */
  virtual jrt::Array<int32_t>* borderIntersections(TileShape* shape);

  /** Inverts the direction of this.middle, if startPoint() has a bigger x coordinate than
   * endPoint(), or an equal x coordinate and a bigger y coordinate. */
  virtual LineSegment* sortEndpointsInXY();
};

}  // namespace fr
