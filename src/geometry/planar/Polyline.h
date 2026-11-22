#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A Polyline is a sequence of lines, where no 2 consecutive lines may be parallel. A Polyline of n
 * lines defines a Polygon of n-1 intersection points of consecutive lines. The lines of the
 * objects of class Polyline are normally defined by points with integer coordinates, whereas the
 * intersections of Lines can be representated in general only by infinite precision rational
 * points. We use polylines with integer coordinates instead of polygons with infinite precision
 * rational coordinates because of its better performance in geometric calculations.
 */
class Polyline : public virtual jrt::Object {
 private:
  static constexpr bool USE_BOUNDING_OCTAGON_FOR_OFFSET_SHAPES = true;

 public:
  /** Stores the array of lines of this polyline. */
  jrt::Array<Line*>* lines;

 private:
  jrt::Array<FloatPoint*>* precalculatedFloatCorners = nullptr;
  jrt::Array<Point*>* precalculatedCorners = nullptr;
  IntBox* precalculatedBoundingBox = nullptr;

 public:
  /** Creates a polyline of length polygon.cornerCount + 1 from polygon, so that the i-th corner
   * of polygon will be the intersection of the i-th and the i+1-th lines of the new created
   * polyline. */
  explicit Polyline(Polygon* polygon);

  /** Creates a polyline from an array of points. */
  explicit Polyline(jrt::Array<Point*>* points);

  /** Creates a polyline consisting of three lines. */
  Polyline(Point* fromCorner, Point* toCorner);

  /** Creates a polyline from an array of lines. Lines, which are parallel to the previous line
   * are skipped. The directed lines are expected to be normalized. */
  explicit Polyline(jrt::Array<Line*>* inputLines);

 private:
  static Line* endpointCap(Line* support, Point* point);
  static jrt::Array<Line*>* removeConsecutiveParallelLines(jrt::Array<Line*>* lines);
  /** Checks if previous and next lines are equal or opposite and removes the resulting overlap. */
  static jrt::Array<Line*>* removeOverlaps(jrt::Array<Line*>* lines);

 public:
  /** Returns the number of lines minus 1. */
  virtual int32_t cornerCount() { return lines->length - 1; }

  virtual bool isEmpty();

  /** Checks, if this polyline is empty or if all corner points are equal. */
  virtual bool isPoint();

  /** Checks if all lines of this polyline are orthogonal. */
  virtual bool isOrthogonal();

  /** Checks if all lines of this polyline are multiples of 45 degrees. */
  virtual bool isMultipleOf45Degree();

  /** Returns the intersection of the first line with the second line. */
  virtual Point* firstCorner();

  /** Returns the intersection of the last line with the line before the last line. */
  virtual Point* lastCorner();

  /** Returns the array of the intersection of two consecutive lines. */
  virtual jrt::Array<Point*>* corners();

  /** Returns the array of intersections of consecutive lines, approximated by FloatPoint's. */
  virtual jrt::Array<FloatPoint*>* cornerApproxArr();

  /** Returns an approximation of the intersection of the no-th with the (no - 1)-th line by a
   * FloatPoint. */
  virtual FloatPoint* cornerApprox(int32_t cornerIndex);

  /** Returns the intersection of the no-th with the (no - 1)-th edge line. */
  virtual Point* corner(int32_t cornerIndex);

  /** Returns the polyline with the reversed order of lines. */
  virtual Polyline* reverse();

  /** Calculates the length of this polyline from fromCorner to toCorner. */
  virtual double lengthApprox(int32_t requestedFromCorner, int32_t requestedToCorner);

  /** Calculates the cumulative distance between consecutive corners of this polyline. */
  virtual double lengthApprox();

  /** Calculates for each line a shape around this line where the right and left edge lines have
   * the distance halfWidth from the center line. Returns an array of convex shapes of length
   * lineCount - 2. */
  virtual jrt::Array<TileShape*>* offsetShapes(int32_t halfWidth);

  /** Calculates for each line between fromNo and toNo a shape around this line, where the right
   * and left edge lines have the distance halfWidth from the center line. */
  virtual jrt::Array<TileShape*>* offsetShapes(int32_t halfWidth, int32_t requestedFromNo,
                                               int32_t requestedToNo);

  /** Calculates for the no-th line segment a shape around this line where the right and left
   * edge lines have the distance halfWidth from the center line. 0 <= no <= arr.length - 3. */
  virtual TileShape* offsetShape(int32_t halfWidth, int32_t no);

  /** Calculates for the no-th line segment a box shape around this line where the border lines
   * have the distance halfWidth from the center line. 0 <= no <= arr.length - 3. */
  virtual IntBox* offsetBox(int32_t halfWidth, int32_t no);

  /** Returns the polyline translated by vector. */
  virtual Polyline* translateBy(Vector* vector);

  /** Returns the polyline turned by factor times 90 degrees around pole. */
  virtual Polyline* turn90Degree(int32_t factor, IntPoint* pole);

  /** Returns an approximation of this polyline rotated around pole. */
  virtual Polyline* rotateApprox(double angle, FloatPoint* pole);

  /** Mirrors this polyline at the vertical line through pole. */
  virtual Polyline* mirrorVertical(IntPoint* pole);

  /** Mirrors this polyline at the horizontal line through pole. */
  virtual Polyline* mirrorHorizontal(IntPoint* pole);

  /** Returns the smallest box containing the intersection points from index fromCornerNo to
   * index toCornerNo of the lines of this polyline. */
  virtual IntBox* boundingBox(int32_t requestedFromCornerNo, int32_t requestedToCornerNo);

  /** Returns the smallest box containing the intersection points of the lines of this
   * polyline. */
  virtual IntBox* boundingBox();

  /** Returns the smallest octagon containing the intersection points from index fromCornerNo to
   * index toCornerNo of the lines of this polyline. */
  virtual IntOctagon* boundingOctagon(int32_t requestedFromCornerNo, int32_t requestedToCornerNo);

  /** Calculates an approximation of the nearest point on this polyline to fromPoint. */
  virtual FloatPoint* nearestPointApprox(FloatPoint* fromPoint);

  /** Calculates the distance of fromPoint to the nearest point on this polyline. */
  virtual double distance(FloatPoint* fromPoint);

  /** Combines the two polylines, if they have a common end corner. The order of lines in this
   * polyline will be preserved. Returns the combined polyline or this polyline, if this polyline
   * and other have no common end corner. */
  virtual Polyline* combine(Polyline* other);

  /** Splits this polyline at the line with index lineIndex into two by inserting endLine as
   * concluding line of the first split piece and as the start line of the second split piece.
   * Returns the 2 pieces of the split polyline, or null, if nothing was split. */
  virtual jrt::Array<Polyline*>* split(int32_t lineIndex, Line* endLine);

  /** Creates a new polyline by skipping lines from fromNo to toNo. */
  virtual Polyline* skipLines(int32_t fromNo, int32_t toNo);

  /** Returns whether this polyline contains the given point. */
  virtual bool contains(Point* point);

 private:
  static std::string debugPoint(Point* point);

 public:
  /** Creates a perpendicular line segment from point onto the nearest line segment of this
   * polyline to point. */
  virtual LineSegment* projectionLine(Point* point);

  /** Shortens this polyline to newLineCount lines. Additionally, the last line segment will be
   * approximately shortened to lastSegmentLength. */
  virtual Polyline* shorten(int32_t newLineCount, double lastSegmentLength);
};

}  // namespace fr
