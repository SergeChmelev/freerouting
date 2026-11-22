#pragma once

#include "board/optimize/TraceTightener.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Pull tight for boards without trace angle restriction. */
class TraceTightenerAnyAngle : public TraceTightener {
 private:
  static constexpr double SKIP_LENGTH = 10.0;

 public:
  /** (Java package-private) */
  TraceTightenerAnyAngle(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                         Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                         int32_t keepPointLayer)
      : TraceTightener(board, onlyNetNoArr, stoppableThread, timeLimit, keepPoint,
                       keepPointLayer) {}

  // Keep the 6-argument base overload visible (C++ name hiding).
  using TraceTightener::pullTight;

  Polyline* pullTight(Polyline* polyline) override;

 private:
  Polyline* reduceCorners(Polyline* polyline);

  Polyline* smoothenCorners(Polyline* polyline);

 public:
  Polyline* repositionLines(Polyline* polyline) override;

 private:
  Polyline* reduceLines(Polyline* polyline);

  Line* smoothenCorner(jrt::Array<Line*>* lines, int32_t startNo);

 protected:
  Line* repositionLine(jrt::Array<Line*>* lines, int32_t startNo) override;

 private:
  Polyline* skipLines(Polyline* polyline);

 public:
  Polyline* smoothenStartCornerAtTrace(PolylineTrace* trace) override;

  Polyline* smoothenEndCornerAtTrace(PolylineTrace* trace) override;
};

}  // namespace fr
