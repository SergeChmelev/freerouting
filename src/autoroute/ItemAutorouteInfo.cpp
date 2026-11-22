#include "autoroute/ItemAutorouteInfo.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/path/Connection.h"
#include "board/model/items/Item.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "logger/FRLogger.h"

namespace fr {

ObstacleExpansionRoom* ItemAutorouteInfo::getExpansionRoom(int32_t index, ShapeSearchTree* autorouteTree) {
  int32_t currentShapeCount = this->item->treeShapeCount(autorouteTree);

  if (expansionRoomArr == nullptr) {
    expansionRoomArr = new (jrt::kOp) jrt::Array<ObstacleExpansionRoom*>(currentShapeCount);
  } else if (expansionRoomArr->length != currentShapeCount) {
    // Item's tree shape count has changed (e.g., trace modified during routing)
    // Resize the array and preserve existing rooms
    jrt::Array<ObstacleExpansionRoom*>* newArray = new (jrt::kOp) jrt::Array<ObstacleExpansionRoom*>(currentShapeCount);
    int32_t copyLength = jrt::Math::min(expansionRoomArr->length, currentShapeCount);
    jrt::arraycopy(expansionRoomArr, 0, newArray, 0, copyLength);
    expansionRoomArr = newArray;
  }

  if (index < 0 || index >= expansionRoomArr->length) {
    FRLogger::warn("ItemAutorouteInfo.get_expansion_room: index " + jrt::str(index) + " out of range [0, " +
                   jrt::str(expansionRoomArr->length) + ")");
    return nullptr;
  }
  if ((*expansionRoomArr)[index] == nullptr) {
    (*expansionRoomArr)[index] = new (jrt::kOp) ObstacleExpansionRoom(this->item, index, autorouteTree);
  }
  return (*expansionRoomArr)[index];
}

void ItemAutorouteInfo::resetDoors() {
  if (expansionRoomArr != nullptr) {
    for (ObstacleExpansionRoom* currentRoom : *expansionRoomArr) {
      if (currentRoom != nullptr) {
        currentRoom->resetDoors();
      }
    }
  }
}

void ItemAutorouteInfo::emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink == nullptr || intensity <= 0 || expansionRoomArr == nullptr) {
    return;
  }
  for (ObstacleExpansionRoom* currentRoom : *expansionRoomArr) {
    if (currentRoom != nullptr) {
      currentRoom->emitDiagnostic(sink, intensity);
    }
  }
}

}  // namespace fr
