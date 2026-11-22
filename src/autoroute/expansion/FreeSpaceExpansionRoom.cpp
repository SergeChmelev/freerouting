#include "autoroute/expansion/FreeSpaceExpansionRoom.h"

#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "geometry/planar/TileShape.h"

namespace fr {

FreeSpaceExpansionRoom::FreeSpaceExpansionRoom(TileShape* shape, int32_t layer) {
  this->shape = shape;
  this->layer = layer;
  doors = new (jrt::kOp) jrt::ArrayList<ExpansionDoor*>();  // ArrayList for better performance
}

void FreeSpaceExpansionRoom::addDoor(ExpansionDoor* door) { this->doors->add(door); }

void FreeSpaceExpansionRoom::clearDoors() {
  this->doors = new (jrt::kOp) jrt::ArrayList<ExpansionDoor*>();  // ArrayList for better performance
}

void FreeSpaceExpansionRoom::resetDoors() {
  for (ExpandableObject* currentDoor : *this->doors) {
    currentDoor->reset();
  }
}

bool FreeSpaceExpansionRoom::removeDoor(ExpandableObject* door) {
  // Java: doors.remove(Object) - an object that is no ExpansionDoor is never contained.
  ExpansionDoor* expansionDoor = jrt::fast_cast<ExpansionDoor>(door);
  if (expansionDoor == nullptr) {
    return false;
  }
  return this->doors->remove(expansionDoor);
}

bool FreeSpaceExpansionRoom::doorExists(ExpansionRoom* other) {
  if (doors == nullptr) {
    return false;
  }
  for (ExpansionDoor* currentDoor : *doors) {
    if (currentDoor->firstRoom == other || currentDoor->secondRoom == other) {
      return true;
    }
  }
  return false;
}

}  // namespace fr
