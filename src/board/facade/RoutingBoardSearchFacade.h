#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Trace-segment checks, move/net-change checks and nearest routing item picking of a
 * RoutingBoard. (Java package-private methods are public in C++.)
 */
class RoutingBoardSearchFacade final : public virtual jrt::Object {
 private:
  RoutingBoard* board;

 public:
  explicit RoutingBoardSearchFacade(RoutingBoard* board) : board(board) {}

  double checkTraceSegment(Point* fromPoint, Point* toPoint, int32_t layer,
                           jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                           int32_t clClassNo, bool onlyNotShovableObstacles);

  double checkTraceSegment(LineSegment* lineSegment, int32_t layer,
                           jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                           int32_t clClassNo, bool onlyNotShovableObstacles);

  bool checkMoveItem(Item* item, Vector* vector, jrt::Collection<Item*>* ignoreItems);

  bool checkChangeNet(Item* item, int32_t newNetNo);

  Item* pickNearestRoutingItem(Point* location, int32_t layer, Item* fromItem);
};

}  // namespace fr
