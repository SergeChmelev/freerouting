// Port of app.freerouting.board.facade.BoardItemRepository
#include "board/facade/BoardItemRepository.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/state/BoardObservers.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/UndoableObjects.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr {

BoardOutline* BoardItemRepository::getOutline() {
  int64_t stamp = board->itemList->getVersion();
  if (stamp != outlineStamp) {
    outline = findOutline();
    outlineStamp = stamp;
  }
  return outline;
}

BoardOutline* BoardItemRepository::findOutline() {
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
    if (currentItem == nullptr) {
      return nullptr;
    }
    if (auto* currentOutline = jrt::fast_cast<BoardOutline>(currentItem)) {
      return currentOutline;
    }
  }
}

Item* BoardItemRepository::getItem(int32_t id) {
  int64_t stamp = board->itemList->getVersion();
  if (idMap == nullptr || stamp != idMapStamp) {
    idMap = new jrt::HashMap<int32_t, Item*>();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator =
        board->itemList->startReadObject();
    for (;;) {
      UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
      if (currentItem == nullptr) {
        break;
      }
      if (auto* item = jrt::fast_cast<Item>(currentItem)) {
        idMap->put(item->getId(), item);
      }
    }
    idMapStamp = stamp;
  }
  return idMap->get(id);
}

int32_t BoardItemRepository::getViaCount() {
  int32_t result = 0;
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
    if (currentItem == nullptr) {
      return result;
    }
    if (jrt::fast_cast<Via>(currentItem) != nullptr) {
      result++;
    }
  }
}

int32_t BoardItemRepository::getPinCount() {
  int32_t result = 0;
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
    if (currentItem == nullptr) {
      return result;
    }
    if (jrt::fast_cast<Pin>(currentItem) != nullptr) {
      result++;
    }
  }
}

jrt::Collection<Item*>* BoardItemRepository::getItems() {
  jrt::Collection<Item*>* result = new jrt::LinkedList<Item*>();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(iterator));
    if (currentItem == nullptr) {
      return result;
    }
    result->add(currentItem);
  }
}

jrt::Collection<ConductionArea*>* BoardItemRepository::getConductionAreas() {
  jrt::Collection<ConductionArea*>* result = new jrt::LinkedList<ConductionArea*>();
  for (Item* currentItem : *getItems()) {
    if (auto* area = jrt::fast_cast<ConductionArea>(currentItem)) {
      result->add(area);
    }
  }
  return result;
}

jrt::Collection<Pin*>* BoardItemRepository::getPins() {
  jrt::Collection<Pin*>* result = new jrt::LinkedList<Pin*>();
  for (Item* currentItem : *getItems()) {
    if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
      result->add(pin);
    }
  }
  return result;
}

jrt::Collection<Pin*>* BoardItemRepository::getSmdPins() {
  jrt::Collection<Pin*>* result = new jrt::LinkedList<Pin*>();
  for (Item* currentItem : *getItems()) {
    if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
      if (pin->firstLayer() == pin->lastLayer()) {
        result->add(pin);
      }
    }
  }
  return result;
}

jrt::Collection<Via*>* BoardItemRepository::getVias() {
  jrt::Collection<Via*>* result = new jrt::LinkedList<Via*>();
  for (Item* currentItem : *getItems()) {
    if (auto* via = jrt::fast_cast<Via>(currentItem)) {
      result->add(via);
    }
  }
  return result;
}

jrt::Collection<Trace*>* BoardItemRepository::getTraces() {
  jrt::Collection<Trace*>* result = new jrt::LinkedList<Trace*>();
  for (Item* currentItem : *getItems()) {
    if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
      result->add(trace);
    }
  }
  return result;
}

double BoardItemRepository::cumulativeTraceLength() {
  double result = 0;
  for (Item* currentItem : *getItems()) {
    if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
      result += trace->getLength();
    }
  }
  return result;
}

void BoardItemRepository::insertItem(Item* item) {
  if (item == nullptr) {
    return;
  }
  if (FRLogger::isTraceEnabled() && isItemActivityDebugCandidate(item)) {
    FRLogger::trace("ITEM_ACTIVITY action=INSERT, id=" + jrt::str(item->getId()) +
                    ", type=" + jrt::className(item) +
                    ", bounds=" + describeBounds(item->boundingBox()) +
                    ", net0=" + firstNetOrNone(item));
  }

  if (board->rules == nullptr || board->rules->clearanceMatrix == nullptr ||
      item->clearanceClassIndex() < 0 ||
      item->clearanceClassIndex() >= board->rules->clearanceMatrix->getClassCount()) {
    FRLogger::warn("LayeredBoard.insert_item: clearanceClass no out of range");
    item->setClearanceClassIndex(0);
  }
  item->board = board;
  board->itemList->insert(item);
  board->searchTreeManager->insert(item);
  if (board->communication != nullptr && board->communication->observers != nullptr) {
    board->communication->observers->notifyNew(item);
  }
  board->additionalUpdateAfterChange(item);
  board->incrementRevision(item);
  board->routingLedger()->noteInserted(item);
}

void BoardItemRepository::removeItem(Item* item) { removeItem(item, false); }

void BoardItemRepository::detachItemForMove(Item* item) {
  if (item->isUserFixed() ||
      (item->getComponentId() > 0 &&
       board->components->get(item->getComponentId())->positionFixed)) {
    throw jrt::IllegalArgumentException("Fixed items cannot be detached for a move");
  }
  removeItem(item, true);
}

void BoardItemRepository::removeItem(Item* item, bool forMove) {
  if (item == nullptr) {
    return;
  }
  if (FRLogger::isTraceEnabled() && isItemActivityDebugCandidate(item)) {
    FRLogger::trace("ITEM_ACTIVITY action=REMOVE, id=" + jrt::str(item->getId()) +
                    ", type=" + jrt::className(item) +
                    ", bounds=" + describeBounds(item->boundingBox()) +
                    ", net0=" + firstNetOrNone(item));
  }
  if (auto* trace = jrt::fast_cast<Trace>(item)) {
    if (trace->netNumbers->length > 0 && (*trace->netNumbers)[0] == 94) {
      logTraceRemoval(trace);
    }
  }
  if (item->isDeletionForbidden() && !(forMove && item->getComponentId() > 0)) {
    return;
  }
  board->additionalUpdateAfterChange(item);
  board->searchTreeManager->remove(item);
  board->itemList->delete_(item);
  if (board->communication != nullptr && board->communication->observers != nullptr) {
    board->communication->observers->notifyDeleted(item);
  }
  board->incrementRevision(item);
  board->routingLedger()->noteRemoved(item);
}

bool BoardItemRepository::removeItems(jrt::Collection<Item*>* items) {
  bool result = true;
  for (Item* currentItem : *items) {
    if (currentItem->isDeletionForbidden() || currentItem->isUserFixed()) {
      result = false;
    } else {
      removeItem(currentItem);
    }
  }
  return result;
}

void BoardItemRepository::deleteAllTracksAndVias() {
  board->routingLedger()->invalidate();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(iterator);
    if (currentItem == nullptr) {
      return;
    }
    if (jrt::fast_cast<Trace>(currentItem) != nullptr ||
        jrt::fast_cast<Via>(currentItem) != nullptr) {
      board->itemList->delete_(currentItem);
    }
  }
}

bool BoardItemRepository::isItemActivityDebugCandidate(Item* item) {
  IntBox* bounds = item->boundingBox();
  IntBox* debugWindow = new IntBox(1620000, -1105000, 1930000, -1003000);
  return bounds != nullptr && bounds->intersects(debugWindow);
}

std::string BoardItemRepository::firstNetOrNone(Item* item) {
  return item->netCount() > 0 ? jrt::Integer::toString(item->getNetNumber(0)) : "none";
}

std::string BoardItemRepository::describeBounds(IntBox* bounds) {
  if (bounds == nullptr) {
    return "null";
  }
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

void BoardItemRepository::logTraceRemoval(Trace* trace) {
  // PORT NOTE: Java logs stack trace elements (Thread.getStackTrace); C++ has no portable stack
  // walk, so the stack lines are omitted and the caller frame is printed as "<unknown>". The guard
  // below keeps the payload from being built when granular tracing is off.
  if (!FRLogger::isTraceEnabled()) {
    return;
  }
  auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace);
  if (polylineTrace != nullptr && polylineTrace->cornerCount() == 2 &&
      trace->firstCorner()->equals(new IntPoint(1885928, -1097274)) &&
      trace->lastCorner()->equals(new IntPoint(1885928, -1098024))) {
    FRLogger::trace("BasicBoard.remove_item", "compare_trace_remove_item",
                    "REMOVE_ITEM called on trace [7,8]",
                    "Net #" + jrt::str((*trace->netNumbers)[0]) + ",Trace #" +
                        jrt::str(trace->getId()) + ",Layer #" + jrt::str(trace->getLayer()),
                    jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()}));
  } else {
    FRLogger::trace("BasicBoard.remove_item", "compare_trace_remove_item",
                    "REMOVE_ITEM called by <unknown>",
                    "Net #" + jrt::str((*trace->netNumbers)[0]) + ",Trace #" +
                        jrt::str(trace->getId()) + ",Layer #" + jrt::str(trace->getLayer()),
                    jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()}));
  }
}

}  // namespace fr
