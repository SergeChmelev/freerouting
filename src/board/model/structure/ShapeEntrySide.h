// Port of app.freerouting.board.model.structure.ShapeEntrySide
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The side of a tile shape where a polyline or line segment enters it. */
class ShapeEntrySide : public virtual jrt::Object {
 public:
  /** Not yet calculated entry side. */
  static ShapeEntrySide* NOT_CALCULATED();

  /** Calculates the edge of shape, where polyline enters before corner no. */
  ShapeEntrySide(Polyline* polyline, int32_t no, TileShape* shape);

  /** Calculates the entry side as the border line of shape nearest to fromPoint. */
  ShapeEntrySide(Point* fromPoint, TileShape* shape);

  /** Calculates the entry side for shoving lineSegment to the left or right through shape. */
  ShapeEntrySide(LineSegment* lineSegment, TileShape* shape, bool shoveToTheLeft);

  ShapeEntrySide(int32_t no, FloatPoint* borderIntersection)
      : no(no), borderIntersection(borderIntersection) {}

  /** The index of the entry edge (final in Java; assigned in the constructor bodies). */
  int32_t no = -1;
  FloatPoint* borderIntersection = nullptr;
};

}  // namespace fr
