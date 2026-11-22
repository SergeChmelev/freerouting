#include "autoroute/plane/PlaneOutlineSmoothing.h"

#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"

namespace fr {

using jrt::Math;

Area* PlaneOutlineSmoothing::smooth(Area* area, bool fillets, double distance) {
  if (distance <= 0) {
    return area;
  }
  PolylineShape* border = jrt::fast_cast<PolylineShape>(area->getBorder());
  if (border == nullptr) {
    return area;
  }
  jrt::Array<FloatPoint*>* corners = border->cornerApproxArr();
  if (corners == nullptr || corners->length < 3) {
    return area;
  }
  auto* result = new (jrt::kOp) jrt::ArrayList<IntPoint*>();
  int32_t n = corners->length;
  for (int32_t i = 0; i < n; i++) {
    FloatPoint* previous = (*corners)[(i + n - 1) % n];
    FloatPoint* corner = (*corners)[i];
    FloatPoint* next = (*corners)[(i + 1) % n];
    double lengthA = corner->distance(previous);
    double lengthB = corner->distance(next);
    if (lengthA <= 0 || lengthB <= 0) {
      continue;
    }
    double ax = (previous->x - corner->x) / lengthA;
    double ay = (previous->y - corner->y) / lengthA;
    double bx = (next->x - corner->x) / lengthB;
    double by = (next->y - corner->y) / lengthB;
    double cosine = Math::max(-1.0, Math::min(1.0, ax * bx + ay * by));
    double angle = Math::acos(cosine);
    if (angle < 1e-6 || Math::PI - angle < 1e-6) {
      // the two sides are parallel: nothing to smooth
      result->add(corner->round());
      continue;
    }
    double along;
    double radius = 0;
    if (fillets) {
      along = distance / Math::tan(angle / 2.0);
    } else {
      along = distance;
    }
    along = Math::min(along, 0.5 * Math::min(lengthA, lengthB));
    if (fillets) {
      radius = along * Math::tan(angle / 2.0);
    }
    FloatPoint* start = new (jrt::kGeom) FloatPoint(corner->x + ax * along, corner->y + ay * along);
    FloatPoint* end = new (jrt::kGeom) FloatPoint(corner->x + bx * along, corner->y + by * along);
    result->add(start->round());
    if (fillets && radius > 0) {
      double bisectorX = ax + bx;
      double bisectorY = ay + by;
      double bisector = Math::hypot(bisectorX, bisectorY);
      double centreDistance = radius / Math::sin(angle / 2.0);
      FloatPoint* centre = new (jrt::kGeom) FloatPoint(corner->x + bisectorX / bisector * centreDistance,
                                          corner->y + bisectorY / bisector * centreDistance);
      double from = Math::atan2(start->y - centre->y, start->x - centre->x);
      double to = Math::atan2(end->y - centre->y, end->x - centre->x);
      double sweep = to - from;
      while (sweep > Math::PI) {
        sweep -= 2 * Math::PI;
      }
      while (sweep < -Math::PI) {
        sweep += 2 * Math::PI;
      }
      double step = 2.0 * Math::acos(Math::max(0.0, 1.0 - MAX_ARC_ERROR / radius));
      int32_t segments = step > 0 ? Math::max(1, jrt::d2i(Math::ceil(Math::abs(sweep) / step))) : 1;
      for (int32_t k = 1; k < segments; k++) {
        double phi = from + sweep * k / segments;
        result->add((new (jrt::kGeom) FloatPoint(centre->x + radius * Math::cos(phi), centre->y + radius * Math::sin(phi)))->round());
      }
    }
    result->add(end->round());
  }
  // consecutive equal corners would make a degenerate side
  auto* points = new (jrt::kOp) jrt::ArrayList<Point*>();
  for (IntPoint* point : *result) {
    if (points->isEmpty() || !points->get(points->size() - 1)->equals(point)) {
      points->add(point);
    }
  }
  if (points->size() > 1 && points->get(0)->equals(points->get(points->size() - 1))) {
    points->removeAt(points->size() - 1);
  }
  if (points->size() < 3) {
    return area;
  }
  PolygonShape* smoothed = new (jrt::kGeom) PolygonShape(points->toArray());
  jrt::Array<Shape*>* holes = area->getHoles();
  auto* polylineHoles = new (jrt::kOp) jrt::ArrayList<PolylineShape*>();
  for (Shape* hole : *holes) {
    if (auto* polylineHole = jrt::fast_cast<PolylineShape>(hole)) {
      polylineHoles->add(polylineHole);
    }
  }
  return new (jrt::kGeom) PolylineArea(smoothed, polylineHoles->toArray());
}

}  // namespace fr
