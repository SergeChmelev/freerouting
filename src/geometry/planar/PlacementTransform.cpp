#include "geometry/planar/PlacementTransform.h"

#include <cmath>

#include "geometry/planar/Area.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"

namespace fr {

FloatPoint* PlacementTransform::direction(FloatPoint* point) {
  FloatPoint* result = point;
  if (mirrored && !rotateFirst) {
    result = new FloatPoint(-result->x, result->y);
  }
  result = rotate(result, rotation);
  if (mirrored && rotateFirst) {
    result = new FloatPoint(-result->x, result->y);
  }
  return result;
}

FloatPoint* PlacementTransform::point(FloatPoint* point) {
  FloatPoint* offset = direction(point);
  return new FloatPoint(location->x + offset->x, location->y + offset->y);
}

FloatPoint* PlacementTransform::rotate(FloatPoint* point, double degrees) {
  // Java double % -> fmod (same sign and value semantics).
  return std::fmod(degrees, 90.0) == 0
             ? point->turn90Degree(jrt::Math::floorMod(jrt::d2i(degrees / 90), 4),
                                   FloatPoint::ZERO())
             : point->rotate(jrt::Math::toRadians(degrees), FloatPoint::ZERO());
}

Shape* PlacementTransform::shape(Shape* shape, FloatPoint* offset, double pinRotation) {
  if (shape->isEmpty()) {
    return shape;
  }
  if (auto* circle = jrt::fast_cast<Circle>(shape)) {
    return new Circle(padPoint(circle->center->toFloat(), offset, pinRotation)->round(),
                      circle->radius);
  }
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  auto* placed = new jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*placed)[mirrored ? corners->length - 1 - i : i] =
        padPoint((*corners)[i], offset, pinRotation)->round();
  }
  if (jrt::fast_cast<TileShape>(shape) != nullptr) {
    // Arrays.stream(placed).distinct(): keeps the first occurrence of equal points, in order.
    std::vector<Point*> uniqueVec;
    for (Point* p : *placed) {
      bool found = false;
      for (Point* q : uniqueVec) {
        if (q->equals(p)) {
          found = true;
          break;
        }
      }
      if (!found) {
        uniqueVec.push_back(p);
      }
    }
    auto* unique = new jrt::Array<Point*>(uniqueVec);
    jrt::Array<Point*>* normalized = unique->length < 3 ? unique : (new PolygonShape(unique))->corners;
    switch (normalized->length) {
      case 0: return IntBox::EMPTY();
      case 1: return TileShape::getInstance((*normalized)[0]);
      case 2: return (new LineSegment(new Polyline(normalized), 1))->toSimplex();
      default: return TileShape::getInstance(normalized);
    }
  }
  return new PolygonShape(placed);
}

FloatPoint* PlacementTransform::padPoint(FloatPoint* point, FloatPoint* offset,
                                         double pinRotation) {
  FloatPoint* rotated = rotate(point, pinRotation);
  return this->point(new FloatPoint(offset->x + rotated->x, offset->y + rotated->y));
}

Area* PlacementTransform::area(Area* area) {
  Shape* border = shape(area->getBorder(), FloatPoint::ZERO(), 0);
  jrt::Array<Shape*>* holes = area->getHoles();
  if (holes->length == 0) {
    return border;
  }
  auto* placedHoles = new jrt::Array<PolylineShape*>(holes->length);
  for (int32_t i = 0; i < holes->length; i++) {
    (*placedHoles)[i] = jrt::fast_cast<PolylineShape>(shape((*holes)[i], FloatPoint::ZERO(), 0));
  }
  return new PolylineArea(jrt::fast_cast<PolylineShape>(border), placedHoles);
}

}  // namespace fr
