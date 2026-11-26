// Port of app.freerouting.board.facade.RoutingBoardUndoFacade
#include <mutex>

#include "board/facade/RoutingBoardUndoFacade.h"

#include "autoroute/plane/FastPlaneConnectivityGrid.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/structure/Components.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/state/BoardObservers.h"
#include "board/state/Communication.h"
#include "datastructures/UndoableObjects.h"
#include "drc/ClearanceBaseline.h"
#include "drc/NetRoutingLedger.h"

namespace fr {

bool RoutingBoardUndoFacade::undo(jrt::Set<int32_t>* changedNets) {
  board->components->undo(board->communication->observers);
  jrt::Collection<UndoableObjects_Storable*>* cancelledObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  jrt::Collection<UndoableObjects_Storable*>* restoredObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  bool result = board->itemList->undo(cancelledObjects, restoredObjects);
  applyUndoRedoSideEffects(cancelledObjects, restoredObjects, changedNets);
  if (result) {
    board->routingLedger()->noteUndone(board->itemList->getStackLevel());
  }
  return result;
}

bool RoutingBoardUndoFacade::redo(jrt::Set<int32_t>* changedNets) {
  board->components->redo(board->communication->observers);
  jrt::Collection<UndoableObjects_Storable*>* cancelledObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  jrt::Collection<UndoableObjects_Storable*>* restoredObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  bool result = board->itemList->redo(cancelledObjects, restoredObjects);
  applyUndoRedoSideEffects(cancelledObjects, restoredObjects, changedNets);
  board->routingLedger()->dropSnapshotsAbove(board->itemList->getStackLevel() - 1);
  return result;
}

RoutingBoard* RoutingBoardUndoFacade::deepCopy() {
  // PORT NOTE: Java writes the board through an ObjectOutputStream that records the conduction
  // areas in write order, then reads it back. The port copies the board directly
  // (copyAsDeserialized, see the design notes in the header) and pairs each plane with its copy
  // through itemMap, which is what Java's positional pairing of written and read planes yields.
  // The planes are visited in the order their copies were made (every plane of the item list,
  // also those only held in the undo history, like Java's serialized graph). Java returns null
  // after logging an exception; the C++ copy does not fail.
  ClearanceBaseline* clearanceBaseline;
  NetRoutingLedger_Counts* ledgerCounts;
  auto* itemMap = new (jrt::kOp) jrt::IdentityHashMap<Item*, Item*>();
  BasicBoard* copied = board->copyAsDeserialized(itemMap);
  jrt::List<ConductionArea*>* writtenPlanes = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
  for (auto* entry : *itemMap->entrySet()) {
    if (auto* plane = jrt::fast_cast<ConductionArea>(entry->getKey())) {
      writtenPlanes->add(plane);
    }
  }
  jrt::List<FastPlaneConnectivityGrid_FractureMemo*>* planeMemos =
      new (jrt::kOp) jrt::ArrayList<FastPlaneConnectivityGrid_FractureMemo*>();
  {
    // Reads the source board (Java: still inside synchronized (board)).
    BasicBoard::CopyScope lock(board);
    for (ConductionArea* plane : *writtenPlanes) {
      planeMemos->add(plane->fractureMemoForCopy());
    }
    clearanceBaseline = board->clearanceBaseline;
    // Taken with the board written: the copy has the same items, so the same counts.
    ledgerCounts = board->routingLedger()->counts();
    lock.copyComplete();  // the last locked part of the copy
  }

  auto* boardCopy = jrt::fast_cast<RoutingBoard>(copied);
  for (int32_t i = 0; i < writtenPlanes->size(); i++) {
    ConductionArea* writtenPlane = writtenPlanes->get(i);
    auto* readPlane = jrt::fast_cast<ConductionArea>(itemMap->get(writtenPlane));
    if (readPlane != nullptr && readPlane->getId() == writtenPlane->getId()) {
      readPlane->adoptFractureMemo(planeMemos->get(i));
    }
  }
  // Transient, so not serialized: without it every pre-existing violation counts as new.
  boardCopy->clearanceBaseline = clearanceBaseline;
  boardCopy->clearAllItemTemporaryAutorouteData();
  boardCopy->finishAutoroute();
  // The copy has the same incomplete counts: no need to count every net again.
  boardCopy->routingLedger()->seed(ledgerCounts);
  // Stage E: copy temporaries (the memos were adopted by the copied planes).
  jrt::disposeMapAndEntries(itemMap);
  jrt::dispose(writtenPlanes);
  jrt::dispose(planeMemos);
  return boardCopy;
}

void RoutingBoardUndoFacade::applyUndoRedoSideEffects(
    jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
    jrt::Collection<UndoableObjects_Storable*>* restoredObjects, jrt::Set<int32_t>* changedNets) {
  for (UndoableObjects_Storable* storable : *cancelledObjects) {
    Item* currentItem = jrt::fast_cast<Item>(storable);
    board->searchTreeManager->remove(currentItem);
    board->incrementRevision(currentItem);
    board->routingLedger()->noteRemoved(currentItem);
    if (board->communication != nullptr && board->communication->observers != nullptr) {
      board->communication->observers->notifyDeleted(currentItem);
    }
    if (changedNets != nullptr) {
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        changedNets->add(currentItem->getNetNumber(i));
      }
    }
  }
  for (UndoableObjects_Storable* storable : *restoredObjects) {
    Item* currentItem = jrt::fast_cast<Item>(storable);
    currentItem->board = board;
    board->searchTreeManager->insert(currentItem);
    board->incrementRevision(currentItem);
    board->routingLedger()->noteInserted(currentItem);
    currentItem->clearAutorouteInfo();
    if (board->communication != nullptr && board->communication->observers != nullptr) {
      board->communication->observers->notifyNew(currentItem);
    }
    if (changedNets != nullptr) {
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        changedNets->add(currentItem->getNetNumber(i));
      }
    }
  }
}

}  // namespace fr
