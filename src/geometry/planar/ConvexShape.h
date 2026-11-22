#pragma once

#include "fwd.h"
#include "geometry/planar/Shape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A shape is defined as convex, if for each line segment with both endpoints contained in the
 * shape the whole segment is contained completely in the shape.
 */
class ConvexShape : public virtual Shape {
 public:
  /** Calculates the offset shape by distance. If distance > 0, the shape will be enlarged, else
   * the result shape will be smaller. */
  virtual ConvexShape* offset(double distance) = 0;

  /** Shrinks the shape by offset. The result shape will not be empty. */
  virtual ConvexShape* shrink(double offset) = 0;

  /** Returns the maximum diameter of the shape. */
  virtual double maxWidth() = 0;

  /** Returns the minimum diameter of the shape. */
  virtual double minWidth() = 0;
};

}  // namespace fr
