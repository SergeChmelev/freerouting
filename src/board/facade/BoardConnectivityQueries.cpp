// Port of app.freerouting.board.facade.BoardConnectivityQueries
#include "board/facade/BoardConnectivityQueries.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "datastructures/UndoableObjects.h"

namespace fr {

jrt::Map<int32_t, jrt::List<Item*>*>* BoardConnectivityQueries::getConnectableIndex() {
  int64_t currentStamp = board->itemList->getVersion() + board->getNetAssignmentChangeCount();
  if (connectableItemsByNet == nullptr || connectableItemsStamp != currentStamp) {
    // PORT NOTE (stage E): a board cache that outlives the operation that asks first (it may run
    // in an operation region): in the domain of this board-owned object. The stale index is
    // freed (only this class reads it).
    jrt::DomainScope domain(this);
    if (connectableItemsByNet != nullptr) {
      for (auto* e : connectableItemsByNet->entryVector()) jrt::dispose(e->getValue());
      jrt::disposeMapAndEntries(connectableItemsByNet);
      connectableItemsByNet = nullptr;
    }
    jrt::Map<int32_t, jrt::List<Item*>*>* map = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    auto iteratorScope = board->itemList->readScope();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = &iteratorScope;
    for (;;) {
      UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
      if (currentItem == nullptr) {
        break;
      }
      if (jrt::fast_cast<Connectable>(currentItem) != nullptr) {
        if (auto* item = jrt::fast_cast<Item>(currentItem)) {
          for (int32_t i = 0; i < item->netCount(); i++) {
            int32_t netNo = item->getNetNumber(i);
            map->computeIfAbsent(netNo,
                                 [](int32_t) -> jrt::List<Item*>* {
                                   return new (jrt::kOp) jrt::ArrayList<Item*>();
                                 })
                ->add(item);
          }
        }
      }
    }
    connectableItemsByNet = map;
    connectableItemsStamp = currentStamp;
  }
  return connectableItemsByNet;
}

jrt::Collection<Item*>* BoardConnectivityQueries::getConnectableItems(int32_t netNumber) {
  jrt::List<Item*>* items = getConnectableIndex()->get(netNumber);
  if (items != nullptr) {
    return new (jrt::kOp) jrt::ArrayList<Item*>(items);
  }
  return new (jrt::kOp) jrt::LinkedList<Item*>();
}

int32_t BoardConnectivityQueries::connectableItemCount(int32_t netNumber) {
  jrt::List<Item*>* items = getConnectableIndex()->get(netNumber);
  return items != nullptr ? items->size() : 0;
}

jrt::Collection<Item*>* BoardConnectivityQueries::getComponentItems(int32_t componentId) {
  jrt::Collection<Item*>* result = new (jrt::kOp) jrt::LinkedList<Item*>();
  auto iteratorScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = &iteratorScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return result;
    }
    if (currentItem->getComponentId() == componentId) {
      result->add(currentItem);
    }
  }
}

jrt::Collection<Pin*>* BoardConnectivityQueries::getComponentPins(int32_t componentId) {
  jrt::Collection<Pin*>* result = new (jrt::kOp) jrt::LinkedList<Pin*>();
  auto iteratorScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = &iteratorScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return result;
    }
    if (currentItem->getComponentId() == componentId) {
      if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
        result->add(pin);
      }
    }
  }
}

Pin* BoardConnectivityQueries::getPin(int32_t componentId, int32_t pinIndex) {
  auto iteratorScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = &iteratorScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return nullptr;
    }
    if (currentItem->getComponentId() == componentId) {
      if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
        if (pin->pinIndex == pinIndex) {
          return pin;
        }
      }
    }
  }
}

jrt::Collection<jrt::Collection<Item*>*>* BoardConnectivityQueries::getConnectedSets(
    int32_t netNumber) {
  jrt::Collection<jrt::Collection<Item*>*>* result =
      new (jrt::kOp) jrt::LinkedList<jrt::Collection<Item*>*>();
  if (netNumber <= 0) {
    return result;
  }
  jrt::TreeSet<Item*>* itemsToHandle = new (jrt::kOp) jrt::TreeSet<Item*>(getConnectableItems(netNumber));
  jrt::Iterator<Item*>* connectedItems = itemsToHandle->iterator();
  while (connectedItems->hasNext()) {
    Item* currentItem = connectedItems->next();
    jrt::Collection<Item*>* nextConnectedSet = currentItem->getConnectedSet(netNumber);
    result->add(nextConnectedSet);
    itemsToHandle->removeAll(nextConnectedSet);
    connectedItems = itemsToHandle->iterator();
  }
  return result;
}

}  // namespace fr
