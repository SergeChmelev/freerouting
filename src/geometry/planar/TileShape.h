#pragma once

#include "fwd.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/PolylineShape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Abstract class defining functionality for convex shapes, whose borders consists of straight
 * lines (sealed: RegularTileShape, Simplex). PORT NOTE: package-private abstract (double dispatch)
 * methods are public in C++.
 */
class TileShape : public PolylineShape, public virtual ConvexShape {
 public:
  /** Creates a Simplex as intersection of the half-planes defined by directed lines. */
  static TileShape* getInstance(jrt::Array<Line*>* lines);

  /** Creates a TileShape from a Point array, who forms the corners of the shape of a convex
   * polygon. May work only for IntPoints. */
  static TileShape* getInstance(jrt::Array<Point*>* convexPolygon);

  /** Creates a half-plane from a directed line. */
  static TileShape* getInstance(Line* line);

  /** Creates a normalized IntOctagon from the input values. For the meaning of the parameter
   * shortcuts see class IntOctagon. */
  static IntOctagon* getInstance(int32_t lx, int32_t ly, int32_t rx, int32_t uy, int32_t ulx,
                                 int32_t lrx, int32_t llx, int32_t urx);

  /** Creates a box-like convex shape. */
  static IntOctagon* getInstance(int32_t lowerLeftX, int32_t lowerLeftY, int32_t upperRightX,
                                 int32_t upperRightY);

  /** Creates the smallest IntBox containing point. */
  static IntBox* getInstance(Point* point);

  /** Tries to simplify the result shape to a simpler shape. Simplifying always in the
   * intersection function may cause performance problems. */
  virtual TileShape* intersectionWithSimplify(TileShape* other);

  /** Converts the physical instance of this shape to a simpler physical instance, if possible. */
  virtual TileShape* simplify() = 0;

  /** Returns a unique ID for this shape for deterministic tie-breaking. */
  virtual int32_t getId() = 0;

  /** Checks if this TileShape is an IntBox or can be converted into an IntBox. */
  virtual bool isIntBox() = 0;

  /** Checks if this TileShape is an IntOctagon or can be converted into an IntOctagon. */
  virtual bool isIntOctagon() = 0;

  /** Returns the intersection of this shape with other. */
  virtual TileShape* intersection(TileShape* other) = 0;
  virtual TileShape* intersection(Simplex* other) = 0;
  virtual TileShape* intersection(IntOctagon* other) = 0;
  virtual TileShape* intersection(IntBox* other) = 0;

  /** Returns the no-th edge line of this shape for no between 0 and borderLineCount() - 1. The
   * edge lines are sorted in counterclock sense around the shape starting with the line with the
   * smallest direction. The left side of an edge line is inside the shape. */
  Line* borderLine(int32_t no) override = 0;

  /** Returns the edge number if line is a border line of this shape; otherwise returns -1. */
  virtual int32_t borderLineIndex(Line* line) = 0;

  /** Converts the internal representation of this TieShape to a Simplex. */
  virtual Simplex* toSimplex() = 0;

  /** Returns the content of the area of the shape. If the shape is unbounded, Double.MAX_VALUE is
   * returned. */
  double area() override;

  /** Returns true, if point is not contained in the inside or the edge of the shape. */
  bool isOutside(Point* point) override;

  bool contains(Point* point) override;

  /** Returns true, if point is contained in this shape. */
  bool contains(FloatPoint* point) override;

  /** Returns true, if point is contained in this shape with tolerance tolerance. tolerance is used
   * when checking that point is on the right side of the border lines. */
  virtual bool contains(FloatPoint* point, double tolerance);

  /** Returns true, if this shape contains other completely. */
  virtual bool contains(TileShape* other);

  /** Returns true, if point is contained in this shape, but not on an edge line. */
  bool containsInside(Point* point) override;

  /** Returns Side.COLLINEAR if point is on the border of this shape with tolerance tolerance.
   * Side.ON_THE_RIGHT if point is outside, Side.ON_THE_LEFT if inside. */
  virtual Side* sideOfBorder(FloatPoint* point, double tolerance);

  /** If point lies on the border of this shape, the number of the edge line segment containing
   * point is returned, otherwise -1 is returned. */
  virtual int32_t containsOnBorderLineNo(Point* point);

  /** Returns true, if point lies exact on the boundary of the shape. */
  bool containsOnBorder(Point* point) override;

  /** Returns true, if this shape contains other completely. There may be some numerical
   * inaccuracy. */
  virtual bool containsApprox(TileShape* other);

  /** Returns the distance between point and its nearest point on the shape. 0, if point is
   * contained in this shape. */
  double distance(FloatPoint* point) override;

  /** Returns the distance between point and its nearest point on the edge of the shape. */
  double borderDistance(FloatPoint* point) override;

  double smallestRadius() override;

  /** Returns the point in this shape, which has the smallest distance to fromPoint. fromPoint, if
   * that point is contained in this shape. */
  virtual Point* nearestPoint(Point* fromPoint);

  FloatPoint* nearestPointApprox(FloatPoint* fromPoint) override;

  /** Returns the nearest point to fromPoint on the edge of the shape. */
  virtual Point* nearestBorderPoint(Point* fromPoint);

  /** Returns an approximation of the nearest point to fromPoint on the border of this shape. */
  virtual FloatPoint* nearestBorderPointApprox(FloatPoint* fromPoint);

  /** Returns an approximation of the count nearest points to fromPoint on the border of this
   * shape. The result points must be located on different border lines and are sorted in
   * ascending order (the nearest point comes first). */
  virtual jrt::Array<FloatPoint*>* nearestBorderPointsApprox(FloatPoint* fromPoint, int32_t count);

  /** Returns the number of the nearest corner of the shape to fromPoint. */
  virtual int32_t indexOfNearestCorner(Point* fromPoint);

  /** Returns a line segment consisting of an approximations of the corners with index 0 and
   * cornerCount / 2. */
  virtual FloatLine* diagonalCornerSegment();

  /** Returns an approximation of the count nearest relative outside locations of shape in the
   * direction of different border lines of this shape. These relative locations are sorted in
   * ascending order (the shortest comes first). */
  virtual jrt::Array<FloatPoint*>* nearestRelativeOutsideLocations(TileShape* shape, int32_t count);

  ConvexShape* shrink(double offset) override;

  /** Returns the maximum of the edge widths of the shape. Only defined when the shape is
   * bounded. */
  virtual double length();

  /** Calculates, if this Shape and other have a common border piece and returns an 2 dimensional
   * array with the indices in this shape and other of the touching edge lines in this case.
   * Otherwise, an array of dimension 0 is returned. Used if the intersection shape is
   * 1-dimensional. */
  virtual jrt::Array<int32_t>* touchingSides(TileShape* other);

  /** Calculates the minimal distance of line to this shape, assuming, that line is on the left of
   * this shape. Returns -1, if line intersects the interior of this shape. */
  virtual double distanceToTheLeft(Line* line);

  /** Returns Side.COLLINEAR, if line intersects with the interior of this shape, Side.ON_THE_LEFT,
   * if this shape is completely on the left of line or Side.ON_THE_RIGHT, if this shape is
   * completely on the right of line. */
  virtual Side* sideOf(Line* line);

  TileShape* turn90Degree(int32_t factor, IntPoint* pole) override;
  TileShape* rotateApprox(double angle, FloatPoint* pole) override;
  TileShape* mirrorVertical(IntPoint* pole) override;
  TileShape* mirrorHorizontal(IntPoint* pole) override;

  /** Calculates the border line of this shape intersecting the ray from point into the direction
   * direction. point is assumed to be inside this shape, otherwise -1 is returned. */
  virtual int32_t intersectingBorderLineNo(Point* point, Direction* direction);

  /** Cuts shape out of this shape and divides the result into convex pieces. */
  virtual jrt::Array<TileShape*>* cutout(TileShape* shape) = 0;

  /** Cuts out the parts of polyline in the interior of this shape and returns a list of the
   * remaining pieces of polyline. Pieces contained in the border of this shape are not cut out. */
  jrt::Array<Polyline*>* cutout(Polyline* polyline) override;

  /** Returns an array of tuples of integers. The length of the array is the number of points,
   * where polyline enters this shape. The array element [i][0] is the index of the polyline line
   * entering this shape and [i][1] is the index of the border line of this shape where it
   * enters. */
  virtual jrt::Array<jrt::Array<int32_t>*>* entrancePoints(Polyline* polyline);

  jrt::Array<TileShape*>* splitToConvex() override;

  /** Cuts this shape into count slices of equal extent across the longer side of its bounding
   * box. */
  virtual jrt::Array<TileShape*>* splitIntoSlices(int32_t count);

  /** Halvings allowed per convex shape in splitToTightOctagons: at most 4096 pieces. */
  static constexpr int32_t MAX_TIGHT_OCTAGON_SPLIT_DEPTH = 12;

  /** Cuts each convex shape until the bounding octagon of every piece exceeds the piece by at
   * most tolerance. */
  static jrt::Array<TileShape*>* splitToTightOctagons(jrt::Array<TileShape*>* convexShapes,
                                                      double tolerance);

 private:
  static void addTightOctagonPieces(TileShape* shape, double tolerance, int32_t depth,
                                    jrt::Collection<TileShape*>* pieces);

 public:
  /** Returns how far the bounding octagon of this shape reaches beyond it: the largest distance
   * from a corner of the octagon to this shape. */
  virtual double boundingOctagonExcess();

  /** Divides this shape into sections with width and height at most maxSectionWidth of about
   * equal size. */
  virtual jrt::Array<TileShape*>* divideIntoSections(double maxSectionWidth);

  /** Checks, if lineSegment has a common point with the interior of this shape. */
  virtual bool isIntersectedInteriorBy(LineSegment* lineSegment);

  /** Checks if the line segment defined by startPoint, endPoint and line has a common point with
   * the interior of this shape. */
  virtual bool isIntersectedInteriorBy(Point* startPoint, Point* endPoint, Line* line);

  /** Auxiliary function to implement cutout(TileShape shape). */
  virtual jrt::Array<TileShape*>* cutoutFrom(IntBox* shape) = 0;
  virtual jrt::Array<TileShape*>* cutoutFrom(IntOctagon* shape) = 0;
  virtual jrt::Array<TileShape*>* cutoutFrom(Simplex* shape) = 0;
};

}  // namespace fr
