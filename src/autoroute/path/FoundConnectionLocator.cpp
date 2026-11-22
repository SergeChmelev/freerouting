#include "autoroute/path/FoundConnectionLocator.h"

#include <functional>

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/drill/ExpansionDrill.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/MazeSearchElement.h"
#include "autoroute/maze/MazeSearchEngine.h"
#include "autoroute/path/FoundConnectionLocator45Degree.h"
#include "autoroute/path/FoundConnectionLocatorAnyAngle.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

void FoundConnectionLocator::initialize(MazeSearchEngine_Result* mazeSearchResult,
                                        jrt::TreeSet<Item*>* rippedItemList,
                                        jrt::Map<Item*, int32_t>* ripupCosts) {
  jrt::Collection<FoundConnectionLocator_BacktrackElement*>* backtrackList =
      backtrack(mazeSearchResult, rippedItemList, ripupCosts, ctrl->netNumber);
  this->backtrackArray = new jrt::Array<FoundConnectionLocator_BacktrackElement*>(backtrackList->size());
  jrt::Iterator<FoundConnectionLocator_BacktrackElement*>* it = backtrackList->iterator();
  for (int32_t i = 0; i < backtrackArray->length; i++) {
    (*this->backtrackArray)[i] = it->next();
  }
  if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
    FRLogger::trace("compare_trace_backtrack_raw net=" + jrt::str(this->ctrl->netNumber)
                    + ", size=" + jrt::str(this->backtrackArray->length));
    for (int32_t i = 0; i < this->backtrackArray->length; i++) {
      FoundConnectionLocator_BacktrackElement* element = (*this->backtrackArray)[i];
      std::string nextRoomType =
          element->nextRoom != nullptr ? jrt::className(element->nextRoom) : "null";
      FRLogger::trace("compare_trace_backtrack_raw net=" + jrt::str(this->ctrl->netNumber)
                      + ", idx=" + jrt::str(i) + ", door_type=" + jrt::className(element->door)
                      + ", section=" + jrt::str(element->sectionNoOfDoor)
                      + ", next_room_type=" + nextRoomType);
    }
  }
  this->connectionItems = new jrt::LinkedList<FoundConnectionLocator_ResultItem*>();
  FoundConnectionLocator_BacktrackElement* startInfo =
      (*this->backtrackArray)[backtrackArray->length - 1];
  if (dynamic_cast<TargetItemExpansionDoor*>(startInfo->door) == nullptr) {
    FRLogger::warn("FoundConnectionLocator: ItemExpansionDoor expected for startInfo.door");
    this->startItem = nullptr;
    this->startLayer = 0;
    this->targetItem = nullptr;
    this->targetLayer = 0;
    this->startDoor = nullptr;
    return;
  }
  this->startDoor = dynamic_cast<TargetItemExpansionDoor*>(startInfo->door);
  this->startItem = startDoor->item;
  this->startLayer = startDoor->room->getLayer();

  this->currentFromDoorIndex = 0;
  bool atFanoutEnd = false;
  if (auto* currentDestinationDoor =
          dynamic_cast<TargetItemExpansionDoor*>(mazeSearchResult->destinationDoor)) {
    this->targetItem = currentDestinationDoor->item;
    this->targetLayer = currentDestinationDoor->room->getLayer();

    this->currentFromPoint = calculateStartingPoint(currentDestinationDoor, searchTree, this->ctrl);
  } else if (auto* currentDrill = dynamic_cast<ExpansionDrill*>(mazeSearchResult->destinationDoor)) {
    // may happen only in case of fanout
    this->targetItem = nullptr;
    this->currentFromPoint = currentDrill->location->toFloat();
    this->targetLayer = currentDrill->firstLayer + mazeSearchResult->sectionNoOfDoor;
    atFanoutEnd = true;
  } else {
    FRLogger::warn("FoundConnectionLocator: unexpected type of destinationDoor");
    this->targetItem = nullptr;
    this->targetLayer = 0;
    return;
  }
  this->currentTraceLayer = this->targetLayer;
  this->previousFromPoint = this->currentFromPoint;

  bool connectionDone = false;
  while (!connectionDone) {
    bool layerChanged = false;
    if (atFanoutEnd) {
      // do not increase this.currentTargetDoorIndex
      layerChanged = true;
    } else {
      this->currentTargetDoorIndex = this->currentFromDoorIndex + 1;
      while (currentTargetDoorIndex < this->backtrackArray->length && !layerChanged) {
        if (dynamic_cast<ExpansionDrill*>((*this->backtrackArray)[this->currentTargetDoorIndex]->door)
            != nullptr) {
          layerChanged = true;
        } else {
          ++this->currentTargetDoorIndex;
        }
      }
    }
    if (layerChanged) {
      // the next trace leads to a via
      auto* currentTargetDrill =
          dynamic_cast<ExpansionDrill*>((*this->backtrackArray)[this->currentTargetDoorIndex]->door);
      this->currentTargetShape = TileShape::getInstance(currentTargetDrill->location);
    } else {
      // the next trace leads to the final target
      connectionDone = true;
      this->currentTargetDoorIndex = this->backtrackArray->length - 1;
      TileShape* targetShape = dynamic_cast<Connectable*>(startItem)->getTraceConnectionShape(
          searchTree, startDoor->treeEntryNo);
      TileShape* boundedShape = insideConductionArea(
          startItem, calculateWidthSafeTargetShape(startDoor, targetShape, this->ctrl));
      if (boundedShape->dimension() >= 2 && dynamic_cast<DrillItem*>(startItem) == nullptr) {
        // the target is a conduction area, make a safe connection
        // by shrinking the shape by the trace halfwidth.
        double traceHalfWidth = (*this->ctrl->compensatedTraceHalfWidth)[startDoor->room->getLayer()];
        auto* shrinkedShape = dynamic_cast<TileShape*>(boundedShape->offset(-traceHalfWidth));
        if (!shrinkedShape->isEmpty()) {
          boundedShape = shrinkedShape;
        }
      }
      this->currentTargetShape = boundedShape;
      if (auto* targetPin = dynamic_cast<Pin*>(startItem)) {
        TileShape* copper = targetPin->getTileShape(startDoor->treeEntryNo);
        double traceHalfWidth = (*this->ctrl->compensatedTraceHalfWidth)[startDoor->room->getLayer()];
        TileShape* targetRoomShape = startDoor->room->getShape();
        IntPoint* usualEnd = this->currentTargetShape->nearestPointApprox(this->currentFromPoint)->round();
        auto* shrunkTargetRoom = dynamic_cast<TileShape*>(targetRoomShape->offset(-traceHalfWidth));
        IntPoint* clearEnd =
            padEndWithClearStub(targetPin, copper != nullptr ? copper : targetShape, shrunkTargetRoom,
                                usualEnd, startDoor->room->getLayer(), false);
        if (clearEnd != nullptr) {
          this->currentTargetShape = TileShape::getInstance(clearEnd);
        }
      }
    }
    this->currentToDoorIndex = this->currentFromDoorIndex + 1;
    FoundConnectionLocator_ResultItem* nextTrace = this->calculateNextTrace(layerChanged, atFanoutEnd);
    atFanoutEnd = false;
    this->connectionItems->add(nextTrace);
  }
}

FoundConnectionLocator* FoundConnectionLocator::getInstance(
    MazeSearchEngine_Result* mazeSearchResult, AutorouteControl* ctrl, ShapeSearchTree* searchTree,
    AngleRestriction angleRestriction, jrt::TreeSet<Item*>* rippedItemList,
    jrt::Map<Item*, int32_t>* ripupCosts) {
  if (mazeSearchResult == nullptr) {
    return nullptr;
  }
  FoundConnectionLocator* result;
  if (angleRestriction == AngleRestriction::NINETY_DEGREE
      || angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
    result = new FoundConnectionLocator45Degree(mazeSearchResult, ctrl, searchTree,
                                                angleRestriction, rippedItemList, ripupCosts);
  } else {
    result = new FoundConnectionLocatorAnyAngle(mazeSearchResult, ctrl, searchTree,
                                                angleRestriction, rippedItemList, ripupCosts);
  }
  // PORT NOTE: second construction phase (see header).
  result->initialize(mazeSearchResult, rippedItemList, ripupCosts);
  return result;
}

TileShape* FoundConnectionLocator::calculateWidthSafeTargetShape(TargetItemExpansionDoor* door,
                                                                 TileShape* targetShape,
                                                                 AutorouteControl* ctrl) {
  if (dynamic_cast<DrillItem*>(door->item) != nullptr) {
    return targetShape->intersection(door->room->getShape());
  }
  int32_t layer = door->room->getLayer();
  int32_t shrinkOffset = (dynamic_cast<ObstacleExpansionRoom*>(door->room) != nullptr)
                             ? (*ctrl->compensatedTraceHalfWidth)[layer]
                             : ctrl->getTraceClearanceOffset(layer);
  auto* safeRoomShape = dynamic_cast<TileShape*>(door->room->getShape()->offset(-shrinkOffset));
  if (!safeRoomShape->isEmpty()) {
    TileShape* safeTargetShape = targetShape->intersection(safeRoomShape);
    if (!safeTargetShape->isEmpty()) {
      return safeTargetShape;
    }
  }
  return targetShape->intersection(door->room->getShape());
}

TileShape* FoundConnectionLocator::insideConductionArea(Item* item, TileShape* shape) {
  auto* area = dynamic_cast<ConductionArea*>(item);
  if (area == nullptr || shape == nullptr || shape->isEmpty()) {
    return shape;
  }
  jrt::Array<TileShape*>* pieces = area->splitToConvex();
  if (pieces == nullptr) {
    return shape;
  }
  for (TileShape* piece : *pieces) {
    TileShape* inside = shape->intersection(piece);
    if (!inside->isEmpty()) {
      return inside;
    }
  }
  return shape;
}

FloatPoint* FoundConnectionLocator::calculateStartingPoint(TargetItemExpansionDoor* fromDoor,
                                                           ShapeSearchTree* searchTree,
                                                           AutorouteControl* ctrl) {
  TileShape* connectionShape = dynamic_cast<Connectable*>(fromDoor->item)
                                   ->getTraceConnectionShape(searchTree, fromDoor->treeEntryNo);
  if (connectionShape == nullptr || connectionShape->isEmpty()) {
    return fromDoor->room->getShape()->centreOfGravity()->round()->toFloat();
  }
  connectionShape = insideConductionArea(
      fromDoor->item, calculateWidthSafeTargetShape(fromDoor, connectionShape, ctrl));
  return connectionShape->centreOfGravity()->round()->toFloat();
}

jrt::Collection<FoundConnectionLocator_BacktrackElement*>* FoundConnectionLocator::backtrack(
    MazeSearchEngine_Result* mazeSearchResult, jrt::TreeSet<Item*>* rippedItemList,
    jrt::Map<Item*, int32_t>* ripupCosts, int32_t netNumber) {
  if (mazeSearchResult == nullptr) {
    return nullptr;
  }
  jrt::Collection<FoundConnectionLocator_BacktrackElement*>* result =
      new jrt::LinkedList<FoundConnectionLocator_BacktrackElement*>();
  CompleteExpansionRoom* currentNextRoom = nullptr;
  ExpandableObject* currentBacktrackDoor = mazeSearchResult->destinationDoor;
  MazeSearchElement* currentMazeSearchElement =
      currentBacktrackDoor->getMazeSearchElement(mazeSearchResult->sectionNoOfDoor);
  bool debugBacktrack = netNumber == 98;
  if (debugBacktrack) {
    std::string destType = jrt::className(currentBacktrackDoor);
    FRLogger::trace("BACKTRACK_START net=" + jrt::str(netNumber) + ", dest_type=" + destType
                    + ", dest_section=" + jrt::str(mazeSearchResult->sectionNoOfDoor)
                    + ", dest_room_ripped=" + jrt::str(currentMazeSearchElement->roomRipped));
  }
  if (auto* door = dynamic_cast<TargetItemExpansionDoor*>(currentBacktrackDoor)) {
    currentNextRoom = door->room;
  } else if (auto* currentDrill = dynamic_cast<ExpansionDrill*>(currentBacktrackDoor)) {
    currentNextRoom =
        (*currentDrill->roomArr)[currentDrill->firstLayer + mazeSearchResult->sectionNoOfDoor];
    if (currentMazeSearchElement->roomRipped) {
      for (CompleteExpansionRoom* tmpRoom : *currentDrill->roomArr) {
        if (auto* room = dynamic_cast<ObstacleExpansionRoom*>(tmpRoom)) {
          rippedItemList->add(room->getItem());
          if (ripupCosts != nullptr) {
            ripupCosts->put(room->getItem(), currentMazeSearchElement->ripupCost);
          }
        }
      }
    }
  }
  auto* currentBacktrackElement = new FoundConnectionLocator_BacktrackElement(
      currentBacktrackDoor, mazeSearchResult->sectionNoOfDoor, currentNextRoom);
  int32_t step = 0;
  for (;;) {
    result->add(currentBacktrackElement);
    currentBacktrackDoor = currentMazeSearchElement->backtrackDoor;
    if (currentBacktrackDoor == nullptr) {
      break;
    }
    int32_t currentSectionNo = currentMazeSearchElement->sectionNoOfBacktrackDoor;
    if (currentSectionNo >= currentBacktrackDoor->mazeSearchElementCount()) {
      FRLogger::warn("FoundConnectionLocator: currentSectionNo to big");
      currentSectionNo = currentBacktrackDoor->mazeSearchElementCount() - 1;
    }
    if (auto* currentDrill = dynamic_cast<ExpansionDrill*>(currentBacktrackDoor)) {
      currentNextRoom = (*currentDrill->roomArr)[currentSectionNo];
    } else {
      currentNextRoom = currentBacktrackDoor->otherRoom(currentNextRoom);
    }
    currentMazeSearchElement = currentBacktrackDoor->getMazeSearchElement(currentSectionNo);
    currentBacktrackElement = new FoundConnectionLocator_BacktrackElement(
        currentBacktrackDoor, currentSectionNo, currentNextRoom);
    if (debugBacktrack) {
      std::string doorType = jrt::className(currentBacktrackDoor);
      std::string nextRoomType =
          currentNextRoom != nullptr ? jrt::className(currentNextRoom) : "null";
      int32_t obstacleId = -1;
      if (auto* obst = dynamic_cast<ObstacleExpansionRoom*>(currentNextRoom)) {
        obstacleId = obst->getItem()->getId();
      }
      FRLogger::trace("BACKTRACK_STEP net=" + jrt::str(netNumber) + ", step=" + jrt::str(step)
                      + ", door_type=" + doorType + ", section=" + jrt::str(currentSectionNo)
                      + ", roomRipped=" + jrt::str(currentMazeSearchElement->roomRipped)
                      + ", ripupCost=" + jrt::str(currentMazeSearchElement->ripupCost)
                      + ", next_room_type=" + nextRoomType + ", obstacle_id="
                      + jrt::str(obstacleId));
    }
    if (currentMazeSearchElement->roomRipped) {
      if (auto* room = dynamic_cast<ObstacleExpansionRoom*>(currentNextRoom)) {
        rippedItemList->add(room->getItem());
        if (ripupCosts != nullptr) {
          ripupCosts->put(room->getItem(), currentMazeSearchElement->ripupCost);
        }
      }
    }
    step++;
  }
  return result;
}

FloatPoint* FoundConnectionLocator::ninetyDegreeCorner(FloatPoint* fromPoint, FloatPoint* toPoint,
                                                       bool horizontalFirst) {
  double x;
  double y;
  if (horizontalFirst) {
    x = toPoint->x;
    y = fromPoint->y;
  } else {
    x = fromPoint->x;
    y = toPoint->y;
  }
  return new FloatPoint(x, y);
}

FloatPoint* FoundConnectionLocator::fortyfiveDegreeCorner(FloatPoint* fromPoint,
                                                          FloatPoint* toPoint,
                                                          bool horizontalFirst) {
  double absDx = jrt::Math::abs(toPoint->x - fromPoint->x);
  double absDy = jrt::Math::abs(toPoint->y - fromPoint->y);
  double x;
  double y;

  if (absDx <= absDy) {
    if (horizontalFirst) {
      x = toPoint->x;
      if (toPoint->y >= fromPoint->y) {
        y = fromPoint->y + absDx;
      } else {
        y = fromPoint->y - absDx;
      }
    } else {
      x = fromPoint->x;
      if (toPoint->y > fromPoint->y) {
        y = toPoint->y - absDx;
      } else {
        y = toPoint->y + absDx;
      }
    }
  } else {
    if (horizontalFirst) {
      y = fromPoint->y;
      if (toPoint->x > fromPoint->x) {
        x = toPoint->x - absDy;
      } else {
        x = toPoint->x + absDy;
      }
    } else {
      y = toPoint->y;
      if (toPoint->x > fromPoint->x) {
        x = fromPoint->x + absDy;
      } else {
        x = fromPoint->x - absDy;
      }
    }
  }
  return new FloatPoint(x, y);
}

FloatPoint* FoundConnectionLocator::calculateAdditionalCorner(FloatPoint* fromPoint,
                                                              FloatPoint* toPoint,
                                                              bool horizontalFirst,
                                                              AngleRestriction angleRestriction) {
  FloatPoint* result;
  if (angleRestriction == AngleRestriction::NINETY_DEGREE) {
    result = ninetyDegreeCorner(fromPoint, toPoint, horizontalFirst);
  } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
    result = fortyfiveDegreeCorner(fromPoint, toPoint, horizontalFirst);
  } else {
    result = toPoint;
  }
  return result;
}

FoundConnectionLocator_ResultItem* FoundConnectionLocator::calculateNextTrace(bool layerChanged,
                                                                             bool atFanoutEnd) {
  jrt::List<FloatPoint*>* cornerList = new jrt::ArrayList<FloatPoint*>();
  cornerList->add(this->currentFromPoint);
  if (!atFanoutEnd) {
    FloatPoint* adjustedStartCorner = this->adjustStartCorner();
    // The adjustment may have moved the pad end itself (PadEndWithClearStub).
    cornerList->set(0, this->currentFromPoint);
    if (adjustedStartCorner != this->currentFromPoint) {
      FloatPoint* addCorner = calculateAdditionalCorner(this->currentFromPoint, adjustedStartCorner,
                                                        true, this->angleRestriction);
      cornerList->add(addCorner);
      cornerList->add(adjustedStartCorner);
      this->previousFromPoint = this->currentFromPoint;
      this->currentFromPoint = adjustedStartCorner;
    }
  }
  FloatPoint* prevCorner = this->currentFromPoint;
  for (;;) {
    jrt::Collection<FloatPoint*>* nextCorners = calculateNextTraceCorners();
    if (nextCorners->isEmpty()) {
      break;
    }
    for (FloatPoint* currentNextCorner : *nextCorners) {
      if (currentNextCorner != prevCorner) {
        cornerList->add(currentNextCorner);
        this->previousFromPoint = this->currentFromPoint;
        this->currentFromPoint = currentNextCorner;
        prevCorner = currentNextCorner;
      }
    }
  }

  int32_t nextLayer = this->currentTraceLayer;
  if (layerChanged) {
    this->currentFromDoorIndex = this->currentTargetDoorIndex + 1;
    CompleteExpansionRoom* nextRoom = (*this->backtrackArray)[this->currentFromDoorIndex]->nextRoom;
    if (nextRoom != nullptr) {
      nextLayer = nextRoom->getLayer();
    }
  }

  // Round the new trace corners to Integer.
  jrt::Collection<IntPoint*>* roundedCornerList = new jrt::LinkedList<IntPoint*>();
  IntPoint* prevPoint = nullptr;
  for (FloatPoint* corner : *cornerList) {
    IntPoint* currentPoint = corner->round();
    if (!currentPoint->equals(prevPoint)) {
      roundedCornerList->add(currentPoint);
      prevPoint = currentPoint;
    }
  }

  // Construct the result item
  auto* corners = new jrt::Array<IntPoint*>(roundedCornerList->size());
  jrt::Iterator<IntPoint*>* it2 = roundedCornerList->iterator();
  for (int32_t i = 0; i < corners->length; i++) {
    (*corners)[i] = it2->next();
  }
  auto* result = new FoundConnectionLocator_ResultItem(corners, this->currentTraceLayer);
  if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
    IntPoint* first = corners->length > 0 ? (*corners)[0] : nullptr;
    IntPoint* last = corners->length > 0 ? (*corners)[corners->length - 1] : nullptr;
    FRLogger::trace("compare_trace_next_trace_raw net=" + jrt::str(this->ctrl->netNumber)
                    + ", traceLayer=" + jrt::str(this->currentTraceLayer)
                    + ", nextLayer=" + jrt::str(nextLayer)
                    + ", cornerCount=" + jrt::str(corners->length) + ", first=" + jrt::str(first)
                    + ", last=" + jrt::str(last)
                    + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                    + ", to_door=" + jrt::str(this->currentToDoorIndex)
                    + ", target_door=" + jrt::str(this->currentTargetDoorIndex));
  }
  this->currentTraceLayer = nextLayer;
  return result;
}

void FoundConnectionLocator::emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink == nullptr || intensity <= 0) {
    return;
  }
  for (int32_t i = 0; i < backtrackArray->length; i++) {
    CompleteExpansionRoom* nextRoom = (*backtrackArray)[i]->nextRoom;
    if (nextRoom != nullptr) {
      nextRoom->emitDiagnostic(sink, intensity);
    }
    ExpandableObject* nextDoor = (*backtrackArray)[i]->door;
    if (auto* drill = dynamic_cast<ExpansionDrill*>(nextDoor)) {
      drill->emitDiagnostic(sink, intensity);
    }
  }
}

FloatPoint* FoundConnectionLocator::adjustStartCorner() {
  if (this->currentFromDoorIndex < 0) {
    return this->currentFromPoint;
  }
  FoundConnectionLocator_BacktrackElement* currentFromInfo =
      (*this->backtrackArray)[this->currentFromDoorIndex];
  if (currentFromInfo->nextRoom == nullptr) {
    return this->currentFromPoint;
  }
  double shrinkOffset = (dynamic_cast<ObstacleExpansionRoom*>(currentFromInfo->nextRoom) != nullptr)
                            ? (*this->ctrl->compensatedTraceHalfWidth)[this->currentTraceLayer]
                            : this->ctrl->getTraceClearanceOffset(this->currentTraceLayer);
  auto* shrinkedRoomShape =
      dynamic_cast<TileShape*>(currentFromInfo->nextRoom->getShape()->offset(-shrinkOffset));
  if (shrinkedRoomShape->isEmpty() || shrinkedRoomShape->contains(this->currentFromPoint)) {
    return this->currentFromPoint;
  }
  // The first trace's stub out of a pad: from the pad end to the room's corner margin, clear of
  // copper overlapping the pad (PadEndWithClearStub), or from a pad end that is.
  if (this->currentFromDoorIndex == 0) {
    if (auto* padDoor = dynamic_cast<TargetItemExpansionDoor*>(currentFromInfo->door)) {
      if (auto* pin = dynamic_cast<Pin*>(padDoor->item)) {
        TileShape* copper = pin->getTileShape(padDoor->treeEntryNo);
        if (copper != nullptr) {
          IntPoint* clearEnd =
              padEndWithClearStub(pin, copper, shrinkedRoomShape, this->currentFromPoint->round(),
                                  this->currentTraceLayer, true);
          if (clearEnd != nullptr) {
            this->currentFromPoint = clearEnd->toFloat();
            this->previousFromPoint = this->currentFromPoint;
          }
        }
      }
    }
  }
  return shrinkedRoomShape->nearestPointApprox(this->currentFromPoint)->round()->toFloat();
}

IntPoint* FoundConnectionLocator::padEndWithClearStub(Pin* pin, TileShape* copper,
                                                      TileShape* shrunkRoom, IntPoint* usualEnd,
                                                      int32_t layer, bool fromPad) {
  if (copper->dimension() < 2 || pin->board == nullptr || shrunkRoom->isEmpty()
      || !unshovableCopperOverlapsPad(pin->board, pin, copper, layer, this->ctrl->netNumber)) {
    return nullptr;
  }
  int32_t halfWidth = (*this->ctrl->traceHalfWidth)[layer];
  std::function<bool(IntPoint*)> clear = [&](IntPoint* end) -> bool {
    FloatPoint* endPoint = end->toFloat();
    FloatPoint* corner = shrunkRoom->nearestPointApprox(endPoint)->round()->toFloat();
    jrt::Array<FloatPoint*>* path;
    if (fromPad) {
      path = jrt::Array<FloatPoint*>::of(
          {endPoint, calculateAdditionalCorner(endPoint, corner, true, this->angleRestriction),
           corner});
    } else {
      FloatPoint* add = calculateAdditionalCorner(corner, endPoint, true, this->angleRestriction);
      if (!shrunkRoom->contains(add)) {
        add = calculateAdditionalCorner(corner, endPoint, false, this->angleRestriction);
      }
      path = jrt::Array<FloatPoint*>::of({corner, add, endPoint});
    }
    jrt::List<Point*>* corners = new jrt::ArrayList<Point*>();
    for (FloatPoint* p : *path) {
      IntPoint* rounded = p->round();
      if (corners->isEmpty() || !rounded->equals(corners->get(corners->size() - 1))) {
        corners->add(rounded);
      }
    }
    if (corners->size() < 2) {
      IntBox* box =
          new IntBox(end->x - halfWidth, end->y - halfWidth, end->x + halfWidth, end->y + halfWidth);
      return !endBlockedByUnshovableCopper(pin->board, box, layer, this->ctrl->netNumber,
                                           this->ctrl->traceClearanceClassIndex);
    }
    auto* cornerArr = new jrt::Array<Point*>(corners->size());
    for (int32_t k = 0; k < cornerArr->length; k++) {
      (*cornerArr)[k] = corners->get(k);
    }
    Polyline* stub = new Polyline(cornerArr);
    for (int32_t i = 0; i < stub->cornerCount() - 1; i++) {
      if (endBlockedByUnshovableCopper(pin->board, stub->offsetShape(halfWidth, i), layer,
                                       this->ctrl->netNumber,
                                       this->ctrl->traceClearanceClassIndex)) {
        return false;
      }
    }
    return true;
  };

  if (clear(usualEnd)) {
    return nullptr;
  }
  IntBox* box = copper->boundingBox();
  const int32_t steps = 32;
  double stepX = (box->ur->x - box->ll->x) / static_cast<double>(steps);
  double stepY = (box->ur->y - box->ll->y) / static_cast<double>(steps);
  IntPoint* best = nullptr;
  double bestDistance = jrt::Double::MAX_VALUE;
  for (int32_t i = 1; i < steps; i++) {
    for (int32_t j = 1; j < steps; j++) {
      IntPoint* at = new IntPoint(jrt::l2i(jrt::Math::round(box->ll->x + i * stepX)),
                                  jrt::l2i(jrt::Math::round(box->ll->y + j * stepY)));
      double distance = at->toFloat()->distance(usualEnd->toFloat());
      if (distance >= bestDistance || !copper->contains(static_cast<Point*>(at)) || !clear(at)) {
        continue;
      }
      best = at;
      bestDistance = distance;
    }
  }
  if (FRLogger::isTraceEnabled()) {
    FRLogger::trace("pad_end_with_clear_stub pin=" + jrt::str(pin->getId()) + ", layer="
                    + jrt::str(layer) + ", usual=" + jrt::str(usualEnd)
                    + ", result=" + (best != nullptr ? best->toString() : std::string("none")));
  }
  return best;
}

bool FoundConnectionLocator::unshovableCopperOverlapsPad(BasicBoard* board, Pin* pin,
                                                         TileShape* copper, int32_t layer,
                                                         int32_t netNumber) {
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  jrt::Collection<ShapeTree_TreeEntry*>* treeEntries = new jrt::ArrayList<ShapeTree_TreeEntry*>();
  defaultTree->overlappingTreeEntries(copper, layer, new jrt::Array<int32_t>(0), treeEntries);
  for (ShapeTree_TreeEntry* entry : *treeEntries) {
    auto* item = dynamic_cast<Item*>(entry->object);
    if (item == nullptr || item == pin || !item->isTraceObstacle(netNumber, entry->shapeIndexInObject)) {
      continue;
    }
    if (!(dynamic_cast<Pin*>(item) != nullptr || item->isUserFixed()
          || !(dynamic_cast<Trace*>(item) != nullptr || dynamic_cast<Via*>(item) != nullptr))) {
      continue;
    }
    TileShape* shape = item->getTileShape(entry->shapeIndexInObject);
    if (shape != nullptr && shape->intersection(copper)->dimension() >= 2) {
      return true;
    }
  }
  return false;
}

bool FoundConnectionLocator::endBlockedByUnshovableCopper(BasicBoard* board, TileShape* shape,
                                                          int32_t layer, int32_t netNumber,
                                                          int32_t clearanceClassIndex) {
  if (!shape->isContainedIn(board->getBoundingBox())) {
    return true;
  }
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  jrt::Collection<ShapeTree_TreeEntry*>* treeEntries = new jrt::ArrayList<ShapeTree_TreeEntry*>();
  auto* ignoreNetNos = new jrt::Array<int32_t>(0);
  if (defaultTree->isClearanceCompensationUsed()) {
    defaultTree->overlappingTreeEntries(shape, layer, ignoreNetNos, treeEntries);
  } else {
    defaultTree->overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                                     treeEntries);
  }
  for (ShapeTree_TreeEntry* entry : *treeEntries) {
    auto* item = dynamic_cast<Item*>(entry->object);
    if (item == nullptr || !item->isTraceObstacle(netNumber, entry->shapeIndexInObject)) {
      continue;
    }
    if (dynamic_cast<Pin*>(item) != nullptr || item->isUserFixed()
        || !(dynamic_cast<Trace*>(item) != nullptr || dynamic_cast<Via*>(item) != nullptr)) {
      return true;
    }
  }
  return false;
}

}  // namespace fr
