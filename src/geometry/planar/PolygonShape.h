#pragma once

#include "fwd.h"
#include "geometry/planar/PolylineShape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Java private inner class PolygonShape.DivisionPoint (hoisted; holds the outer instance).
 * At a concave corner of the closed polygon, a minimal axis parallel division line is
 * constructed, to divide the closed polygon into two.
 */
class PolygonShape_DivisionPoint : public virtual jrt::Object {
 public:
  PolygonShape* outer;
  int32_t cornerNoAfterProjection;
  FloatPoint* projection;

  /** At a concave corner of the closed polygon, a minimal axis parallel division line is
   * constructed, to divide the closed polygon into two. */
  PolygonShape_DivisionPoint(PolygonShape* outer, int32_t concaveCornerNo);
};

/**
 * Shape described bei a closed polygon of corner points. The corners are ordered in counterclock
 * sense around the border of the shape. The corners are normalized, so that the corner with the
 * lowest y-value comes first. In case of equal y-value the corner with the lowest x-value comes
 * first.
 */
class PolygonShape : public PolylineShape {
  friend class PolygonShape_DivisionPoint;

 private:
  static constexpr int32_t seed = 99;
  /** PORT NOTE: Java ThreadLocal<Random> seeded with `seed` -> single (single-threaded) static
   * Random accessor. */
  static jrt::Random* randomGenerator();

 public:
  using PolylineShape::intersects;

  jrt::Array<Point*>* corners;

 private:
  /** Stores precalculated data for this polygon shape. */
  IntBox* precalculatedBoundingBox = nullptr;
  IntOctagon* precalculatedBoundingOctagon = nullptr;
  jrt::Array<TileShape*>* precalculatedConvexPieces = nullptr;

 public:
  /** Creates a new instance of PolygonShape. */
  explicit PolygonShape(Polygon* polygon);

  /** Creates a polygon shape from an array of corner points. */
  explicit PolygonShape(jrt::Array<Point*>* corners);

  Point* corner(int32_t no) override;
  int32_t borderLineCount() override;
  bool cornerIsBounded(int32_t no) override;
  bool intersects(Shape* shape) override;
  bool intersects(Circle* circle) override;
  bool intersects(Simplex* simplex) override;
  bool intersects(IntOctagon* oct) override;
  bool intersects(IntBox* box) override;
  jrt::Array<Polyline*>* cutout(Polyline* polyline) override;
  PolygonShape* enlarge(double offset) override;
  double borderDistance(FloatPoint* point) override;
  double smallestRadius() override;
  bool contains(FloatPoint* point) override;
  bool contains(Point* point) override;
  bool containsInside(Point* point) override;
  bool isOutside(Point* point) override;

  /** Returns true if point lies on an edge of this polygon, its corners included. A hole of a
   * PolylineArea is a PolygonShape, so this decides hole-border points there. */
  bool containsOnBorder(Point* point) override;

  double distance(FloatPoint* point) override;
  PolygonShape* translateBy(Vector* vector) override;
  RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) override;
  IntBox* boundingBox() override;
  IntOctagon* boundingOctagon() override;

  /** Checks, if every line segment between 2 points of the shape is contained completely in the
   * shape. */
  virtual bool isConvex();

  /** Returns the convex hull of this polygon shape. */
  virtual PolygonShape* convexHull();

  TileShape* boundingTile() override;
  double area() override;
  int32_t dimension() override;
  bool isBounded() override;
  bool isEmpty() override;
  Line* borderLine(int32_t no) override;
  FloatPoint* nearestPointApprox(FloatPoint* fromPoint) override;
  PolygonShape* turn90Degree(int32_t factor, IntPoint* pole) override;
  PolygonShape* rotateApprox(double angle, FloatPoint* pole) override;
  PolygonShape* mirrorVertical(IntPoint* pole) override;
  PolygonShape* mirrorHorizontal(IntPoint* pole) override;

  /** Takes the convex pieces of source, if source is split already and has the same corners, so
   * that this shape need not be split again. */
  virtual void reuseConvexPiecesOf(PolygonShape* source);

  /** Splits this polygon shape into convex pieces. The result is not exact, because rounded
   * intersections of lines are used in the result pieces. It can be made exact, if Polylines are
   * returned instead of Polygons. */
  jrt::Array<TileShape*>* splitToConvex() override;

 private:
  /** Private recursive part of split_to_convex. Returns a collection of polygon shape pieces. */
  jrt::Collection<PolygonShape*>* splitToConvexRecu();
};

}  // namespace fr
