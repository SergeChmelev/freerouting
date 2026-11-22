// Port of app.freerouting.board.trace.PolylineTraceGeometry.
#include "board/trace/PolylineTraceGeometry.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"

namespace fr {

Point* PolylineTraceGeometry::firstCorner(Polyline* lines) { return lines->corner(0); }

Point* PolylineTraceGeometry::lastCorner(Polyline* lines) {
  return lines->corner(lines->lines->length - 2);
}

int32_t PolylineTraceGeometry::cornerCount(Polyline* lines) { return lines->lines->length - 1; }

double PolylineTraceGeometry::length(Polyline* lines) { return lines->lengthApprox(); }

IntBox* PolylineTraceGeometry::boundingBox(Polyline* lines, int32_t halfWidth) {
  return lines->boundingBox()->offset(halfWidth);
}

int32_t PolylineTraceGeometry::tileShapeCount(Polyline* lines) {
  return jrt::Math::max(lines->lines->length - 2, 0);
}

Polyline* PolylineTraceGeometry::translate(Polyline* lines, Vector* vector) {
  return lines->translateBy(vector);
}

Polyline* PolylineTraceGeometry::turn90Degree(Polyline* lines, int32_t factor, IntPoint* pole) {
  return lines->turn90Degree(factor, pole);
}

Polyline* PolylineTraceGeometry::rotateApprox(Polyline* lines, double angleInDegree,
                                              FloatPoint* pole) {
  return lines->rotateApprox(jrt::Math::toRadians(angleInDegree), pole);
}

Polyline* PolylineTraceGeometry::mirrorVertical(Polyline* lines, IntPoint* pole) {
  return lines->mirrorVertical(pole);
}

TileShape* PolylineTraceGeometry::connectionShape(Polyline* lines, int32_t index) {
  LineSegment* currentLineSegment = new LineSegment(lines, index + 1);
  return currentLineSegment->toSimplex()->simplify();
}

}  // namespace fr
