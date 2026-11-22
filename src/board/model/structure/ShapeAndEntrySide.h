// Port of app.freerouting.board.model.structure.ShapeAndEntrySide
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A tree shape of a trace together with the side where the trace enters it. */
class ShapeAndEntrySide : public virtual jrt::Object {
 public:
  /**
   * Calculates the shape with index of trace and its entry side. In inShoveCheck the fallback entry
   * side calculation is skipped.
   */
  ShapeAndEntrySide(PolylineTrace* trace, int32_t index, bool orthogonal, bool inShoveCheck);

  TileShape* shape;          // final
  ShapeEntrySide* fromSide;  // final

 private:
  static Line* calcCutlineAtEnd(int32_t index, PolylineTrace* trace);

  static Line* calcCutlineAtStart(int32_t index, PolylineTrace* trace);
};

}  // namespace fr
