#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"

#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "geometry/planar/TileShape.h"

namespace fr {

jrt::Collection<TargetItemExpansionDoor*>* IncompleteFreeSpaceExpansionRoom::getTargetDoors() {
  // Java: Collections.emptyList()
  return jrt::Collections::emptyList<TargetItemExpansionDoor*>();
}

int32_t IncompleteFreeSpaceExpansionRoom::getId() {
  // Stable hash of shape and layer
  return 31 * getShape()->getId() + getLayer();
}

}  // namespace fr
