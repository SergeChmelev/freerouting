#include "autoroute/maze/MazeListElement.h"

#include "autoroute/expansion/ExpandableObject.h"

namespace fr {

int32_t MazeListElement::compareTo(MazeListElement* other) {
  if (this->sortingValue < other->sortingValue) {
    return -1;
  }
  if (this->sortingValue > other->sortingValue) {
    return 1;
  }
  // Tie-break 1: expansionValue
  if (this->expansionValue < other->expansionValue) {
    return -1;
  }
  if (this->expansionValue > other->expansionValue) {
    return 1;
  }
  // Tie-break 2: door id
  int32_t id1 = this->door->getId();
  int32_t id2 = other->door->getId();
  if (id1 < id2) {
    return -1;
  }
  if (id1 > id2) {
    return 1;
  }
  // Tie-break 3: sectionIndex
  if (this->sectionNoOfDoor < other->sectionNoOfDoor) {
    return -1;
  }
  if (this->sectionNoOfDoor > other->sectionNoOfDoor) {
    return 1;
  }
  // If truly equal (same door, same section, same values), return 0 to avoid duplicates in the
  // set.
  return 0;
}

}  // namespace fr
