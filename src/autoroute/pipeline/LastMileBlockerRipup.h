#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Removes a bounded set of unfixed foreign traces and vias that overlap the bounding boxes of the
 * remaining airlines. Used when the autorouter has only a few incomplete connections and the score
 * has stopped improving.
 */
class LastMileBlockerRipup : public virtual jrt::Object {
 public:
  static constexpr int32_t MAX_ITEMS = 24;

 private:
  LastMileBlockerRipup() = default;

 public:
  /** Rips blockers for the current incomplete airlines. Returns the number of items removed. */
  static int32_t ripBlockers(RoutingBoard* board);
  /**
   * Rips blockers for the current incomplete airlines, respecting nets kept away by the rip-up
   * memory. Returns the number of items removed.
   */
  static int32_t ripBlockers(RoutingBoard* board, RipupMemory* memory);
  static bool isRemovableBlocker(Item* item, int32_t airlineNetNumber);
  static bool isRemovableBlocker(Item* item, int32_t airlineNetNumber, RipupMemory* memory);
  static IntBox* corridor(FloatPoint* from, FloatPoint* to);
};

}  // namespace fr
