#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Changed-area marking, route optimization and pull-tight operations of a RoutingBoard.
 * (Java package-private methods are public in C++.)
 */
class RoutingBoardOperations final : public virtual jrt::Object {
 private:
  static constexpr int32_t PULL_TIGHT_TIME_LIMIT = 2000;

  RoutingBoard* board;

 public:
  explicit RoutingBoardOperations(RoutingBoard* board) : board(board) {}

  void startMarkingChangedArea();

  void joinChangedArea(FloatPoint* point, int32_t layer);

  void markAllChangedArea();

  void optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape, int32_t accuracy,
                      jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                      Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                      int32_t keepPointLayer);

  bool removeItemsAndPullTight(jrt::Collection<Item*>* itemList, int32_t tidyWidth,
                               int32_t pullTightAccuracy);
};

}  // namespace fr
