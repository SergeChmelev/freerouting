#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Connections the design itself blocks. A pin is blocked when, on every layer it has copper, fixed
 * copper (a pin, a fixed trace or via, a conduction area) of another net or of no net overlaps it
 * and comes within clearance of its centre, where traces attach.
 *
 * <p>Ported from freerouting.net (Erik Gavriluk, Experimental Television Center).
 */
class DesignBlocks final : public virtual jrt::Object {
 private:
  DesignBlocks() = default;

 public:
  /** Whether every copper layer of pin overlaps foreign fixed copper, or the rules check proved
   * that no practical trace can reach it. */
  static bool isBlocked(Pin* pin);

  /** Whether the rules check proved that no practical trace can reach pin. */
  static bool isRuleBlocked(Pin* pin);

  /** The foreign fixed items pin overlaps, or null when some layer of the pin is free of them. */
  static jrt::List<Item*>* blockingItems(Pin* pin);

  /** A connected set of pins only (no traces, vias or areas), every one of them blocked. */
  static bool isBlockedSet(jrt::Collection<Item*>* connectedSet);

  /** Whether the open connections of item's connected set on netNumber are all blocked by the
   * design: the set itself is blocked, or every other connected set of the net is. */
  static bool connectionsBlocked(BasicBoard* board, Item* item, int32_t netNumber);

  /** The airlines (open connections) with a blocked end. */
  static jrt::List<AirLine*>* blockedAirlines(jrt::Collection<AirLine*>* airlines);

 private:
  static bool endBlocked(Item* item, int32_t netNumber);

  static bool coversCentre(Pin* pin, Item* other, int32_t layer, double clearance);

  static bool isCopper(Item* item);
};

}  // namespace fr
