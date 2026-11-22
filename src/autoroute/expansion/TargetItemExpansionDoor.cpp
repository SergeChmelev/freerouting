#include "autoroute/expansion/TargetItemExpansionDoor.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/maze/MazeSearchElement.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"

namespace fr {

TargetItemExpansionDoor::TargetItemExpansionDoor(Item* item, int32_t treeEntryNo, CompleteExpansionRoom* room,
                                                 ShapeSearchTree* searchTree) {
  this->item = item;
  this->treeEntryNo = treeEntryNo;
  this->room = room;
  if (room == nullptr) {
    this->shape = Simplex::EMPTY();
  } else {
    TileShape* itemShape = item->getTreeShape(searchTree, treeEntryNo);
    this->shape = itemShape->intersection(room->getShape());
  }
  mazeSearchInfo = new MazeSearchElement();
}

bool TargetItemExpansionDoor::isDestinationDoor() {
  ItemAutorouteInfo* itemInfo = this->item->getAutorouteInfo();
  if (dynamic_cast<ConductionArea*>(this->item) != nullptr) {
    return itemInfo->isDestinationInfo();
  }
  return !itemInfo->isStartInfo();
}

bool TargetItemExpansionDoor::isStartDoor() {
  ItemAutorouteInfo* itemInfo = this->item->getAutorouteInfo();
  return itemInfo->isStartInfo();
}

void TargetItemExpansionDoor::reset() { mazeSearchInfo->reset(); }

int32_t TargetItemExpansionDoor::getId() {
  // Unique ID for a target door: hash of target item ID and room ID.
  return 31 * item->getId() + (room != nullptr ? room->getId() : 0);
}

}  // namespace fr
