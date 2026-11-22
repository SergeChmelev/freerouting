#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Record SortedItem(Item item, double projection), ordered by projection. */
class MoveComponent_SortedItem : public virtual jrt::Object {
 public:
  MoveComponent_SortedItem(Item* item, double projection) : item_(item), projection_(projection) {}
  Item* item() { return item_; }
  double projection() { return projection_; }
  int32_t compareTo(MoveComponent_SortedItem* other);

 private:
  Item* item_;
  double projection_;
};

/**
 * Moves a single item or all items of a component by a vector, shoving the surrounding traces and
 * vias out of the way (used by interactive dragging). Port of board.actions.MoveComponent (not
 * part of the headless closure; added for the interactive front end).
 */
class MoveComponent : public virtual jrt::Object {
 public:
  MoveComponent(Item* item, Vector* translateVector, int32_t maxRecursionDepth,
                int32_t maxViaRecursionDepth);

  /** Minimum width of the drill items of the group that need the drill item mover, or -1. */
  double getMinDrillItemWidthWithTraces();
  /** Checks whether the move is possible (shoving traces and vias if necessary). */
  bool check();
  /** Performs the move; check() must have been called before. */
  bool insert(int32_t tidyWidth, int32_t pullTightAccuracy);

 private:
  static constexpr int32_t PULL_TIGHT_TIME_LIMIT = 1000;
  static constexpr int32_t CHECK_TIME_LIMIT = 3000;

  static bool hasConnectingTraces(DrillItem* drillItem);
  static bool requiresDrillItemMover(DrillItem* drillItem);

  Vector* translateVector;
  int32_t maxRecursionDepth;
  int32_t maxViaRecursionDepth;
  RoutingBoard* board = nullptr;
  jrt::Array<MoveComponent_SortedItem*>* itemGroupArr = nullptr;
  jrt::Set<Item*>* itemGroupSet = nullptr;
  jrt::Set<Item*>* itemsToShove = nullptr;
  bool allItemsMovable = true;
  Component* component = nullptr;
};

}  // namespace fr
