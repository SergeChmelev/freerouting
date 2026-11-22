#include "autoroute/maze/MazeExpansionEngine.h"

#include "autoroute/drill/DrillPage.h"
#include "autoroute/drill/ExpansionDrill.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/expansion/TargetItemExpansionDoor.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/DestinationDistance.h"
#include "autoroute/maze/MazeFanoutDiagnostics.h"
#include "autoroute/maze/MazeListElement.h"
#include "autoroute/maze/MazeSearchEngine.h"
#include "board/actions/ForcedPadRouter.h"
#include "board/actions/ForcedViaInserter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Via.h"
#include "core/library/Padstack.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/HoleSpacing.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"

namespace fr {

void MazeExpansionEngine::expandToDrill(ExpansionDrill* drill, MazeListElement* fromElement,
                                        int32_t addCosts) {
  AutorouteControl* ctrl = search->ctrl;
  int32_t layer = fromElement->nextRoom->getLayer();
  int32_t traceHalfWidth = (*ctrl->compensatedTraceHalfWidth)[layer];
  bool roomShapeIsThin = fromElement->nextRoom->getShape()->minWidth() < 2 * traceHalfWidth;

  if (roomShapeIsThin &&
      (fromElement->backtrackDoor == nullptr ||
       !drill->getShape()->intersects(
           static_cast<Shape*>(fromElement->backtrackDoor->getShape())))) {
    if (search->fanoutDiagnostics->enabled()) {
      search->fanoutDiagnostics->trace(
          "drill_rejected_thin_room_no_backtrack_intersection",
          "drill=" + MazeSearchEngine::describeExpandable(drill) +
              ", from_door=" + MazeSearchEngine::describeExpandable(fromElement->door) +
              ", backtrack=" + MazeSearchEngine::describeExpandable(fromElement->backtrackDoor) +
              ", room=" + MazeSearchEngine::describeRoom(fromElement->nextRoom));
    }
    return;
  }

  double viaRadius = (*ctrl->viaRadii)[layer];
  ConvexShape* shrinkedDrillShape = drill->getShape()->shrink(viaRadius);
  FloatPoint* compareCorner = fromElement->shapeEntry->a->middlePoint(fromElement->shapeEntry->b);
  if (dynamic_cast<DrillPage*>(fromElement->door) != nullptr) {
    if (auto* door = dynamic_cast<TargetItemExpansionDoor*>(fromElement->backtrackDoor)) {
      if (auto* pin = dynamic_cast<Pin*>(door->item)) {
        FloatPoint* nearestExitCorner =
            pin->nearestTraceExitCorner(drill->location->toFloat(), traceHalfWidth, layer);
        if (nearestExitCorner != nullptr) {
          compareCorner = nearestExitCorner;
        }
      }
    }
  }
  FloatPoint* nearestPoint = shrinkedDrillShape->nearestPointApprox(compareCorner);
  FloatLine* shapeEntry = new FloatLine(nearestPoint, nearestPoint);
  int32_t sectionIndex = layer - drill->firstLayer;
  double expansionValue =
      fromElement->expansionValue + addCosts +
      nearestPoint->weightedDistance(compareCorner, (*ctrl->traceCosts)[layer]->horizontal(),
                                     (*ctrl->traceCosts)[layer]->vertical());
  ExpandableObject* newBacktrackDoor;
  int32_t newSectionNoOfBacktrackDoor;
  if (dynamic_cast<DrillPage*>(fromElement->door) != nullptr) {
    newBacktrackDoor = fromElement->backtrackDoor;
    newSectionNoOfBacktrackDoor = fromElement->sectionNoOfBacktrackDoor;
  } else {
    newBacktrackDoor = fromElement->door;
    newSectionNoOfBacktrackDoor = fromElement->sectionNoOfDoor;
    expansionValue += ctrl->minNormalViaCost;
  }
  double sortingValue =
      expansionValue + search->destinationDistance->calculateWithPrepaidVia(nearestPoint, layer);
  MazeListElement* newElement = new MazeListElement(
      drill, sectionIndex, newBacktrackDoor, newSectionNoOfBacktrackDoor, expansionValue,
      sortingValue, nullptr, shapeEntry, fromElement->roomRipped,
      MazeSearchElement_Adjustment::NONE, false);
  search->mazeExpansionList->add(newElement);
  if (search->fanoutDiagnostics->enabled()) {
    search->fanoutDiagnostics->trace(
        "drill_accepted", "drill=" + MazeSearchEngine::describeExpandable(drill) +
                              ", room=" + MazeSearchEngine::describeRoom(fromElement->nextRoom) +
                              ", nearestPoint=" + jrt::str(nearestPoint) +
                              ", expansionValue=" + jrt::str(expansionValue));
  }
}

void MazeExpansionEngine::expandToDrillPage(DrillPage* drillPage, MazeListElement* fromElement) {
  AutorouteControl* ctrl = search->ctrl;
  int32_t layer = fromElement->nextRoom->getLayer();
  FloatPoint* fromElementShapeEntryMiddle =
      fromElement->shapeEntry->a->middlePoint(fromElement->shapeEntry->b);
  FloatPoint* nearestPoint = drillPage->shape->nearestPoint(fromElementShapeEntryMiddle);
  double expansionValue = fromElement->expansionValue + ctrl->minNormalViaCost;
  double sortingValue =
      expansionValue +
      nearestPoint->weightedDistance(fromElementShapeEntryMiddle,
                                     (*ctrl->traceCosts)[layer]->horizontal(),
                                     (*ctrl->traceCosts)[layer]->vertical()) +
      search->destinationDistance->calculateWithPrepaidVia(nearestPoint, layer);
  MazeListElement* newElement = new MazeListElement(
      drillPage, layer, fromElement->door, fromElement->sectionNoOfDoor, expansionValue,
      sortingValue, fromElement->nextRoom, fromElement->shapeEntry, fromElement->roomRipped,
      MazeSearchElement_Adjustment::NONE, false);
  search->mazeExpansionList->add(newElement);
}

void MazeExpansionEngine::expandToDrillsOfPage(MazeListElement* fromElement) {
  AutorouteControl* ctrl = search->ctrl;
  int32_t fromRoomLayer = fromElement->sectionNoOfDoor;
  DrillPage* drillPage = dynamic_cast<DrillPage*>(fromElement->door);
  jrt::Collection<ExpansionDrill*>* drillList = drillPage->getDrills(
      search->autorouteEngine, ctrl->attachSmdAllowed, ctrl->centreOfGravityViaCandidate);
  if (search->fanoutDiagnostics->enabled()) {
    search->fanoutDiagnostics->trace(
        "drill_page_scan",
        "candidate_count=" + jrt::str(drillList->size()) +
            ", attachSmdAllowed=" + jrt::str(ctrl->attachSmdAllowed) +
            ", room=" + MazeSearchEngine::describeRoom(fromElement->nextRoom) +
            ", from_door=" + MazeSearchEngine::describeExpandable(fromElement->door));
    if (drillList->isEmpty()) {
      if (search->fanoutDiagnostics->enabled()) {
        search->fanoutDiagnostics->trace("drill_page_empty", "no_candidates=true");
      }
    }
  }
  bool firstMismatchLogged = false;
  for (ExpansionDrill* currentDrill : *drillList) {
    int32_t sectionIndex = fromRoomLayer - currentDrill->firstLayer;
    if (sectionIndex < 0 || sectionIndex >= currentDrill->roomArr->length) {
      if (search->fanoutDiagnostics->enabled()) {
        search->fanoutDiagnostics->trace(
            "drill_rejected_section_out_of_range",
            "drill=" + MazeSearchEngine::describeExpandable(currentDrill) +
                ", section=" + jrt::str(sectionIndex) +
                ", room_arr_len=" + jrt::str(currentDrill->roomArr->length));
      }
      continue;
    }
    if ((*currentDrill->roomArr)[sectionIndex] != fromElement->nextRoom) {
      if (search->fanoutDiagnostics->enabled()) {
        search->fanoutDiagnostics->trace(
            "drill_rejected_room_mismatch",
            "drill=" + MazeSearchEngine::describeExpandable(currentDrill) +
                ", expected_room=" + MazeSearchEngine::describeRoom(fromElement->nextRoom) +
                ", drill_room=" +
                MazeSearchEngine::describeRoom((*currentDrill->roomArr)[sectionIndex]));
      }
      if (!firstMismatchLogged && search->fanoutDiagnostics->enabled()) {
        firstMismatchLogged = true;
        CompleteExpansionRoom* expansionRoom = fromElement->nextRoom;
        CompleteExpansionRoom* drillRoom = (*currentDrill->roomArr)[sectionIndex];
        if (FRLogger::isTraceEnabled()) {
          FRLogger::trace(
              "FANOUT_DIAG event=first_room_mismatch_detail" + std::string(", pin=") +
              search->fanoutDiagnostics->labelForLog() + ", net=" + jrt::str(ctrl->netNumber) +
              ", drillLocation=" + jrt::str(currentDrill->location) +
              ", expansion_room_id=" + jrt::str(jrt::System::identityHashCode(expansionRoom)) +
              ", expansion_room_bounds=" +
              (expansionRoom != nullptr ? jrt::str(expansionRoom->getShape())
                                        : std::string("null")) +
              ", drill_room_id=" + jrt::str(jrt::System::identityHashCode(drillRoom)) +
              ", drill_room_bounds=" +
              (drillRoom != nullptr ? jrt::str(drillRoom->getShape()) : std::string("null")) +
              ", from_door_type=" +
              (fromElement->door != nullptr ? jrt::className(fromElement->door)
                                            : std::string("null")) +
              ", backtrack_door_type=" +
              (fromElement->backtrackDoor != nullptr ? jrt::className(fromElement->backtrackDoor)
                                                     : std::string("null")) +
              ", sectionIndex=" + jrt::str(sectionIndex) + ", layer=" + jrt::str(fromRoomLayer));
        }
      }
      continue;
    }
    if (currentDrill->getMazeSearchElement(sectionIndex)->isOccupied) {
      if (search->fanoutDiagnostics->enabled()) {
        search->fanoutDiagnostics->trace(
            "drill_rejected_section_occupied",
            "drill=" + MazeSearchEngine::describeExpandable(currentDrill) +
                ", section=" + jrt::str(sectionIndex));
      }
      continue;
    }
    expandToDrill(currentDrill, fromElement, 0);
  }
}

void MazeExpansionEngine::expandToOtherLayers(MazeListElement* listElement) {
  AutorouteControl* ctrl = search->ctrl;
  int32_t viaLowerBound = 0;
  int32_t viaUpperBound = -1;
  ExpansionDrill* currentDrill = dynamic_cast<ExpansionDrill*>(listElement->door);
  int32_t fromLayer = currentDrill->firstLayer + listElement->sectionNoOfDoor;
  bool smdAttachedOnComponentSide = false;
  bool smdAttachedOnSolderSide = false;
  bool roomRipped;
  if (auto* room =
          dynamic_cast<ObstacleExpansionRoom*>((*currentDrill->roomArr)[listElement->sectionNoOfDoor])) {
    if (!ctrl->ripupAllowed) {
      return;
    }
    Item* currentObstacleItem = room->getItem();
    if (dynamic_cast<Via*>(currentObstacleItem) == nullptr) {
      return;
    }
    if (!ctrl->netsNotToRip->isEmpty()) {
      for (int32_t i = 0; i < currentObstacleItem->netCount(); i++) {
        if (ctrl->netsNotToRip->contains(currentObstacleItem->getNetNumber(i))) {
          return;
        }
      }
    }
    Padstack* currentObstaclePadstack = dynamic_cast<Via*>(currentObstacleItem)->getPadstack();
    if (!ctrl->viaRule->containsPadstack(currentObstaclePadstack) ||
        currentObstacleItem->clearanceClassIndex() != ctrl->viaClearanceClass) {
      return;
    }
    viaLowerBound = currentObstaclePadstack->fromLayer();
    viaUpperBound = currentObstaclePadstack->toLayer();
    roomRipped = true;
  } else {
    jrt::Array<int32_t>* netNumbers = jrt::Array<int32_t>::of({ctrl->netNumber});
    roomRipped = false;
    int32_t viaLowerLimit = jrt::Math::max(currentDrill->firstLayer, ctrl->viaLowerBound);
    int32_t viaUpperLimit = jrt::Math::min(currentDrill->lastLayer, ctrl->viaUpperBound);
    int32_t currentLayer = fromLayer;
    for (;;) {
      TileShape* currentRoomShape =
          (*currentDrill->roomArr)[currentLayer - currentDrill->firstLayer]->getShape();
      ForcedPadRouter_CheckDrillResult drillResult =
          checkLayerWithAnyMatchingVia(currentDrill, currentLayer, currentRoomShape, netNumbers);
      if (drillResult == ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
        viaLowerBound = currentLayer + 1;
        break;
      } else if (drillResult == ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD) {
        if (currentLayer == 0) {
          smdAttachedOnComponentSide = true;
        } else if (currentLayer == ctrl->layerCount - 1) {
          smdAttachedOnSolderSide = true;
        }
      }
      if (currentLayer <= viaLowerLimit) {
        viaLowerBound = viaLowerLimit;
        break;
      }
      --currentLayer;
    }
    if (viaLowerBound > currentDrill->firstLayer) {
      return;
    }
    currentLayer = fromLayer + 1;
    for (;;) {
      if (currentLayer > viaUpperLimit) {
        viaUpperBound = viaUpperLimit;
        break;
      }
      TileShape* currentRoomShape =
          (*currentDrill->roomArr)[currentLayer - currentDrill->firstLayer]->getShape();
      ForcedPadRouter_CheckDrillResult drillResult =
          checkLayerWithAnyMatchingVia(currentDrill, currentLayer, currentRoomShape, netNumbers);
      if (drillResult == ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
        viaUpperBound = currentLayer - 1;
        break;
      } else if (drillResult == ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD) {
        if (currentLayer == ctrl->layerCount - 1) {
          smdAttachedOnSolderSide = true;
        }
      }
      ++currentLayer;
    }
    if (viaUpperBound < currentDrill->lastLayer) {
      return;
    }
  }

  for (int32_t toLayer = viaLowerBound; toLayer <= viaUpperBound; toLayer++) {
    if (toLayer == fromLayer) {
      continue;
    }
    int32_t currentFirstLayer;
    int32_t currentLastLayer;
    if (toLayer < fromLayer) {
      currentFirstLayer = toLayer;
      currentLastLayer = fromLayer;
    } else {
      currentFirstLayer = fromLayer;
      currentLastLayer = toLayer;
    }
    bool maskFound = false;
    for (int32_t k = 0; k < ctrl->viaInfos->length; k++) {
      AutorouteControl_ViaMask* currentViaInfo = (*ctrl->viaInfos)[k];
      if (currentFirstLayer >= currentViaInfo->fromLayer &&
          currentLastLayer <= currentViaInfo->toLayer &&
          currentViaInfo->fromLayer >= viaLowerBound && currentViaInfo->toLayer <= viaUpperBound) {
        bool maskOk = !((currentViaInfo->fromLayer == 0 && smdAttachedOnComponentSide) ||
                        (currentViaInfo->toLayer == ctrl->layerCount - 1 &&
                         smdAttachedOnSolderSide)) ||
                      currentViaInfo->attachSmdAllowed;
        if (maskOk) {
          maskFound = true;
          break;
        }
      }
    }
    if (!maskFound) {
      continue;
    }
    MazeSearchElement* currentDrillLayerInfo =
        currentDrill->getMazeSearchElement(toLayer - currentDrill->firstLayer);
    if (currentDrillLayerInfo->isOccupied) {
      continue;
    }
    double expansionValue =
        listElement->expansionValue + (*(*ctrl->addViaCosts)[fromLayer]->toLayer)[toLayer];
    FloatPoint* shapeEntryMiddle =
        listElement->shapeEntry->a->middlePoint(listElement->shapeEntry->b);
    double sortingValue =
        expansionValue + search->destinationDistance->calculate(shapeEntryMiddle, toLayer);
    int32_t currentRoomIndex = toLayer - currentDrill->firstLayer;
    MazeListElement* newElement = new MazeListElement(
        currentDrill, currentRoomIndex, currentDrill, listElement->sectionNoOfDoor, expansionValue,
        sortingValue, (*currentDrill->roomArr)[currentRoomIndex], listElement->shapeEntry,
        roomRipped, MazeSearchElement_Adjustment::NONE, false);
    search->mazeExpansionList->add(newElement);
  }
}

ForcedPadRouter_CheckDrillResult MazeExpansionEngine::checkLayerWithAnyMatchingVia(
    ExpansionDrill* drill, int32_t layer, TileShape* roomShape, jrt::Array<int32_t>* netNumbers) {
  AutorouteControl* ctrl = search->ctrl;
  bool drillableWithAttachSmd = false;
  for (int32_t i = 0; i < ctrl->viaRule->viaCount(); i++) {
    ViaInfo* viaInfo = ctrl->viaRule->getVia(i);
    Padstack* viaPadstack = viaInfo->getPadstack();
    if (layer < viaPadstack->fromLayer() || layer > viaPadstack->toLayer()) {
      continue;
    }
    if (!HoleSpacing::clear(search->autorouteEngine->board, viaPadstack, drill->location)) {
      continue;
    }
    if (!ctrl->viaHoleAllowed(search->autorouteEngine->board, viaInfo, drill->location,
                              netNumbers)) {
      continue;
    }
    ConvexShape* viaShape = viaPadstack->getShape(layer);
    double viaRadius = viaShape == nullptr ? 0 : 0.5 * viaShape->maxWidth();
    double requiredRadius =
        jrt::Math::max(viaRadius, static_cast<double>((*ctrl->traceHalfWidth)[layer]));
    ForcedPadRouter_CheckDrillResult result = ForcedViaInserter::checkLayer(
        requiredRadius, viaInfo->getClearanceClassIndex(), ctrl->viaAttachAllowed(viaInfo),
        roomShape, drill->location, layer, netNumbers, ctrl->maxShoveTraceRecursionDepth, 0,
        search->autorouteEngine->board, (*ctrl->traceHalfWidth)[layer],
        ctrl->traceClearanceClassIndex, dynamic_cast<Circle*>(viaShape) != nullptr);
    if (result == ForcedPadRouter_CheckDrillResult::DRILLABLE) {
      return result;
    }
    if (result == ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD) {
      drillableWithAttachSmd = true;
    }
  }
  return drillableWithAttachSmd ? ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD
                                : ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
}

}  // namespace fr
