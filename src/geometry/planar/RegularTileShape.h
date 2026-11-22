#pragma once

#include "fwd.h"
#include "geometry/planar/TileShape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * TileShapes whose border lines may have only directions out of a fixed set, as for example
 * orthogonal directions, which define axis parallel box shapes (sealed: IntBox, IntOctagon).
 * PORT NOTE: Java `union` is a C++ keyword -> `union_`; package-private abstract (double dispatch)
 * methods are public in C++.
 */
class RegularTileShape : public TileShape {
 public:
  using TileShape::contains;

  /** Compares the edgelines of index edgeIndex of this regular TileShape and other. returns
   * Side.ON_THE_LEFT, if the edgeline of this simplex is to the left of the edgeline of other;
   * Side.COLLINEAR, if the edlines are equal, and Side.ON_THE_RIGHT, if this edgeline is to the
   * right of the edgeline of other. */
  virtual Side* compare(RegularTileShape* other, int32_t edgeIndex) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual Side* compare(IntBox* other, int32_t edgeIndex) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual Side* compare(IntOctagon* other, int32_t edgeIndex) = 0;

  /** Calculates the smallest RegularTileShape containing this shape and other. */
  virtual RegularTileShape* union_(RegularTileShape* other) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual RegularTileShape* union_(IntBox* other) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual RegularTileShape* union_(IntOctagon* other) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual bool contains(RegularTileShape* other) = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  bool isContainedIn(IntBox* other) override = 0;

  /** Auxiliary function to implement the same function with parameter type RegularTileShape. */
  virtual bool isContainedIn(IntOctagon* other) = 0;
};

}  // namespace fr
