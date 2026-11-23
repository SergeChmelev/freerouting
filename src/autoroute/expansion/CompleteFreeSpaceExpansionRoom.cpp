#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

CompleteFreeSpaceExpansionRoom::CompleteFreeSpaceExpansionRoom(TileShape* shape, int32_t layer, int32_t id)
    : FreeSpaceExpansionRoom(shape, layer) {
  targetDoors = new (jrt::kSearch) jrt::LinkedList<TargetItemExpansionDoor*>();
  this->id = id;
}

int32_t CompleteFreeSpaceExpansionRoom::compareTo(jrt::Object* other) {
  int32_t result;
  if (jrt::fast_cast<FreeSpaceExpansionRoom>(other) != nullptr) {
    auto* otherRoom = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(other);
    if (otherRoom == nullptr) {
      // Java: the cast throws a ClassCastException here (other is an incomplete room).
      throw jrt::ClassCastException("FreeSpaceExpansionRoom is no CompleteFreeSpaceExpansionRoom");
    }
    result = otherRoom->id - this->id;
  } else {
    result = -1;
  }
  return result;
}

void CompleteFreeSpaceExpansionRoom::removeFromTree(ShapeTree* shapeTree) { shapeTree->remove(this->treeEntries); }

void CompleteFreeSpaceExpansionRoom::addTargetDoor(TargetItemExpansionDoor* door) { this->targetDoors->add(door); }

bool CompleteFreeSpaceExpansionRoom::removeDoor(ExpandableObject* door) {
  bool result;
  if (auto* targetDoor = jrt::fast_cast<TargetItemExpansionDoor>(door)) {
    result = this->targetDoors->remove(targetDoor);
  } else {
    result = FreeSpaceExpansionRoom::removeDoor(door);
  }
  return result;
}

void CompleteFreeSpaceExpansionRoom::calculateTargetDoors(ShapeTree_TreeEntry* ownNetObject, int32_t netNumber,
                                                          ShapeSearchTree* autorouteSearchTree) {
  this->setNetDependent();

  if (auto* currentObject = jrt::fast_cast<Connectable>(ownNetObject->object)) {
    if (currentObject->containsNet(netNumber)) {
      TileShape* currentConnectionShape =
          currentObject->getTraceConnectionShape(autorouteSearchTree, ownNetObject->shapeIndexInObject);
      if (currentConnectionShape != nullptr && this->getShape()->intersects(currentConnectionShape)) {
        Item* currentItem = jrt::fast_cast<Item>(currentObject);
        TargetItemExpansionDoor* newTargetDoor =
            new (jrt::kSearch) TargetItemExpansionDoor(currentItem, ownNetObject->shapeIndexInObject, this, autorouteSearchTree);
        this->addTargetDoor(newTargetDoor);
      }
    }
  }
}

void CompleteFreeSpaceExpansionRoom::emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink != nullptr && intensity > 0) {
    sink->accept(new (jrt::kOp) AutorouteDiagnostic(AutorouteDiagnostic_Kind::FREE_SPACE_ROOM, this->getShape(),
                                         this->getLayer(), intensity));
  }
}

bool CompleteFreeSpaceExpansionRoom::validate(AutorouteEngine* autorouteEngine) {
  bool result = true;
  jrt::Collection<ShapeTree_TreeEntry*>* overlappingObjects = new (jrt::kOp) jrt::LinkedList<ShapeTree_TreeEntry*>();
  jrt::Array<int32_t>* netNumbers = new (jrt::kOp) jrt::Array<int32_t>(1);
  (*netNumbers)[0] = autorouteEngine->getNetNumber();
  autorouteEngine->autorouteSearchTree->overlappingTreeEntries(this->getShape(), this->getLayer(), netNumbers,
                                                               overlappingObjects);
  for (ShapeTree_TreeEntry* currentEntry : *overlappingObjects) {
    if (currentEntry->object == static_cast<ShapeTree_Storable*>(this)) {
      continue;
    }
    SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentEntry->object);
    if (!currentObject->isTraceObstacle(autorouteEngine->getNetNumber(), currentEntry->shapeIndexInObject)) {
      continue;
    }
    if (currentObject->shapeLayer(currentEntry->shapeIndexInObject) != getLayer()) {
      continue;
    }
    TileShape* currentShape =
        currentObject->getTreeShape(autorouteEngine->autorouteSearchTree, currentEntry->shapeIndexInObject);
    TileShape* intersection = this->getShape()->intersection(currentShape);
    if (intersection->dimension() > 1) {
      FRLogger::warn("ExpansionRoom overlap conflict");
      result = false;
    }
  }
  return result;
}

void CompleteFreeSpaceExpansionRoom::clearDoors() {
  FreeSpaceExpansionRoom::clearDoors();
  this->targetDoors = new (jrt::kSearch) jrt::LinkedList<TargetItemExpansionDoor*>();
}

void CompleteFreeSpaceExpansionRoom::resetDoors() {
  FreeSpaceExpansionRoom::resetDoors();
  for (ExpandableObject* currentDoor : *this->targetDoors) {
    currentDoor->reset();
  }
}

}  // namespace fr
