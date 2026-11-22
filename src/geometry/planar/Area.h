#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * An Area is a not necessarily simply connected Shape, which means, that it may contain holes. The
 * border and the holes of an Area are of class Shape.
 */
class Area : public virtual jrt::Object {
 public:
  /** Returns true if the area is empty. */
  virtual bool isEmpty() = 0;

  /** Returns true if the area is contained in a sufficiently large box. */
  virtual bool isBounded() = 0;

  /** Returns 2 if the area contains two-dimensional shapes, 1 if it contains curves, 0 if it is
   * reduced to a point and -1 if it is empty. */
  virtual int32_t dimension() = 0;

  /** Checks, if this area is completely contained in box. */
  virtual bool isContainedIn(IntBox* box) = 0;

  /** Returns the border shape of this area. */
  virtual Shape* getBorder() = 0;

  /** Returns the array of holes, of this area. */
  virtual jrt::Array<Shape*>* getHoles() = 0;

  /** Returns the smallest surrounding box of the area. If the area is not bounded, some
   * coordinates of the resulting box may be equal Integer.MAX_VALUE. */
  virtual IntBox* boundingBox() = 0;

  /** Returns the smallest surrounding octagon of the area. If the area is not bounded, some
   * coordinates of the resulting octagon may be equal Integer.MAX_VALUE. */
  virtual IntOctagon* boundingOctagon() = 0;

  /** Returns true, if point is contained in this area, but not inside a hole. Being on the border
   * is not defined for FloatPoints because of numerical inaccuracy. */
  virtual bool contains(FloatPoint* point) = 0;

  /** Returns true, if point is inside or on the border of this area, but not inside a hole. */
  virtual bool contains(Point* point) = 0;

  /** Calculates an approximation of the nearest point of the shape to fromPoint. */
  virtual FloatPoint* nearestPointApprox(FloatPoint* fromPoint) = 0;

  /** Turns this area by factor times 90 degree around pole. */
  virtual Area* turn90Degree(int32_t factor, IntPoint* pole) = 0;

  /** Rotates the area around pole by angle. The result may be not exact. */
  virtual Area* rotateApprox(double angle, FloatPoint* pole) = 0;

  /** Returns the affine translation of the area by vector. */
  virtual Area* translateBy(Vector* vector) = 0;

  /** Mirrors this area at the horizontal line through pole. */
  virtual Area* mirrorHorizontal(IntPoint* pole) = 0;

  /** Mirrors this area at the vertical line through pole. */
  virtual Area* mirrorVertical(IntPoint* pole) = 0;

  /** Returns an approximation of the corners of this area. */
  virtual jrt::Array<FloatPoint*>* cornerApproxArr() = 0;

  /** Returns a division of this area into convex pieces. */
  virtual jrt::Array<TileShape*>* splitToConvex() = 0;
};

}  // namespace fr
