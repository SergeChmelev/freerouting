// Port of app.freerouting.board.trace.PolylineTraceGeometry (package-private final class).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Geometry-only operations used by PolylineTrace. This collaborator deliberately has no board or
 * trace state.
 */
class PolylineTraceGeometry final : public virtual jrt::Object {
 private:
  PolylineTraceGeometry() {}

 public:
  static Point* firstCorner(Polyline* lines);
  static Point* lastCorner(Polyline* lines);
  static int32_t cornerCount(Polyline* lines);
  static double length(Polyline* lines);
  static IntBox* boundingBox(Polyline* lines, int32_t halfWidth);
  static int32_t tileShapeCount(Polyline* lines);
  static Polyline* translate(Polyline* lines, Vector* vector);
  static Polyline* turn90Degree(Polyline* lines, int32_t factor, IntPoint* pole);
  static Polyline* rotateApprox(Polyline* lines, double angleInDegree, FloatPoint* pole);
  static Polyline* mirrorVertical(Polyline* lines, IntPoint* pole);
  static TileShape* connectionShape(Polyline* lines, int32_t index);
};

}  // namespace fr
