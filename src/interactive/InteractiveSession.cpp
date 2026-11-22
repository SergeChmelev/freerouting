// Interactive route/drag session: the routing logic of the Java GUI states (gui/interactive)
// without Swing. Statement order follows the Java states; repaint/ratsnest/screen-message calls
// are dropped (front ends redraw from board snapshots and recompute incompletes themselves).
#include "interactive/InteractiveSession.h"

#include "board/actions/ItemSelectionFilter.h"
#include "board/actions/MoveComponent.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Vector.h"
#include "interactive/Route.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaRule.h"

namespace fr {

InteractiveSession::InteractiveSession(RoutingBoard* board) : board_(board) {}

void InteractiveSession::setMode(Mode mode) {
  if (route_ != nullptr) cancel();
  if (dragging()) mouseReleased();
  mode_ = mode;
  message_.clear();
}

// ------------------------------------------------------------- GuiBoardRoutingSettings -------

int32_t InteractiveSession::getTraceHalfwidth(int32_t netNumber, int32_t layer) {
  return board_->rules->getTraceHalfWidth(netNumber, layer);
}

bool InteractiveSession::isActiveRoutingLayer(int32_t netNumber, int32_t layer) {
  Net* currentNet = board_->rules->nets->get(netNumber);
  if (currentNet == nullptr) return true;
  NetClass* currentNetClass = currentNet->getNetClass();
  return currentNetClass == nullptr || currentNetClass->isActiveRoutingLayer(layer);
}

int32_t InteractiveSession::getTraceClearanceClass(int32_t netNumber) {
  return board_->rules->nets->get(netNumber)->getNetClass()->getTraceClearanceClass();
}

ViaRule* InteractiveSession::getViaRule(int32_t netNumber) {
  return board_->rules->nets->get(netNumber)->getNetClass()->getViaRule();
}

// --------------------------------------------------------------------------- layers ---------

bool InteractiveSession::changeLayer(int32_t newLayer) {
  if (newLayer < 0 || newLayer >= board_->getLayerCount()) return false;
  if (route_ == nullptr) {
    layer_ = newLayer;  // InteractiveState.changeLayerAction
    return true;
  }
  // RouteState.changeLayerAction
  bool result = true;
  if (!route_->isLayerActive(newLayer)) {
    message_ = "layer " + (*board_->layerStructure->layers)[newLayer]->name + " is not active for this net";
  }
  bool changeLayerSucceeded = route_->changeLayer(newLayer);
  if (changeLayerSucceeded) {
    bool connectedToPlane = false;
    int32_t oldLayer = layer_;
    auto* selectionFilter = new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::VIAS);
    jrt::Set<Item*>* pickedItems = board_->pickItems(route_->getLastCorner(), oldLayer, selectionFilter);
    Via* newVia = nullptr;
    for (Item* currentVia : *pickedItems) {
      if (currentVia->sharesNetNo(route_->netNumbers)) {
        newVia = jrt::fast_cast<Via>(currentVia);
        break;
      }
    }
    if (newVia != nullptr) {
      int32_t fromLayer, toLayer;
      if (oldLayer < newLayer) {
        fromLayer = oldLayer + 1;
        toLayer = newLayer;
      } else {
        fromLayer = newLayer;
        toLayer = oldLayer - 1;
      }
      for (Item* currentItem : *newVia->getNormalContacts()) {
        if (auto* currentArea = jrt::fast_cast<ConductionArea>(currentItem)) {
          if (currentArea->getLayer() >= fromLayer && currentArea->getLayer() <= toLayer) {
            connectedToPlane = true;
            break;
          }
        }
      }
    }
    if (connectedToPlane) {
      endRoute();  // hdlg.setEditorState(returnState)
    } else {
      layer_ = newLayer;
      message_ = "layer changed to " + (*board_->layerStructure->layers)[newLayer]->name;
      board_->generateSnapshot();
    }
  } else {
    int32_t shoveFailingLayer = board_->getShoveFailingLayer();
    message_ = shoveFailingLayer >= 0
                   ? "layer not changed: obstacle on " + (*board_->layerStructure->layers)[shoveFailingLayer]->name
                   : "layer not changed";
    result = false;
  }
  return result;
}

// ---------------------------------------------------------------------------- route ---------

Item* InteractiveSession::startOk(IntPoint* location) {
  // RouteState.startOk
  Item* pickedItem = board_->pickNearestRoutingItem(location, layer_, nullptr);
  int32_t layerCount = board_->getLayerCount();
  auto pickRoutingItem = [&](int32_t layerIndex) -> Item* {
    if (layerIndex == layer_) return nullptr;  // (all layers are visible here)
    Item* item = board_->pickNearestRoutingItem(location, layerIndex, nullptr);
    if (item != nullptr) layer_ = item->firstLayer();
    return item;
  };
  if (pickedItem == nullptr && selectOnAllVisibleLayers) {
    pickedItem = pickRoutingItem(0);
    if (pickedItem == nullptr) pickedItem = pickRoutingItem(layerCount - 1);
    if (pickedItem == nullptr) {
      for (int32_t i = 1; i < layerCount - 1; i++) {
        if ((*board_->layerStructure->layers)[i]->isSignal) {
          pickedItem = pickRoutingItem(i);
          if (pickedItem != nullptr) break;
        }
      }
    }
    if (pickedItem == nullptr) {
      for (int32_t i = 1; i < layerCount - 1; i++) {
        if (!(*board_->layerStructure->layers)[i]->isSignal) {
          pickedItem = pickRoutingItem(i);
          if (pickedItem != nullptr) break;
        }
      }
    }
  }
  return pickedItem;
}

jrt::Array<int32_t>* InteractiveSession::getRouteNetNumbersAtTiePin(Pin* pin, int32_t layer) {
  auto* netNumberList = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t i = 0; i < pin->netCount(); i++) netNumberList->add(pin->getNetNumber(i));
  for (Item* currentContact : *pin->getNormalContacts()) {
    if (currentContact->firstLayer() <= layer && currentContact->lastLayer() >= layer) {
      for (int32_t i = 0; i < currentContact->netCount(); i++) netNumberList->remove(currentContact->getNetNumber(i));
    }
  }
  auto* result = new (jrt::kOp) jrt::Array<int32_t>(netNumberList->size());
  int32_t currentInd = 0;
  for (int32_t n : *netNumberList) (*result)[currentInd++] = n;
  return result;
}

bool InteractiveSession::startRoute(FloatPoint* floatLocation) {
  IntPoint* location = floatLocation->round();
  Item* pickedItem = startOk(location);
  if (pickedItem == nullptr) return false;
  int32_t netCount = pickedItem->netCount();
  if (netCount <= 0) return false;
  jrt::Array<int32_t>* routeNetNoArr;
  auto* tiePin = jrt::fast_cast<Pin>(pickedItem);
  if (tiePin != nullptr && netCount > 1) {
    routeNetNoArr = getRouteNetNumbersAtTiePin(tiePin, layer_);
  } else {
    routeNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(netCount);
    for (int32_t i = 0; i < netCount; i++) (*routeNetNoArr)[i] = pickedItem->getNetNumber(i);
  }
  if (routeNetNoArr->length == 0) return false;
  auto* traceHalfWidths = new (jrt::kOp) jrt::Array<int32_t>(board_->getLayerCount());
  auto* layerActiveArr = new (jrt::kOp) jrt::Array<bool>(traceHalfWidths->length);
  for (int32_t i = 0; i < traceHalfWidths->length; i++) {
    (*traceHalfWidths)[i] = getTraceHalfwidth((*routeNetNoArr)[0], i);
    (*layerActiveArr)[i] = false;
    for (int32_t j = 0; j < routeNetNoArr->length; j++) {
      if (isActiveRoutingLayer((*routeNetNoArr)[j], i)) (*layerActiveArr)[i] = true;
    }
  }
  int32_t traceClearanceClass = getTraceClearanceClass((*routeNetNoArr)[0]);
  bool startOkFlag = true;
  if (auto* pickedTrace = jrt::fast_cast<Trace>(pickedItem)) {
    Point* pickedCorner = pickedTrace->nearestEndPoint(location);
    auto* point = jrt::fast_cast<IntPoint>(pickedCorner);
    if (point != nullptr && floatLocation->distance(pickedCorner->toFloat()) < 5 * pickedTrace->getHalfWidth()) {
      location = point;
    } else {
      if (auto* trace = jrt::fast_cast<PolylineTrace>(pickedTrace)) {
        FloatPoint* nearestPoint = trace->polyline()->nearestPointApprox(floatLocation);
        location = nearestPoint->round();
      }
      if (!board_->connectToTrace(location, pickedTrace, pickedTrace->getHalfWidth(),
                                  pickedTrace->clearanceClassIndex())) {
        startOkFlag = false;
      }
    }
    if (startOkFlag) {  // !manualRuleSelection
      auto* newTraceHalfWidths = traceHalfWidths->clone();
      (*newTraceHalfWidths)[pickedTrace->getLayer()] = pickedTrace->getHalfWidth();
      traceHalfWidths = newTraceHalfWidths;
      traceClearanceClass = pickedTrace->clearanceClassIndex();
    }
  } else if (auto* drillItem = jrt::fast_cast<DrillItem>(pickedItem)) {
    if (auto* center = jrt::fast_cast<IntPoint>(drillItem->getCenter())) location = center;
  }
  if (!startOkFlag) return false;
  Net* currentNet = board_->rules->nets->get((*routeNetNoArr)[0]);
  if (currentNet == nullptr) return false;
  stitchRoute_ = isStitchRoute || currentNet->getNetClass()->isShoveFixed() ||
                 !currentNet->getNetClass()->getPullTight();
  board_->generateSnapshot();
  jrt::Set<Item*>* routingTargetSet = pickedItem->getUnconnectedSet(-1);
  route_ = new (jrt::kOp) Route(location, layer_, traceHalfWidths, layerActiveArr, routeNetNoArr, traceClearanceClass,
                     getViaRule((*routeNetNoArr)[0]), pushEnabled, tracePullTightRegionWidth,
                     tracePullTightAccuracy, pickedItem, routingTargetSet, board_, stitchRoute_,
                     automaticNeckdown, viaSnapToSmdCenter, highlightRoutingObstacle);
  message_ = "routing net " + currentNet->name;
  return true;
}

bool InteractiveSession::addCorner(FloatPoint* location) {
  // RouteState.addCorner (StitchRouteState adds a snapshot first)
  if (stitchRoute_) board_->generateSnapshot();
  bool routeCompleted = route_->nextCorner(location);
  if (routeCompleted) {
    message_ = "connection completed";
    endRoute();
  }
  return true;
}

void InteractiveSession::endRoute() { route_ = nullptr; }

// ----------------------------------------------------------------------------- drag ---------

bool InteractiveSession::startDrag(FloatPoint* location) {
  // DragState.getInstance / findItemToMove / selectItemToMove
  int32_t tryCount = 1;
  if (selectOnAllVisibleLayers) tryCount += board_->getLayerCount();
  int32_t currentLayer = layer_;
  int32_t pickLayer = currentLayer;
  bool itemFound = false;
  Item* itemToMove = nullptr;
  auto* filter = new (jrt::kOp) ItemSelectionFilter();
  for (int32_t i = 0; i < tryCount && itemToMove == nullptr; i++) {
    if (i == 0 || pickLayer != currentLayer) {
      jrt::Set<Item*>* foundItems = board_->pickItems(location->round(), pickLayer, filter);
      for (Item* currentItem : *foundItems) {
        itemFound = true;
        if (jrt::fast_cast<Trace>(currentItem) != nullptr) continue;  // traces are not moved
        if (!dragComponentsEnabled && currentItem->getComponentId() != 0) continue;
        itemToMove = currentItem;
        if (jrt::fast_cast<DrillItem>(currentItem) != nullptr) break;  // drill items preferred
      }
    }
    pickLayer = i;
  }
  previousLocation_ = location;
  somethingDragged_ = false;
  if (itemToMove != nullptr) {
    dragItem_ = itemToMove;
    message_ = "dragging item";
    return true;
  }
  if (!itemFound) {
    // MakeSpaceState: an invisible trace of the hidden net pushes the traces aside.
    auto* shoveTraceWidthArr = new (jrt::kOp) jrt::Array<int32_t>(board_->getLayerCount());
    auto* layerActiveArr = new (jrt::kOp) jrt::Array<bool>(shoveTraceWidthArr->length);
    int32_t shoveTraceWidth = jrt::Math::min(100, board_->getMinTraceHalfWidth() / 10);
    shoveTraceWidth = jrt::Math::max(shoveTraceWidth, 5);
    for (int32_t i = 0; i < shoveTraceWidthArr->length; i++) {
      (*shoveTraceWidthArr)[i] = shoveTraceWidth;
      (*layerActiveArr)[i] = true;
    }
    auto* routeNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(1);
    (*routeNetNoArr)[0] = Nets::hidden_net_number;
    makeSpace_ = new (jrt::kOp) Route(location->round(), layer_, shoveTraceWidthArr, layerActiveArr, routeNetNoArr, 0,
                           ViaRule::EMPTY(), true, tracePullTightRegionWidth, tracePullTightAccuracy,
                           nullptr, nullptr, board_, false, false, false, highlightRoutingObstacle);
    message_ = "making space";
    return true;
  }
  return false;
}

bool InteractiveSession::dragItemMoveTo(FloatPoint* toLocation) {
  IntPoint* roundedLocation = toLocation->round();
  IntPoint* fromLocation = this->previousLocation_->round();
  AngleRestriction angle = board_->rules->getTraceAngleRestriction();
  if (angle == AngleRestriction::NINETY_DEGREE) {
    roundedLocation = roundedLocation->orthogonalProjection(fromLocation);
  } else if (angle == AngleRestriction::FORTYFIVE_DEGREE) {
    roundedLocation = roundedLocation->fortyfiveDegreeProjection(fromLocation);
  }
  if (roundedLocation->equals(fromLocation)) return false;
  if (dragItem_->isUserFixed()) {
    message_ = "Please unfix item before dragging";
    return false;
  }
  MoveComponent* moveComponent = nullptr;
  Vector* relCoor = roundedLocation->differenceBy(fromLocation);
  double length = relCoor->lengthApprox();
  bool shoveOk = false;
  for (int32_t i = 0; i < 2; i++) {
    moveComponent = new (jrt::kOp) MoveComponent(dragItem_, relCoor, 99, 5);
    if (moveComponent->check()) {
      shoveOk = true;
      break;
    }
    if (i == 0) {
      double sampleWidth = 2 * board_->getMinTraceHalfWidth();
      double minDrillWidth = moveComponent->getMinDrillItemWidthWithTraces();
      if (minDrillWidth > 0 && sampleWidth >= minDrillWidth) sampleWidth = jrt::Math::max(1.0, minDrillWidth - 1.0);
      if (length > sampleWidth) relCoor = relCoor->changeLengthApprox(sampleWidth);
    }
  }
  if (!shoveOk) return false;
  if (!this->somethingDragged_) {
    board_->generateSnapshot();
    this->somethingDragged_ = true;
  }
  if (!moveComponent->insert(tracePullTightRegionWidth, tracePullTightAccuracy)) {
    // Java returns the parent state; DragState.mouseDragged then undoes the partial move.
    jrt::Set<int32_t>* changedNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
    board_->undo(changedNets);
    endDrag();
    return true;
  }
  this->previousLocation_ = this->previousLocation_->add(relCoor->toFloat());
  return true;
}

bool InteractiveSession::makeSpaceMoveTo(FloatPoint* toLocation) {
  if (!somethingDragged_) {
    board_->generateSnapshot();
    somethingDragged_ = true;
  }
  makeSpace_->nextCorner(toLocation);
  return true;
}

void InteractiveSession::endDrag() {
  if (makeSpace_ != nullptr) {
    // MakeSpaceState.buttonReleased: remove the hidden helper trace
    board_->removeItems(board_->getConnectableItems(Nets::hidden_net_number));
  }
  dragItem_ = nullptr;
  makeSpace_ = nullptr;
  somethingDragged_ = false;
  message_.clear();
}

// ----------------------------------------------------------------------- mouse events -------

bool InteractiveSession::mousePressed(double x, double y) {
  if (mode_ != Mode::Drag || dragging()) return false;  // DragMenuState.mousePressed
  return startDrag(new (jrt::kGeom) FloatPoint(x, y));
}

bool InteractiveSession::mouseDragged(double x, double y) {
  if (dragItem_ != nullptr) return dragItemMoveTo(new (jrt::kGeom) FloatPoint(x, y));
  if (makeSpace_ != nullptr) return makeSpaceMoveTo(new (jrt::kGeom) FloatPoint(x, y));
  return false;
}

bool InteractiveSession::mouseReleased() {
  if (!dragging()) return false;
  bool changed = somethingDragged_ || makeSpace_ != nullptr;
  endDrag();
  return changed;
}

bool InteractiveSession::mouseMoved(double x, double y) {
  if (route_ == nullptr) return false;
  if (stitchRoute_) {
    route_->calcNearestTargetPoint(new (jrt::kGeom) FloatPoint(x, y));  // StitchRouteState.mouseMoved
    return false;
  }
  return addCorner(new (jrt::kGeom) FloatPoint(x, y));  // DynamicRouteState.mouseMoved
}

bool InteractiveSession::leftClicked(double x, double y) {
  if (mode_ != Mode::Route) return false;
  if (route_ == nullptr) return startRoute(new (jrt::kGeom) FloatPoint(x, y));  // RouteMenuState
  if (stitchRoute_) return addCorner(new (jrt::kGeom) FloatPoint(x, y));         // StitchRouteState
  endRoute();                                                         // DynamicRouteState
  message_.clear();
  return false;
}

bool InteractiveSession::complete() {
  if (route_ != nullptr) {
    endRoute();
    message_.clear();
    return false;
  }
  return mouseReleased();
}

bool InteractiveSession::cancel() {
  if (dragging()) return mouseReleased();
  if (route_ == nullptr) return false;
  // RouteState.cancel: remove the dangling tail of the unfinished route
  Trace* tail = board_->getTraceTail(route_->getLastCorner(), layer_, route_->netNumbers);
  if (tail != nullptr) {
    jrt::Collection<Item*>* removeItems = tail->getConnectionItems(Item_StopConnectionOption::VIA);
    if (pushEnabled) {
      board_->removeItemsAndPullTight(removeItems, tracePullTightRegionWidth, tracePullTightAccuracy);
    } else {
      board_->removeItems(removeItems);
    }
  }
  endRoute();
  message_.clear();
  return true;
}

bool InteractiveSession::undo() {
  if (routing() || dragging()) return false;
  return board_->undo(new (jrt::kOp) jrt::TreeSet<int32_t>());
}

bool InteractiveSession::redo() {
  if (routing() || dragging()) return false;
  return board_->redo(new (jrt::kOp) jrt::TreeSet<int32_t>());
}

}  // namespace fr
