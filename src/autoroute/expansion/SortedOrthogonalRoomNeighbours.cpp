#include "autoroute/expansion/SortedOrthogonalRoomNeighbours.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/ExpansionRoom.h"
#include "autoroute/expansion/FreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/SortedRoomNeighbours.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

SortedOrthogonalRoomNeighbours::SortedOrthogonalRoomNeighbours(ExpansionRoom* fromRoom,
                                                               CompleteExpansionRoom* completedRoom) {
  this->fromRoom = fromRoom;
  this->completedRoom = completedRoom;
  isObstacleExpansionRoom = jrt::fast_cast<ObstacleExpansionRoom>(fromRoom) != nullptr;
  roomShape = jrt::fast_cast<IntBox>(completedRoom->getShape());
  sortedNeighbours = new (jrt::kOp) jrt::TreeSet<SortedOrthogonalRoomNeighbours_SortedRoomNeighbour*>();
  edgeInteriorTouchesObstacle = new (jrt::kOp) jrt::Array<bool>(4);
  for (int32_t i = 0; i < 4; i++) {
    (*edgeInteriorTouchesObstacle)[i] = false;
  }
}

CompleteExpansionRoom* SortedOrthogonalRoomNeighbours::calculate(ExpansionRoom* room,
                                                                 AutorouteEngine* autorouteEngine) {
  int32_t netNumber = autorouteEngine->getNetNumber();
  SortedOrthogonalRoomNeighbours* roomNeighbours = SortedOrthogonalRoomNeighbours::calculateNeighbours(
      room, netNumber, autorouteEngine->autorouteSearchTree, autorouteEngine->generateRoomIdNo());
  if (roomNeighbours == nullptr) {
    return nullptr;
  }

  // Check, that each side of the room shape has at least one touching neighbour.
  // Otherwise, improve the room shape by enlarging.
  bool edgeRemoved = roomNeighbours->tryRemoveEdge(netNumber, autorouteEngine->autorouteSearchTree);
  CompleteExpansionRoom* result = roomNeighbours->completedRoom;
  if (edgeRemoved) {
    autorouteEngine->removeAllDoors(result);
    return calculate(room, autorouteEngine);
  }

  // Now calculate the new incomplete rooms together with the doors
  // between this room and the sorted neighbours.

  if (roomNeighbours->sortedNeighbours->isEmpty()) {
    if (auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(result)) {
      calculateIncompleteRoomsWithEmptyNeighbours(obstacleRoom, autorouteEngine);
    }
  } else {
    roomNeighbours->calculateNewIncompleteRooms(autorouteEngine);
  }
  return result;
}

void SortedOrthogonalRoomNeighbours::calculateIncompleteRoomsWithEmptyNeighbours(ObstacleExpansionRoom* room,
                                                                                 AutorouteEngine* autorouteEngine) {
  TileShape* roomShape = room->getShape();
  IntBox* roomBox = jrt::fast_cast<IntBox>(roomShape);
  if (roomBox == nullptr) {
    FRLogger::warn(
        "SortedOrthoganelRoomNeighbours.calculate_incomplete_rooms_with_empty_neighbours: "
        "IntBox expected for roomShape");
    return;
  }
  IntBox* boundingBox = autorouteEngine->board->getBoundingBox();
  for (int32_t i = 0; i < 4; i++) {
    IntBox* newRoomBox;
    switch (i) {
      case 0:
        newRoomBox = new (jrt::kGeom) IntBox(boundingBox->ll->x, boundingBox->ll->y, boundingBox->ur->x, roomBox->ll->y);
        break;
      case 1:
        newRoomBox = new (jrt::kGeom) IntBox(roomBox->ur->x, boundingBox->ll->y, boundingBox->ur->x, boundingBox->ur->y);
        break;
      case 2:
        newRoomBox = new (jrt::kGeom) IntBox(boundingBox->ll->x, roomBox->ur->y, boundingBox->ur->x, boundingBox->ur->y);
        break;
      default:  // i == 3
        newRoomBox = new (jrt::kGeom) IntBox(boundingBox->ll->x, boundingBox->ll->y, roomBox->ll->x, boundingBox->ur->y);
        break;
    }
    IntBox* newContainedBox = roomBox->intersection(newRoomBox);
    FreeSpaceExpansionRoom* newRoom =
        autorouteEngine->addIncompleteExpansionRoom(newRoomBox, room->getLayer(), newContainedBox);
    ExpansionDoor* newDoor = new (jrt::kOp) ExpansionDoor(room, newRoom, 1);
    room->addDoor(newDoor);
    newRoom->addDoor(newDoor);
  }
}

SortedOrthogonalRoomNeighbours* SortedOrthogonalRoomNeighbours::calculateNeighbours(
    ExpansionRoom* room, int32_t netNumber, ShapeSearchTree* autorouteSearchTree, int32_t roomIdNo) {
  TileShape* roomShape = room->getShape();
  IntBox* roomBox = jrt::fast_cast<IntBox>(roomShape);
  if (roomBox == nullptr) {
    FRLogger::warn("SortedOrthogonalRoomNeighbours.calculate: IntBox expected for roomShape");
    return nullptr;
  }
  CompleteExpansionRoom* completedRoom;
  if (jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(room) != nullptr) {
    completedRoom = new (jrt::kOp) CompleteFreeSpaceExpansionRoom(roomShape, room->getLayer(), roomIdNo);
  } else if (auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(room)) {
    completedRoom = obstacleRoom;
  } else {
    FRLogger::warn("SortedOrthogonalRoomNeighbours.calculate: unexpected expansion room type");
    return nullptr;
  }
  SortedOrthogonalRoomNeighbours* result = new (jrt::kOp) SortedOrthogonalRoomNeighbours(room, completedRoom);
  jrt::ArrayList<ShapeTree_TreeEntry*>* overlappingObjects = new (jrt::kOp) jrt::ArrayList<ShapeTree_TreeEntry*>();
  autorouteSearchTree->overlappingTreeEntries(roomShape, room->getLayer(), overlappingObjects);

  // Sort the overlapping objects deterministically to ensure parity with v1.9.
  overlappingObjects->sort([](ShapeTree_TreeEntry* e1, ShapeTree_TreeEntry* e2) {
    int32_t idDiff = jrt::fast_cast<SearchTreeObject>(e1->object)->getId() -
                     jrt::fast_cast<SearchTreeObject>(e2->object)->getId();
    if (idDiff != 0) {
      return idDiff;
    }
    return e1->shapeIndexInObject - e2->shapeIndexInObject;
  });

  // Calculate the touching neighbour objects and sort them in counterclock sense
  // around the border of the room shape.
  for (ShapeTree_TreeEntry* currentEntry : *overlappingObjects) {
    SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentEntry->object);
    if (static_cast<jrt::Object*>(currentObject) == static_cast<jrt::Object*>(room)) {
      continue;
    }
    auto* fsRoom = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(completedRoom);
    if (fsRoom != nullptr && !currentObject->isTraceObstacle(netNumber, currentEntry->shapeIndexInObject)) {
      fsRoom->calculateTargetDoors(currentEntry, netNumber, autorouteSearchTree);
      continue;
    }
    TileShape* currentShape = currentObject->getTreeShape(autorouteSearchTree, currentEntry->shapeIndexInObject);
    IntBox* currentBox = jrt::fast_cast<IntBox>(currentShape);
    if (currentBox == nullptr) {
      FRLogger::warn(
          "OrthogonalAutorouteEngine:calculate_sorted_neighbours: "
          "IntBox expected for currentShape");
      return nullptr;
    }
    IntBox* intersection = roomBox->intersection(currentBox);
    int32_t dimension = intersection->dimension();
    auto* obsRoom = jrt::fast_cast<ObstacleExpansionRoom>(completedRoom);
    if (dimension > 1 && obsRoom != nullptr) {
      if (auto* currentItem = jrt::fast_cast<Item>(currentObject)) {
        // only Obstacle expansion room may have a 2-dim overlap
        if (currentItem->isRoutable()) {
          ItemAutorouteInfo* itemInfo = currentItem->getAutorouteInfo();
          ObstacleExpansionRoom* currentOverlapRoom =
              itemInfo->getExpansionRoom(currentEntry->shapeIndexInObject, autorouteSearchTree);
          obsRoom->createOverlapDoor(currentOverlapRoom);
        }
      }
      continue;
    }
    if (dimension < 0) {
      FRLogger::warn("AutorouteEngine.calculate_doors: dimension >= 0 expected");
      continue;
    }
    result->addSortedNeighbour(currentObject, currentBox, intersection);
    if (dimension > 0) {
      // make  sure, that there is a door to the neighbour room.
      ExpansionRoom* neighbourRoom = nullptr;
      if (auto* exRoom = jrt::fast_cast<ExpansionRoom>(currentObject)) {
        neighbourRoom = exRoom;
      } else if (auto* currentItem = jrt::fast_cast<Item>(currentObject)) {
        if (currentItem->isRoutable()) {
          // expand the item for ripup and pushing purposes
          ItemAutorouteInfo* itemInfo = currentItem->getAutorouteInfo();
          neighbourRoom = itemInfo->getExpansionRoom(currentEntry->shapeIndexInObject, autorouteSearchTree);
        }
      }
      if (neighbourRoom != nullptr) {
        if (SortedRoomNeighbours::insertDoorOk(completedRoom, neighbourRoom, intersection)) {
          ExpansionDoor* newDoor = new (jrt::kOp) ExpansionDoor(completedRoom, neighbourRoom);
          neighbourRoom->addDoor(newDoor);
          completedRoom->addDoor(newDoor);
        }
      }
    }
  }
  return result;
}

IntBox* SortedOrthogonalRoomNeighbours::removeBorderLine(IntBox* roomBox, int32_t removeEdgeNo) {
  switch (removeEdgeNo) {
    case 0:
      return new (jrt::kGeom) IntBox(roomBox->ll->x, -Limits::CRIT_INT, roomBox->ur->x, roomBox->ur->y);
    case 1:
      return new (jrt::kGeom) IntBox(roomBox->ll->x, roomBox->ll->y, Limits::CRIT_INT, roomBox->ur->y);
    case 2:
      return new (jrt::kGeom) IntBox(roomBox->ll->x, roomBox->ll->y, roomBox->ur->x, Limits::CRIT_INT);
    case 3:
      return new (jrt::kGeom) IntBox(-Limits::CRIT_INT, roomBox->ll->y, roomBox->ur->x, roomBox->ur->y);
    default: {
      FRLogger::warn("SortedOrthogonalRoomNeighbours.removeBorderLine: illegal removeEdgeNo");
      return nullptr;
    }
  }
}

void SortedOrthogonalRoomNeighbours::calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine) {
  IntBox* boardBounds = autorouteEngine->board->boundingBox;
  SortedOrthogonalRoomNeighbours_SortedRoomNeighbour* prevNeighbour = this->sortedNeighbours->last();

  for (SortedOrthogonalRoomNeighbours_SortedRoomNeighbour* nextNeighbour : *this->sortedNeighbours) {
    if (!nextNeighbour->intersection->intersects(prevNeighbour->intersection)) {
      // create a door to a new incomplete expansion room between
      // the last corner of the previous neighbour and the first corner of the
      // current neighbour.
      switch (nextNeighbour->firstTouchingSide) {
        case 0: {
          if (prevNeighbour->lastTouchingSide == 0) {
            if (prevNeighbour->intersection->ur->x < nextNeighbour->intersection->ll->x) {
              insertIncompleteRoom(autorouteEngine, prevNeighbour->intersection->ur->x, boardBounds->ll->y,
                                   nextNeighbour->intersection->ll->x, this->roomShape->ll->y);
            }
          } else {
            if (prevNeighbour->intersection->ll->y > this->roomShape->ll->y ||
                nextNeighbour->intersection->ll->x > this->roomShape->ll->x) {
              if (isObstacleExpansionRoom) {
                // no 2-dim doors between obstacle_expansion_rooms and free space rooms allowed.
                if (prevNeighbour->lastTouchingSide == 3) {
                  insertIncompleteRoom(autorouteEngine, boardBounds->ll->x, roomShape->ll->y, roomShape->ll->x,
                                       prevNeighbour->intersection->ll->y);
                }
                insertIncompleteRoom(autorouteEngine, roomShape->ll->x, boardBounds->ll->y,
                                     nextNeighbour->intersection->ll->x, roomShape->ll->y);
              } else {
                insertIncompleteRoom(autorouteEngine, boardBounds->ll->x, boardBounds->ll->y,
                                     nextNeighbour->intersection->ll->x, prevNeighbour->intersection->ll->y);
              }
            }
          }
          break;
        }
        case 1: {
          if (prevNeighbour->lastTouchingSide == 1) {
            if (prevNeighbour->intersection->ur->y < nextNeighbour->intersection->ll->y) {
              insertIncompleteRoom(autorouteEngine, this->roomShape->ur->x, prevNeighbour->intersection->ur->y,
                                   boardBounds->ur->x, nextNeighbour->intersection->ll->y);
            }
          } else {
            if (prevNeighbour->intersection->ur->x < this->roomShape->ur->x ||
                nextNeighbour->intersection->ll->y > this->roomShape->ll->y) {
              if (isObstacleExpansionRoom) {
                // no 2-dim doors between obstacle_expansion_rooms and free space rooms allowed.
                if (prevNeighbour->lastTouchingSide == 0) {
                  insertIncompleteRoom(autorouteEngine, prevNeighbour->intersection->ur->x, boardBounds->ll->y,
                                       roomShape->ur->x, roomShape->ll->y);
                }
                insertIncompleteRoom(autorouteEngine, roomShape->ur->x, roomShape->ll->y, roomShape->ur->x,
                                     nextNeighbour->intersection->ll->y);
              } else {
                insertIncompleteRoom(autorouteEngine, prevNeighbour->intersection->ur->x, boardBounds->ll->y,
                                     boardBounds->ur->x, nextNeighbour->intersection->ll->y);
              }
            }
          }
          break;
        }
        case 2: {
          if (prevNeighbour->lastTouchingSide == 2) {
            if (prevNeighbour->intersection->ll->x > nextNeighbour->intersection->ur->x) {
              insertIncompleteRoom(autorouteEngine, nextNeighbour->intersection->ur->x, this->roomShape->ur->y,
                                   prevNeighbour->intersection->ll->x, boardBounds->ur->y);
            }
          } else {
            if (prevNeighbour->intersection->ur->y < this->roomShape->ur->y ||
                nextNeighbour->intersection->ur->x < this->roomShape->ur->x) {
              if (isObstacleExpansionRoom) {
                // no 2-dim doors between obstacle_expansion_rooms and free space rooms allowed.
                if (prevNeighbour->lastTouchingSide == 1) {
                  insertIncompleteRoom(autorouteEngine, roomShape->ur->x, prevNeighbour->intersection->ur->y,
                                       boardBounds->ur->x, roomShape->ur->y);
                }
                insertIncompleteRoom(autorouteEngine, nextNeighbour->intersection->ur->x, roomShape->ur->y,
                                     roomShape->ur->x, boardBounds->ur->y);
              } else {
                insertIncompleteRoom(autorouteEngine, nextNeighbour->intersection->ur->x,
                                     prevNeighbour->intersection->ur->y, boardBounds->ur->x, boardBounds->ur->y);
              }
            }
          }
          break;
        }
        case 3: {
          if (prevNeighbour->lastTouchingSide == 3) {
            if (prevNeighbour->intersection->ll->y > nextNeighbour->intersection->ur->y) {
              insertIncompleteRoom(autorouteEngine, boardBounds->ll->x, nextNeighbour->intersection->ur->y,
                                   this->roomShape->ll->x, prevNeighbour->intersection->ll->y);
            }
          } else {
            if (nextNeighbour->intersection->ur->y < this->roomShape->ur->y ||
                prevNeighbour->intersection->ll->x > this->roomShape->ll->x) {
              if (isObstacleExpansionRoom) {
                // no 2-dim doors between obstacle_expansion_rooms and free space rooms allowed.
                if (prevNeighbour->lastTouchingSide == 2) {
                  insertIncompleteRoom(autorouteEngine, roomShape->ll->x, roomShape->ur->y,
                                       prevNeighbour->intersection->ll->x, boardBounds->ur->y);
                }
                insertIncompleteRoom(autorouteEngine, boardBounds->ll->x, nextNeighbour->intersection->ur->y,
                                     roomShape->ll->x, roomShape->ur->y);
              } else {
                insertIncompleteRoom(autorouteEngine, boardBounds->ll->x, nextNeighbour->intersection->ur->y,
                                     prevNeighbour->intersection->ll->x, boardBounds->ur->y);
              }
            }
          }
          break;
        }
        default:
          FRLogger::warn("SortedOrthogonalRoomNeighbour.calculate_new_incomplete: illegal touching side");
          break;
      }
    }
    prevNeighbour = nextNeighbour;
  }
}

void SortedOrthogonalRoomNeighbours::insertIncompleteRoom(AutorouteEngine* autorouteEngine, int32_t llX,
                                                          int32_t llY, int32_t urX, int32_t urY) {
  IntBox* newIncompleteRoomShape = new (jrt::kGeom) IntBox(llX, llY, urX, urY);
  if (newIncompleteRoomShape->dimension() == 2) {
    IntBox* newContainedShape = this->roomShape->intersection(newIncompleteRoomShape);
    if (!newContainedShape->isEmpty()) {
      int32_t doorDimension = newIncompleteRoomShape->intersection(this->roomShape)->dimension();
      if (doorDimension > 0) {
        FreeSpaceExpansionRoom* newRoom = autorouteEngine->addIncompleteExpansionRoom(
            newIncompleteRoomShape, this->fromRoom->getLayer(), newContainedShape);
        ExpansionDoor* newDoor = new (jrt::kOp) ExpansionDoor(this->completedRoom, newRoom, doorDimension);
        this->completedRoom->addDoor(newDoor);
        newRoom->addDoor(newDoor);
      }
    }
  }
}

bool SortedOrthogonalRoomNeighbours::tryRemoveEdge(int32_t netNumber, ShapeSearchTree* autorouteSearchTree) {
  auto* currentIncompleteRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(this->fromRoom);
  if (currentIncompleteRoom == nullptr) {
    return false;
  }
  IntBox* roomBox = jrt::fast_cast<IntBox>(currentIncompleteRoom->getShape());
  if (roomBox == nullptr) {
    FRLogger::warn("SortedOrthogonalRoomNeighbours.try_remove_edge: IntBox expected for roomShape type");
    return false;
  }
  double roomArea = roomBox->area();

  int32_t removeEdgeNo = -1;
  for (int32_t i = 0; i < 4; i++) {
    if (!(*this->edgeInteriorTouchesObstacle)[i]) {
      removeEdgeNo = i;
      break;
    }
  }

  if (removeEdgeNo >= 0) {
    // Touching neighbour missing at the edge side with index removeEdgeNo
    // Remove the edge line and restart the algorithm.
    FRLogger::trace("ROOM_EDGE_REMOVE start, net=" + jrt::str(netNumber) +
                    ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                    jrt::str(removeEdgeNo) + ", room_bounds=" + jrt::str(roomBox));
    IntBox* enlargedBox = removeBorderLine(roomBox, removeEdgeNo);
    jrt::Collection<ExpansionDoor*>* doorList = this->completedRoom->getDoors();
    TileShape* ignoreShape = nullptr;
    SearchTreeObject* ignoreObject = nullptr;
    double maxDoorArea = 0;
    int32_t ignoreCandidateCount = 0;
    int32_t equalAreaTieCount = 0;
    for (ExpansionDoor* currentDoor : *doorList) {
      // insert the overlapping doors with CompleteFreeSpaceExpansionRooms
      // for the information in complete_shape about the objects to ignore.
      if (currentDoor->dimension == 2) {
        CompleteExpansionRoom* otherRoom = currentDoor->otherRoom(this->completedRoom);
        {
          if (auto* freeSpaceRoom = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(otherRoom)) {
            TileShape* currentDoorShape = currentDoor->getShape();
            double currentDoorArea = currentDoorShape->area();
            ++ignoreCandidateCount;
            FRLogger::trace("ROOM_EDGE_REMOVE ignore_candidate, net=" + jrt::str(netNumber) +
                            ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                            jrt::str(removeEdgeNo) + ", candidate_no=" + jrt::str(ignoreCandidateCount) +
                            ", candidate_bounds=" + jrt::str(currentDoorShape->boundingBox()) +
                            ", candidate_area=" + jrt::str(currentDoorArea));
            if (currentDoorArea > maxDoorArea) {
              maxDoorArea = currentDoorArea;
              ignoreShape = currentDoorShape;
              ignoreObject = freeSpaceRoom;
              FRLogger::trace("ROOM_EDGE_REMOVE ignore_selected, net=" + jrt::str(netNumber) +
                              ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                              jrt::str(removeEdgeNo) + ", reason=larger_area, selected_bounds=" +
                              jrt::str(currentDoorShape->boundingBox()) +
                              ", selected_area=" + jrt::str(currentDoorArea));
            } else if (jrt::Double::compare(currentDoorArea, maxDoorArea) == 0) {
              ++equalAreaTieCount;
              FRLogger::trace("ROOM_EDGE_REMOVE ignore_tie, net=" + jrt::str(netNumber) +
                              ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                              jrt::str(removeEdgeNo) + ", tie_no=" + jrt::str(equalAreaTieCount) +
                              ", tie_bounds=" + jrt::str(currentDoorShape->boundingBox()) +
                              ", tie_area=" + jrt::str(currentDoorArea));
            }
          }
        }
      }
    }
    FRLogger::trace("ROOM_EDGE_REMOVE ignore_summary, net=" + jrt::str(netNumber) +
                    ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                    jrt::str(removeEdgeNo) + ", candidate_count=" + jrt::str(ignoreCandidateCount) +
                    ", tie_count=" + jrt::str(equalAreaTieCount) + ", selected_bounds=" +
                    (ignoreShape == nullptr ? std::string("null") : jrt::str(ignoreShape->boundingBox())) +
                    ", selected_area=" + jrt::str(maxDoorArea));
    IncompleteFreeSpaceExpansionRoom* enlargedRoom = new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(
        enlargedBox, currentIncompleteRoom->getLayer(), currentIncompleteRoom->getContainedShape());
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newRooms =
        autorouteSearchTree->completeShape(enlargedRoom, netNumber, ignoreObject, ignoreShape);
    FRLogger::trace("ROOM_EDGE_REMOVE complete_shape, net=" + jrt::str(netNumber) +
                    ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                    jrt::str(removeEdgeNo) + ", enlarged_bounds=" + jrt::str(enlargedBox) +
                    ", candidate_count=" + jrt::str(newRooms->size()));
    if (newRooms->size() == 1) {
      // Check, that the area increases to prevent endless loop.
      IncompleteFreeSpaceExpansionRoom* newRoom = newRooms->iterator()->next();
      if (newRoom->getShape()->area() > roomArea) {
        FRLogger::trace("ROOM_EDGE_REMOVE applied, net=" + jrt::str(netNumber) +
                        ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                        jrt::str(removeEdgeNo) + ", old_bounds=" + jrt::str(roomBox) +
                        ", newBounds=" + jrt::str(newRoom->getShape()->boundingBox()));
        currentIncompleteRoom->setShape(newRoom->getShape());
        currentIncompleteRoom->setContainedShape(newRoom->getContainedShape());
        return true;
      }
    }
  }
  return false;
}

void SortedOrthogonalRoomNeighbours::addSortedNeighbour(SearchTreeObject* searchTreeObject, IntBox* neighbourShape,
                                                        IntBox* intersection) {
  SortedOrthogonalRoomNeighbours_SortedRoomNeighbour* newNeighbour =
      new (jrt::kOp) SortedOrthogonalRoomNeighbours_SortedRoomNeighbour(this, searchTreeObject, neighbourShape, intersection);
  sortedNeighbours->add(newNeighbour);
}

// ---------------------------------------------------------------------------------------------
// SortedOrthogonalRoomNeighbours_SortedRoomNeighbour
// ---------------------------------------------------------------------------------------------

SortedOrthogonalRoomNeighbours_SortedRoomNeighbour::SortedOrthogonalRoomNeighbours_SortedRoomNeighbour(
    SortedOrthogonalRoomNeighbours* outer, SearchTreeObject* searchTreeObject, IntBox* neighbourShape,
    IntBox* intersection) {
  this->outer = outer;
  this->searchTreeObject = searchTreeObject;
  this->shape = neighbourShape;
  this->intersection = intersection;
  IntBox* roomShape = outer->roomShape;
  jrt::Array<bool>* edgeInteriorTouchesObstacle = outer->edgeInteriorTouchesObstacle;

  if (intersection->ll->y == roomShape->ll->y && intersection->ur->x > roomShape->ll->x &&
      intersection->ll->x < roomShape->ur->x) {
    (*edgeInteriorTouchesObstacle)[0] = true;
  }
  if (intersection->ur->x == roomShape->ur->x && intersection->ur->y > roomShape->ll->y &&
      intersection->ll->y < roomShape->ur->y) {
    (*edgeInteriorTouchesObstacle)[1] = true;
  }
  if (intersection->ur->y == roomShape->ur->y && intersection->ur->x > roomShape->ll->x &&
      intersection->ll->x < roomShape->ur->x) {
    (*edgeInteriorTouchesObstacle)[2] = true;
  }
  if (intersection->ll->x == roomShape->ll->x && intersection->ur->y > roomShape->ll->y &&
      intersection->ll->y < roomShape->ur->y) {
    (*edgeInteriorTouchesObstacle)[3] = true;
  }

  if (intersection->ll->y == roomShape->ll->y && intersection->ll->x > roomShape->ll->x) {
    this->firstTouchingSide = 0;
  } else if (intersection->ur->x == roomShape->ur->x && intersection->ll->y > roomShape->ll->y) {
    this->firstTouchingSide = 1;
  } else if (intersection->ur->y == roomShape->ur->y) {
    this->firstTouchingSide = 2;
  } else if (intersection->ll->x == roomShape->ll->x) {
    this->firstTouchingSide = 3;
  } else {
    FRLogger::warn("SortedRoomNeighbour: case not expected");
    this->firstTouchingSide = -1;
  }

  if (intersection->ll->x == roomShape->ll->x && intersection->ll->y > roomShape->ll->y) {
    this->lastTouchingSide = 3;
  } else if (intersection->ur->y == roomShape->ur->y && intersection->ll->x > roomShape->ll->x) {
    this->lastTouchingSide = 2;
  } else if (intersection->ur->x == roomShape->ur->x) {
    this->lastTouchingSide = 1;
  } else if (intersection->ll->y == roomShape->ll->y) {
    this->lastTouchingSide = 0;
  } else {
    FRLogger::warn("SortedRoomNeighbour: case not expected");
    this->lastTouchingSide = -1;
  }
}

int32_t SortedOrthogonalRoomNeighbours_SortedRoomNeighbour::compareTo(
    SortedOrthogonalRoomNeighbours_SortedRoomNeighbour* other) {
  if (this->firstTouchingSide > other->firstTouchingSide) {
    return 1;
  }
  if (this->firstTouchingSide < other->firstTouchingSide) {
    return -1;
  }

  // now the first touch of this and other is at the same side
  IntBox* is1 = this->intersection;
  IntBox* is2 = other->intersection;
  int32_t cmpValue;

  switch (firstTouchingSide) {
    case 0:
      cmpValue = is1->ll->x - is2->ll->x;
      break;
    case 1:
      cmpValue = is1->ll->y - is2->ll->y;
      break;
    case 2:
      cmpValue = is2->ur->x - is1->ur->x;
      break;
    case 3:
      cmpValue = is2->ur->y - is1->ur->y;
      break;
    default: {
      FRLogger::warn("SortedRoomNeighbour.compareTo: firstTouchingSide out of range ");
      return 0;
    }
  }
  if (cmpValue == 0) {
    // The first touching points of this neighbour and other with the room shape are equal.
    // Compare the last touching points.
    int32_t thisTouchingSideDiff = (this->lastTouchingSide - this->firstTouchingSide + 4) % 4;
    int32_t otherTouchingSideDiff = (other->lastTouchingSide - other->firstTouchingSide + 4) % 4;
    if (thisTouchingSideDiff > otherTouchingSideDiff) {
      return 1;
    }
    if (thisTouchingSideDiff < otherTouchingSideDiff) {
      return -1;
    }

    // now the last touch of this and other is at the same side
    switch (lastTouchingSide) {
      case 0:
        cmpValue = is1->ur->x - is2->ur->x;
        break;
      case 1:
        cmpValue = is1->ur->y - is2->ur->y;
        break;
      case 2:
        cmpValue = is2->ll->x - is1->ll->x;
        break;
      case 3:
        cmpValue = is2->ll->y - is1->ll->y;
        break;
      default: {
        FRLogger::warn("SortedRoomNeighbour.compareTo: firstTouchingSide out of range ");
        return 0;
      }
    }
  }
  if (cmpValue == 0) {
    // Deterministic tie-breaker for identical geometry
    cmpValue = this->searchTreeObject->getId() - other->searchTreeObject->getId();
  }
  return cmpValue;
}

}  // namespace fr
