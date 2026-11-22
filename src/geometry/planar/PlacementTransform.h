#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Affine package placement; quantization happens only after local and world transforms. */
class PlacementTransform final : public virtual jrt::Object {
 private:
  FloatPoint* location;
  double rotation;
  bool mirrored;
  bool rotateFirst;

 public:
  /** Creates a placement using the board's mirror/rotation convention. */
  PlacementTransform(FloatPoint* location, double rotation, bool mirrored, bool rotateFirst)
      : location(location), rotation(rotation), mirrored(mirrored), rotateFirst(rotateFirst) {}

  /** Applies only the linear part, for offsets and directions. */
  FloatPoint* direction(FloatPoint* point);

  /** Places a package-local point without rounding intermediate coordinates. */
  FloatPoint* point(FloatPoint* point);

  /** Exact permutations avoid trigonometric noise at quarter turns. */
  static FloatPoint* rotate(FloatPoint* point, double degrees);

  /** Places pad geometry, including asymmetric local offsets, on the board grid. */
  Shape* shape(Shape* shape, FloatPoint* offset, double pinRotation);

  /** Places a point of a pad, given relative to the pad's origin, as shape() places the pad's
   * geometry. */
  FloatPoint* padPoint(FloatPoint* point, FloatPoint* offset, double pinRotation);

  /** Places an outline or keepout while retaining its holes. */
  Area* area(Area* area);
};

}  // namespace fr
