#include "autoroute/expansion/Sorted45DegreeRoomNeighbours.h"

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
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

Sorted45DegreeRoomNeighbours::Sorted45DegreeRoomNeighbours(ExpansionRoom* fromRoom,
                                                           CompleteExpansionRoom* completedRoom) {
  this->fromRoom = fromRoom;
  this->completedRoom = completedRoom;
  roomShape = completedRoom->getShape()->boundingOctagon();
  sortedNeighbours = new (jrt::kOp) jrt::TreeSet<Sorted45DegreeRoomNeighbours_SortedRoomNeighbour*>();

  edgeInteriorTouchesObstacle = new (jrt::kOp) jrt::Array<bool>(8);
  for (int32_t i = 0; i < 8; i++) {
    (*edgeInteriorTouchesObstacle)[i] = false;
  }
}

CompleteExpansionRoom* Sorted45DegreeRoomNeighbours::calculate(ExpansionRoom* room, AutorouteEngine* autorouteEngine) {
  int32_t netNumber = autorouteEngine->getNetNumber();
  Sorted45DegreeRoomNeighbours* roomNeighbours = Sorted45DegreeRoomNeighbours::calculateNeighbours(
      room, netNumber, autorouteEngine->autorouteSearchTree, autorouteEngine->generateRoomIdNo());
  if (roomNeighbours == nullptr) {
    return nullptr;
  }

  // Check, that each side of the room shape has at least one touching neighbour.
  // Otherwise, improve the room shape by enlarging.
  bool edgeRemoved = roomNeighbours->tryRemoveEdgeLine(netNumber, autorouteEngine->autorouteSearchTree);
  CompleteExpansionRoom* result = roomNeighbours->completedRoom;
  if (edgeRemoved) {
    autorouteEngine->removeAllDoors(result);
    return calculate(room, autorouteEngine);
  }

  // Now calculate the new incomplete rooms together with the doors
  // between this room and the sorted neighbours.

  if (roomNeighbours->sortedNeighbours->isEmpty()) {
    if (jrt::fast_cast<ObstacleExpansionRoom>(result) != nullptr) {
      roomNeighbours->calculateEdgeIncompleteRoomsOfObstacleExpansionRoom(0, 7, autorouteEngine);
    }
  } else {
    roomNeighbours->calculateNewIncompleteRooms(autorouteEngine);
  }
  return result;
}

Sorted45DegreeRoomNeighbours* Sorted45DegreeRoomNeighbours::calculateNeighbours(ExpansionRoom* room,
                                                                                int32_t netNumber,
                                                                                ShapeSearchTree* autorouteSearchTree,
                                                                                int32_t roomIdNo) {
  TileShape* roomShape = room->getShape();
  CompleteExpansionRoom* completedRoom;
  if (jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(room) != nullptr) {
    completedRoom = new (jrt::kSearch) CompleteFreeSpaceExpansionRoom(roomShape, room->getLayer(), roomIdNo);
  } else if (auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(room)) {
    completedRoom = obstacleRoom;
  } else {
    FRLogger::warn("Sorted45DegreeRoomNeighbours.calculate_neighbours: unexpected expansion room type");
    return nullptr;
  }
  IntOctagon* roomOct = roomShape->boundingOctagon();
  Sorted45DegreeRoomNeighbours* result = new (jrt::kOp) Sorted45DegreeRoomNeighbours(room, completedRoom);
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
    IntOctagon* currentOct = currentShape->boundingOctagon();
    IntOctagon* intersection = roomOct->intersection(currentOct);
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
      // may happen at a corner from 2 diagonal lines with non integer  coordinates (--.5, ---.5).
      continue;
    }
    result->addSortedNeighbour(currentObject, currentOct, intersection);
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
          ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(completedRoom, neighbourRoom);
          neighbourRoom->addDoor(newDoor);
          completedRoom->addDoor(newDoor);
        }
      }
    }
  }
  return result;
}

IntOctagon* Sorted45DegreeRoomNeighbours::removeNotTouchingBorderLines(IntOctagon* roomOct,
                                                                       jrt::Array<bool>* edgeInteriorTouchesObstacle) {
  int32_t leftX;
  if ((*edgeInteriorTouchesObstacle)[6]) {
    leftX = roomOct->leftX;
  } else {
    leftX = -Limits::CRIT_INT;
  }

  int32_t bottomY;
  if ((*edgeInteriorTouchesObstacle)[0]) {
    bottomY = roomOct->bottomY;
  } else {
    bottomY = -Limits::CRIT_INT;
  }

  int32_t rightX;
  if ((*edgeInteriorTouchesObstacle)[2]) {
    rightX = roomOct->rightX;
  } else {
    rightX = Limits::CRIT_INT;
  }

  int32_t topY;
  if ((*edgeInteriorTouchesObstacle)[4]) {
    topY = roomOct->topY;
  } else {
    topY = Limits::CRIT_INT;
  }

  int32_t upperLeftDiagonalX;
  if ((*edgeInteriorTouchesObstacle)[5]) {
    upperLeftDiagonalX = roomOct->upperLeftDiagonalX;
  } else {
    upperLeftDiagonalX = -Limits::CRIT_INT;
  }

  int32_t lowerRightDiagonalX;
  if ((*edgeInteriorTouchesObstacle)[1]) {
    lowerRightDiagonalX = roomOct->lowerRightDiagonalX;
  } else {
    lowerRightDiagonalX = Limits::CRIT_INT;
  }

  int32_t lowerLeftDiagonalX;
  if ((*edgeInteriorTouchesObstacle)[7]) {
    lowerLeftDiagonalX = roomOct->lowerLeftDiagonalX;
  } else {
    lowerLeftDiagonalX = -Limits::CRIT_INT;
  }

  int32_t upperRightDiagonalX;
  if ((*edgeInteriorTouchesObstacle)[3]) {
    upperRightDiagonalX = roomOct->upperRightDiagonalX;
  } else {
    upperRightDiagonalX = Limits::CRIT_INT;
  }

  IntOctagon* result = new (jrt::kGeom) IntOctagon(leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                                      lowerLeftDiagonalX, upperRightDiagonalX);
  return result->normalize();
}

void Sorted45DegreeRoomNeighbours::addSortedNeighbour(SearchTreeObject* searchTreeObject, IntOctagon* neighbourShape,
                                                      IntOctagon* intersection) {
  Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* newNeighbour =
      new (jrt::kOp) Sorted45DegreeRoomNeighbours_SortedRoomNeighbour(this, searchTreeObject, neighbourShape, intersection);
  if (newNeighbour->lastTouchingSide >= 0) {
    sortedNeighbours->add(newNeighbour);
  }
}

void Sorted45DegreeRoomNeighbours::calculateEdgeIncompleteRoomsOfObstacleExpansionRoom(
    int32_t fromSideIndex, int32_t toSideIndex, AutorouteEngine* autorouteEngine) {
  if (jrt::fast_cast<ObstacleExpansionRoom>(this->fromRoom) == nullptr) {
    FRLogger::warn(
        "Sorted45DegreeRoomNeighbours.calculate_side_incomplete_rooms_of_obstacle_expansion_room:"
        " ObstacleExpansionRoom expected for this.fromRoom");
    return;
  }
  IntOctagon* boardBoundingOct = autorouteEngine->board->getBoundingBox()->boundingOctagon();
  IntPoint* currentCorner = this->roomShape->corner(fromSideIndex);
  int32_t currentSideIndex = fromSideIndex;
  for (;;) {
    int32_t nextSideNo = (currentSideIndex + 1) % 8;
    IntPoint* nextCorner = this->roomShape->corner(nextSideNo);
    if (!currentCorner->equals(nextCorner)) {
      int32_t leftX = boardBoundingOct->leftX;
      int32_t bottomY = boardBoundingOct->bottomY;
      int32_t rightX = boardBoundingOct->rightX;
      int32_t topY = boardBoundingOct->topY;
      int32_t upperLeftDiagonalX = boardBoundingOct->upperLeftDiagonalX;
      int32_t lowerRightDiagonalX = boardBoundingOct->lowerRightDiagonalX;
      int32_t lowerLeftDiagonalX = boardBoundingOct->lowerLeftDiagonalX;
      int32_t upperRightDiagonalX = boardBoundingOct->upperRightDiagonalX;
      switch (currentSideIndex) {
        case 0:
          topY = this->roomShape->bottomY;
          break;
        case 1:
          upperLeftDiagonalX = this->roomShape->lowerRightDiagonalX;
          break;
        case 2:
          leftX = this->roomShape->rightX;
          break;
        case 3:
          lowerLeftDiagonalX = this->roomShape->upperRightDiagonalX;
          break;
        case 4:
          bottomY = this->roomShape->topY;
          break;
        case 5:
          lowerRightDiagonalX = this->roomShape->upperLeftDiagonalX;
          break;
        case 6:
          rightX = this->roomShape->leftX;
          break;
        case 7:
          upperRightDiagonalX = this->roomShape->lowerLeftDiagonalX;
          break;
        default: {
          FRLogger::warn(
              "SortedOrthoganelRoomNeighbours.calculate_edge_incomplete_rooms_of_obstacle_"
              "expansion_room: currentSideIndex illegal");
          return;
        }
      }
      insertIncompleteRoom(autorouteEngine, leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                           lowerLeftDiagonalX, upperRightDiagonalX);
    }
    if (currentSideIndex == toSideIndex) {
      break;
    }
    currentSideIndex = nextSideNo;
  }
}

bool Sorted45DegreeRoomNeighbours::tryRemoveEdgeLine(int32_t netNumber, ShapeSearchTree* autorouteSearchTree) {
  auto* currentIncompleteRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(this->fromRoom);
  if (currentIncompleteRoom == nullptr) {
    return false;
  }
  IntOctagon* roomOct = jrt::fast_cast<IntOctagon>(currentIncompleteRoom->getShape());
  if (roomOct == nullptr) {
    FRLogger::warn(
        "Sorted45DegreeRoomNeighbours.tryRemoveEdgeLine: "
        "IntOctagon expected for roomShape type");
    return false;
  }
  double roomArea = roomOct->area();

  bool tryRemoveEdgeLines = false;
  for (int32_t i = 0; i < 8; i++) {
    if (!(*this->edgeInteriorTouchesObstacle)[i]) {
      FloatPoint* prevCorner = this->roomShape->cornerApprox(i);
      FloatPoint* nextCorner = this->roomShape->cornerApprox(this->roomShape->nextNo(i));
      if (prevCorner->distanceSquare(nextCorner) > 1) {
        tryRemoveEdgeLines = true;
        break;
      }
    }
  }

  if (tryRemoveEdgeLines) {
    // Touching neighbour missing at the edge side with index removeEdgeNo
    // Remove the edge line and restart the algorithm.
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace("ROOM_EDGE_REMOVE start, net=" + jrt::str(netNumber) +
                      ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) +
                      ", room_bounds=" + describeBounds(roomOct->boundingBox()));
    }

    IntOctagon* enlargedOct = removeNotTouchingBorderLines(roomOct, this->edgeInteriorTouchesObstacle);
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace("ROOM_EDGE_REMOVE enlarged, net=" + jrt::str(netNumber) +
                      ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) +
                      ", enlarged_bounds=" + describeBounds(enlargedOct->boundingBox()));
    }
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace("ROOM_EDGE_REMOVE contained, net=" + jrt::str(netNumber) +
                      ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) +
                      ", type=" + jrt::className(currentIncompleteRoom->getContainedShape()) +
                      ", bounds=" + describeBounds(currentIncompleteRoom->getContainedShape()->boundingBox()));
    }

    jrt::Collection<ExpansionDoor*>* doorList = this->completedRoom->getDoors();
    TileShape* ignoreShape = nullptr;
    SearchTreeObject* ignoreObject = nullptr;
    double maxDoorArea = 0;
    for (ExpansionDoor* currentDoor : *doorList) {
      // insert the overlapping doors with CompleteFreeSpaceExpansionRooms
      // for the information in complete_shape about the objects to ignore.
      if (currentDoor->dimension == 2) {
        CompleteExpansionRoom* otherRoom = currentDoor->otherRoom(this->completedRoom);
        {
          if (auto* room = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(otherRoom)) {
            TileShape* currentDoorShape = currentDoor->getShape();
            double currentDoorArea = currentDoorShape->area();
            if (currentDoorArea > maxDoorArea) {
              maxDoorArea = currentDoorArea;
              ignoreShape = currentDoorShape;
              ignoreObject = room;
            }
          }
        }
      }
    }
    IncompleteFreeSpaceExpansionRoom* enlargedRoom = new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(
        enlargedOct, currentIncompleteRoom->getLayer(), currentIncompleteRoom->getContainedShape());
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newRooms =
        autorouteSearchTree->completeShape(enlargedRoom, netNumber, ignoreObject, ignoreShape);
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace("ROOM_EDGE_REMOVE complete_shape, net=" + jrt::str(netNumber) +
                      ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) +
                      ", candidate_count=" + jrt::str(newRooms->size()));
    }
    if (newRooms->size() == 1) {
      // Check, that the area increases to prevent endless loop.
      IncompleteFreeSpaceExpansionRoom* newRoom = newRooms->iterator()->next();
      if (newRoom->getShape()->area() > roomArea) {
        if (FRLogger::isTraceEnabled()) {
          FRLogger::trace("ROOM_EDGE_REMOVE applied, net=" + jrt::str(netNumber) +
                          ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) +
                          ", old_bounds=" + describeBounds(roomOct->boundingBox()) +
                          ", newBounds=" + describeBounds(newRoom->getShape()->boundingBox()));
        }
        currentIncompleteRoom->setShape(newRoom->getShape());
        currentIncompleteRoom->setContainedShape(newRoom->getContainedShape());
        return true;
      }
    }
  }
  return false;
}

std::string Sorted45DegreeRoomNeighbours::describeBounds(IntBox* bounds) {
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" + jrt::str(bounds->ur->x) + "," +
         jrt::str(bounds->ur->y) + ")]";
}

void Sorted45DegreeRoomNeighbours::insertIncompleteRoom(AutorouteEngine* autorouteEngine, int32_t leftX,
                                                        int32_t bottomY, int32_t rightX, int32_t topY,
                                                        int32_t upperLeftDiagonalX, int32_t lowerRightDiagonalX,
                                                        int32_t lowerLeftDiagonalX, int32_t upperRightDiagonalX) {
  IntOctagon* newIncompleteRoomShape = new (jrt::kGeom) IntOctagon(leftX, bottomY, rightX, topY, upperLeftDiagonalX,
                                                      lowerRightDiagonalX, lowerLeftDiagonalX, upperRightDiagonalX);
  newIncompleteRoomShape = newIncompleteRoomShape->normalize();
  if (newIncompleteRoomShape->dimension() == 2) {
    IntOctagon* newContainedShape = this->roomShape->intersection(newIncompleteRoomShape);
    if (!newContainedShape->isEmpty()) {
      int32_t doorDimension = newContainedShape->dimension();
      if (doorDimension > 0) {
        FreeSpaceExpansionRoom* newRoom = autorouteEngine->addIncompleteExpansionRoom(
            newIncompleteRoomShape, this->fromRoom->getLayer(), newContainedShape);
        ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(this->completedRoom, newRoom, doorDimension);
        this->completedRoom->addDoor(newDoor);
        newRoom->addDoor(newDoor);
      }
    }
  }
}

void Sorted45DegreeRoomNeighbours::calculateNewIncompleteRoomsForObstacleExpansionRoom(
    Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* prevNeighbour,
    Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* nextNeighbour, AutorouteEngine* autorouteEngine) {
  int32_t fromSideIndex = prevNeighbour->lastTouchingSide;
  int32_t toSideIndex = nextNeighbour->firstTouchingSide;
  if (fromSideIndex == toSideIndex && prevNeighbour != nextNeighbour) {
    // no return in case of only 1 neighbour.
    return;
  }
  IntOctagon* boardBoundingOct = autorouteEngine->board->boundingBox->boundingOctagon();

  // insert the new incomplete room from prevNeighbour to the next corner of the room shape.

  int32_t leftX = boardBoundingOct->leftX;
  int32_t bottomY = boardBoundingOct->bottomY;
  int32_t rightX = boardBoundingOct->rightX;
  int32_t topY = boardBoundingOct->topY;
  int32_t upperLeftDiagonalX = boardBoundingOct->upperLeftDiagonalX;
  int32_t lowerRightDiagonalX = boardBoundingOct->lowerRightDiagonalX;
  int32_t lowerLeftDiagonalX = boardBoundingOct->lowerLeftDiagonalX;
  int32_t upperRightDiagonalX = boardBoundingOct->upperRightDiagonalX;
  switch (fromSideIndex) {
    case 0:
      topY = this->roomShape->bottomY;
      upperLeftDiagonalX = prevNeighbour->intersection->lowerRightDiagonalX;
      break;
    case 1:
      upperLeftDiagonalX = this->roomShape->lowerRightDiagonalX;
      leftX = prevNeighbour->intersection->rightX;
      break;
    case 2:
      leftX = this->roomShape->rightX;
      lowerLeftDiagonalX = prevNeighbour->intersection->upperRightDiagonalX;
      break;
    case 3:
      lowerLeftDiagonalX = this->roomShape->upperRightDiagonalX;
      bottomY = prevNeighbour->intersection->topY;
      break;
    case 4:
      bottomY = this->roomShape->topY;
      lowerRightDiagonalX = prevNeighbour->intersection->upperLeftDiagonalX;
      break;
    case 5:
      lowerRightDiagonalX = this->roomShape->upperLeftDiagonalX;
      rightX = prevNeighbour->intersection->leftX;
      break;
    case 6:
      rightX = this->roomShape->leftX;
      upperRightDiagonalX = prevNeighbour->intersection->lowerLeftDiagonalX;
      break;
    case 7:
      upperRightDiagonalX = this->roomShape->lowerLeftDiagonalX;
      topY = prevNeighbour->intersection->bottomY;
      break;
    default:
      break;
  }
  insertIncompleteRoom(autorouteEngine, leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                       lowerLeftDiagonalX, upperRightDiagonalX);

  // insert the new incomplete room from prevNeighbour to the next corner of the room shape.

  leftX = boardBoundingOct->leftX;
  bottomY = boardBoundingOct->bottomY;
  rightX = boardBoundingOct->rightX;
  topY = boardBoundingOct->topY;
  upperLeftDiagonalX = boardBoundingOct->upperLeftDiagonalX;
  lowerRightDiagonalX = boardBoundingOct->lowerRightDiagonalX;
  lowerLeftDiagonalX = boardBoundingOct->lowerLeftDiagonalX;
  upperRightDiagonalX = boardBoundingOct->upperRightDiagonalX;

  switch (toSideIndex) {
    case 0:
      topY = this->roomShape->bottomY;
      upperRightDiagonalX = nextNeighbour->intersection->lowerLeftDiagonalX;
      break;
    case 1:
      upperLeftDiagonalX = this->roomShape->lowerRightDiagonalX;
      topY = nextNeighbour->intersection->bottomY;
      break;
    case 2:
      leftX = this->roomShape->rightX;
      upperLeftDiagonalX = nextNeighbour->intersection->lowerRightDiagonalX;
      break;
    case 3:
      lowerLeftDiagonalX = this->roomShape->upperRightDiagonalX;
      leftX = nextNeighbour->intersection->rightX;
      break;
    case 4:
      bottomY = this->roomShape->topY;
      lowerLeftDiagonalX = nextNeighbour->intersection->upperRightDiagonalX;
      break;
    case 5:
      lowerRightDiagonalX = this->roomShape->upperLeftDiagonalX;
      bottomY = nextNeighbour->intersection->topY;
      break;
    case 6:
      rightX = this->roomShape->leftX;
      lowerRightDiagonalX = nextNeighbour->intersection->upperLeftDiagonalX;
      break;
    case 7:
      upperRightDiagonalX = this->roomShape->lowerLeftDiagonalX;
      rightX = nextNeighbour->intersection->leftX;
      break;
    default:
      break;
  }
  insertIncompleteRoom(autorouteEngine, leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                       lowerLeftDiagonalX, upperRightDiagonalX);

  // Insert the new incomplete rooms on the intermediate free sides of the obstacle expansion
  // room.
  int32_t currentFromSideNo = (fromSideIndex + 1) % 8;
  if (currentFromSideNo == toSideIndex) {
    return;
  }
  int32_t currentToSideNo = (toSideIndex + 7) % 8;
  this->calculateEdgeIncompleteRoomsOfObstacleExpansionRoom(currentFromSideNo, currentToSideNo, autorouteEngine);
}

void Sorted45DegreeRoomNeighbours::calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine) {
  IntOctagon* boardBoundingOct = autorouteEngine->board->boundingBox->boundingOctagon();
  Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* prevNeighbour = this->sortedNeighbours->last();
  if (jrt::fast_cast<ObstacleExpansionRoom>(this->fromRoom) != nullptr && this->sortedNeighbours->size() == 1) {
    // ObstacleExpansionRoom has only 1 neighbour
    calculateNewIncompleteRoomsForObstacleExpansionRoom(prevNeighbour, prevNeighbour, autorouteEngine);
    return;
  }

  for (Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* nextNeighbour : *this->sortedNeighbours) {
    bool insertIncompleteRoom;

    TileShape* intersection = nextNeighbour->intersection->intersection(prevNeighbour->intersection);
    if (intersection->isEmpty()) {
      insertIncompleteRoom = true;
    } else if (intersection->dimension() >= 1) {
      insertIncompleteRoom = false;
    } else {
      // Point contact (dimension == 0): touch at a corner of the room shape
      if (prevNeighbour->lastTouchingSide == nextNeighbour->firstTouchingSide) {
        insertIncompleteRoom = false;
      } else {
        insertIncompleteRoom = prevNeighbour->lastTouchingSide != (nextNeighbour->firstTouchingSide + 1) % 8;
      }
    }

    if (insertIncompleteRoom) {
      // create a door to a new incomplete expansion room between
      // the last corner of the previous neighbour and the first corner of the
      // current neighbour

      if (jrt::fast_cast<ObstacleExpansionRoom>(this->fromRoom) != nullptr &&
          nextNeighbour->firstTouchingSide != prevNeighbour->lastTouchingSide) {
        calculateNewIncompleteRoomsForObstacleExpansionRoom(prevNeighbour, nextNeighbour, autorouteEngine);
      } else {
        int32_t lx = boardBoundingOct->leftX;
        int32_t ly = boardBoundingOct->bottomY;
        int32_t rx = boardBoundingOct->rightX;
        int32_t uy = boardBoundingOct->topY;
        int32_t ulx = boardBoundingOct->upperLeftDiagonalX;
        int32_t lrx = boardBoundingOct->lowerRightDiagonalX;
        int32_t llx = boardBoundingOct->lowerLeftDiagonalX;
        int32_t urx = boardBoundingOct->upperRightDiagonalX;

        IntOctagon* prevIs = prevNeighbour->intersection;
        IntOctagon* nextIs = nextNeighbour->intersection;
        switch (nextNeighbour->firstTouchingSide) {
          case 0: {
            if (prevIs->lowerLeftDiagonalX < nextIs->lowerLeftDiagonalX) {
              urx = nextIs->lowerLeftDiagonalX;
              uy = prevIs->bottomY;
              if (prevNeighbour->lastTouchingSide == 0) {
                ulx = prevIs->lowerRightDiagonalX;
              }
            } else if (prevIs->lowerLeftDiagonalX > nextIs->lowerLeftDiagonalX) {
              rx = nextIs->leftX;
              urx = prevIs->lowerLeftDiagonalX;
            } else {  // prevNeighbour.intersection.llx == nextNeighbour.intersection.llx
              urx = nextIs->lowerLeftDiagonalX;
            }
            break;
          }
          case 1: {
            if (prevIs->bottomY < nextIs->bottomY) {
              uy = nextIs->bottomY;
              ulx = prevIs->lowerRightDiagonalX;
              if (prevNeighbour->lastTouchingSide == 1) {
                lx = prevIs->rightX;
              }
            } else if (prevIs->bottomY > nextIs->bottomY) {
              uy = prevIs->bottomY;
              urx = nextIs->lowerLeftDiagonalX;
            } else {  // prevNeighbour.intersection.ly == nextNeighbour.intersection.ly
              uy = nextIs->bottomY;
            }
            break;
          }
          case 2: {
            if (prevIs->lowerRightDiagonalX > nextIs->lowerRightDiagonalX) {
              ulx = nextIs->lowerRightDiagonalX;
              lx = prevIs->rightX;
              if (prevNeighbour->lastTouchingSide == 2) {
                llx = prevIs->upperRightDiagonalX;
              }
            } else if (prevIs->lowerRightDiagonalX < nextIs->lowerRightDiagonalX) {
              uy = nextIs->bottomY;
              ulx = prevIs->lowerRightDiagonalX;
            } else {  // prevNeighbour.intersection.lrx == nextNeighbour.intersection.lrx
              ulx = nextIs->lowerRightDiagonalX;
            }
            break;
          }
          case 3: {
            if (prevIs->rightX > nextIs->rightX) {
              lx = nextIs->rightX;
              llx = prevIs->upperRightDiagonalX;
              if (prevNeighbour->lastTouchingSide == 3) {
                ly = prevIs->topY;
              }
            } else if (prevIs->rightX < nextIs->rightX) {
              lx = prevIs->rightX;
              ulx = nextIs->lowerRightDiagonalX;
            } else {  // prevNeighbour.intersection.ry == nextNeighbour.intersection.ry
              lx = nextIs->rightX;
            }
            break;
          }
          case 4: {
            if (prevIs->upperRightDiagonalX > nextIs->upperRightDiagonalX) {
              llx = nextIs->upperRightDiagonalX;
              ly = prevIs->topY;
              if (prevNeighbour->lastTouchingSide == 4) {
                lrx = prevIs->upperLeftDiagonalX;
              }
            } else if (prevIs->upperRightDiagonalX < nextIs->upperRightDiagonalX) {
              lx = nextIs->rightX;
              llx = prevIs->upperRightDiagonalX;
            } else {  // prevNeighbour.intersection.urx == nextNeighbour.intersection.urx
              llx = nextIs->upperRightDiagonalX;
            }
            break;
          }
          case 5: {
            if (prevIs->topY > nextIs->topY) {
              ly = nextIs->topY;
              lrx = prevIs->upperLeftDiagonalX;
              if (prevNeighbour->lastTouchingSide == 5) {
                rx = prevIs->leftX;
              }
            } else if (prevIs->topY < nextIs->topY) {
              ly = prevIs->topY;
              llx = nextIs->upperRightDiagonalX;
            } else {  // prevNeighbour.intersection.uy == nextNeighbour.intersection.uy
              ly = nextIs->topY;
            }
            break;
          }
          case 6: {
            if (prevIs->upperLeftDiagonalX < nextIs->upperLeftDiagonalX) {
              lrx = nextIs->upperLeftDiagonalX;
              rx = prevIs->leftX;
              if (prevNeighbour->lastTouchingSide == 6) {
                urx = prevIs->lowerLeftDiagonalX;
              }
            } else if (prevIs->upperLeftDiagonalX > nextIs->upperLeftDiagonalX) {
              ly = nextIs->topY;
              lrx = prevIs->upperLeftDiagonalX;
            } else {  // prevNeighbour.intersection.ulx == nextNeighbour.intersection.ulx
              lrx = nextIs->upperLeftDiagonalX;
            }
            break;
          }
          case 7: {
            if (prevIs->leftX < nextIs->leftX) {
              rx = nextIs->leftX;
              urx = prevIs->lowerLeftDiagonalX;
              if (prevNeighbour->lastTouchingSide == 7) {
                uy = prevIs->bottomY;
              }
            } else if (prevIs->leftX > nextIs->leftX) {
              rx = prevIs->leftX;
              lrx = nextIs->upperLeftDiagonalX;
            } else {  // prevNeighbour.intersection.lx == nextNeighbour.intersection.lx
              rx = nextIs->leftX;
            }
            break;
          }
          default:
            FRLogger::warn("Sorted45DegreeRoomNeighbour.calculate_new_incomplete: illegal touching side");
            break;
        }
        this->insertIncompleteRoom(autorouteEngine, lx, ly, rx, uy, ulx, lrx, llx, urx);
      }
    }
    prevNeighbour = nextNeighbour;
  }
}

// ---------------------------------------------------------------------------------------------
// Sorted45DegreeRoomNeighbours_SortedRoomNeighbour
// ---------------------------------------------------------------------------------------------

Sorted45DegreeRoomNeighbours_SortedRoomNeighbour::Sorted45DegreeRoomNeighbours_SortedRoomNeighbour(
    Sorted45DegreeRoomNeighbours* outer, SearchTreeObject* searchTreeObject, IntOctagon* neighbourShape,
    IntOctagon* intersection) {
  this->outer = outer;
  this->searchTreeObject = searchTreeObject;
  this->shape = neighbourShape;
  this->intersection = intersection;
  IntOctagon* roomShape = outer->roomShape;
  jrt::Array<bool>* edgeInteriorTouchesObstacle = outer->edgeInteriorTouchesObstacle;

  if (intersection->bottomY == roomShape->bottomY && intersection->lowerLeftDiagonalX > roomShape->lowerLeftDiagonalX) {
    this->firstTouchingSide = 0;
  } else if (intersection->lowerRightDiagonalX == roomShape->lowerRightDiagonalX &&
             intersection->bottomY > roomShape->bottomY) {
    this->firstTouchingSide = 1;
  } else if (intersection->rightX == roomShape->rightX &&
             intersection->lowerRightDiagonalX < roomShape->lowerRightDiagonalX) {
    this->firstTouchingSide = 2;
  } else if (intersection->upperRightDiagonalX == roomShape->upperRightDiagonalX &&
             intersection->rightX < roomShape->rightX) {
    this->firstTouchingSide = 3;
  } else if (intersection->topY == roomShape->topY &&
             intersection->upperRightDiagonalX < roomShape->upperRightDiagonalX) {
    this->firstTouchingSide = 4;
  } else if (intersection->upperLeftDiagonalX == roomShape->upperLeftDiagonalX &&
             intersection->topY < roomShape->topY) {
    this->firstTouchingSide = 5;
  } else if (intersection->leftX == roomShape->leftX &&
             intersection->upperLeftDiagonalX > roomShape->upperLeftDiagonalX) {
    this->firstTouchingSide = 6;
  } else if (intersection->lowerLeftDiagonalX == roomShape->lowerLeftDiagonalX &&
             intersection->leftX > roomShape->leftX) {
    this->firstTouchingSide = 7;
  } else {
    // the roomShape may be contained in the neighbourShape
    this->firstTouchingSide = -1;
    this->lastTouchingSide = -1;
    return;
  }

  if (intersection->lowerLeftDiagonalX == roomShape->lowerLeftDiagonalX && intersection->bottomY > roomShape->bottomY) {
    this->lastTouchingSide = 7;
  } else if (intersection->leftX == roomShape->leftX &&
             intersection->lowerLeftDiagonalX > roomShape->lowerLeftDiagonalX) {
    this->lastTouchingSide = 6;
  } else if (intersection->upperLeftDiagonalX == roomShape->upperLeftDiagonalX &&
             intersection->leftX > roomShape->leftX) {
    this->lastTouchingSide = 5;
  } else if (intersection->topY == roomShape->topY &&
             intersection->upperLeftDiagonalX > roomShape->upperLeftDiagonalX) {
    this->lastTouchingSide = 4;
  } else if (intersection->upperRightDiagonalX == roomShape->upperRightDiagonalX &&
             intersection->topY < roomShape->topY) {
    this->lastTouchingSide = 3;
  } else if (intersection->rightX == roomShape->rightX &&
             intersection->upperRightDiagonalX < roomShape->upperRightDiagonalX) {
    this->lastTouchingSide = 2;
  } else if (intersection->lowerRightDiagonalX == roomShape->lowerRightDiagonalX &&
             intersection->rightX < roomShape->rightX) {
    this->lastTouchingSide = 1;
  } else if (intersection->bottomY == roomShape->bottomY &&
             intersection->lowerRightDiagonalX < roomShape->lowerRightDiagonalX) {
    this->lastTouchingSide = 0;
  } else {
    // the roomShape may be contained in the neighbourShape
    this->lastTouchingSide = -1;
    return;
  }

  int32_t nextSideNo = this->firstTouchingSide;
  for (;;) {
    int32_t currentSideIndex = nextSideNo;
    nextSideNo = (nextSideNo + 1) % 8;
    if (!(*edgeInteriorTouchesObstacle)[currentSideIndex]) {
      bool touchOnlyAtCorner = false;
      if (currentSideIndex == this->firstTouchingSide) {
        if (intersection->corner(currentSideIndex)->equals(roomShape->corner(nextSideNo))) {
          touchOnlyAtCorner = true;
        }
      }
      if (currentSideIndex == this->lastTouchingSide) {
        if (intersection->corner(nextSideNo)->equals(roomShape->corner(currentSideIndex))) {
          touchOnlyAtCorner = true;
        }
      }
      if (!touchOnlyAtCorner) {
        (*edgeInteriorTouchesObstacle)[currentSideIndex] = true;
      }
    }
    if (currentSideIndex == this->lastTouchingSide) {
      break;
    }
  }
}

int32_t Sorted45DegreeRoomNeighbours_SortedRoomNeighbour::compareTo(
    Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* other) {
  if (this->firstTouchingSide > other->firstTouchingSide) {
    return 1;
  }
  if (this->firstTouchingSide < other->firstTouchingSide) {
    return -1;
  }

  // now the first touch of this and other is at the same side
  IntOctagon* is1 = this->intersection;
  IntOctagon* is2 = other->intersection;
  int32_t cmpValue;

  switch (firstTouchingSide) {
    case 0:
      cmpValue = is1->corner(0)->x - is2->corner(0)->x;
      break;
    case 1:
      cmpValue = is1->corner(1)->x - is2->corner(1)->x;
      break;
    case 2:
      cmpValue = is1->corner(2)->y - is2->corner(2)->y;
      break;
    case 3:
      cmpValue = is1->corner(3)->y - is2->corner(3)->y;
      break;
    case 4:
      cmpValue = is2->corner(4)->x - is1->corner(4)->x;
      break;
    case 5:
      cmpValue = is2->corner(5)->x - is1->corner(5)->x;
      break;
    case 6:
      cmpValue = is2->corner(6)->y - is1->corner(6)->y;
      break;
    case 7:
      cmpValue = is2->corner(7)->y - is1->corner(7)->y;
      break;
    default: {
      FRLogger::warn("SortedRoomNeighbour.compareTo: firstTouchingSide out of range ");
      return 0;
    }
  }

  if (cmpValue == 0) {
    // The first touching points of this neighbour and other with the room shape are equal.
    // Compare the last touching points.
    int32_t thisTouchingSideDiff = (this->lastTouchingSide - this->firstTouchingSide + 8) % 8;
    int32_t otherTouchingSideDiff = (other->lastTouchingSide - other->firstTouchingSide + 8) % 8;
    if (thisTouchingSideDiff > otherTouchingSideDiff) {
      return 1;
    }
    if (thisTouchingSideDiff < otherTouchingSideDiff) {
      return -1;
    }
    // now the last touch of this and other is at the same side
    switch (lastTouchingSide) {
      case 0:
        cmpValue = is1->corner(1)->x - is2->corner(1)->x;
        break;
      case 1:
        cmpValue = is1->corner(2)->x - is2->corner(2)->x;
        break;
      case 2:
        cmpValue = is1->corner(3)->y - is2->corner(3)->y;
        break;
      case 3:
        cmpValue = is1->corner(4)->y - is2->corner(4)->y;
        break;
      case 4:
        cmpValue = is2->corner(5)->x - is1->corner(5)->x;
        break;
      case 5:
        cmpValue = is2->corner(6)->x - is1->corner(6)->x;
        break;
      case 6:
        cmpValue = is2->corner(7)->y - is1->corner(7)->y;
        break;
      case 7:
        cmpValue = is2->corner(0)->y - is1->corner(0)->y;
        break;
      default:
        break;
    }
  }
  if (cmpValue == 0) {
    // Deterministic tie-breaker for identical geometry
    cmpValue = this->searchTreeObject->getId() - other->searchTreeObject->getId();
  }
  return cmpValue;
}

}  // namespace fr
