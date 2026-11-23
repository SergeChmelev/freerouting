#include "autoroute/expansion/SortedRoomNeighbours.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/ExpansionRoom.h"
#include "autoroute/expansion/FreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/Sorted45DegreeRoomNeighbours.h"
#include "autoroute/expansion/SortedOrthogonalRoomNeighbours.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/searchtree/ShapeSearchTree45Degree.h"
#include "board/searchtree/ShapeSearchTree90Degree.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/ShapeTree.h"
#include "datastructures/Signum.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

SortedRoomNeighbours::SortedRoomNeighbours(ExpansionRoom* fromRoom, CompleteExpansionRoom* completedRoom) {
  this->fromRoom = fromRoom;
  this->completedRoom = completedRoom;
  roomShape = completedRoom->getShape();
  sortedNeighbours = new (jrt::kOp) jrt::TreeSet<SortedRoomNeighbours_SortedRoomNeighbour*>();
  ownNetObjects = new (jrt::kOp) jrt::ArrayList<ShapeTree_TreeEntry*>();
}

CompleteExpansionRoom* SortedRoomNeighbours::complete(ExpansionRoom* room, AutorouteEngine* autorouteEngine) {
  switch (selectCalculationMode(autorouteEngine->autorouteSearchTree)) {
    case SortedRoomNeighbours_CalculationMode::ORTHOGONAL:
      return SortedOrthogonalRoomNeighbours::calculate(room, autorouteEngine);
    case SortedRoomNeighbours_CalculationMode::DEGREE_45:
      return Sorted45DegreeRoomNeighbours::calculate(room, autorouteEngine);
    case SortedRoomNeighbours_CalculationMode::ANY_ANGLE:
      return SortedRoomNeighbours::calculate(room, autorouteEngine);
  }
  return nullptr;
}

SortedRoomNeighbours_CalculationMode SortedRoomNeighbours::selectCalculationMode(ShapeSearchTree* searchTree) {
  if (dynamic_cast<ShapeSearchTree90Degree*>(searchTree) != nullptr) {
    return SortedRoomNeighbours_CalculationMode::ORTHOGONAL;
  }
  if (dynamic_cast<ShapeSearchTree45Degree*>(searchTree) != nullptr) {
    return SortedRoomNeighbours_CalculationMode::DEGREE_45;
  }
  return SortedRoomNeighbours_CalculationMode::ANY_ANGLE;
}

CompleteExpansionRoom* SortedRoomNeighbours::calculate(ExpansionRoom* room, AutorouteEngine* autorouteEngine) {
  int32_t netNumber = autorouteEngine->getNetNumber();

  SortedRoomNeighbours* roomNeighbours = calculateNeighbours(
      room, netNumber, autorouteEngine->autorouteSearchTree, autorouteEngine->generateRoomIdNo());

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
    if (jrt::fast_cast<ObstacleExpansionRoom>(result) != nullptr) {
      calculateIncompleteRoomsWithEmptyNeighbours(jrt::fast_cast<ObstacleExpansionRoom>(room), autorouteEngine);
    }
  } else {
    roomNeighbours->calculateNewIncompleteRooms(autorouteEngine);
    if (result->getShape()->dimension() < 2) {
      FRLogger::trace(
          "AutorouteEngine.calculate_new_incomplete_rooms_with_more_than_1_neighbour: "
          "unexpected dimension for smoothened_shape");
    }
  }

  if (auto* freeRoom = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(result)) {
    calculateTargetDoors(freeRoom, roomNeighbours->ownNetObjects, autorouteEngine);
  }
  return result;
}

void SortedRoomNeighbours::calculateIncompleteRoomsWithEmptyNeighbours(ObstacleExpansionRoom* room,
                                                                       AutorouteEngine* autorouteEngine) {
  TileShape* roomShape = room->getShape();
  for (int32_t i = 0; i < roomShape->borderLineCount(); i++) {
    Line* currentLine = roomShape->borderLine(i);
    if (SortedRoomNeighbours::insertDoorOk(room, currentLine)) {
      jrt::Array<Line*>* shapeLine = new (jrt::kGeom) jrt::Array<Line*>(1);
      (*shapeLine)[0] = currentLine->opposite();
      TileShape* newRoomShape = new (jrt::kGeom) Simplex(shapeLine);
      TileShape* newContainedShape = roomShape->intersection(newRoomShape);
      FreeSpaceExpansionRoom* newRoom =
          autorouteEngine->addIncompleteExpansionRoom(newRoomShape, room->getLayer(), newContainedShape);
      ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(room, newRoom, 1);
      room->addDoor(newDoor);
      newRoom->addDoor(newDoor);
    }
  }
}

void SortedRoomNeighbours::calculateTargetDoors(CompleteFreeSpaceExpansionRoom* room,
                                                jrt::Collection<ShapeTree_TreeEntry*>* ownNetObjects,
                                                AutorouteEngine* autorouteEngine) {
  if (!ownNetObjects->isEmpty()) {
    room->setNetDependent();
  }
  for (ShapeTree_TreeEntry* currentEntry : *ownNetObjects) {
    if (auto* currentObject = jrt::fast_cast<Connectable>(currentEntry->object)) {
      if (currentObject->containsNet(autorouteEngine->getNetNumber())) {
        TileShape* currentConnectionShape = currentObject->getTraceConnectionShape(
            autorouteEngine->autorouteSearchTree, currentEntry->shapeIndexInObject);
        if (currentConnectionShape != nullptr && room->getShape()->intersects(currentConnectionShape)) {
          Item* currentItem = jrt::fast_cast<Item>(currentObject);
          TargetItemExpansionDoor* newTargetDoor = new (jrt::kSearch) TargetItemExpansionDoor(
              currentItem, currentEntry->shapeIndexInObject, room, autorouteEngine->autorouteSearchTree);
          room->addTargetDoor(newTargetDoor);
        }
      }
    }
  }
}

SortedRoomNeighbours* SortedRoomNeighbours::calculateNeighbours(ExpansionRoom* room, int32_t netNumber,
                                                                ShapeSearchTree* autorouteSearchTree,
                                                                int32_t roomIdNo) {
  TileShape* roomShape = room->getShape();
  CompleteExpansionRoom* completedRoom;
  if (jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(room) != nullptr) {
    completedRoom = new (jrt::kSearch) CompleteFreeSpaceExpansionRoom(roomShape, room->getLayer(), roomIdNo);
  } else if (auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(room)) {
    completedRoom = obstacleRoom;
  } else {
    FRLogger::warn("SortedRoomNeighbours.calculate: unexpected expansion room type");
    return nullptr;
  }
  SortedRoomNeighbours* result = new (jrt::kOp) SortedRoomNeighbours(room, completedRoom);
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
    if ((jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(room) != nullptr) &&
        !currentObject->isTraceObstacle(netNumber, currentEntry->shapeIndexInObject)) {
      // delay processing the target doors until the room shape will not change anymore
      result->ownNetObjects->add(currentEntry);
      continue;
    }
    TileShape* currentShape = currentObject->getTreeShape(autorouteSearchTree, currentEntry->shapeIndexInObject);
    TileShape* intersection = roomShape->intersection(currentShape);
    int32_t dimension = intersection->dimension();
    if (dimension > 1) {
      auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(completedRoom);
      auto* currentItem = jrt::fast_cast<Item>(currentObject);
      if (obstacleRoom != nullptr && currentItem != nullptr) {
        // only Obstacle expansion room may have a 2-dim overlap
        if (currentItem->isRoutable()) {
          ItemAutorouteInfo* itemInfo = currentItem->getAutorouteInfo();
          ObstacleExpansionRoom* currentOverlapRoom =
              itemInfo->getExpansionRoom(currentEntry->shapeIndexInObject, autorouteSearchTree);
          obstacleRoom->createOverlapDoor(currentOverlapRoom);
        }
      } else {
        FRLogger::trace(
            "SortedRoomNeighbours.calculate: "
            "unexpected area overlap of free space expansion room");
      }
      continue;
    }
    if (dimension < 0) {
      FRLogger::debug("SortedRoomNeighbours.calculate: dimension >= 0 expected");
      continue;
    }
    if (dimension == 1) {
      jrt::Array<int32_t>* touchingSides = roomShape->touchingSides(currentShape);
      if (touchingSides->length != 2) {
        FRLogger::debug("SortedRoomNeighbours.calculate: touchingSides length 2 expected");
        continue;
      }
      result->addSortedNeighbour(currentObject, currentShape, intersection, (*touchingSides)[0], (*touchingSides)[1],
                                 false, false);
      // make  sure, that there is a door to the neighbour room.
      ExpansionRoom* neighbourRoom = nullptr;
      if (auto* targetExpansionRoom = jrt::fast_cast<ExpansionRoom>(currentObject)) {
        neighbourRoom = targetExpansionRoom;
      } else if (auto* currentItem = jrt::fast_cast<Item>(currentObject)) {
        if (currentItem->isRoutable()) {
          // expand the item for ripup and pushing purposes
          ItemAutorouteInfo* itemInfo = currentItem->getAutorouteInfo();
          neighbourRoom = itemInfo->getExpansionRoom(currentEntry->shapeIndexInObject, autorouteSearchTree);
        }
      }
      if (neighbourRoom != nullptr) {
        if (SortedRoomNeighbours::insertDoorOk(completedRoom, neighbourRoom, intersection)) {
          ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(completedRoom, neighbourRoom, 1);
          neighbourRoom->addDoor(newDoor);
          completedRoom->addDoor(newDoor);
        }
      }
    } else {
      // dimension = 0
      Point* touchingPoint = intersection->corner(0);
      int32_t roomCornerNo = roomShape->equalsCorner(touchingPoint);
      bool roomTouchIsCorner;
      int32_t touchingSideNoOfRoom;
      if (roomCornerNo >= 0) {
        roomTouchIsCorner = true;
        touchingSideNoOfRoom = roomCornerNo;
      } else {
        roomTouchIsCorner = false;
        touchingSideNoOfRoom = roomShape->containsOnBorderLineNo(touchingPoint);
        if (touchingSideNoOfRoom < 0) {
          FRLogger::debug("SortedRoomNeighbours.calculate: touchingSideNoOfRoom >= 0 expected");
        }
      }
      int32_t neighbourRoomCornerNo = currentShape->equalsCorner(touchingPoint);
      bool neighbourRoomTouchIsCorner;
      int32_t touchingSideNoOfNeighbourRoom;
      if (neighbourRoomCornerNo >= 0) {
        neighbourRoomTouchIsCorner = true;
        // The previous border line is preferred to make the shape of the incomplete room as big
        // as possible
        touchingSideNoOfNeighbourRoom = currentShape->prevNo(neighbourRoomCornerNo);
      } else {
        neighbourRoomTouchIsCorner = false;
        touchingSideNoOfNeighbourRoom = currentShape->containsOnBorderLineNo(touchingPoint);
        if (touchingSideNoOfNeighbourRoom < 0) {
          FRLogger::debug("SortedRoomNeighbours.calculate: touchingSideNoOfNeighbourRoom >= 0 expected");
        }
      }
      result->addSortedNeighbour(currentObject, currentShape, intersection, touchingSideNoOfRoom,
                                 touchingSideNoOfNeighbourRoom, roomTouchIsCorner, neighbourRoomTouchIsCorner);
    }
  }
  return result;
}

bool SortedRoomNeighbours::insertDoorOk(ExpansionRoom* room1, ExpansionRoom* room2, TileShape* doorShape) {
  if (room1->doorExists(room2)) {
    return false;
  }
  auto* obsRoom1 = jrt::fast_cast<ObstacleExpansionRoom>(room1);
  auto* obsRoom2 = jrt::fast_cast<ObstacleExpansionRoom>(room2);
  if (obsRoom1 != nullptr && obsRoom2 != nullptr) {
    Item* firstItem = obsRoom1->getItem();
    Item* secondItem = obsRoom2->getItem();
    // insert only overlap_doors between items of the same net for performance reasons.
    return firstItem->sharesNet(secondItem);
  }
  if (obsRoom1 == nullptr && obsRoom2 == nullptr) {
    return true;
  }
  // Insert 1 dimensional doors of trace rooms only, if they are parallel to the trace line.
  // Otherwise, there may be check ripup problems with entering at the wrong side at a fork.
  Line* doorLine = nullptr;
  Point* prevCorner = doorShape->corner(0);
  int32_t cornerCount = doorShape->borderLineCount();
  for (int32_t i = 1; i < cornerCount; i++) {
    Point* currentCorner = doorShape->corner(i);
    if (!currentCorner->equals(prevCorner)) {
      doorLine = doorShape->borderLine(i - 1);
      break;
    }
    prevCorner = currentCorner;
  }
  if (obsRoom1 != nullptr) {
    if (!insertDoorOk(obsRoom1, doorLine)) {
      return false;
    }
  }
  if (obsRoom2 != nullptr) {
    return insertDoorOk(obsRoom2, doorLine);
  }
  return true;
}

bool SortedRoomNeighbours::insertDoorOk(ObstacleExpansionRoom* room, Line* doorLine) {
  if (doorLine == nullptr) {
    FRLogger::warn("SortedRoomNeighbours.insert_door_ok: doorLine is null");
    return false;
  }
  Item* currentItem = room->getItem();
  if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
    int32_t roomIndex = room->getIndexInItem();
    if (roomIndex == 0 || roomIndex == currentTrace->tileShapeCount() - 1) {
      Line* currentTraceLine = (*currentTrace->polyline()->lines)[roomIndex + 1];
      return currentTraceLine->isParallel(doorLine);
    }
  }
  return true;
}

void SortedRoomNeighbours::addSortedNeighbour(SearchTreeObject* searchTreeObject, TileShape* neighbourShape,
                                              TileShape* intersection, int32_t touchingSideNoOfRoom,
                                              int32_t touchingSideNoOfNeighbourRoom, bool roomTouchIsCorner,
                                              bool neighbourRoomTouchIsCorner) {
  SortedRoomNeighbours_SortedRoomNeighbour* newNeighbour = new (jrt::kOp) SortedRoomNeighbours_SortedRoomNeighbour(
      this, searchTreeObject, neighbourShape, intersection, touchingSideNoOfRoom, touchingSideNoOfNeighbourRoom,
      roomTouchIsCorner, neighbourRoomTouchIsCorner);
  sortedNeighbours->add(newNeighbour);
}

bool SortedRoomNeighbours::tryRemoveEdge(int32_t netNumber, ShapeSearchTree* autorouteSearchTree) {
  auto* currentIncompleteRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(this->fromRoom);
  if (currentIncompleteRoom == nullptr) {
    return false;
  }
  int32_t removeEdgeNo = -1;
  Simplex* roomSimplex = currentIncompleteRoom->getShape()->toSimplex();
  double roomShapeArea = roomSimplex->area();

  int32_t prevEdgeNo = -1;
  int32_t currentEdgeNo = 0;
  for (SortedRoomNeighbours_SortedRoomNeighbour* nextNeighbour : *sortedNeighbours) {
    if (nextNeighbour->touchingSideNoOfRoom == prevEdgeNo) {
      continue;
    }
    if (nextNeighbour->touchingSideNoOfRoom == currentEdgeNo) {
      prevEdgeNo = currentEdgeNo;
      ++currentEdgeNo;
    } else {
      // On the edge side with index currentEdgeNo is no touching
      // neighbour.
      removeEdgeNo = currentEdgeNo;
      break;
    }
  }

  if (removeEdgeNo < 0 && currentEdgeNo < roomSimplex->borderLineCount()) {
    // missing touching neighbour at the last edge side.
    removeEdgeNo = currentEdgeNo;
  }

  if (removeEdgeNo >= 0) {
    // Touching neighbour missing at the edge side with index removeEdgeNo
    // Remove the edge line and restart the algorithm.
    FRLogger::trace("ROOM_EDGE_REMOVE start, net=" + jrt::str(netNumber) +
                    ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                    jrt::str(removeEdgeNo) + ", room_bounds=" +
                    jrt::str(currentIncompleteRoom->getShape()->boundingBox()));
    Simplex* enlargedShape = roomSimplex->removeBorderLine(removeEdgeNo);
    IncompleteFreeSpaceExpansionRoom* enlargedRoom = new (jrt::kSearch) IncompleteFreeSpaceExpansionRoom(
        enlargedShape, currentIncompleteRoom->getLayer(), currentIncompleteRoom->getContainedShape());
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newRooms =
        autorouteSearchTree->completeShape(enlargedRoom, netNumber, nullptr, nullptr);
    FRLogger::trace("ROOM_EDGE_REMOVE complete_shape, net=" + jrt::str(netNumber) +
                    ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                    jrt::str(removeEdgeNo) + ", candidate_count=" + jrt::str(newRooms->size()));
    if (newRooms->size() != 1) {
      FRLogger::trace("AutorouteEngine.calculate_doors: 1 completed shape expected");
      return false;
    }
    bool removeEdge = false;
    // Check, that the area increases to prevent endless loop.
    IncompleteFreeSpaceExpansionRoom* newShape = newRooms->iterator()->next();
    if (newShape->getShape()->area() > roomShapeArea) {
      removeEdge = true;
    }
    if (removeEdge) {
      jrt::Iterator<IncompleteFreeSpaceExpansionRoom*>* it2 = newRooms->iterator();
      IncompleteFreeSpaceExpansionRoom* newRoom = it2->next();
      FRLogger::trace("ROOM_EDGE_REMOVE applied, net=" + jrt::str(netNumber) +
                      ", layer=" + jrt::str(currentIncompleteRoom->getLayer()) + ", removeEdge=" +
                      jrt::str(removeEdgeNo) + ", old_bounds=" +
                      jrt::str(currentIncompleteRoom->getShape()->boundingBox()) +
                      ", newBounds=" + jrt::str(newRoom->getShape()->boundingBox()));
      currentIncompleteRoom->setShape(newRoom->getShape());
      currentIncompleteRoom->setContainedShape(newRoom->getContainedShape());
      return true;
    }
  }
  return false;
}

void SortedRoomNeighbours::calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine) {
  SortedRoomNeighbours_SortedRoomNeighbour* prevNeighbour = this->sortedNeighbours->last();
  Simplex* roomSimplex = this->fromRoom->getShape()->toSimplex();
  for (SortedRoomNeighbours_SortedRoomNeighbour* nextNeighbour : *this->sortedNeighbours) {
    int32_t firstTouchingSideNo = prevNeighbour->touchingSideNoOfRoom;
    int32_t lastTouchingSideNo = nextNeighbour->touchingSideNoOfRoom;

    int32_t currentNextNo = roomSimplex->nextNo(firstTouchingSideNo);
    bool intersectionWithPrevNeighbourEndsAtCorner =
        (firstTouchingSideNo != lastTouchingSideNo || prevNeighbour == this->sortedNeighbours->last()) &&
        prevNeighbour->lastCorner()->equals(roomSimplex->corner(currentNextNo));
    bool intersectionWithNextNeighbourStartsAtCorner =
        (firstTouchingSideNo != lastTouchingSideNo || prevNeighbour == this->sortedNeighbours->last()) &&
        nextNeighbour->firstCorner()->equals(roomSimplex->corner(lastTouchingSideNo));

    if (intersectionWithPrevNeighbourEndsAtCorner) {
      firstTouchingSideNo = currentNextNo;
    }

    if (intersectionWithNextNeighbourStartsAtCorner) {
      lastTouchingSideNo = roomSimplex->prevNo(lastTouchingSideNo);
    }
    bool neighboursTouch = false;

    if (this->sortedNeighbours->size() > 1) {
      neighboursTouch = prevNeighbour->lastCorner()->equals(nextNeighbour->firstCorner());
    }

    if (!neighboursTouch) {
      // create a door to a new incomplete expansion room between
      // the last corner of the previous neighbour and the first corner of the
      // current neighbour.
      int32_t lastBoundingLineNo = prevNeighbour->touchingSideNoOfNeighbourRoom;
      if (!(intersectionWithPrevNeighbourEndsAtCorner || prevNeighbour->roomTouchIsCorner)) {
        lastBoundingLineNo = prevNeighbour->neighbourShape->prevNo(lastBoundingLineNo);
      }

      int32_t firstBoundingLineNo = nextNeighbour->touchingSideNoOfNeighbourRoom;
      if (!(intersectionWithNextNeighbourStartsAtCorner || nextNeighbour->neighbourRoomTouchIsCorner)) {
        firstBoundingLineNo = nextNeighbour->neighbourShape->nextNo(firstBoundingLineNo);
      }
      Line* startEdgeLine = nextNeighbour->neighbourShape->borderLine(firstBoundingLineNo)->opposite();
      // startEdgeLine is only used for the first new incomplete room.
      Line* middleEdgeLine = nullptr;
      int32_t currentTouchingSideNo = lastTouchingSideNo;
      bool firstTime = true;
      // The loop goes backwards from the edge line of nextNeighbour to the edge line of
      // prevNeighbour.
      for (;;) {
        bool cornerCutOff = false;
        if (auto* incompleteRoom = jrt::fast_cast<IncompleteFreeSpaceExpansionRoom>(this->fromRoom)) {
          if (currentTouchingSideNo == lastTouchingSideNo && firstTouchingSideNo != lastTouchingSideNo) {
            // Create a new line approximately from the last corner of the previous
            // neighbour to the first corner of the next neighbour to cut off
            // the outstanding corners of the room shape in the empty space.
            // That is only tried in the first pass of the loop.
            IntPoint* cutLineStart = prevNeighbour->lastCorner()->toFloat()->round();
            IntPoint* cutLineEnd = nextNeighbour->firstCorner()->toFloat()->round();
            Line* cutLine = new (jrt::kGeom) Line(cutLineStart, cutLineEnd);
            TileShape* cutHalfPlane = TileShape::getInstance(cutLine);
            jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(this->completedRoom)
                ->setShape(this->completedRoom->getShape()->intersection(cutHalfPlane));
            // Otherwise room.containedShape would no longer be contained
            // in the shape after cutting of the corner.
            cornerCutOff = incompleteRoom->getContainedShape()->sideOf(cutLine) == Side::ON_THE_LEFT();
            if (cornerCutOff) {
              middleEdgeLine = cutLine->opposite();
            }
          }
        }
        const int32_t nextTouchingSideNo = roomSimplex->prevNo(currentTouchingSideNo);

        if (!cornerCutOff) {
          middleEdgeLine = roomSimplex->borderLine(currentTouchingSideNo)->opposite();
        }

        Direction* middleLineDir = middleEdgeLine->direction();

        bool lastTime = (currentTouchingSideNo == firstTouchingSideNo &&
                         !(prevNeighbour == this->sortedNeighbours->last() && firstTime))
                        // The expression above handles the case, when all neighbours are on 1 edge line.
                        || cornerCutOff;

        Line* endEdgeLine;
        // endEdgeLine is only used for the last new incomplete room.
        if (lastTime) {
          endEdgeLine = prevNeighbour->neighbourShape->borderLine(lastBoundingLineNo)->opposite();
          if (endEdgeLine->direction()->sideOf(middleLineDir) != Side::ON_THE_LEFT()) {
            // Concave corner between the middle and the last line.
            // Maybe there is a 1 point touch.
            endEdgeLine = nullptr;
          }
        } else {
          endEdgeLine = nullptr;
        }

        if (startEdgeLine != nullptr && middleLineDir->sideOf(startEdgeLine->direction()) != Side::ON_THE_LEFT()) {
          // concave corner between the first and the middle line
          // May be there is a 1 point touch.
          startEdgeLine = nullptr;
        }
        int32_t newEdgeLineCount = 1;
        if (startEdgeLine != nullptr) {
          ++newEdgeLineCount;
        }
        if (endEdgeLine != nullptr) {
          ++newEdgeLineCount;
        }
        jrt::Array<Line*>* newEdgeLines = new (jrt::kGeom) jrt::Array<Line*>(newEdgeLineCount);
        int32_t currentIndex = 0;
        if (startEdgeLine != nullptr) {
          (*newEdgeLines)[currentIndex] = startEdgeLine;
          ++currentIndex;
        }
        (*newEdgeLines)[currentIndex] = middleEdgeLine;
        if (endEdgeLine != nullptr) {
          ++currentIndex;
          (*newEdgeLines)[currentIndex] = endEdgeLine;
        }
        Simplex* newRoomShape = Simplex::getInstance(newEdgeLines);
        if (!newRoomShape->isEmpty()) {
          TileShape* newContainedShape = this->completedRoom->getShape()->intersection(newRoomShape);
          if (!newContainedShape->isEmpty()) {
            FreeSpaceExpansionRoom* newRoom =
                autorouteEngine->addIncompleteExpansionRoom(newRoomShape, this->fromRoom->getLayer(), newContainedShape);
            ExpansionDoor* newDoor = new (jrt::kSearch) ExpansionDoor(this->completedRoom, newRoom, 1);
            this->completedRoom->addDoor(newDoor);
            newRoom->addDoor(newDoor);
          }
        }
        if (lastTime) {
          break;
        }
        currentTouchingSideNo = nextTouchingSideNo;
        startEdgeLine = nullptr;
        firstTime = false;
      }
    }
    prevNeighbour = nextNeighbour;
  }
}

// ---------------------------------------------------------------------------------------------
// SortedRoomNeighbours_SortedRoomNeighbour
// ---------------------------------------------------------------------------------------------

int32_t SortedRoomNeighbours_SortedRoomNeighbour::compareTo(SortedRoomNeighbours_SortedRoomNeighbour* other) {
  TileShape* roomShape = outer->roomShape;
  int32_t compareValue = this->touchingSideNoOfRoom - other->touchingSideNoOfRoom;
  if (compareValue != 0) {
    return compareValue;
  }
  FloatPoint* compareCorner = roomShape->cornerApprox(touchingSideNoOfRoom);
  double thisDistance = this->firstCorner()->toFloat()->distance(compareCorner);
  double otherDistance = other->firstCorner()->toFloat()->distance(compareCorner);
  double deltaDistance = thisDistance - otherDistance;
  if (jrt::Math::abs(deltaDistance) <= c_dist_tolerance) {
    // check corners for equality
    if (this->firstCorner()->equals(other->firstCorner())) {
      // in this case compare the last corners
      double thisDistance2 = this->lastCorner()->toFloat()->distance(compareCorner);
      double otherDistance2 = other->lastCorner()->toFloat()->distance(compareCorner);
      deltaDistance = thisDistance2 - otherDistance2;
      if (jrt::Math::abs(deltaDistance) <= c_dist_tolerance) {
        if (this->neighbourRoomTouchIsCorner && other->neighbourRoomTouchIsCorner) {
          // Otherwise there may be a short 1 dim. touch at a link between 2 trace lines.
          // In this case equality is ok, because the 2 intersection pieces with
          // the expansion room are identical, so that only 1 obstacle is needed.
          int32_t compareLineNo = touchingSideNoOfRoom;
          if (roomTouchIsCorner) {
            compareLineNo = roomShape->prevNo(compareLineNo);
          }
          Direction* compareDir = roomShape->borderLine(compareLineNo)->direction()->opposite();
          Line* thisCompareLine = this->neighbourShape->borderLine(this->touchingSideNoOfNeighbourRoom);
          Line* otherCompareLine = other->neighbourShape->borderLine(other->touchingSideNoOfNeighbourRoom);
          deltaDistance = compareDir->compareFrom(thisCompareLine->direction(), otherCompareLine->direction());
        }
      }
    }
  }
  int32_t res = Signum::asInt(deltaDistance);
  if (res == 0) {
    // Deterministic tie-breaker for identical geometry
    res = this->searchTreeObject->getId() - other->searchTreeObject->getId();
  }
  return res;
}

Point* SortedRoomNeighbours_SortedRoomNeighbour::firstCorner() {
  TileShape* roomShape = outer->roomShape;
  if (precalculatedFirstCorner == nullptr) {
    if (roomTouchIsCorner) {
      precalculatedFirstCorner = roomShape->corner(touchingSideNoOfRoom);
    } else if (neighbourRoomTouchIsCorner) {
      precalculatedFirstCorner = neighbourShape->corner(touchingSideNoOfNeighbourRoom);
    } else {
      Point* currentFirstCorner = neighbourShape->corner(neighbourShape->nextNo(touchingSideNoOfNeighbourRoom));
      Line* prevLine = roomShape->borderLine(roomShape->prevNo(touchingSideNoOfRoom));
      if (prevLine->sideOf(currentFirstCorner) == Side::ON_THE_RIGHT()) {
        precalculatedFirstCorner = currentFirstCorner;
      } else {
        // currentFirstCorner is outside the door shape
        precalculatedFirstCorner = roomShape->corner(touchingSideNoOfRoom);
      }
    }
  }
  return precalculatedFirstCorner;
}

Point* SortedRoomNeighbours_SortedRoomNeighbour::lastCorner() {
  TileShape* roomShape = outer->roomShape;
  if (precalculatedLastCorner == nullptr) {
    if (roomTouchIsCorner) {
      precalculatedLastCorner = roomShape->corner(touchingSideNoOfRoom);
    } else if (neighbourRoomTouchIsCorner) {
      precalculatedLastCorner = neighbourShape->corner(touchingSideNoOfNeighbourRoom);
    } else {
      Point* currentLastCorner = neighbourShape->corner(touchingSideNoOfNeighbourRoom);
      Line* nextLine = roomShape->borderLine(roomShape->nextNo(touchingSideNoOfRoom));
      if (nextLine->sideOf(currentLastCorner) == Side::ON_THE_RIGHT()) {
        precalculatedLastCorner = currentLastCorner;
      } else {
        // currentLastCorner is outside the door shape
        precalculatedLastCorner = roomShape->corner(roomShape->nextNo(touchingSideNoOfRoom));
      }
    }
  }
  return precalculatedLastCorner;
}

}  // namespace fr
