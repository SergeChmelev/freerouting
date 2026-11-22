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
    jrt::Map<int32_t, jrt::List<Item*>*>* map = new jrt::HashMap<int32_t, jrt::List<Item*>*>();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator =
        board->itemList->startReadObject();
    for (;;) {
      UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
      if (currentItem == nullptr) {
        break;
      }
      if (dynamic_cast<Connectable*>(currentItem) != nullptr) {
        if (auto* item = dynamic_cast<Item*>(currentItem)) {
          for (int32_t i = 0; i < item->netCount(); i++) {
            int32_t netNo = item->getNetNumber(i);
            map->computeIfAbsent(netNo,
                                 [](int32_t) -> jrt::List<Item*>* {
                                   return new jrt::ArrayList<Item*>();
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
    return new jrt::ArrayList<Item*>(items);
  }
  return new jrt::LinkedList<Item*>();
}

int32_t BoardConnectivityQueries::connectableItemCount(int32_t netNumber) {
  jrt::List<Item*>* items = getConnectableIndex()->get(netNumber);
  return items != nullptr ? items->size() : 0;
}

jrt::Collection<Item*>* BoardConnectivityQueries::getComponentItems(int32_t componentId) {
  jrt::Collection<Item*>* result = new jrt::LinkedList<Item*>();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = dynamic_cast<Item*>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return result;
    }
    if (currentItem->getComponentId() == componentId) {
      result->add(currentItem);
    }
  }
}

jrt::Collection<Pin*>* BoardConnectivityQueries::getComponentPins(int32_t componentId) {
  jrt::Collection<Pin*>* result = new jrt::LinkedList<Pin*>();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = dynamic_cast<Item*>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return result;
    }
    if (currentItem->getComponentId() == componentId) {
      if (auto* pin = dynamic_cast<Pin*>(currentItem)) {
        result->add(pin);
      }
    }
  }
}

Pin* BoardConnectivityQueries::getPin(int32_t componentId, int32_t pinIndex) {
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = dynamic_cast<Item*>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return nullptr;
    }
    if (currentItem->getComponentId() == componentId) {
      if (auto* pin = dynamic_cast<Pin*>(currentItem)) {
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
      new jrt::LinkedList<jrt::Collection<Item*>*>();
  if (netNumber <= 0) {
    return result;
  }
  jrt::TreeSet<Item*>* itemsToHandle = new jrt::TreeSet<Item*>(getConnectableItems(netNumber));
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
