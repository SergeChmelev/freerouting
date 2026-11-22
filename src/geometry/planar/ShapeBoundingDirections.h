#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Describing the functionality for the fixed directions of a RegularTileShape.
 */
class ShapeBoundingDirections : public virtual jrt::Object {
 public:
  /** Returns the count of the fixed directions. */
  virtual int32_t count() = 0;

  /** Calculates for an arbitrary ConvexShape a surrounding RegularTileShape with this fixed
   * directions. Is used in the implementation of the search trees. */
  virtual RegularTileShape* bounds(ConvexShape* shape) = 0;

  /** Auxiliary function to implement the same function with parameter type ConvexShape. */
  virtual RegularTileShape* bounds(IntBox* box) = 0;

  /** Auxiliary function to implement the same function with parameter type ConvexShape. */
  virtual RegularTileShape* bounds(IntOctagon* oct) = 0;

  /** Auxiliary function to implement the same function with parameter type ConvexShape. */
  virtual RegularTileShape* bounds(Simplex* simplex) = 0;

  /** Auxiliary function to implement the same function with parameter type ConvexShape. */
  virtual RegularTileShape* bounds(Circle* circle) = 0;

  /** Auxiliary function to implement the same function with parameter type ConvexShape. */
  virtual RegularTileShape* bounds(PolygonShape* polygon) = 0;
};

}  // namespace fr
