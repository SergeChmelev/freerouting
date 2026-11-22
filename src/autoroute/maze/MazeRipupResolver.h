#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Rip-up decisions of the maze search (Java: package-private final class). */
class MazeRipupResolver final : public virtual jrt::Object {
 private:
  static constexpr double FANOUT_COST_CONSTANT = 20000;

  MazeSearchEngine* search;

 public:
  explicit MazeRipupResolver(MazeSearchEngine* search) : search(search) {}

  /** Returns the additional cost factor for ripping the trace, if it is connected to a fanout
   * via, or 1 if no fanout via was found. */
  static double calcFanoutViaRipupCostFactor(Trace* trace);

  /** Checks if the obstacle item may be ripped; returns the ripup costs or -1. */
  int32_t checkRipup(MazeListElement* listElement, Item* obstacleItem, bool doorIsSmall);

  /** Returns false if listElement leaves a ripped item through a forbidden door. */
  bool checkLeavingRippedItem(MazeListElement* listElement);

 private:
  bool enterThroughSmallDoor(MazeListElement* listElement, Item* ignoreItem);
};

}  // namespace fr
