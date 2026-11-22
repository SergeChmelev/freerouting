#pragma once

#include "fwd.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements the abstract class ShapeDirections as the 4 orthogonal directions. The class is a
 * singleton with the only instantiation INSTANCE.
 */
class OrthogonalBoundingDirections final : public virtual ShapeBoundingDirections {
 public:
  /** The one and only instantiation. PORT NOTE: static field -> accessor. */
  static OrthogonalBoundingDirections* INSTANCE();

  int32_t count() override { return 4; }

  RegularTileShape* bounds(ConvexShape* shape) override;
  RegularTileShape* bounds(IntBox* box) override;
  RegularTileShape* bounds(IntOctagon* oct) override;
  RegularTileShape* bounds(Simplex* simplex) override;
  RegularTileShape* bounds(Circle* circle) override;
  RegularTileShape* bounds(PolygonShape* polygon) override;

 private:
  /** Prevent instantiation. */
  OrthogonalBoundingDirections() {}
};

}  // namespace fr
