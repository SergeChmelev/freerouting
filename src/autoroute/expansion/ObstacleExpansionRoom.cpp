#include "autoroute/expansion/ObstacleExpansionRoom.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"

namespace fr {

ObstacleExpansionRoom::ObstacleExpansionRoom(Item* item, int32_t indexInItem, ShapeSearchTree* shapeTree) {
  this->item = item;
  this->indexInItem = indexInItem;
  this->shape = item->getTreeShape(shapeTree, indexInItem);
  this->doors = new (jrt::kSearch) jrt::ArrayList<ExpansionDoor*>();
}

int32_t ObstacleExpansionRoom::getLayer() { return this->item->shapeLayer(this->indexInItem); }

int32_t ObstacleExpansionRoom::getId() { return jrt::shl(this->item->getId(), 10) | this->indexInItem; }

bool ObstacleExpansionRoom::doorExists(ExpansionRoom* other) {
  if (doors != nullptr) {
    for (ExpansionDoor* currentDoor : *this->doors) {
      if (currentDoor->firstRoom == other || currentDoor->secondRoom == other) {
        return true;
      }
    }
  }
  return false;
}

void ObstacleExpansionRoom::addDoor(ExpansionDoor* door) { this->doors->add(door); }

bool ObstacleExpansionRoom::createOverlapDoor(ObstacleExpansionRoom* other) {
  if (this->doorExists(other)) {
    return false;
  }
  if (!(this->item->isRoutable() && other->item->isRoutable())) {
    return false;
  }
  if (!this->item->sharesNet(other->item)) {
    return false;
  }
  if (this->item == other->item) {
    if (jrt::fast_cast<PolylineTrace>(this->item) == nullptr) {
      return false;
    }
    // create only doors between consecutive trace segments
    if (this->indexInItem != other->indexInItem + 1 && this->indexInItem != other->indexInItem - 1) {
      return false;
    }
  }
  if (this->item != other->item && meetOnPin(this->item, other->item)) {
    return false;
  }
  ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(this, other, 2);
  this->addDoor(newDoor);
  other->addDoor(newDoor);
  return true;
}

bool ObstacleExpansionRoom::meetOnPin(Item* first, Item* second) {
  PolylineTrace* a = jrt::fast_cast<PolylineTrace>(first);
  PolylineTrace* b = jrt::fast_cast<PolylineTrace>(second);
  if (a == nullptr || b == nullptr) {
    return false;
  }
  for (Point* end : *jrt::Array<Point*>::of({a->firstCorner(), a->lastCorner()})) {
    if (end->equals(b->firstCorner()) || end->equals(b->lastCorner())) {
      ItemSelectionFilter* pinFilter = new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::PINS);
      jrt::Set<Item*>* pins = a->board->pickItems(end, a->getLayer(), pinFilter);
      if (!pins->isEmpty()) {
        return true;
      }
    }
  }
  return false;
}

void ObstacleExpansionRoom::clearDoors() { this->doors = new (jrt::kSearch) jrt::ArrayList<ExpansionDoor*>(); }

void ObstacleExpansionRoom::resetDoors() {
  for (ExpandableObject* currentDoor : *this->doors) {
    currentDoor->reset();
  }
}

jrt::Collection<TargetItemExpansionDoor*>* ObstacleExpansionRoom::getTargetDoors() {
  return new (jrt::kSearch) jrt::ArrayList<TargetItemExpansionDoor*>();
}

SearchTreeObject* ObstacleExpansionRoom::getObject() { return this->item; }

bool ObstacleExpansionRoom::removeDoor(ExpandableObject* door) {
  // Java: doors.remove(Object); an object that is no ExpansionDoor is never contained.
  ExpansionDoor* expansionDoor = jrt::fast_cast<ExpansionDoor>(door);
  if (expansionDoor == nullptr) {
    return false;
  }
  return this->doors->remove(expansionDoor);
}

void ObstacleExpansionRoom::emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink != nullptr && intensity > 0) {
    sink->accept(new (jrt::kOp) AutorouteDiagnostic(AutorouteDiagnostic_Kind::OBSTACLE_ROOM, this->getShape(), this->getLayer(),
                                         intensity));
  }
}

}  // namespace fr
