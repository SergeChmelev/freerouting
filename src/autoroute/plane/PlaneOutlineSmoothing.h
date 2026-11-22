#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * The outline of a zone with its corners smoothed the way KiCad smooths it before the fill
 * (SHAPE_POLY_SET::Fillet and Chamfer): every corner of the border is replaced by an arc of the
 * given radius tangent to both sides (fillet) or cut at the given distance (chamfer).
 */
class PlaneOutlineSmoothing : public virtual jrt::Object {
 private:
  /** The largest distance of the chords of a fillet to its arc, in board units (5 um). */
  static constexpr double MAX_ARC_ERROR = 50.0;

  PlaneOutlineSmoothing() = default;

 public:
  /**
   * The area with the corners of its border smoothed, or the area itself if its border is not a
   * polygon. fillets: true for fillets, false for chamfers; distance: the radius / chamfer size.
   */
  static Area* smooth(Area* area, bool fillets, double distance);
};

}  // namespace fr
