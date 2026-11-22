#pragma once

#include "fwd.h"
#include "geometry/planar/Area.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Interface describing functionality for connected 2-dimensional shapes in the plane. A Shape
 * object is expected to be simply connected, that means, it may not contain holes.
 */
class Shape : public virtual Area {
 public:
  /** Returns the length of the border of this shape. If the shape is unbounded, Integer.MAX_VALUE
   * is returned. */
  virtual double circumference() = 0;

  /** Returns the content of the area of the shape. If the shape is unbounded, Double.MAX_VALUE is
   * returned. */
  virtual double area() = 0;

  /** Returns the gravity point of this shape. */
  virtual FloatPoint* centreOfGravity() = 0;

  /** Returns true, if point is not contained in the inside or the boundary of the shape. */
  virtual bool isOutside(Point* point) = 0;

  /** Returns true, if point is contained in this shape, but not on the border. */
  virtual bool containsInside(Point* point) = 0;

  /** Returns true, if point lies exact on the boundary of the shape. */
  virtual bool containsOnBorder(Point* point) = 0;

  /** Returns the distance between point and its nearest point on the shape. 0, if point is
   * contained in this shape. */
  virtual double distance(FloatPoint* point) = 0;

  /** Return a bounding TileShape of this shape. */
  virtual TileShape* boundingTile() = 0;

  /** Returns the bounding RegularTileShape with the fixed directions dirs. */
  virtual RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) = 0;

  /** Returns the distance between point and its nearest point on the border of the shape. */
  virtual double borderDistance(FloatPoint* point) = 0;

  /** Returns the smallest distance from the centre of gravity to the border of the shape. */
  virtual double smallestRadius() = 0;

  /** Returns the offset shape of this shape by offsetting the boundary by distance to the outside.
   * The result may be not exact. */
  virtual Shape* enlarge(double offset) = 0;

  /** Checks, if this shape and other have a nonempty intersection. */
  virtual bool intersects(Shape* other) = 0;

  /** Auxiliary function to implement the same function with parameter type Shape. */
  virtual bool intersects(IntBox* other) = 0;

  /** Auxiliary function to implement the same function with parameter type Shape. */
  virtual bool intersects(IntOctagon* other) = 0;

  /** Auxiliary function to implement the same function with parameter type Shape. */
  virtual bool intersects(Simplex* other) = 0;

  /** Auxiliary function to implement the same function with parameter type Shape. */
  virtual bool intersects(Circle* other) = 0;

  /** Cuts out the parts of polyline in the interior of this shape and returns a list of the
   * remaining pieces of polyline. Pieces contained in the border of this shape are not cut out. */
  virtual jrt::Array<Polyline*>* cutout(Polyline* polyline) = 0;
};

}  // namespace fr
