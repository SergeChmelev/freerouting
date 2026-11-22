// Port of app.freerouting.board.actions.MoveComponent.
#include "board/actions/MoveComponent.h"

#include "board/actions/DrillItemMover.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/state/BoardObservers.h"
#include "board/state/Communication.h"
#include "datastructures/Signum.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Vector.h"

namespace fr {

int32_t MoveComponent_SortedItem::compareTo(MoveComponent_SortedItem* other) {
  return Signum::asInt(this->projection_ - other->projection_);
}

MoveComponent::MoveComponent(Item* item, Vector* translateVector, int32_t maxRecursionDepth,
                             int32_t maxViaRecursionDepth)
    : translateVector(translateVector),
      maxRecursionDepth(maxRecursionDepth),
      maxViaRecursionDepth(maxViaRecursionDepth) {
  itemsToShove = new (jrt::kOp) jrt::HashSet<Item*>();
  if (auto* routingBoard = jrt::fast_cast<RoutingBoard>(item->board)) {
    board = routingBoard;
  } else {
    board = nullptr;
    allItemsMovable = false;
  }
  jrt::Collection<Item*>* itemGroupList;
  int32_t componentId = item->getComponentId();
  if (componentId > 0) {
    itemGroupList = board->getComponentItems(componentId);
    this->component = board->components->get(componentId);
  } else {
    itemGroupList = new (jrt::kOp) jrt::LinkedList<Item*>();
    itemGroupList->add(item);
  }
  auto* itemCenters = new (jrt::kOp) jrt::LinkedList<FloatPoint*>();
  for (Item* currentItem : *itemGroupList) {
    bool currentItemMovable = !currentItem->isUserFixed() &&
                              (jrt::fast_cast<DrillItem>(currentItem) != nullptr ||
                               jrt::fast_cast<ObstacleArea>(currentItem) != nullptr ||
                               jrt::fast_cast<ComponentOutline>(currentItem) != nullptr);
    if (!currentItemMovable) {
      allItemsMovable = false;
      itemGroupArr = new (jrt::kOp) jrt::Array<MoveComponent_SortedItem*>(0);
      itemGroupSet = new (jrt::kOp) jrt::HashSet<Item*>();  // Java Set.of()
      return;
    }
    if (auto* drillItem = jrt::fast_cast<DrillItem>(currentItem)) {
      itemCenters->add(drillItem->getCenter()->toFloat());
    }
  }
  this->itemGroupSet = new (jrt::kOp) jrt::HashSet<Item*>(itemGroupList);
  if (itemCenters->isEmpty()) {
    if (this->component != nullptr) {
      itemCenters->add(this->component->getLocation()->toFloat());
    } else {
      for (Item* currentItem : *itemGroupList) {
        itemCenters->add(currentItem->boundingBox()->centreOfGravity());
      }
    }
  }
  double gravityX = 0;
  double gravityY = 0;
  for (FloatPoint* currentCenter : *itemCenters) {
    gravityX += currentCenter->x;
    gravityY += currentCenter->y;
  }
  if (!itemCenters->isEmpty()) {
    gravityX /= itemCenters->size();
    gravityY /= itemCenters->size();
  }
  Point* gravityPoint = new (jrt::kGeom) IntPoint(jrt::l2i(jrt::Math::round(gravityX)), jrt::l2i(jrt::Math::round(gravityY)));
  itemGroupArr = new (jrt::kOp) jrt::Array<MoveComponent_SortedItem*>(itemGroupList->size());
  auto it = itemGroupList->iterator();
  for (int32_t i = 0; i < itemGroupArr->length; i++) {
    Item* currentItem = it->next();
    Point* itemCenter;
    if (auto* drillItem = jrt::fast_cast<DrillItem>(currentItem)) {
      itemCenter = drillItem->getCenter();
    } else {
      itemCenter = currentItem->boundingBox()->centreOfGravity()->round();
    }
    Vector* compareVector = gravityPoint->differenceBy(itemCenter);
    double currentProjection = compareVector->scalarProduct(translateVector);
    (*itemGroupArr)[i] = new (jrt::kOp) MoveComponent_SortedItem(currentItem, currentProjection);
  }
  jrt::Arrays::sort<MoveComponent_SortedItem*>(itemGroupArr);
}

bool MoveComponent::hasConnectingTraces(DrillItem* drillItem) {
  jrt::Collection<Item*>* contacts = drillItem->getNormalContacts();
  for (Item* currentContact : *contacts) {
    if (jrt::fast_cast<Trace>(currentContact) != nullptr) return true;
  }
  return false;
}

bool MoveComponent::requiresDrillItemMover(DrillItem* drillItem) {
  return hasConnectingTraces(drillItem) || drillItem->netCount() > 1;
}

double MoveComponent::getMinDrillItemWidthWithTraces() {
  double min = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < itemGroupArr->length; i++) {
    auto* drillItem = jrt::fast_cast<DrillItem>((*itemGroupArr)[i]->item());
    if (drillItem != nullptr && requiresDrillItemMover(drillItem)) {
      min = jrt::Math::min(min, drillItem->minWidth());
    }
  }
  return min == jrt::Double::MAX_VALUE ? -1.0 : min;
}

bool MoveComponent::check() {
  if (!allItemsMovable) return false;
  itemsToShove->clear();
  auto* timeLimit = new (jrt::kOp) TimeLimit(CHECK_TIME_LIMIT);
  jrt::Set<Item*>* ignoreItems = new (jrt::kOp) jrt::HashSet<Item*>(static_cast<jrt::Collection<Item*>*>(this->itemGroupSet));
  for (int32_t i = 0; i < itemGroupArr->length; i++) {
    bool moveOk;
    Item* groupItem = (*itemGroupArr)[i]->item();
    if (auto* currentDrillItem = jrt::fast_cast<DrillItem>(groupItem)) {
      bool useDrillMover = requiresDrillItemMover(currentDrillItem);
      if (useDrillMover) {
        if (this->translateVector->lengthApprox() >= currentDrillItem->minWidth()) {
          moveOk = false;
        } else {
          moveOk = DrillItemMover::check(currentDrillItem, this->translateVector, this->maxRecursionDepth,
                                         this->maxViaRecursionDepth, ignoreItems, board, timeLimit);
          if (moveOk) itemsToShove->add(currentDrillItem);
        }
      } else {
        moveOk = board->checkMoveItem(currentDrillItem, this->translateVector, ignoreItems);
        if (!moveOk && this->translateVector->lengthApprox() < currentDrillItem->minWidth()) {
          moveOk = DrillItemMover::check(currentDrillItem, this->translateVector, this->maxRecursionDepth,
                                         this->maxViaRecursionDepth, ignoreItems, board, timeLimit);
          if (moveOk) itemsToShove->add(currentDrillItem);
        }
      }
    } else {
      moveOk = board->checkMoveItem(groupItem, this->translateVector, ignoreItems);
    }
    if (!moveOk) return false;
  }
  return true;
}

bool MoveComponent::insert(int32_t tidyWidth, int32_t pullTightAccuracy) {
  if (!allItemsMovable) return false;
  if (this->component != nullptr) {
    board->components->move(this->component->id, translateVector);
    if (board->communication->observers != nullptr)
      board->communication->observers->notifyMoved(this->component);
  }
  for (int32_t i = 0; i < itemGroupArr->length; i++) {
    Item* groupItem = (*itemGroupArr)[i]->item();
    if (auto* currentDrillItem = jrt::fast_cast<DrillItem>(groupItem)) {
      bool moveOk;
      if (itemsToShove->contains(currentDrillItem) || requiresDrillItemMover(currentDrillItem)) {
        moveOk = board->moveDrillItem(currentDrillItem, this->translateVector, this->maxRecursionDepth,
                                      this->maxViaRecursionDepth, tidyWidth, pullTightAccuracy,
                                      PULL_TIGHT_TIME_LIMIT);
      } else {
        currentDrillItem->moveBy(this->translateVector);
        moveOk = true;
      }
      if (!moveOk) {
        if (this->component != nullptr) this->component->translateBy(translateVector->negate());
        return false;
      }
    } else {
      groupItem->moveBy(this->translateVector);
    }
  }
  board->invalidateEdgePinNetCache();
  return true;
}

}  // namespace fr
