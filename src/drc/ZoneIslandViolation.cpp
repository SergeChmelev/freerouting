// Port of app.freerouting.drc.ZoneIslandViolation
#include "drc/ZoneIslandViolation.h"

#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "geometry/planar/PlanarArea.h"

namespace fr {

ZoneIslandViolation::ZoneIslandViolation(ConductionArea* conductionArea, PlanarArea* islandArea,
                                         int32_t layer, int32_t netNumber,
                                         const std::string& type,
                                         jrt::List<Item*>* itemsInIsland)
    : conductionArea(conductionArea),
      islandArea(islandArea),
      layer(layer),
      netNumber(netNumber),
      type(type) {
  // Java: unmodifiableList(new ArrayList<>(itemsInIsland)) or emptyList()
  this->itemsInIsland = itemsInIsland != nullptr ? new jrt::ArrayList<Item*>(itemsInIsland)
                                                 : new jrt::ArrayList<Item*>();
}

jrt::Array<double>* ZoneIslandViolation::getCenter() {
  if (islandArea == nullptr || islandArea->isEmpty()) {
    return jrt::Array<double>::of({0.0, 0.0});
  }
  return islandArea->getCenter();
}

double ZoneIslandViolation::getAreaInBoardUnits() {
  if (islandArea == nullptr || islandArea->isEmpty()) {
    return 0.0;
  }
  return islandArea->getArea();
}

}  // namespace fr
