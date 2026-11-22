#pragma once

#include "board/optimize/TraceTightener.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Pull tight for boards with 90 degree trace angle restriction. */
class TraceTightener90 : public TraceTightener {
 public:
  TraceTightener90(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                   Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                   int32_t keepPointLayer)
      : TraceTightener(board, onlyNetNoArr, stoppableThread, timeLimit, keepPoint,
                       keepPointLayer) {}

  // Keep the 6-argument base overload visible (C++ name hiding).
  using TraceTightener::pullTight;

  Polyline* pullTight(Polyline* polyline) override;

 private:
  Polyline* trySkipSecondCorner(Polyline* polyline);

  Polyline* trySkipCorners(Polyline* polyline);

 public:
  Polyline* smoothenStartCornerAtTrace(PolylineTrace* trace) override;

  Polyline* smoothenEndCornerAtTrace(PolylineTrace* trace) override;
};

}  // namespace fr
