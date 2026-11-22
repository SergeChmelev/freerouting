#include "autoroute/maze/MazeSearchEngine.h"

#include <unordered_set>

#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/drill/DrillPage.h"
#include "autoroute/drill/DrillPageArray.h"
#include "autoroute/drill/ExpansionDrill.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/FreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/DestinationDistance.h"
#include "autoroute/maze/MazeExpansionEngine.h"
#include "autoroute/maze/MazeFanoutDiagnostics.h"
#include "autoroute/maze/MazeListElement.h"
#include "autoroute/maze/MazeRipupResolver.h"
#include "autoroute/maze/MazeTraceShover.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

namespace {

// Java: anonymous TreeSet<MazeListElement> subclass overriding add() (fanout escape window).
class MazeExpansionList : public jrt::TreeSet<MazeListElement*> {
 public:
  MazeExpansionList(AutorouteControl* ctrl, AutorouteEngine* autorouteEngine)
      : ctrl(ctrl), autorouteEngine(autorouteEngine) {}

  bool add(MazeListElement* element) override {
    if (ctrl->isFanout && !ctrl->ignoreEscapeWindow && ctrl->fanoutStartPinCenter != nullptr) {
      FloatPoint* pinCenterFloat = ctrl->fanoutStartPinCenter->toFloat();
      bool onStartLayer =
          element->nextRoom != nullptr && element->nextRoom->getLayer() == ctrl->fanoutStartPinLayer;
      if (onStartLayer) {
        double maxLen =
            ctrl->settings->fanout != nullptr && ctrl->settings->fanout->maxEscapeLengthMm.has_value()
                ? *ctrl->settings->fanout->maxEscapeLengthMm * 1000.0
                : 3000.0;
        double resolution = autorouteEngine->board->communication->getResolution(Unit::UM);
        FloatPoint* entryPoint = element->shapeEntry->a->middlePoint(element->shapeEntry->b);
        double dist = entryPoint->distance(pinCenterFloat);
        if (dist > maxLen * resolution) {
          return false;
        }
      }
      if (auto* drill = dynamic_cast<ExpansionDrill*>(element->door)) {
        double minLen =
            ctrl->settings->fanout != nullptr && ctrl->settings->fanout->minEscapeLengthMm.has_value()
                ? *ctrl->settings->fanout->minEscapeLengthMm * 1000.0
                : 500.0;
        double resolution = autorouteEngine->board->communication->getResolution(Unit::UM);
        double drillDist = drill->location->toFloat()->distance(pinCenterFloat);
        if (drillDist < minLen * resolution) {
          return false;
        }
      }
    }
    return jrt::TreeSet<MazeListElement*>::add(element);
  }

 private:
  AutorouteControl* ctrl;
  AutorouteEngine* autorouteEngine;
};

}  // namespace

MazeSearchEngine::MazeSearchEngine(AutorouteEngine* autorouteEngine, AutorouteControl* ctrl)
    : autorouteEngine(autorouteEngine), ctrl(ctrl) {
  this->fanoutDiagnostics = new MazeFanoutDiagnostics(ctrl);
  randomGenerator->setSeed(ctrl->ripupCosts);  // Keep v1.9 deterministic randomization across passes.
  this->searchTree = autorouteEngine->autorouteSearchTree;
  this->expansionEngine = new MazeExpansionEngine(this);
  this->ripupResolver = new MazeRipupResolver(this);
  mazeExpansionList = new MazeExpansionList(ctrl, autorouteEngine);
  destinationDistance = new DestinationDistance(ctrl->traceCosts, ctrl->layerActive,
                                                ctrl->minNormalViaCost, ctrl->minCheapViaCost);
}

MazeSearchEngine* MazeSearchEngine::getInstance(jrt::Set<Item*>* startItems,
                                                jrt::Set<Item*>* destinationItems,
                                                AutorouteEngine* autorouteDatabase,
                                                AutorouteControl* ctrl) {
  MazeSearchEngine* newInstance = new MazeSearchEngine(autorouteDatabase, ctrl);
  MazeSearchEngine* result;
  if (newInstance->init(startItems, destinationItems)) {
    result = newInstance;
  } else {
    result = nullptr;
  }
  return result;
}

void MazeSearchEngine::reduceTraceShapesAtTiePins(jrt::Collection<Item*>* itemList,
                                                  int32_t ownNetNo,
                                                  ShapeSearchTree* autorouteTree) {
  for (Item* currentItem : *itemList) {
    auto* currentTiePin = dynamic_cast<Pin*>(currentItem);
    if (currentTiePin != nullptr && currentItem->netCount() > 1) {
      jrt::Collection<Item*>* pinContacts = currentItem->getNormalContacts();
      for (Item* currentContact : *pinContacts) {
        if (dynamic_cast<PolylineTrace*>(currentContact) == nullptr ||
            currentContact->containsNet(ownNetNo)) {
          continue;
        }
        autorouteTree->reduceTraceShapeAtTiePin(currentTiePin,
                                                dynamic_cast<PolylineTrace*>(currentContact));
      }
    }
  }
}

FloatLine* MazeSearchEngine::segmentProjection(FloatLine* fromSegment, FloatLine* toSegment) {
  FloatLine* checkSegment = fromSegment->adjustDirection(toSegment);
  FloatLine* firstProjection = toSegment->segmentProjection(checkSegment);
  FloatLine* secondProjection = toSegment->segmentProjection2(checkSegment);
  FloatLine* result;
  if (firstProjection != nullptr && secondProjection != nullptr) {
    FloatPoint* resultA;
    if (firstProjection->a == toSegment->a || secondProjection->a == toSegment->a) {
      resultA = toSegment->a;
    } else if (firstProjection->a->distanceSquare(toSegment->a) <=
               secondProjection->a->distanceSquare(toSegment->a)) {
      resultA = firstProjection->a;
    } else {
      resultA = secondProjection->a;
    }
    FloatPoint* resultB;
    if (firstProjection->b == toSegment->b || secondProjection->b == toSegment->b) {
      resultB = toSegment->b;
    } else if (firstProjection->b->distanceSquare(toSegment->b) <=
               secondProjection->b->distanceSquare(toSegment->b)) {
      resultB = firstProjection->b;
    } else {
      resultB = secondProjection->b;
    }
    result = new FloatLine(resultA, resultB);
  } else if (firstProjection != nullptr) {
    result = firstProjection;
  } else {
    result = secondProjection;
  }
  return result;
}

std::string MazeSearchEngine::describeExpandable(ExpandableObject* door) {
  if (door == nullptr) {
    return "null";
  }
  std::string sectionCount = safeMazeSectionCount(door);
  if (auto* targetDoor = dynamic_cast<TargetItemExpansionDoor*>(door)) {
    return "TargetItemExpansionDoor" + std::string("/item=") + jrt::str(targetDoor->item->getId()) +
           "/tree_entry=" + jrt::str(targetDoor->treeEntryNo) +
           "/dim=" + jrt::str(door->getDimension()) + "/sections=" + sectionCount;
  }
  if (auto* drill = dynamic_cast<ExpansionDrill*>(door)) {
    return "ExpansionDrill" + std::string("/location=") + jrt::str(drill->location) +
           "/layers=" + jrt::str(drill->firstLayer) + "-" + jrt::str(drill->lastLayer) +
           "/dim=" + jrt::str(door->getDimension()) + "/sections=" + sectionCount;
  }
  IntBox* bounds = door->getShape()->boundingBox();
  return jrt::className(door) + "/bounds=[(" + jrt::str(bounds->ll->x) + "," +
         jrt::str(bounds->ll->y) + ")..(" + jrt::str(bounds->ur->x) + "," +
         jrt::str(bounds->ur->y) + ")]" + "/dim=" + jrt::str(door->getDimension()) +
         "/sections=" + sectionCount;
}

std::string MazeSearchEngine::safeMazeSectionCount(ExpandableObject* door) {
  // PORT NOTE: Java catches the NullPointerException of an ExpansionDoor whose sections are not
  // allocated yet; the null check reproduces that without dereferencing.
  if (auto* expansionDoor = dynamic_cast<ExpansionDoor*>(door)) {
    if (expansionDoor->sectionArr == nullptr) {
      return "uninitialized";
    }
  }
  try {
    return jrt::Integer::toString(door->mazeSearchElementCount());
  } catch (const jrt::RuntimeException& e) {
    return "uninitialized";
  }
}

std::string MazeSearchEngine::describeExpandableBounds(ExpandableObject* door) {
  if (door == nullptr) {
    return "null";
  }
  IntBox* bounds = door->getShape()->boundingBox();
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

std::string MazeSearchEngine::describeRoom(CompleteExpansionRoom* room) {
  if (room == nullptr) {
    return "null";
  }
  IntBox* bounds = room->getShape()->boundingBox();
  return jrt::className(room) + "/layer=" + jrt::str(room->getLayer()) + "/bounds=[(" +
         jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

jrt::Array<Point*>* MazeSearchEngine::toImpactedPoints(FloatLine* shapeEntry) {
  if (shapeEntry == nullptr) {
    return nullptr;
  }
  return jrt::Array<Point*>::of({shapeEntry->a->round(), shapeEntry->b->round()});
}

MazeSearchEngine_Result* MazeSearchEngine::findConnection() {
  while (occupyNextElement()) {
    continue;
  }
  if (this->destinationDoor == nullptr) {
    return nullptr;
  }
  return new MazeSearchEngine_Result(this->destinationDoor, this->sectionNoOfDestinationDoor);
}

bool MazeSearchEngine::occupyNextElement() {
  if (this->destinationDoor != nullptr) {
    return false;  // destination already reached
  }
  MazeListElement* listElement = nullptr;
  MazeSearchElement* currentDoorSection = nullptr;
  // Search the next element, which is not yet expanded.
  bool nextElementFound = false;
  while (!mazeExpansionList->isEmpty()) {
    if (this->autorouteEngine->isStopRequested()) {
      return false;
    }

    // Java: iterator().next() + it.remove() == removal of the first element.
    listElement = mazeExpansionList->pollFirst();

    int32_t currentSectionNo = listElement->sectionNoOfDoor;
    currentDoorSection = listElement->door->getMazeSearchElement(currentSectionNo);

    if (!currentDoorSection->isOccupied) {
      // A section left unoccupied may already be the backtrack door of elements expanded from
      // it, for example the drill of a via room. Reassigning its backtrack door from one of
      // those descendants would close a cycle in the backtrack chain.
      if (currentDoorSection->backtrackDoor != nullptr &&
          closesBacktrackCycle(listElement, currentDoorSection)) {
        continue;
      }
      nextElementFound = true;
      break;
    }
  }
  if (!nextElementFound) {
    return false;
  }
  currentDoorSection->backtrackDoor = listElement->backtrackDoor;
  currentDoorSection->sectionNoOfBacktrackDoor = listElement->sectionNoOfBacktrackDoor;
  currentDoorSection->roomRipped = listElement->roomRipped;
  currentDoorSection->ripupCost = listElement->ripupCost;
  currentDoorSection->adjustment = listElement->adjustment;

  if (dynamic_cast<DrillPage*>(listElement->door) != nullptr) {
    expansionEngine->expandToDrillsOfPage(listElement);
    return true;
  }

  if (auto* currentDoor = dynamic_cast<TargetItemExpansionDoor*>(listElement->door)) {
    if (currentDoor->isDestinationDoor()) {
      // The destination is reached.
      this->destinationDoor = currentDoor;
      this->sectionNoOfDestinationDoor = listElement->sectionNoOfDoor;
      return false;
    }
  }
  if (ctrl->isFanout && dynamic_cast<ExpansionDrill*>(listElement->door) != nullptr &&
      dynamic_cast<ExpansionDrill*>(listElement->backtrackDoor) != nullptr &&
      fanoutMayStopAt(listElement)) {
    // algorithm completed after the first drill;
    this->destinationDoor = listElement->door;
    this->sectionNoOfDestinationDoor = listElement->sectionNoOfDoor;
    return false;
  }
  if (ctrl->viasAllowed && dynamic_cast<ExpansionDrill*>(listElement->door) != nullptr &&
      !(dynamic_cast<ExpansionDrill*>(listElement->backtrackDoor) != nullptr)) {
    expansionEngine->expandToOtherLayers(listElement);
  }

  if (listElement->nextRoom != nullptr) {
    if (!expandToRoomDoors(listElement)) {
      return true;  // occupation by ripup is delayed or nothing was expanded
      // In case nothing was expanded allow the section to be occupied from
      // somewhere else, if the next room is thin.
    }
  }
  currentDoorSection->isOccupied = true;
  return true;
}

bool MazeSearchEngine::fanoutMayStopAt(MazeListElement* listElement) {
  ExpansionDrill* drill = dynamic_cast<ExpansionDrill*>(listElement->door);
  int32_t layer = drill->firstLayer + listElement->sectionNoOfDoor;
  if ((*ctrl->layerActive)[layer]) {
    return true;
  }
  if ((*autorouteEngine->board->layerStructure->layers)[layer]->isSignal) {
    return false;
  }
  ExpansionDrill* fromDrill = dynamic_cast<ExpansionDrill*>(listElement->backtrackDoor);
  int32_t fromLayer = fromDrill->firstLayer + listElement->sectionNoOfBacktrackDoor;
  if (everyViaThroughPlaneReachesAnActiveLayer(fromLayer, layer)) {
    return true;
  }
  ItemSelectionFilter* filter =
      new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::CONDUCTION);
  for (Item* item : *autorouteEngine->board->pickItems(drill->location, layer, filter)) {
    if (item->containsNet(ctrl->netNumber)) {
      return true;
    }
  }
  return false;
}

bool MazeSearchEngine::everyViaThroughPlaneReachesAnActiveLayer(int32_t fromLayer,
                                                                int32_t planeLayer) {
  int32_t low = jrt::Math::min(fromLayer, planeLayer);
  int32_t high = jrt::Math::max(fromLayer, planeLayer);
  bool spanningViaFound = false;
  for (int32_t k = 0; k < ctrl->viaInfos->length; k++) {
    AutorouteControl_ViaMask* via = (*ctrl->viaInfos)[k];
    if (via->fromLayer > low || via->toLayer < high) {
      continue;
    }
    spanningViaFound = true;
    if (!viaReachesActiveLayer(via, fromLayer)) {
      return false;
    }
  }
  return spanningViaFound;
}

bool MazeSearchEngine::viaReachesActiveLayer(AutorouteControl_ViaMask* via,
                                             int32_t excludedLayer) {
  for (int32_t i = via->fromLayer; i <= via->toLayer; i++) {
    if (i != excludedLayer && (*ctrl->layerActive)[i]) {
      return true;
    }
  }
  return false;
}

bool MazeSearchEngine::closesBacktrackCycle(MazeListElement* listElement,
                                            MazeSearchElement* section) {
  // Java: Collections.newSetFromMap(new IdentityHashMap<>()) -- identity membership only.
  std::unordered_set<MazeSearchElement*> visited;
  ExpandableObject* door = listElement->backtrackDoor;
  int32_t sectionNo = listElement->sectionNoOfBacktrackDoor;
  while (door != nullptr) {
    MazeSearchElement* current = door->getMazeSearchElement(
        jrt::Math::min(sectionNo, door->mazeSearchElementCount() - 1));
    if (current == section) {
      return true;
    }
    if (!visited.insert(current).second) {
      return false;
    }
    door = current->backtrackDoor;
    sectionNo = current->sectionNoOfBacktrackDoor;
  }
  return false;
}

bool MazeSearchEngine::expandToRoomDoors(MazeListElement* listElement) {
  // Complete the neighbour rooms to make sure, that the
  // doors of this room will not change later on.
  int32_t layerIndex = listElement->nextRoom->getLayer();

  bool layerActive = (*ctrl->layerActive)[layerIndex];
  if (!layerActive) {
    if ((*autorouteEngine->board->layerStructure->layers)[layerIndex]->isSignal) {
      return true;
    }
  }

  double halfWidth = (*ctrl->compensatedTraceHalfWidth)[layerIndex];
  bool currentDoorIsSmall = false;
  if (auto* currentDoor = dynamic_cast<ExpansionDoor*>(listElement->door)) {
    double halfWidthAdd = halfWidth + AutorouteEngine::TRACE_WIDTH_TOLERANCE;
    if (this->ctrl->withNeckdown) {
      // try evtl. neckdown at a destination pin
      double neckDownHalfWidth = checkNeckDownAtDestPin(listElement->nextRoom);
      if (neckDownHalfWidth > 0) {
        halfWidthAdd = jrt::Math::min(halfWidthAdd, neckDownHalfWidth);
        halfWidth = halfWidthAdd;
      }
    }
    currentDoorIsSmall = doorIsSmall(currentDoor, 2 * halfWidthAdd);
  }

  int32_t doorCountBeforeCompletion = listElement->nextRoom->getDoors()->size();
  this->autorouteEngine->completeNeighbourRooms(listElement->nextRoom);
  int32_t doorCountAfterCompletion = listElement->nextRoom->getDoors()->size();
  if (FRLogger::isTraceEnabled()) {
    FRLogger::trace("ROOM_COMPLETE_SYNC" + std::string(", net=") + jrt::str(ctrl->netNumber) +
                    ", layer=" + jrt::str(layerIndex) +
                    ", from_section=" + jrt::str(listElement->sectionNoOfDoor) +
                    ", backtrack_section=" + jrt::str(listElement->sectionNoOfBacktrackDoor) +
                    ", from_door=" + describeExpandable(listElement->door) +
                    ", nextRoom=" + describeRoom(listElement->nextRoom) +
                    ", door_count_before=" + jrt::str(doorCountBeforeCompletion) +
                    ", door_count_after=" + jrt::str(doorCountAfterCompletion));
  }

  FloatPoint* shapeEntryMiddle =
      listElement->shapeEntry->a->middlePoint(listElement->shapeEntry->b);

  if (this->ctrl->withNeckdown) {
    if (auto* door = dynamic_cast<TargetItemExpansionDoor*>(listElement->door)) {
      // try evtl. neckdown at a start pin
      Item* startItem = door->item;
      if (auto* pin = dynamic_cast<Pin*>(startItem)) {
        double neckdownHalfWidth = pin->getTraceNeckdownHalfwidth(layerIndex);
        if (neckdownHalfWidth > 0) {
          halfWidth = jrt::Math::min(halfWidth, neckdownHalfWidth);
        }
      }
    }
  }

  bool nextRoomIsThick = true;
  if (auto* room = dynamic_cast<ObstacleExpansionRoom*>(listElement->nextRoom)) {
    nextRoomIsThick = roomShapeIsThick(room);
  } else {
    TileShape* nextRoomShape = listElement->nextRoom->getShape();
    if (nextRoomShape->minWidth() < 2 * halfWidth) {
      nextRoomIsThick = false;  // to prevent problems with the opposite side
    } else if (!listElement->alreadyChecked && listElement->door->getDimension() == 1 &&
               !currentDoorIsSmall) {
      // The algorithm below works only, if location is on the border of roomShape.
      // That is only the case for 1 dimensional doors.
      // For small doors the check is done in check_leaving_via below.

      jrt::Array<FloatPoint*>* nearestPoints =
          nextRoomShape->nearestBorderPointsApprox(shapeEntryMiddle, 2);
      if (nearestPoints->length < 2) {
        FRLogger::warn("MazeSearchEngine.expand_to_room_doors: nearestPoints.length == 2 expected");
        nextRoomIsThick = false;
      } else {
        double currentDistance = (*nearestPoints)[1]->distance(shapeEntryMiddle);
        nextRoomIsThick = currentDistance > halfWidth + 1;
      }
    }
  }
  if (!layerActive) {
    if (auto* drill = dynamic_cast<ExpansionDrill*>(listElement->door)) {
      // check for drill to a foreign conduction area on split plane.
      Point* drillLocation = drill->location;
      ItemSelectionFilter* filter =
          new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::CONDUCTION);
      jrt::Set<Item*>* pickedItems =
          autorouteEngine->board->pickItems(drillLocation, layerIndex, filter);
      for (Item* currentItem : *pickedItems) {
        if (!currentItem->containsNet(ctrl->netNumber)) {
          return true;
        }
      }
    }
  }
  bool somethingExpanded =
      expandToTargetDoors(listElement, nextRoomIsThick, currentDoorIsSmall, shapeEntryMiddle);

  if (!layerActive) {
    return true;
  }

  int32_t ripupCosts = 0;

  if (dynamic_cast<FreeSpaceExpansionRoom*>(listElement->nextRoom) != nullptr) {
    if (!listElement->alreadyChecked) {
      if (currentDoorIsSmall) {
        bool enterThroughSmallDoor = false;
        if (nextRoomIsThick) {
          // check to enter the thick room from a ripped item through a small door (after
          // ripup)
          enterThroughSmallDoor = ripupResolver->checkLeavingRippedItem(listElement);
        }
        if (!enterThroughSmallDoor) {
          return somethingExpanded;
        }
      }
    }
  } else if (auto* obstacleRoom = dynamic_cast<ObstacleExpansionRoom*>(listElement->nextRoom)) {
    if (!listElement->alreadyChecked) {
      bool roomRippable = false;
      if (this->ctrl->ripupAllowed) {
        ripupCosts =
            ripupResolver->checkRipup(listElement, obstacleRoom->getItem(), currentDoorIsSmall);
        roomRippable = ripupCosts >= 0;
      }

      if (ripupCosts != ALREADY_RIPPED_COSTS && nextRoomIsThick) {
        Item* obstacleItem = obstacleRoom->getItem();
        if (!currentDoorIsSmall && this->ctrl->maxShoveTraceRecursionDepth > 0 &&
            dynamic_cast<PolylineTrace*>(obstacleItem) != nullptr) {
          bool shoved = shoveTraceRoom(listElement, obstacleRoom);
          if (!shoved) {
            if (ripupCosts > 0) {
              // delay the occupation by ripup to allow shoving the room by another door
              // sections.
              MazeListElement* newElement = new MazeListElement(
                  listElement->door, listElement->sectionNoOfDoor, listElement->backtrackDoor,
                  listElement->sectionNoOfBacktrackDoor, listElement->expansionValue + ripupCosts,
                  listElement->sortingValue + ripupCosts, listElement->nextRoom,
                  listElement->shapeEntry, true, listElement->adjustment, true);
              newElement->ripupCost = ripupCosts;
              this->mazeExpansionList->add(newElement);
            }
            return somethingExpanded;
          }
        }
      }
      if (!roomRippable) {
        return true;
      }
    }
  }

  jrt::List<ExpansionDoor*>* roomDoorsSnapshot =
      new jrt::LinkedList<ExpansionDoor*>(listElement->nextRoom->getDoors());
  if (FRLogger::isTraceEnabled()) {
    FRLogger::trace("ROOM_DOOR context from_section=" + jrt::str(listElement->sectionNoOfDoor) +
                    ", backtrack_section=" + jrt::str(listElement->sectionNoOfBacktrackDoor) +
                    ", from_door=" + describeExpandable(listElement->door) +
                    ", nextRoom=" + describeRoom(listElement->nextRoom) +
                    ", net=" + jrt::str(ctrl->netNumber));
    for (int32_t doorIndex = 0; doorIndex < roomDoorsSnapshot->size(); doorIndex++) {
      ExpansionDoor* candidateDoor = roomDoorsSnapshot->get(doorIndex);
      if (FRLogger::isTraceEnabled()) {
        FRLogger::trace("ROOM_DOOR candidate index=" + jrt::str(doorIndex) +
                        ", from_section=" + jrt::str(listElement->sectionNoOfDoor) +
                        ", backtrack_section=" + jrt::str(listElement->sectionNoOfBacktrackDoor) +
                        ", from_door=" + describeExpandable(listElement->door) +
                        ", nextRoom=" + describeRoom(listElement->nextRoom) +
                        ", candidate=" + describeExpandable(candidateDoor) +
                        ", net=" + jrt::str(ctrl->netNumber));
      }
    }
  }

  for (ExpansionDoor* toDoor : *roomDoorsSnapshot) {
    if (static_cast<ExpandableObject*>(toDoor) == listElement->door) {
      continue;
    }
    if (expandToDoor(toDoor, listElement, ripupCosts, nextRoomIsThick,
                     MazeSearchElement_Adjustment::NONE)) {
      somethingExpanded = true;
    }
  }

  // Expand also the drill pages intersecting the room.
  if (ctrl->viasAllowed && !(dynamic_cast<ExpansionDrill*>(listElement->door) != nullptr)) {
    if ((somethingExpanded || nextRoomIsThick) &&
        dynamic_cast<CompleteFreeSpaceExpansionRoom*>(listElement->nextRoom) != nullptr) {
      // avoid setting somethingExpanded to true when nextRoom is thin to allow
      // occupying by
      // different sections of the door
      jrt::Collection<DrillPage*>* overlappingDrillPages =
          this->autorouteEngine->drillPageArray->overlappingPages(
              listElement->nextRoom->getShape());
      {
        for (DrillPage* toDrillPage : *overlappingDrillPages) {
          expansionEngine->expandToDrillPage(toDrillPage, listElement);
          somethingExpanded = true;
        }
      }
    } else if (auto* room = dynamic_cast<ObstacleExpansionRoom*>(listElement->nextRoom)) {
      Item* currentObstacleItem = room->getItem();
      if (auto* currentVia = dynamic_cast<Via*>(currentObstacleItem)) {
        ExpansionDrill* viaDrillInfo =
            currentVia->getAutorouteDrillInfo(this->autorouteEngine->autorouteSearchTree);
        expansionEngine->expandToDrill(viaDrillInfo, listElement, ripupCosts);
      }
    }
  }

  return somethingExpanded;
}

bool MazeSearchEngine::expandToTargetDoors(MazeListElement* listElement, bool nextRoomIsThick,
                                           bool currentDoorIsSmall,
                                           FloatPoint* shapeEntryMiddle) {
  if (currentDoorIsSmall) {
    bool enterThroughSmallDoor = false;
    if (dynamic_cast<ExpansionDoor*>(listElement->door) != nullptr) {
      CompleteExpansionRoom* fromRoom = listElement->door->otherRoom(listElement->nextRoom);
      if (dynamic_cast<ObstacleExpansionRoom*>(fromRoom) != nullptr) {
        // otherwise entering through the small door may fail, because it was not
        // checked.
        enterThroughSmallDoor = true;
      }
    }
    if (!enterThroughSmallDoor) {
      return false;
    }
  }
  bool result = false;
  for (TargetItemExpansionDoor* toDoor : *listElement->nextRoom->getTargetDoors()) {
    // A target on an inactive signal layer is out of reach; an inactive power layer is a plane
    // the routed net drops into through a via, so its targets stay reachable.
    if (toDoor->room != nullptr && !(*ctrl->layerActive)[toDoor->room->getLayer()] &&
        (*autorouteEngine->board->layerStructure->layers)[toDoor->room->getLayer()]->isSignal) {
      continue;
    }
    if (static_cast<ExpandableObject*>(toDoor) == listElement->door) {
      continue;
    }
    TileShape* safeTargetShape = getWidthSafeTargetShape(toDoor);
    if (safeTargetShape == nullptr || safeTargetShape->isEmpty()) {
      continue;
    }
    if (!(*ctrl->layerActive)[listElement->nextRoom->getLayer()] &&
        !safeTargetShape->contains(shapeEntryMiddle)) {
      // On an inactive power layer the via has to land inside the plane: no trace there.
      continue;
    }
    FloatPoint* connectionPoint = safeTargetShape->nearestPointApprox(shapeEntryMiddle);
    if (!nextRoomIsThick) {
      // check the line from shapeEntryMiddle to the nearest point.
      jrt::Array<int32_t>* currentNetNumbers = new jrt::Array<int32_t>(1);
      (*currentNetNumbers)[0] = this->ctrl->netNumber;
      int32_t currentLayer = listElement->nextRoom->getLayer();
      // Java: IntPoint[] passed to Polyline(Point[]).
      jrt::Array<Point*>* checkPoints = new jrt::Array<Point*>(2);
      (*checkPoints)[0] = shapeEntryMiddle->round();
      (*checkPoints)[1] = connectionPoint->round();
      if (!(*checkPoints)[0]->equals((*checkPoints)[1])) {
        Polyline* checkPolyline = new Polyline(checkPoints);
        bool checkOk = autorouteEngine->board->checkForcedTracePolyline(
            checkPolyline, (*ctrl->traceHalfWidth)[currentLayer], currentLayer, currentNetNumbers,
            ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
            ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth);
        if (!checkOk) {
          continue;
        }
      }
    }

    FloatLine* newShapeEntry = new FloatLine(connectionPoint, connectionPoint);

    if (expandToDoorSection(toDoor, 0, newShapeEntry, listElement, 0,
                            MazeSearchElement_Adjustment::NONE)) {
      result = true;
    }
  }
  return result;
}

bool MazeSearchEngine::expandToDoor(ExpansionDoor* toDoor, MazeListElement* listElement,
                                    int32_t addCosts, bool nextRoomIsThick,
                                    MazeSearchElement_Adjustment adjustment) {
  double halfWidth = (*ctrl->compensatedTraceHalfWidth)[listElement->nextRoom->getLayer()];
  bool somethingExpanded = false;
  jrt::Array<FloatLine*>* lineSections = toDoor->getSectionSegments(halfWidth);

  for (int32_t i = 0; i < lineSections->length; i++) {
    if ((*toDoor->sectionArr)[i]->isOccupied) {
      continue;
    }
    FloatLine* newShapeEntry;
    if (nextRoomIsThick) {
      newShapeEntry = (*lineSections)[i];
      if (toDoor->dimension == 1 && lineSections->length == 1 &&
          dynamic_cast<CompleteFreeSpaceExpansionRoom*>(toDoor->firstRoom) != nullptr &&
          dynamic_cast<CompleteFreeSpaceExpansionRoom*>(toDoor->secondRoom) != nullptr) {
        // check entering the toDoor at an acute corner of the shape of
        // listElement.nextRoom
        FloatPoint* shapeEntryMiddle = newShapeEntry->a->middlePoint(newShapeEntry->b);
        TileShape* roomShape = listElement->nextRoom->getShape();
        if (roomShape->minWidth() < 2 * halfWidth) {
          return false;
        }
        jrt::Array<FloatPoint*>* nearestPoints =
            roomShape->nearestBorderPointsApprox(shapeEntryMiddle, 2);
        if (nearestPoints->length < 2 ||
            (*nearestPoints)[1]->distance(shapeEntryMiddle) <= halfWidth + 1) {
          return false;
        }
      }
    } else {
      // expand only doors on the opposite side of the room from the shapeEntry.
      if (toDoor->dimension == 1 && i == 0 &&
          (*lineSections)[0]->b->distanceSquare((*lineSections)[0]->a) < 1) {
        // toDoor is small belonging to a via or thin room
        continue;
      }
      newShapeEntry = segmentProjection(listElement->shapeEntry, (*lineSections)[i]);
      if (newShapeEntry == nullptr) {
        continue;
      }
    }

    if (expandToDoorSection(toDoor, i, newShapeEntry, listElement, addCosts, adjustment)) {
      somethingExpanded = true;
    }
  }
  return somethingExpanded;
}

bool MazeSearchEngine::doorIsSmall(ExpansionDoor* door, double traceWidth) {
  if (door->dimension == 1 ||
      (dynamic_cast<CompleteFreeSpaceExpansionRoom*>(door->firstRoom) != nullptr &&
       dynamic_cast<CompleteFreeSpaceExpansionRoom*>(door->secondRoom) != nullptr)) {
    TileShape* doorShape = door->getShape();
    if (doorShape->isEmpty()) {
      if (FRLogger::isTraceEnabled()) {
        FRLogger::trace("MazeSearchEngine:check_door_width doorShape is empty");
      }
      return true;
    }

    double doorLength;
    AngleRestriction angleRestriction = autorouteEngine->board->rules->getTraceAngleRestriction();
    if (angleRestriction == AngleRestriction::NINETY_DEGREE) {
      IntBox* doorBox = doorShape->boundingBox();
      doorLength = doorBox->maxWidth();
    } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
      IntOctagon* doorOct = doorShape->boundingOctagon();
      doorLength = doorOct->maxWidth();
    } else {
      FloatLine* doorLineSegment = doorShape->diagonalCornerSegment();
      doorLength = doorLineSegment->b->distance(doorLineSegment->a);
    }
    return doorLength < traceWidth;
  }
  return false;
}

bool MazeSearchEngine::expandToDoorSection(ExpandableObject* door, int32_t sectionIndex,
                                           FloatLine* shapeEntry, MazeListElement* fromElement,
                                           int32_t addCosts,
                                           MazeSearchElement_Adjustment adjustment) {
  bool doorSectionOccupied = door->getMazeSearchElement(sectionIndex)->isOccupied;
  if (doorSectionOccupied || shapeEntry == nullptr) {
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace(
          "RAW_SECTION skip selected_section=" + jrt::str(sectionIndex) +
          ", from_section=" + jrt::str(fromElement->sectionNoOfDoor) +
          ", backtrack_section=" + jrt::str(fromElement->sectionNoOfBacktrackDoor) +
          ", occupied=" + jrt::str(doorSectionOccupied) +
          ", shape_entry_null=" + jrt::str(shapeEntry == nullptr) +
          ", adjustment=" + MazeSearchElement_Adjustment_toString(adjustment) +
          ", door=" + describeExpandable(door) + ", door_bounds=" + describeExpandableBounds(door) +
          ", from_door=" + describeExpandable(fromElement->door) +
          ", from_door_bounds=" + describeExpandableBounds(fromElement->door) +
          ", net=" + jrt::str(ctrl->netNumber));
    }
    if (FRLogger::isGranularTraceActive()) {
      FRLogger::trace(
          "MazeSearchEngine.expand_to_door_section", "skip_assign_raw",
          "selected_section=" + jrt::str(sectionIndex) +
              ", from_section=" + jrt::str(fromElement->sectionNoOfDoor) +
              ", backtrack_section=" + jrt::str(fromElement->sectionNoOfBacktrackDoor) +
              ", occupied=" + jrt::str(doorSectionOccupied) +
              ", shape_entry_null=" + jrt::str(shapeEntry == nullptr) +
              ", adjustment=" + MazeSearchElement_Adjustment_toString(adjustment),
          "Net #" + jrt::str(ctrl->netNumber) + ", door=" + describeExpandable(door) +
              ", door_bounds=" + describeExpandableBounds(door) +
              ", from_door=" + describeExpandable(fromElement->door) +
              ", from_door_bounds=" + describeExpandableBounds(fromElement->door),
          toImpactedPoints(shapeEntry));
    }
    return false;
  }
  CompleteExpansionRoom* nextRoom = door->otherRoom(fromElement->nextRoom);
  int32_t layer = fromElement->nextRoom->getLayer();
  FloatPoint* shapeEntryMiddle = shapeEntry->a->middlePoint(shapeEntry->b);

  double bendCostPenalty = 0.0;
  if ((*ctrl->bendCosts)[layer] > 0.0 && fromElement->backtrackDoor != nullptr) {
    FloatPoint* fromMid = fromElement->shapeEntry->a->middlePoint(fromElement->shapeEntry->b);
    // Build vectors prev->current and current->next to detect a direction change.
    FloatPoint* backtrackCog = fromElement->backtrackDoor->getShape()->centreOfGravity();
    double prevDx = fromMid->x - backtrackCog->x;
    double prevDy = fromMid->y - backtrackCog->y;
    double nextDx = shapeEntryMiddle->x - fromMid->x;
    double nextDy = shapeEntryMiddle->y - fromMid->y;
    double crossProduct = prevDx * nextDy - prevDy * nextDx;
    double sqLenPrev = prevDx * prevDx + prevDy * prevDy;
    double sqLenNext = nextDx * nextDx + nextDy * nextDy;
    // Use a normalized threshold (sin^2 of angle > 0.01, approx. 5.7 degrees) to be
    // scale-independent.
    if (sqLenPrev > 0.0 && sqLenNext > 0.0 &&
        (crossProduct * crossProduct) > 0.01 * sqLenPrev * sqLenNext) {
      bendCostPenalty = (*ctrl->bendCosts)[layer];
    }
  }

  double expansionValue =
      fromElement->expansionValue + addCosts + bendCostPenalty +
      shapeEntryMiddle->weightedDistance(
          fromElement->shapeEntry->a->middlePoint(fromElement->shapeEntry->b),
          (*ctrl->traceCosts)[layer]->horizontal(), (*ctrl->traceCosts)[layer]->vertical());
  double sortingValue =
      expansionValue + this->destinationDistance->calculate(shapeEntryMiddle, layer);
  bool roomRipped = (addCosts > 0 && adjustment == MazeSearchElement_Adjustment::NONE) ||
                    (fromElement->alreadyChecked && fromElement->roomRipped);

  MazeListElement* newElement =
      new MazeListElement(door, sectionIndex, fromElement->door, fromElement->sectionNoOfDoor,
                          expansionValue, sortingValue, nextRoom, shapeEntry, roomRipped,
                          adjustment, false);
  // Store the direct ripup cost on this element (non-zero only when this specific door
  // caused a ripup; propagated roomRipped from a parent keeps ripupCost=0).
  if (addCosts > 0 && adjustment == MazeSearchElement_Adjustment::NONE) {
    newElement->ripupCost = addCosts;
  }
  if (FRLogger::isTraceEnabled()) {
    FRLogger::trace(
        "RAW_SECTION assign selected_section=" + jrt::str(sectionIndex) +
        ", from_section=" + jrt::str(fromElement->sectionNoOfDoor) +
        ", backtrack_section=" + jrt::str(fromElement->sectionNoOfBacktrackDoor) +
        ", add_costs=" + jrt::str(addCosts) +
        ", adjustment=" + MazeSearchElement_Adjustment_toString(adjustment) +
        ", roomRipped=" + jrt::str(roomRipped) + ", expansionValue=" + jrt::str(expansionValue) +
        ", sortingValue=" + jrt::str(sortingValue) + ", door=" + describeExpandable(door) +
        ", door_bounds=" + describeExpandableBounds(door) +
        ", from_door=" + describeExpandable(fromElement->door) +
        ", from_door_bounds=" + describeExpandableBounds(fromElement->door) +
        ", net=" + jrt::str(ctrl->netNumber));
  }
  if (FRLogger::isGranularTraceActive()) {
    FRLogger::trace(
        "MazeSearchEngine.expand_to_door_section", "assign_raw",
        "selected_section=" + jrt::str(sectionIndex) +
            ", from_section=" + jrt::str(fromElement->sectionNoOfDoor) +
            ", backtrack_section=" + jrt::str(fromElement->sectionNoOfBacktrackDoor) +
            ", add_costs=" + jrt::str(addCosts) +
            ", adjustment=" + MazeSearchElement_Adjustment_toString(adjustment) +
            ", roomRipped=" + jrt::str(roomRipped) +
            ", expansionValue=" + jrt::str(expansionValue) +
            ", sortingValue=" + jrt::str(sortingValue),
        "Net #" + jrt::str(ctrl->netNumber) + ", door=" + describeExpandable(door) +
            ", door_bounds=" + describeExpandableBounds(door) +
            ", from_door=" + describeExpandable(fromElement->door) +
            ", from_door_bounds=" + describeExpandableBounds(fromElement->door),
        toImpactedPoints(shapeEntry));
  }
  this->mazeExpansionList->add(newElement);
  return true;
}

bool MazeSearchEngine::init(jrt::Set<Item*>* startItems, jrt::Set<Item*>* destinationItems) {
  reduceTraceShapesAtTiePins(startItems, this->ctrl->netNumber, this->searchTree);
  reduceTraceShapesAtTiePins(destinationItems, this->ctrl->netNumber, this->searchTree);
  for (Item* currentItem : *this->autorouteEngine->board->getConnectableItems(this->ctrl->netNumber)) {
    ItemAutorouteInfo* currentInfo = currentItem->getAutorouteInfo();
    currentInfo->setStartInfo(false);
    currentInfo->setDestinationInfo(false);
  }
  // process the destination items
  bool destinationOk = false;
  for (Item* currentItem : *destinationItems) {
    if (this->autorouteEngine->isStopRequested()) {
      return false;
    }
    ItemAutorouteInfo* currentInfo = currentItem->getAutorouteInfo();
    currentInfo->setStartInfo(false);
    currentInfo->setDestinationInfo(true);
    bool anyInactive = false;
    bool anyActive = false;
    for (int32_t i = 0; i < currentItem->treeShapeCount(this->searchTree); i++) {
      TileShape* currentTreeShape = currentItem->getTreeShape(this->searchTree, i);
      if (currentTreeShape != nullptr) {
        destinationDistance->join(currentTreeShape->boundingBox(), currentItem->shapeLayer(i));
      }
      int32_t shapeLayer = currentItem->shapeLayer(i);
      // A plane on an inactive power layer is reached through a via: a valid destination.
      if ((*this->ctrl->layerActive)[shapeLayer] ||
          !(*this->autorouteEngine->board->layerStructure->layers)[shapeLayer]->isSignal) {
        anyActive = true;
      } else {
        anyInactive = true;
      }
    }
    if (anyActive || !anyInactive) {
      destinationOk = true;
    }
  }
  if (!destinationOk && this->ctrl->isFanout) {
    // destination set is not needed for fanout
    IntBox* boardBoundingBox = this->autorouteEngine->board->boundingBox;
    destinationDistance->join(boardBoundingBox, 0);
    destinationDistance->join(boardBoundingBox, this->ctrl->layerCount - 1);
    destinationOk = true;
  }

  if (!destinationOk) {
    FRLogger::debug("MazeSearchEngine.init: Failed - no valid destination items found" +
                    std::string(" (dest set size: ") + jrt::str(destinationItems->size()) +
                    ", isFanout: " + jrt::str(this->ctrl->isFanout) + ")");
    return false;
  }
  // process the start items
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* startRooms =
      new jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  for (Item* currentItem : *startItems) {
    if (this->autorouteEngine->isStopRequested()) {
      return false;
    }
    ItemAutorouteInfo* currentInfo = currentItem->getAutorouteInfo();
    currentInfo->setStartInfo(true);
    currentInfo->setDestinationInfo(false);
    if (auto* connectable = dynamic_cast<Connectable*>(currentItem)) {
      for (int32_t i = 0; i < currentItem->treeShapeCount(searchTree); i++) {
        TileShape* containedShape = connectable->getTraceConnectionShape(searchTree, i);
        IncompleteFreeSpaceExpansionRoom* newStartRoom = autorouteEngine->addIncompleteExpansionRoom(
            nullptr, currentItem->shapeLayer(i), containedShape);
        startRooms->add(newStartRoom);
      }
    }
  }

  // complete the start rooms
  jrt::Collection<CompleteFreeSpaceExpansionRoom*>* completedStartRooms =
      new jrt::LinkedList<CompleteFreeSpaceExpansionRoom*>();

  if (this->autorouteEngine->maintainDatabase) {
    // add the completed start rooms carried over from the last autoroute to the
    // start rooms.
    completedStartRooms->addAll(this->autorouteEngine->getRoomsWithTargetItems(startItems));
  }

  for (IncompleteFreeSpaceExpansionRoom* currentRoom : *startRooms) {
    if (this->autorouteEngine->isStopRequested()) {
      return false;
    }
    jrt::Collection<CompleteFreeSpaceExpansionRoom*>* currentCompletedRooms =
        autorouteEngine->completeExpansionRoom(currentRoom);
    completedStartRooms->addAll(currentCompletedRooms);
  }

  // Put the ItemExpansionDoors of the completed start rooms into
  // the mazeExpansionList.
  bool startOk = false;
  int32_t expansionDoorsFound = 0;
  int32_t expansionDoorsDestination = 0;
  for (CompleteFreeSpaceExpansionRoom* currentRoom : *completedStartRooms) {
    for (TargetItemExpansionDoor* currentDoor : *currentRoom->getTargetDoors()) {
      expansionDoorsFound++;
      if (this->autorouteEngine->isStopRequested()) {
        return false;
      }
      if (currentDoor->isDestinationDoor()) {
        expansionDoorsDestination++;
        continue;
      }
      if (!currentDoor->isStartDoor()) {
        continue;
      }
      TileShape* safeConnectionShape = getWidthSafeTargetShape(currentDoor);
      if (safeConnectionShape == nullptr || safeConnectionShape->isEmpty()) {
        continue;
      }
      FloatPoint* currentCenter = safeConnectionShape->centreOfGravity();
      FloatLine* shapeEntry = new FloatLine(currentCenter, currentCenter);
      double sortingValue =
          this->destinationDistance->calculate(currentCenter, currentRoom->getLayer());
      MazeListElement* newListElement =
          new MazeListElement(currentDoor, 0, nullptr, 0, 0, sortingValue, currentRoom, shapeEntry,
                              false, MazeSearchElement_Adjustment::NONE, false);
      mazeExpansionList->add(newListElement);
      startOk = true;
    }
  }
  if (!startOk) {
    FRLogger::debug("MazeSearchEngine.init: Failed - no accessible expansion doors found" +
                    std::string(" (start items: ") + jrt::str(startItems->size()) +
                    ", start rooms: " + jrt::str(startRooms->size()) +
                    ", completed start rooms: " + jrt::str(completedStartRooms->size()) +
                    ", expansion doors found: " + jrt::str(expansionDoorsFound) +
                    ", destination doors: " + jrt::str(expansionDoorsDestination) +
                    ", ripupAllowed: " + jrt::str(this->ctrl->ripupAllowed) +
                    ", ripupCosts: " + jrt::str(this->ctrl->ripupCosts) + ")");
  }
  return startOk;
}

bool MazeSearchEngine::roomShapeIsThick(ObstacleExpansionRoom* obstacleRoom) {
  Item* obstacleItem = obstacleRoom->getItem();
  int32_t layer = obstacleRoom->getLayer();
  double obstacleHalfWidth;
  if (auto* trace = dynamic_cast<Trace*>(obstacleItem)) {
    obstacleHalfWidth =
        trace->getHalfWidth() +
        this->searchTree->clearanceCompensationValue(obstacleItem->clearanceClassIndex(), layer);

  } else if (auto* via = dynamic_cast<Via*>(obstacleItem)) {
    TileShape* viaShape = via->getTreeShapeOnLayer(this->searchTree, layer);
    obstacleHalfWidth = 0.5 * viaShape->maxWidth();
  } else {
    FRLogger::warn("MazeSearchEngine. room_shape_is_thick: unexpected obstacle item");
    obstacleHalfWidth = 0;
  }
  return obstacleHalfWidth >= (*this->ctrl->compensatedTraceHalfWidth)[layer];
}

bool MazeSearchEngine::shoveTraceRoom(MazeListElement* listElement,
                                      ObstacleExpansionRoom* obstacleRoom) {
  if (listElement->sectionNoOfDoor != 0 &&
      listElement->sectionNoOfDoor != listElement->door->mazeSearchElementCount() - 1) {
    // No delay of occupation necessary because inner sections of a door are
    // currently not
    // shoved.
    return true;
  }
  bool result = false;
  if (listElement->adjustment != MazeSearchElement_Adjustment::RIGHT) {
    jrt::Collection<MazeTraceShover_DoorSection*>* leftToDoorSectionList =
        new jrt::LinkedList<MazeTraceShover_DoorSection*>();

    if (MazeTraceShover::checkShoveTraceLine(listElement, obstacleRoom,
                                             this->autorouteEngine->board, this->ctrl, false,
                                             leftToDoorSectionList)) {
      result = true;
    }

    for (MazeTraceShover_DoorSection* currentLeftDoorSection : *leftToDoorSectionList) {
      MazeSearchElement_Adjustment currentAdjustment;
      if (currentLeftDoorSection->door->dimension == 2) {
        // the door is the link door to the next room
        currentAdjustment = MazeSearchElement_Adjustment::LEFT;
      } else {
        currentAdjustment = MazeSearchElement_Adjustment::NONE;
      }

      expandToDoorSection(currentLeftDoorSection->door, currentLeftDoorSection->sectionIndex,
                          currentLeftDoorSection->sectionLine, listElement, 0, currentAdjustment);
    }
  }

  if (listElement->adjustment != MazeSearchElement_Adjustment::LEFT) {
    jrt::Collection<MazeTraceShover_DoorSection*>* rightToDoorSectionList =
        new jrt::LinkedList<MazeTraceShover_DoorSection*>();

    if (MazeTraceShover::checkShoveTraceLine(listElement, obstacleRoom,
                                             this->autorouteEngine->board, this->ctrl, true,
                                             rightToDoorSectionList)) {
      result = true;
    }
    for (MazeTraceShover_DoorSection* currentRightDoorSection : *rightToDoorSectionList) {
      MazeSearchElement_Adjustment currentAdjustment;
      if (currentRightDoorSection->door->dimension == 2) {
        // the door is the link door to the next room
        currentAdjustment = MazeSearchElement_Adjustment::RIGHT;
      } else {
        currentAdjustment = MazeSearchElement_Adjustment::NONE;
      }
      expandToDoorSection(currentRightDoorSection->door, currentRightDoorSection->sectionIndex,
                          currentRightDoorSection->sectionLine, listElement, 0, currentAdjustment);
    }
  }
  return result;
}

TileShape* MazeSearchEngine::getWidthSafeTargetShape(TargetItemExpansionDoor* targetDoor) {
  if (targetDoor == nullptr || targetDoor->room == nullptr || targetDoor->item == nullptr) {
    return nullptr;
  }
  int32_t layer = targetDoor->room->getLayer();
  if (layer < 0 || layer >= this->ctrl->layerCount) {
    return nullptr;
  }
  auto* connectable = dynamic_cast<Connectable*>(targetDoor->item);
  if (connectable == nullptr) {
    return nullptr;
  }
  int32_t treeShapeCount =
      targetDoor->item->treeShapeCount(this->autorouteEngine->autorouteSearchTree);
  if (targetDoor->treeEntryNo < 0 || targetDoor->treeEntryNo >= treeShapeCount) {
    return nullptr;
  }
  TileShape* targetShape = connectable->getTraceConnectionShape(
      this->autorouteEngine->autorouteSearchTree, targetDoor->treeEntryNo);
  if (targetShape == nullptr || targetShape->isEmpty()) {
    return nullptr;
  }
  if (dynamic_cast<DrillItem*>(targetDoor->item) != nullptr) {
    TileShape* connectionShape = targetShape->intersection(targetDoor->room->getShape());
    return connectionShape->isEmpty() ? nullptr : connectionShape;
  }

  int32_t shrinkOffset = (dynamic_cast<ObstacleExpansionRoom*>(targetDoor->room) != nullptr)
                             ? (*this->ctrl->compensatedTraceHalfWidth)[layer]
                             : this->ctrl->getTraceClearanceOffset(layer);

  TileShape* safeRoomShape =
      dynamic_cast<TileShape*>(targetDoor->room->getShape()->offset(-shrinkOffset));
  if (!safeRoomShape->isEmpty()) {
    TileShape* safeShape = targetShape->intersection(safeRoomShape);
    if (!safeShape->isEmpty()) {
      return safeShape;
    }
  }

  return nullptr;
}

double MazeSearchEngine::checkNeckDownAtDestPin(CompleteExpansionRoom* room) {
  jrt::Collection<TargetItemExpansionDoor*>* targetDoors = room->getTargetDoors();
  for (TargetItemExpansionDoor* currentTargetDoor : *targetDoors) {
    if (auto* pin = dynamic_cast<Pin*>(currentTargetDoor->item)) {
      return pin->getTraceNeckdownHalfwidth(room->getLayer());
    }
  }
  return 0;
}

}  // namespace fr
