// Port of app.freerouting.board.searchtree.ExactCopper
#include "board/searchtree/ExactCopper.h"

#include "core/library/PadOval.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"

namespace fr {

ExactCopper::ExactCopper(TileShape* polygon, FloatPoint* centre, double radius) {
  this->polygon = polygon;
  this->corners = polygon != nullptr ? polygon->cornerApproxArr() : nullptr;
  this->centre = centre;
  this->radius = radius;
}

ExactCopper::ExactCopper(jrt::Array<FloatPoint*>* segment, double radius) {
  this->polygon = nullptr;
  this->corners = segment;
  this->centre = (*segment)[0];
  this->radius = radius;
}

ExactCopper* ExactCopper::of(Shape* shape) { return of(shape, 0); }

ExactCopper* ExactCopper::of(Shape* shape, double grow) {
  if (auto* circle = jrt::fast_cast<Circle>(shape)) {
    return new (jrt::kOp) ExactCopper(static_cast<TileShape*>(nullptr), circle->center->toFloat(),
                           circle->radius + grow);
  }
  if (auto* tile = jrt::fast_cast<TileShape>(shape)) {
    if (!tile->isEmpty() && tile->isBounded() && tile->borderLineCount() > 0) {
      return new (jrt::kOp) ExactCopper(tile, FloatPoint::ZERO(), grow);
    }
  }
  return nullptr;
}

ExactCopper* ExactCopper::ofOval(PadOval* oval, double grow) {
  if (oval->start()->x == oval->end()->x && oval->start()->y == oval->end()->y) {
    return new (jrt::kOp) ExactCopper(static_cast<TileShape*>(nullptr), oval->start(), oval->radius() + grow);
  }
  return new (jrt::kOp) ExactCopper(jrt::Array<FloatPoint*>::of({oval->start(), oval->end()}),
                         oval->radius() + grow);
}

double ExactCopper::distance(ExactCopper* a, ExactCopper* b) {
  double core;
  if (a->corners == nullptr && b->corners == nullptr) {
    core = a->centre->distance(b->centre);
  } else if (a->corners == nullptr) {
    core = pointDistance(b->corners, a->centre);
  } else if (b->corners == nullptr) {
    core = pointDistance(a->corners, b->centre);
  } else if (a->polygon != nullptr && b->polygon != nullptr) {
    core = polygonDistance(a, b);
  } else {
    core = segmentDistance(a, b);
  }
  return jrt::Math::max(0.0, core - a->radius - b->radius);
}

double ExactCopper::segmentDistance(ExactCopper* a, ExactCopper* b) {
  ExactCopper* segment = a->polygon == nullptr ? a : b;
  ExactCopper* other = segment == a ? b : a;
  FloatPoint* start = (*segment->corners)[0];
  FloatPoint* end = (*segment->corners)[1];
  int32_t count = other->corners->length;
  FloatPoint* previous = (*other->corners)[count - 1];
  for (int32_t i = 0; i < count; i++) {
    FloatPoint* current = (*other->corners)[i];
    if (segmentsCross(start, end, previous, current)) {
      return 0;
    }
    previous = current;
  }
  double result = jrt::Double::MAX_VALUE;
  for (FloatPoint* corner : *segment->corners) {
    result = jrt::Math::min(result, pointDistance(other->corners, corner));
  }
  for (FloatPoint* corner : *other->corners) {
    result = jrt::Math::min(result, pointDistance(segment->corners, corner));
  }
  return result;
}

bool ExactCopper::segmentsCross(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2) {
  double d1 = cross(q1, q2, p1);
  double d2 = cross(q1, q2, p2);
  double d3 = cross(p1, p2, q1);
  double d4 = cross(p1, p2, q2);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

double ExactCopper::cross(FloatPoint* from, FloatPoint* to, FloatPoint* point) {
  return (to->x - from->x) * (point->y - from->y) - (to->y - from->y) * (point->x - from->x);
}

double ExactCopper::polygonDistance(ExactCopper* a, ExactCopper* b) {
  if (a->polygon->intersects(static_cast<Shape*>(b->polygon))) {
    return 0;
  }
  double result = jrt::Double::MAX_VALUE;
  for (FloatPoint* corner : *a->corners) {
    result = jrt::Math::min(result, pointDistance(b->corners, corner));
  }
  for (FloatPoint* corner : *b->corners) {
    result = jrt::Math::min(result, pointDistance(a->corners, corner));
  }
  return result;
}

double ExactCopper::pointDistance(jrt::Array<FloatPoint*>* corners, FloatPoint* point) {
  int32_t count = corners->length;
  if (count == 1) {
    return (*corners)[0]->distance(point);
  }
  bool left = false;
  bool right = false;
  double best = jrt::Double::MAX_VALUE;
  FloatPoint* previous = (*corners)[count - 1];
  for (int32_t i = 0; i < count; i++) {
    FloatPoint* current = (*corners)[i];
    double ex = current->x - previous->x;
    double ey = current->y - previous->y;
    double px = point->x - previous->x;
    double py = point->y - previous->y;
    double cross = ex * py - ey * px;
    if (cross > 0) {
      left = true;
    } else if (cross < 0) {
      right = true;
    }
    double lengthSquare = ex * ex + ey * ey;
    double t =
        lengthSquare > 0 ? jrt::Math::clamp((px * ex + py * ey) / lengthSquare, 0.0, 1.0) : 0;
    double dx = px - t * ex;
    double dy = py - t * ey;
    best = jrt::Math::min(best, jrt::Math::sqrt(dx * dx + dy * dy));
    previous = current;
  }
  return count <= 2 || (left && right) ? best : 0;
}

}  // namespace fr
