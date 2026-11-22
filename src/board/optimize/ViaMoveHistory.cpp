#include "board/optimize/ViaMoveHistory.h"

#include "board/model/items/Via.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"

namespace fr {

bool ViaMoveHistory::allowsMove(Via* via, Point* from, Point* to, int32_t accuracy) {
  ViaMoveHistory_Track* track = tracks->get(via->getId());
  if (track == nullptr || track->returns + 1 < RETURNS_FOR_CYCLE) {
    return true;
  }
  return !track->isReturn(from->toFloat(), to->toFloat(), accuracy);
}

void ViaMoveHistory::recordMove(Via* via, Point* from, Point* to, int32_t accuracy) {
  ViaMoveHistory_Track* track =
      tracks->computeIfAbsent(via->getId(), [](int32_t) { return new (jrt::kOp) ViaMoveHistory_Track(); });
  FloatPoint* fromPoint = from->toFloat();
  FloatPoint* toPoint = to->toFloat();
  if (track->locations->isEmpty()) {
    track->locations->add(fromPoint);
  }
  if (track->isReturn(fromPoint, toPoint, accuracy)) {
    ++track->returns;
  }
  track->locations->add(toPoint);
}

ViaMoveHistory_Track::ViaMoveHistory_Track() : locations(new (jrt::kOp) jrt::ArrayList<FloatPoint*>()) {}

bool ViaMoveHistory_Track::isReturn(FloatPoint* from, FloatPoint* to, int32_t accuracy) {
  if (to->distance(from) <= accuracy) {
    return false;
  }
  for (FloatPoint* location : *locations) {
    if (location->distance(to) <= accuracy && location->distance(from) > accuracy) {
      return true;
    }
  }
  return false;
}

}  // namespace fr
