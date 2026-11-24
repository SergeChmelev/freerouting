// Port of app.freerouting.drc.NetRoutingLedger
#include "drc/NetRoutingLedger.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "drc/NetIncompletes.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Nets.h"

namespace fr {

NetRoutingLedger::NetRoutingLedger(BasicBoard* board)
    : board(board),
      conductionAreas(new (jrt::kOp) jrt::ArrayList<ConductionArea*>()),
      itemNets(nullptr) {
  // PORT NOTE (stage E): the ledger owns its per-item net arrays and the map's entries.
  auto* map = new (jrt::kOp) jrt::IdentityHashMap<Item*, jrt::Array<int32_t>*>();
  map->setFreesNodes(true);
  itemNets = map;
}

void NetRoutingLedger::clearItemNets() {
  for (auto* e : itemNets->entryVector()) jrt::dispose(e->getValue());
  itemNets->clear();
}

void NetRoutingLedger::disposeOwned() {
  clearItemNets();
  jrt::dispose(itemNets);
  itemNets = nullptr;
  jrt::dispose(conductionAreas);
  conductionAreas = nullptr;
  jrt::dispose(dirtyNets);
  dirtyNets = nullptr;
  jrt::dispose(incompleteByNet);
  incompleteByNet = nullptr;
  if (itemsByNet != nullptr) {
    for (jrt::List<Item*>* list : *itemsByNet) jrt::dispose(list);
    jrt::dispose(itemsByNet);
    itemsByNet = nullptr;
  }
}

void NetRoutingLedger::invalidate() {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  built = false;
  conductionAreas->clear();
  dirtyNets->clear();
  clearItemNets();
  itemsByNet = emptyLists();
  incompleteByNet = new (jrt::kOp) jrt::Array<int32_t>(0);
  incompleteTotal = 0;
  maximumConnections_ = 0;
}

void NetRoutingLedger::noteInserted(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  if (!built || jrt::fast_cast<Connectable>(item) == nullptr) {
    return;
  }
  jrt::dispose(itemNets->put(item, item->netNumbers->clone()));
  addItem(item);
}

void NetRoutingLedger::noteRemoved(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  if (!built || jrt::fast_cast<Connectable>(item) == nullptr) {
    return;
  }
  removeItem(item);
}

int32_t NetRoutingLedger::incompleteCount() {
  ensureBuilt();
  flushDirty();
  return incompleteTotal;
}

int32_t NetRoutingLedger::maximumConnections() {
  ensureBuilt();
  return maximumConnections_;
}

int32_t NetRoutingLedger::incompleteCount(int32_t netNumber) {
  ensureBuilt();
  flushDirty();
  return netNumber >= 1 && netNumber < incompleteByNet->length ? (*incompleteByNet)[netNumber] : 0;
}

NetRoutingLedger_Counts* NetRoutingLedger::counts() {
  if (!built) {
    return nullptr;
  }
  flushDirty();
  return new (jrt::kOp) NetRoutingLedger_Counts(incompleteByNet->clone(), incompleteTotal,
                                     maximumConnections_);
}

void NetRoutingLedger::seed(NetRoutingLedger_Counts* counts) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  if (counts == nullptr ||
      counts->incompleteByNet()->length != board->rules->nets->maxNetNumber() + 1) {
    return;
  }
  buildItemLists();
  incompleteByNet = counts->incompleteByNet()->clone();
  incompleteTotal = counts->incompleteTotal();
  maximumConnections_ = counts->maximumConnections();
  built = true;
}

jrt::Set<int32_t>* NetRoutingLedger::incompleteNetNumbers() {
  ensureBuilt();
  flushDirty();
  jrt::Set<int32_t>* result = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t netNumber = 1; netNumber < incompleteByNet->length; netNumber++) {
    if ((*incompleteByNet)[netNumber] > 0) {
      result->add(netNumber);
    }
  }
  return result;
}

void NetRoutingLedger::ensureBuilt() {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  if (built) {
    return;
  }
  int32_t maxNet = buildItemLists();
  incompleteByNet = new (jrt::kOp) jrt::Array<int32_t>(maxNet + 1);
  maximumConnections_ = countMaximumConnections();
  incompleteTotal = 0;
  for (int32_t netNumber = 1; netNumber <= maxNet; netNumber++) {
    int32_t count = countNet(netNumber);
    (*incompleteByNet)[netNumber] = count;
    incompleteTotal += count;
  }
  dirtyNets->clear();
  built = true;
}

int32_t NetRoutingLedger::buildItemLists() {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  int32_t maxNet = board->rules->nets->maxNetNumber();
  auto* lists = new (jrt::kOp) jrt::Array<jrt::List<Item*>*>(maxNet + 1);
  itemsByNet = lists;
  dirtyNets->clear();
  clearItemNets();
  for (int32_t netNumber = 1; netNumber <= maxNet; netNumber++) {
    (*itemsByNet)[netNumber] = new (jrt::kOp) jrt::ArrayList<Item*>();
  }
  conductionAreas->clear();
  jrt::DisposeOnExit loopSource1(board->getItems());
  for (Item* item : *loopSource1) {
    if (jrt::fast_cast<Connectable>(item) != nullptr) {
      addItemToLists(item);
      jrt::dispose(itemNets->put(item, item->netNumbers->clone()));
    }
    if (auto* ca = jrt::fast_cast<ConductionArea>(item)) {
      conductionAreas->add(ca);
    }
  }
  return maxNet;
}

void NetRoutingLedger::flushDirty() {
  if (dirtyNets->isEmpty()) {
    return;
  }
  for (int32_t netNumber : dirtyNets->toVector()) {
    int32_t count = countNet(netNumber);
    incompleteTotal += count - (*incompleteByNet)[netNumber];
    (*incompleteByNet)[netNumber] = count;
  }
  dirtyNets->clear();
  maximumConnections_ = countMaximumConnections();
}

int32_t NetRoutingLedger::countNet(int32_t netNumber) {
  if (netNumber < 1 || netNumber >= itemsByNet->length) {
    return 0;
  }
  return (new (jrt::kOp) NetIncompletes(netNumber, new (jrt::kOp) jrt::ArrayList<Item*>((*itemsByNet)[netNumber]),
                             board))
      ->count();
}

int32_t NetRoutingLedger::countMaximumConnections() {
  int32_t maximum = 0;
  for (int32_t netNumber = 1; netNumber < itemsByNet->length; netNumber++) {
    int64_t endpoints = 0;
    for (Item* item : *(*itemsByNet)[netNumber]) {
      if (jrt::fast_cast<Pin>(item) != nullptr || jrt::fast_cast<ConductionArea>(item) != nullptr) {
        endpoints++;
      }
    }
    maximum += static_cast<int32_t>(jrt::Math::max(static_cast<int64_t>(0), endpoints - 1));
  }
  return maximum;
}

void NetRoutingLedger::addItem(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  addItemToLists(item);
  auto* ca = jrt::fast_cast<ConductionArea>(item);
  if (ca != nullptr && !conductionAreas->contains(ca)) {
    conductionAreas->add(ca);
  }
  markNets(item);
}

void NetRoutingLedger::removeItem(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  jrt::Array<int32_t>* registeredNets = itemNets->remove(item);
  if (registeredNets != nullptr) {
    for (int32_t netNumber : *registeredNets) {
      if (netNumber >= 1 && netNumber < itemsByNet->length) {
        (*itemsByNet)[netNumber]->remove(item);
        dirtyNets->add(netNumber);
      }
    }
    jrt::dispose(registeredNets);
  }
  for (int32_t index = 0; index < item->netCount(); index++) {
    int32_t netNumber = item->getNetNumber(index);
    if (netNumber >= 1 && netNumber < itemsByNet->length) {
      (*itemsByNet)[netNumber]->remove(item);
    }
  }
  if (auto* ca = jrt::fast_cast<ConductionArea>(item)) {
    conductionAreas->remove(ca);
  }
  markNets(item);
}

void NetRoutingLedger::addItemToLists(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  for (int32_t index = 0; index < item->netCount(); index++) {
    int32_t netNumber = item->getNetNumber(index);
    if (netNumber >= 1 && netNumber < itemsByNet->length &&
        !(*itemsByNet)[netNumber]->contains(item)) {
      (*itemsByNet)[netNumber]->add(item);
    }
  }
}

void NetRoutingLedger::markNets(Item* item) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the ledger is board-owned
  for (int32_t index = 0; index < item->netCount(); index++) {
    int32_t netNumber = item->getNetNumber(index);
    if (netNumber >= 1 && netNumber < itemsByNet->length) {
      dirtyNets->add(netNumber);
    }
  }
  IntBox* itemBox = item->boundingBox();
  for (ConductionArea* ca : *conductionAreas) {
    if (ca->getLayer() >= item->firstLayer() && ca->getLayer() <= item->lastLayer()) {
      IntBox* caBox = ca->boundingBox();
      double maxClearance = 0.0;
      if (board != nullptr && board->rules != nullptr && board->rules->clearanceMatrix != nullptr) {
        maxClearance = board->rules->clearanceMatrix->maxValue(ca->clearanceClassIndex(),
                                                               ca->getLayer());
      } else if (board != nullptr) {
        maxClearance = board->clearanceValue(ca->clearanceClassIndex(),
                                             item->clearanceClassIndex(), ca->getLayer());
      }
      if (maxClearance <= 0.0) {
        maxClearance = 1000.0;
      }
      int32_t cl = jrt::d2i(jrt::Math::ceil(maxClearance));
      IntBox* inflatedItemBox =
          new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(itemBox->ll->x - cl, itemBox->ll->y - cl),
                     new (jrt::kGeom) IntPoint(itemBox->ur->x + cl, itemBox->ur->y + cl));
      if (caBox->intersects(inflatedItemBox)) {
        for (int32_t index = 0; index < ca->netCount(); index++) {
          int32_t netNumber = ca->getNetNumber(index);
          if (netNumber >= 1 && netNumber < itemsByNet->length) {
            dirtyNets->add(netNumber);
          }
        }
      }
    }
  }
}

}  // namespace fr
