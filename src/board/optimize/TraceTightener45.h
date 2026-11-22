#pragma once

#include "board/optimize/TraceTightener.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Pull tight for boards with 45 degree trace angle restriction. */
class TraceTightener45 : public TraceTightener {
 public:
  TraceTightener45(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                   Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                   int32_t keepPointLayer)
      : TraceTightener(board, onlyNetNoArr, stoppableThread, timeLimit, keepPoint,
                       keepPointLayer) {}

  // Keep the 6-argument base overload visible (C++ name hiding).
  using TraceTightener::pullTight;

  Polyline* pullTight(Polyline* polyline) override;

  /** (Java package-private; returns AngleRestriction.FORTYFIVE_DEGREE) */
  virtual AngleRestriction getAngleRestriction();

 private:
  Polyline* reduceCorners(Polyline* polyline);

  Polyline* smoothenCorners(Polyline* polyline);

  Line* smoothenSharpCorner(jrt::Array<Line*>* lines, int32_t no);

  Line* smoothenNonIntegerCorner(jrt::Array<Line*>* lines, int32_t no);

  Line* smoothenCorner(jrt::Array<Line*>* lines, int32_t no);

 public:
  Polyline* smoothenStartCornerAtTrace(PolylineTrace* trace) override;

  Polyline* smoothenEndCornerAtTrace(PolylineTrace* trace) override;
};

}  // namespace fr
