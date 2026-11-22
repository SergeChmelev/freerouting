#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Represents a DRC violation where a copper pour (conduction area) is fragmented into
 * disconnected islands, causing isolated pins or floating dead copper.
 */
class ZoneIslandViolation : public virtual jrt::Object {
 public:
  /** The parent conduction area (copper pour) that contains this island. */
  ConductionArea* conductionArea;

  /** The isolated polygonal island geometry in board units. */
  PlanarArea* islandArea;

  /** The board layer index where this island resides. */
  int32_t layer;

  /** The net number associated with this copper pour. */
  int32_t netNumber;

  /**
   * The violation classification: "isolated_island_unconnected" (island contains pins/vias
   * disconnected from the net's primary set) or "isolated_island_dead_copper" (no contacts).
   */
  std::string type;

  /** Items (e.g. pins or vias) located within this island (unmodifiable copy). */
  jrt::List<Item*>* itemsInIsland;

  /** Creates a new zone island violation. */
  ZoneIslandViolation(ConductionArea* conductionArea, PlanarArea* islandArea, int32_t layer,
                      int32_t netNumber, const std::string& type,
                      jrt::List<Item*>* itemsInIsland);

  /** Returns the center of the bounding box of this island in board units. */
  virtual jrt::Array<double>* getCenter();

  /** Returns the approximate area in square board units. */
  virtual double getAreaInBoardUnits();
};

}  // namespace fr
