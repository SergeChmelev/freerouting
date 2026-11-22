#pragma once

#include "fwd.h"
#include "geometry/planar/ConvexShape.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes functionality of a circle shape in the plane. */
class Circle : public virtual ConvexShape {
 public:
  IntPoint* center;
  int32_t radius;

  /** Creates a new instance of Circle. */
  Circle(IntPoint* center, int32_t radius);

  bool isEmpty() override;
  bool isBounded() override;
  int32_t dimension() override;
  double circumference() override;
  double area() override;
  FloatPoint* centreOfGravity() override;
  bool isOutside(Point* point) override;
  bool contains(Point* point) override;
  bool contains(FloatPoint* point) override;
  bool containsInside(Point* point) override;
  bool containsOnBorder(Point* point) override;
  double distance(FloatPoint* point) override;
  double smallestRadius() override;
  IntBox* boundingBox() override;
  IntOctagon* boundingOctagon() override;
  TileShape* boundingTile() override;

  /** Creates a bounding tile shape around this circle, so that the length of the line segments of
   * the tile is at most maxSegmentLength. */
  virtual TileShape* boundingTile(int32_t maxSegmentLength);

  /** Creates a bounding tile shape around this circle with quadrantDivisionCount divisions per
   * quadrant. */
  virtual TileShape* boundingTileWithDivisions(int32_t quadrantDivisionCount);

  bool isContainedIn(IntBox* box) override;
  Circle* turn90Degree(int32_t factor, IntPoint* pole) override;
  Circle* rotateApprox(double angle, FloatPoint* pole) override;
  Circle* mirrorVertical(IntPoint* pole) override;
  Circle* mirrorHorizontal(IntPoint* pole) override;
  double maxWidth() override;
  double minWidth() override;
  RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) override;
  Circle* offset(double offset) override;
  Circle* shrink(double offset) override;
  Circle* translateBy(Vector* vector) override;
  FloatPoint* nearestPointApprox(FloatPoint* point) override;
  double borderDistance(FloatPoint* point) override;
  Circle* enlarge(double offset) override;
  bool intersects(Shape* other) override;
  bool intersects(Circle* other) override;
  bool intersects(IntBox* box) override;
  bool intersects(IntOctagon* oct) override;
  bool intersects(Simplex* simplex) override;
  jrt::Array<Polyline*>* cutout(Polyline* polyline) override;
  jrt::Array<TileShape*>* splitToConvex() override;
  Circle* getBorder() override { return this; }
  jrt::Array<Shape*>* getHoles() override;
  jrt::Array<FloatPoint*>* cornerApproxArr() override;

  /** Java: toString(Locale.ENGLISH). PORT NOTE: java.util.Locale is not ported; the
   * toString(Locale) overload is dropped (always English formatting). */
  std::string toString() override;
};

}  // namespace fr
