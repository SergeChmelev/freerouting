#include "autoroute/maze/AutorouteEngine.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/drill/DrillPageArray.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/ExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/SortedRoomNeighbours.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/MazeSearchEngine.h"
#include "autoroute/path/FoundConnectionInserter.h"
#include "autoroute/path/FoundConnectionLocator.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "datastructures/Stoppable.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"

namespace fr {

AutorouteEngine::AutorouteEngine(RoutingBoard* board, int32_t traceClearanceClassIndex,
                                 bool maintainDatabase)
    : autorouteSearchTree(board->searchTreeManager->getAutorouteTree(traceClearanceClassIndex)),
      maintainDatabase(maintainDatabase),
      drillPageArray(nullptr),
      board(board) {
  this->netNumber = -1;
  int32_t maxDrillPageWidth = jrt::d2i(5 * board->rules->getDefaultViaDiameter());
  maxDrillPageWidth = jrt::Math::max(maxDrillPageWidth, 10000);
  this->drillPageArray = new (jrt::kOp) DrillPageArray(this->board, maxDrillPageWidth);
  this->stoppableThread = nullptr;
}

void AutorouteEngine::initConnection(int32_t netNumber, Stoppable* stoppableThread,
                                     TimeLimit* timeLimit) {
  if (this->maintainDatabase) {
    if (netNumber != this->netNumber) {
      if (this->completeExpansionRooms != nullptr) {
        // invalidate the net dependent complete free space expansion rooms.
        jrt::Collection<CompleteFreeSpaceExpansionRoom*>* roomsToRemove =
            new (jrt::kOp) jrt::ArrayList<CompleteFreeSpaceExpansionRoom*>();
        for (CompleteFreeSpaceExpansionRoom* currentRoom : *completeExpansionRooms) {
          if (currentRoom->isNetDependent()) {
            roomsToRemove->add(currentRoom);
          }
        }
        for (CompleteFreeSpaceExpansionRoom* currentRoom : *roomsToRemove) {
          this->removeCompleteExpansionRoom(currentRoom);
        }
      }
      // invalidate the neighbour rooms of the items of netNumber
      jrt::Collection<Item*>* itemList = this->board->getItems();
      for (Item* currentItem : *itemList) {
        if (currentItem->containsNet(netNumber)) {
          this->board->additionalUpdateAfterChange(currentItem);
        }
      }
    }
  }
  this->netNumber = netNumber;
  this->stoppableThread = stoppableThread;
  this->timeLimit = timeLimit;
}

AutorouteAttemptResult* AutorouteEngine::autorouteConnection(jrt::Set<Item*>* startSet,
                                                             jrt::Set<Item*>* destSet,
                                                             AutorouteControl* ctrl,
                                                             jrt::TreeSet<Item*>* rippedItemList,
                                                             jrt::Map<Item*, int32_t>* ripupCosts) {
  MazeSearchEngine* mazeSearchAlgo;
  try {
    mazeSearchAlgo = MazeSearchEngine::getInstance(startSet, destSet, this, ctrl);
  } catch (const jrt::Exception& e) {
    FRLogger::error(
        "AutorouteEngine.autoroute_connection: Exception in MazeSearchEngine.get_instance", &e);
    mazeSearchAlgo = nullptr;
  }

  if (mazeSearchAlgo == nullptr) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::FAILED,
        "Failed to route connection between " + describeConnection(startSet, destSet) +
            ", because the maze search algorithm could not be created.");
  }

  MazeSearchEngine_Result* searchResult = nullptr;
  if (mazeSearchAlgo != nullptr) {
    try {
      searchResult = mazeSearchAlgo->findConnection();
    } catch (const jrt::Exception& e) {
      FRLogger::error(
          "AutorouteEngine.autoroute_connection: Exception in mazeSearchAlgo.find_connection", &e);
    }
  }

  if (searchResult != nullptr) {
    if (ctrl->netNumber == 33 || ctrl->netNumber == 66 || ctrl->netNumber == 67) {
      std::string destinationType = searchResult->destinationDoor != nullptr
                                        ? jrt::className(searchResult->destinationDoor)
                                        : "null";
      if (FRLogger::isTraceEnabled()) {
        FRLogger::trace("compare_trace_maze_result_raw net=" + jrt::str(ctrl->netNumber) +
                        ", section=" + jrt::str(searchResult->sectionNoOfDoor) +
                        ", destination_type=" + destinationType);
      }
    }
  }

  FoundConnectionLocator* autorouteResult = nullptr;
  if (searchResult != nullptr) {
    try {
      autorouteResult = FoundConnectionLocator::getInstance(
          searchResult, ctrl, this->autorouteSearchTree, board->rules->getTraceAngleRestriction(),
          rippedItemList, ripupCosts);
    } catch (const jrt::Exception& e) {
      FRLogger::error(
          "AutorouteEngine.autoroute_connection: Exception in "
          "FoundConnectionLocator.get_instance",
          &e);
    }
  }

  // Always clean up expansion rooms from the search tree, regardless of search outcome.
  // This mirrors v1.9's behavior: clear() is called before any early returns so that
  // stale CompleteFreeSpaceExpansionRoom objects don't pollute subsequent routing attempts.
  if (!this->maintainDatabase) {
    this->clear();
  } else {
    this->resetAllDoors();
  }

  if (searchResult == nullptr) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::FAILED,
        "Failed to route connection between " + describeConnection(startSet, destSet) +
            ", because no connection was found between their nets.");
  }

  if (autorouteResult == nullptr) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::FAILED,
        "Failed to route connection between " + describeConnection(startSet, destSet) + ".");
  }

  if (isDisabledSignalLayer(ctrl, autorouteResult->startLayer) ||
      isDisabledSignalLayer(ctrl, autorouteResult->targetLayer)) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::FAILED,
        "Failed to route connection between " + describeConnection(startSet, destSet) +
            ", because some of their layers are disabled.");
  }

  if (autorouteResult->connectionItems == nullptr) {
    FRLogger::debug("AutorouteEngine.autoroute_connection: result_items != null expected");
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::SKIPPED,
        "No new connections were made between " + describeConnection(startSet, destSet) + ".");
  }

  // Delete the ripped connections while observers see the insert that follows.
  bool observersActivated = !this->board->observersActive();
  if (observersActivated) {
    this->board->startNotifyObservers();
  }
  ripConnections(board, rippedItemList, ctrl->removeUnconnectedVias);
  FoundConnectionInserter* insertFoundConnectionAlgo =
      FoundConnectionInserter::getInstance(autorouteResult, board, ctrl);

  if (observersActivated) {
    this->board->endNotifyObservers();
  }
  if (insertFoundConnectionAlgo == nullptr) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::FAILED,
        "Failed to route connection between " + describeConnection(startSet, destSet) +
            ", because the new connection could not be inserted.");
  }

  return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::ROUTED);
}

jrt::Set<int32_t>* AutorouteEngine::ripConnections(RoutingBoard* board,
                                                   jrt::Collection<Item*>* rippedItems,
                                                   bool removeUnconnectedVias) {
  jrt::TreeSet<Item*>* rippedConnections = new (jrt::kOp) jrt::TreeSet<Item*>();
  jrt::TreeSet<int32_t>* changedNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
  Item_StopConnectionOption stopConnectionOption = removeUnconnectedVias
                                                       ? Item_StopConnectionOption::NONE
                                                       : Item_StopConnectionOption::FANOUT_VIA;
  if (rippedItems != nullptr) {
    for (Item* currentRippedItem : *rippedItems) {
      rippedConnections->addAll(currentRippedItem->getConnectionItems(stopConnectionOption));
      for (int32_t i = 0; i < currentRippedItem->netCount(); i++) {
        changedNets->add(currentRippedItem->getNetNumber(i));
      }
    }
  }
  board->removeItems(rippedConnections);
  for (int32_t currentNetNumber : *changedNets) {
    board->removeTraceTails(currentNetNumber, stopConnectionOption);
  }
  return changedNets;
}

bool AutorouteEngine::isDisabledSignalLayer(AutorouteControl* ctrl, int32_t layer) {
  return !(*ctrl->layerActive)[layer] && (*board->layerStructure->layers)[layer]->isSignal;
}

std::string AutorouteEngine::describeConnection(jrt::Set<Item*>* startSet,
                                                jrt::Set<Item*>* destSet) {
  std::vector<std::string> startNames;
  for (Item* item : *startSet) {
    startNames.push_back(item->toString());
  }
  std::vector<std::string> destNames;
  for (Item* item : *destSet) {
    destNames.push_back(item->toString());
  }
  return jrt::join(", ", startNames) + " and " + jrt::join(", ", destNames);
}

bool AutorouteEngine::isStopRequested() {
  if (this->timeLimit != nullptr) {
    if (this->timeLimit->limitExceeded()) {
      return true;
    }
  }
  if (this->stoppableThread == nullptr) {
    return false;
  }
  return this->stoppableThread->isStopRequested();
}

void AutorouteEngine::clear() {
  if (completeExpansionRooms != nullptr) {
    for (CompleteFreeSpaceExpansionRoom* currentRoom : *completeExpansionRooms) {
      currentRoom->removeFromTree(this->autorouteSearchTree);
    }
  }
  completeExpansionRooms = nullptr;
  incompleteExpansionRooms = nullptr;
  expansionRoomInstanceCount = 0;
  board->clearAllItemTemporaryAutorouteData();
}

void AutorouteEngine::emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink == nullptr || intensity <= 0 || completeExpansionRooms == nullptr) {
    return;
  }
  for (CompleteFreeSpaceExpansionRoom* currentRoom : *completeExpansionRooms) {
    currentRoom->emitDiagnostic(sink, intensity);
  }
  jrt::Collection<Item*>* itemList = this->board->getItems();
  for (Item* currentItem : *itemList) {
    ItemAutorouteInfo* autorouteInfo = currentItem->getAutorouteInfo();
    if (autorouteInfo != nullptr) {
      autorouteInfo->emitDiagnostics(sink, intensity);
    }
  }
}

IncompleteFreeSpaceExpansionRoom* AutorouteEngine::addIncompleteExpansionRoom(
    TileShape* shape, int32_t layer, TileShape* containedShape) {
  IncompleteFreeSpaceExpansionRoom* newRoom =
      new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(shape, layer, containedShape);
  if (this->incompleteExpansionRooms == nullptr) {
    this->incompleteExpansionRooms = new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
  }
  this->incompleteExpansionRooms->add(newRoom);
  return newRoom;
}

IncompleteFreeSpaceExpansionRoom* AutorouteEngine::getFirstIncompleteExpansionRoom() {
  if (incompleteExpansionRooms == nullptr) {
    return nullptr;
  }
  if (incompleteExpansionRooms->isEmpty()) {
    return nullptr;
  }
  jrt::Iterator<IncompleteFreeSpaceExpansionRoom*>* it = incompleteExpansionRooms->iterator();
  return it->next();
}

void AutorouteEngine::removeIncompleteExpansionRoom(IncompleteFreeSpaceExpansionRoom* room) {
  this->removeAllDoors(room);
  incompleteExpansionRooms->remove(room);
}

void AutorouteEngine::removeCompleteExpansionRoom(CompleteFreeSpaceExpansionRoom* room) {
  // create new incomplete expansion rooms for all neighbours
  TileShape* roomShape = room->getShape();
  int32_t roomLayer = room->getLayer();
  jrt::Collection<ExpansionDoor*>* roomDoors = room->getDoors();
  for (ExpansionDoor* currentDoor : *roomDoors) {
    // Java overload resolution: static type CompleteFreeSpaceExpansionRoom selects
    // otherRoom(CompleteExpansionRoom).
    ExpansionRoom* currentNeighbour =
        currentDoor->otherRoom(static_cast<CompleteExpansionRoom*>(room));
    if (currentNeighbour == nullptr) {
      continue;
    }
    currentNeighbour->removeDoor(currentDoor);
    TileShape* neighbourShape = currentNeighbour->getShape();
    TileShape* intersection = roomShape->intersection(neighbourShape);
    if (intersection->dimension() == 1) {
      // add a new incomplete room to currentNeighbour.
      jrt::Array<int32_t>* touchingSides = roomShape->touchingSides(neighbourShape);
      jrt::Array<Line*>* lines = new (jrt::kGeom) jrt::Array<Line*>(1);
      (*lines)[0] = neighbourShape->borderLine((*touchingSides)[1])->opposite();
      Simplex* newIncompleteRoomShape = Simplex::getInstance(lines);
      IncompleteFreeSpaceExpansionRoom* newIncompleteRoom =
          addIncompleteExpansionRoom(newIncompleteRoomShape, roomLayer, intersection);
      ExpansionDoor* newDoor = new (jrt::kOp) ExpansionDoor(currentNeighbour, newIncompleteRoom, 1);
      currentNeighbour->addDoor(newDoor);
      newIncompleteRoom->addDoor(newDoor);
    }
  }
  this->removeAllDoors(room);
  room->removeFromTree(this->autorouteSearchTree);
  if (completeExpansionRooms != nullptr) {
    completeExpansionRooms->remove(room);
  } else {
    FRLogger::warn(
        "AutorouteEngine.remove_complete_expansion_room: this.completeExpansionRooms is null");
  }
  this->drillPageArray->invalidate(roomShape);
}

jrt::Collection<CompleteFreeSpaceExpansionRoom*>* AutorouteEngine::completeExpansionRoom(
    IncompleteFreeSpaceExpansionRoom* room) {
  try {
    jrt::Collection<CompleteFreeSpaceExpansionRoom*>* result =
        new (jrt::kOp) jrt::ArrayList<CompleteFreeSpaceExpansionRoom*>();
    TileShape* fromDoorShape = nullptr;
    SearchTreeObject* ignoreObject = nullptr;
    jrt::Collection<ExpansionDoor*>* roomDoors = room->getDoors();
    for (ExpansionDoor* currentDoor : *roomDoors) {
      ExpansionRoom* otherRoom = currentDoor->otherRoom(static_cast<ExpansionRoom*>(room));
      auto* freeRoom = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(otherRoom);
      if (freeRoom != nullptr && currentDoor->dimension == 2) {
        fromDoorShape = currentDoor->getShape();
        ignoreObject = freeRoom;
        break;
      }
    }
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace(
          "COMPLETE_ROOM input" + std::string(", net=") + jrt::str(this->netNumber) +
          ", layer=" + jrt::str(room->getLayer()) +
          ", room_bounds=" + describeShapeBounds(room->getShape()) +
          ", contained_bounds=" + describeShapeBounds(room->getContainedShape()) +
          ", from_door_bounds=" + describeShapeBounds(fromDoorShape) + ", ignoreObject=" +
          (ignoreObject == nullptr ? std::string("null") : jrt::className(ignoreObject)));
    }
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completedShapes =
        this->autorouteSearchTree->completeShape(room, this->netNumber, ignoreObject,
                                                 fromDoorShape);
    int32_t initialCandidateIndex = 0;
    for (IncompleteFreeSpaceExpansionRoom* initialCandidate : *completedShapes) {
      if (FRLogger::isTraceEnabled()) {
        FRLogger::trace("COMPLETE_ROOM initial_candidate" + std::string(", net=") +
                        jrt::str(this->netNumber) + ", layer=" +
                        jrt::str(initialCandidate->getLayer()) + ", index=" +
                        jrt::str(initialCandidateIndex) + ", dimension=" +
                        jrt::str(initialCandidate->getShape()->dimension()) +
                        ", incomplete_bounds=" + describeShapeBounds(initialCandidate->getShape()) +
                        ", from_door_bounds=" + describeShapeBounds(fromDoorShape));
      }
      ++initialCandidateIndex;
    }
    this->removeIncompleteExpansionRoom(room);
    bool isFirstCompletedRoom = true;
    for (IncompleteFreeSpaceExpansionRoom* currentIncompleteRoom : *completedShapes) {
      if (currentIncompleteRoom->getShape()->dimension() != 2) {
        continue;
      }
      if (isFirstCompletedRoom) {
        isFirstCompletedRoom = false;
        if (FRLogger::isTraceEnabled()) {
          FRLogger::trace("COMPLETE_ROOM first_candidate" + std::string(", net=") +
                          jrt::str(this->netNumber) + ", layer=" +
                          jrt::str(currentIncompleteRoom->getLayer()) + ", incomplete_bounds=" +
                          describeShapeBounds(currentIncompleteRoom->getShape()) +
                          ", from_door_bounds=" + describeShapeBounds(fromDoorShape));
        }
        CompleteFreeSpaceExpansionRoom* completedRoom = this->addCompleteRoom(currentIncompleteRoom);
        if (completedRoom != nullptr) {
          result->add(completedRoom);
        }
      } else {
        // the shape of the first completed room may have changed and may
        // intersect now with the other shapes. Therefore, the completed shapes
        // have to be recalculated.
        jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* currentCompletedShapes =
            this->autorouteSearchTree->completeShape(currentIncompleteRoom, this->netNumber,
                                                     ignoreObject, fromDoorShape);
        for (IncompleteFreeSpaceExpansionRoom* tmpRoom : *currentCompletedShapes) {
          if (FRLogger::isTraceEnabled()) {
            FRLogger::trace("COMPLETE_ROOM recalc_candidate" + std::string(", net=") +
                            jrt::str(this->netNumber) + ", layer=" +
                            jrt::str(tmpRoom->getLayer()) + ", incomplete_bounds=" +
                            describeShapeBounds(tmpRoom->getShape()) +
                            ", from_door_bounds=" + describeShapeBounds(fromDoorShape));
          }
          CompleteFreeSpaceExpansionRoom* completedRoom = this->addCompleteRoom(tmpRoom);
          if (completedRoom != nullptr) {
            result->add(completedRoom);
          }
        }
      }
    }
    return result;
  } catch (const jrt::Exception& e) {
    FRLogger::error("AutorouteEngine.complete_expansion_room: ", &e);
    return new (jrt::kOp) jrt::ArrayList<CompleteFreeSpaceExpansionRoom*>();
  }
}

CompleteFreeSpaceExpansionRoom* AutorouteEngine::addCompleteRoom(
    IncompleteFreeSpaceExpansionRoom* room) {
  CompleteFreeSpaceExpansionRoom* completedRoom =
      jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(calculateDoors(room));
  if (completedRoom == nullptr || completedRoom->getShape()->dimension() != 2) {
    return nullptr;
  }
  if (completeExpansionRooms == nullptr) {
    completeExpansionRooms = new (jrt::kOp) jrt::ArrayList<CompleteFreeSpaceExpansionRoom*>();
  }
  completeExpansionRooms->add(completedRoom);
  this->autorouteSearchTree->insert(static_cast<ShapeTree_Storable*>(completedRoom));
  if (FRLogger::isTraceEnabled()) {
    FRLogger::trace("COMPLETE_ROOM added" + std::string(", net=") + jrt::str(this->netNumber) +
                    ", layer=" + jrt::str(completedRoom->getLayer()) +
                    ", bounds=" + describeShapeBounds(completedRoom->getShape()));
  }
  return completedRoom;
}

std::string AutorouteEngine::describeShapeBounds(TileShape* shape) {
  if (shape == nullptr) {
    return "null";
  }
  IntBox* bounds = shape->boundingBox();
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

CompleteExpansionRoom* AutorouteEngine::calculateDoors(ExpansionRoom* room) {
  return SortedRoomNeighbours::complete(room, this);
}

void AutorouteEngine::completeNeighbourRooms(CompleteExpansionRoom* room) {
  if (room->getDoors() == nullptr) {
    return;
  }
  // Keep v1.9 semantics: completing a neighbour can mutate door topology, so
  // restart iteration on the updated door set.
  jrt::Iterator<ExpansionDoor*>* it = room->getDoors()->iterator();
  while (it->hasNext()) {
    ExpansionDoor* currentDoor = it->next();
    // cast to ExpansionRoom because ExpansionDoor.otherRoom works differently with
    // parameter type CompleteExpansionRoom.
    ExpansionRoom* neighbourRoom = currentDoor->otherRoom(static_cast<ExpansionRoom*>(room));
    if (neighbourRoom == nullptr) {
      continue;
    }
    if (auto* freeRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(neighbourRoom)) {
      this->completeExpansionRoom(freeRoom);
      it = room->getDoors()->iterator();
    } else if (auto* obstacleNeighbourRoom = jrt::fast_cast<ObstacleExpansionRoom>(neighbourRoom)) {
      if (!obstacleNeighbourRoom->allDoorsCalculated()) {
        this->calculateDoors(obstacleNeighbourRoom);
        obstacleNeighbourRoom->setDoorsCalculated(true);
      }
    }
  }
}

void AutorouteEngine::invalidateDrillPages(TileShape* shape) {
  this->drillPageArray->invalidate(shape);
}

void AutorouteEngine::removeAllDoors(ExpansionRoom* room) {
  for (ExpansionDoor* currentDoor : *room->getDoors()) {
    ExpansionRoom* otherRoom = currentDoor->otherRoom(room);
    if (otherRoom == nullptr) {
      continue;
    }
    otherRoom->removeDoor(currentDoor);
    if (auto* freeRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(otherRoom)) {
      this->removeIncompleteExpansionRoom(freeRoom);
    }
  }
  room->clearDoors();
}

jrt::Set<CompleteFreeSpaceExpansionRoom*>* AutorouteEngine::getRoomsWithTargetItems(
    jrt::Set<Item*>* items) {
  jrt::Set<CompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::TreeSet<CompleteFreeSpaceExpansionRoom*>();
  if (this->completeExpansionRooms != nullptr) {
    for (CompleteFreeSpaceExpansionRoom* currentRoom : *this->completeExpansionRooms) {
      jrt::Collection<TargetItemExpansionDoor*>* targetDoorList = currentRoom->getTargetDoors();
      for (TargetItemExpansionDoor* currentTargetDoor : *targetDoorList) {
        Item* currentTargetItem = currentTargetDoor->item;
        if (items->contains(currentTargetItem)) {
          result->add(currentRoom);
        }
      }
    }
  }
  return result;
}

bool AutorouteEngine::validate() {
  if (completeExpansionRooms == nullptr) {
    return true;
  }
  bool result = true;
  for (CompleteFreeSpaceExpansionRoom* currentRoom : *completeExpansionRooms) {
    if (!currentRoom->validate(this)) {
      result = false;
    }
  }
  return result;
}

void AutorouteEngine::resetAllDoors() {
  if (this->completeExpansionRooms != nullptr) {
    for (ExpansionRoom* currentRoom : *this->completeExpansionRooms) {
      currentRoom->resetDoors();
    }
  }
  jrt::Collection<Item*>* itemList = this->board->getItems();
  for (Item* currentItem : *itemList) {
    ItemAutorouteInfo* currentAutorouteInfo = currentItem->getAutorouteInfoPur();
    if (currentAutorouteInfo != nullptr) {
      currentAutorouteInfo->resetDoors();
      currentAutorouteInfo->setPrecalculatedConnection(nullptr);
    }
  }
  this->drillPageArray->reset();
}

int32_t AutorouteEngine::generateRoomIdNo() { return ++expansionRoomInstanceCount; }

}  // namespace fr
