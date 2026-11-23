#include "autoroute/DesignBlocks.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "drc/AirLine.h"
#include "drc/ClearanceViolation.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"

namespace fr {

bool DesignBlocks::isBlocked(Pin* pin) {
  return isRuleBlocked(pin) || (blockingItems(pin) != nullptr && !blockingItems(pin)->isEmpty());
}

bool DesignBlocks::isRuleBlocked(Pin* pin) {
  return pin->board != nullptr && pin->board->ruleBlockedPins != nullptr &&
         pin->board->ruleBlockedPins->contains(pin->getId());
}

jrt::List<Item*>* DesignBlocks::blockingItems(Pin* pin) {
  jrt::Set<int32_t>* pinLayers = new (jrt::kOp) jrt::HashSet<int32_t>();
  for (int32_t i = 0; i < pin->tileShapeCount(); i++) {
    pinLayers->add(pin->shapeLayer(i));
  }
  if (pinLayers->isEmpty()) {
    return nullptr;
  }
  jrt::Set<int32_t>* blockedLayers = new (jrt::kOp) jrt::HashSet<int32_t>();
  jrt::List<Item*>* blockers = new (jrt::kOp) jrt::ArrayList<Item*>();
  for (ClearanceViolation* v : *pin->clearanceViolations()) {
    Item* other = (v->firstItem == static_cast<Item*>(pin)) ? v->secondItem : v->firstItem;
    if (v->actualClearance > 0 || other->isRoutable() || !isCopper(other) || other->sharesNet(pin) ||
        !coversCentre(pin, other, v->layer, v->expectedClearance)) {
      continue;
    }
    blockedLayers->add(v->layer);
    if (!blockers->contains(other)) {
      blockers->add(other);
    }
  }
  return blockedLayers->containsAll(pinLayers) ? blockers : nullptr;
}

bool DesignBlocks::isBlockedSet(jrt::Collection<Item*>* connectedSet) {
  if (connectedSet == nullptr || connectedSet->isEmpty()) {
    return false;
  }
  for (Item* item : *connectedSet) {
    Pin* pin = jrt::fast_cast<Pin>(item);
    if (pin == nullptr || !isBlocked(pin)) {
      return false;
    }
  }
  return true;
}

bool DesignBlocks::connectionsBlocked(BasicBoard* board, Item* item, int32_t netNumber) {
  jrt::DisposeOnExit<jrt::Set<Item*>> ownSetOwner(item->getConnectedSet(netNumber));
  jrt::Set<Item*>* ownSet = ownSetOwner.get();
  if (isBlockedSet(ownSet)) {
    return true;
  }
  jrt::Set<Item*>* seen = new (jrt::kOp) jrt::HashSet<Item*>(ownSet);
  bool sawOtherSet = false;
  for (Item* netItem : *board->getConnectableItems(netNumber)) {
    if (seen->contains(netItem)) {
      continue;
    }
    jrt::DisposeOnExit<jrt::Set<Item*>> otherSetOwner(netItem->getConnectedSet(netNumber));
    jrt::Set<Item*>* otherSet = otherSetOwner.get();
    seen->addAll(otherSet);
    if (!isBlockedSet(otherSet)) {
      return false;
    }
    sawOtherSet = true;
  }
  return sawOtherSet;
}

jrt::List<AirLine*>* DesignBlocks::blockedAirlines(jrt::Collection<AirLine*>* airlines) {
  jrt::List<AirLine*>* result = new (jrt::kOp) jrt::ArrayList<AirLine*>();
  for (AirLine* a : *airlines) {
    if (a != nullptr && a->net != nullptr &&
        (endBlocked(a->fromItem, a->net->netNumber) || endBlocked(a->toItem, a->net->netNumber))) {
      result->add(a);
    }
  }
  return result;
}

bool DesignBlocks::endBlocked(Item* item, int32_t netNumber) {
  Pin* pin = jrt::fast_cast<Pin>(item);
  return pin != nullptr && isBlocked(pin) && isBlockedSet(item->getConnectedSet(netNumber));
}

bool DesignBlocks::coversCentre(Pin* pin, Item* other, int32_t layer, double clearance) {
  FloatPoint* centre = pin->getCenter()->toFloat();
  int32_t halfWidth = 0;
  if (pin->netCount() > 0 && pin->board != nullptr &&
      pin->board->rules->nets->get(pin->getNetNumber(0)) != nullptr) {
    Net* net = pin->board->rules->nets->get(pin->getNetNumber(0));
    halfWidth = net->getNetClass()->getTraceHalfWidth(layer);
  }
  for (int32_t i = 0; i < other->tileShapeCount(); i++) {
    if (other->shapeLayer(i) == layer && other->getTileShape(i) != nullptr) {
      TileShape* shape = other->getTileShape(i);
      if (shape->distance(centre) < clearance + halfWidth) {
        return true;
      }
    }
  }
  return false;
}

bool DesignBlocks::isCopper(Item* item) {
  return jrt::fast_cast<Pin>(item) != nullptr || jrt::fast_cast<Trace>(item) != nullptr ||
         jrt::fast_cast<Via>(item) != nullptr || jrt::fast_cast<ConductionArea>(item) != nullptr;
}

}  // namespace fr
