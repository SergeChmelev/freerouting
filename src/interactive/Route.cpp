// Port of app.freerouting.gui.interactive.Route (routing logic only).
// PORT NOTE: Route.draw() and the SwapPinInfo set it alone used are not ported; front ends read
// the overlay accessors instead (nearest target point/item, shove-failing obstacle).
#include "interactive/Route.h"

#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/Padstack.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"

namespace fr {

Route::Route(Point* startCorner, int32_t layerIndex, jrt::Array<int32_t>* penHalfWidthArr,
             jrt::Array<bool>* layerActiveArr, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
             ViaRule* viaRuleValue, bool pushEnabled, int32_t traceTidyWidthValue,
             int32_t pullTightAccuracyValue, Item* startItem, jrt::Set<Item*>* targetSetValue,
             RoutingBoard* routingBoard, bool stitchMode, bool neckdown, bool snapToSmdCenter,
             bool highlightShoveFailingObstacleValue) {
  board = routingBoard;
  layer = layerIndex;
  if (pushEnabled) {
    maxShoveTraceRecursionDepth = 20;
    maxShoveViaRecursionDepth = 8;
    maxSpringOverRecursionDepth = 5;
  } else {
    maxShoveTraceRecursionDepth = 0;
    maxShoveViaRecursionDepth = 0;
    maxSpringOverRecursionDepth = 0;
  }
  traceTidyWidth = traceTidyWidthValue;
  pullTightAccuracy = pullTightAccuracyValue;
  prevCorner = startCorner;
  this->netNumbers = netNumbers;
  this->penHalfWidthArr = penHalfWidthArr;
  this->layerActive = layerActiveArr;
  this->clearanceClass = clearanceClassIndex;
  this->viaRule = viaRuleValue;
  this->startItem = startItem;
  this->targetSet = targetSetValue;
  this->isStitchMode = stitchMode;
  this->withNeckdown = neckdown;
  this->viaSnapToSmdCenter = snapToSmdCenter;
  this->highlightShoveFailingObstacle = highlightShoveFailingObstacleValue;
  pullTightTimeLimit = PULL_TIGHT_TIME_LIMIT;
  calculateTargetPointsAndAreas();
}

bool Route::nextCorner(FloatPoint* corner) {
  if (!(*this->layerActive)[this->layer]) return false;
  IntPoint* currentCorner = corner->round();
  if (!(board->contains(prevCorner) && board->contains(currentCorner) &&
        (*board->layerStructure->layers)[this->layer]->isSignal)) {
    return false;
  }
  if (currentCorner->equals(prevCorner)) return false;
  if (auto* target = jrt::fast_cast<DrillItem>(nearestTargetItem)) {
    if (this->prevCorner->equals(target->getCenter())) return true;  // already completed
  }
  this->shoveFailingObstacle = nullptr;
  AngleRestriction angleRestriction = this->board->rules->getTraceAngleRestriction();
  auto* prevIntCorner = jrt::fast_cast<IntPoint>(prevCorner);
  if (angleRestriction != AngleRestriction::NONE && prevIntCorner == nullptr) return false;
  if (angleRestriction == AngleRestriction::NINETY_DEGREE) {
    currentCorner = currentCorner->orthogonalProjection(prevIntCorner);
  } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
    currentCorner = currentCorner->fortyfiveDegreeProjection(prevIntCorner);
  }
  Item* endRoutingItem = board->pickNearestRoutingItem(prevCorner, this->layer, nullptr);
  nearestTargetItem = board->pickNearestRoutingItem(currentCorner, this->layer, endRoutingItem);
  TimeLimit* checkForcedTraceTimeLimit =
      isStitchMode ? nullptr : new TimeLimit(CHECK_FORCED_TRACE_TIME_LIMIT);
  Point* okPoint = board->insertForcedTraceSegment(
      prevCorner, currentCorner, (*penHalfWidthArr)[layer], layer, netNumbers, clearanceClass,
      maxShoveTraceRecursionDepth, maxShoveViaRecursionDepth, maxSpringOverRecursionDepth,
      traceTidyWidth, pullTightAccuracy, !isStitchMode, checkForcedTraceTimeLimit);
  if (okPoint == prevCorner && this->withNeckdown) okPoint = tryNeckdownAtStart(currentCorner);
  if (okPoint == prevCorner && this->withNeckdown) okPoint = tryNeckdownAtEnd(this->prevCorner, currentCorner);
  if (okPoint == nullptr) {
    board->undo(nullptr);
    return !isStitchMode;
  }
  if (okPoint == prevCorner) {
    setShoveFailingObstacle(board->getShoveFailingObstacle());
    return false;
  }
  this->prevCorner = okPoint;
  bool routeCompleted = false;
  if (okPoint == currentCorner) routeCompleted = connectToTarget(currentCorner);
  IntOctagon* tidyClipShape;
  if (traceTidyWidth == jrt::Integer::MAX_VALUE) {
    tidyClipShape = nullptr;
  } else if (traceTidyWidth == 0) {
    tidyClipShape = IntOctagon::EMPTY();
  } else {
    tidyClipShape = okPoint->surroundingOctagon()->enlarge(traceTidyWidth);
  }
  jrt::Array<int32_t>* optNetNoArr =
      maxShoveTraceRecursionDepth <= 0 ? netNumbers : new jrt::Array<int32_t>(0);
  if (routeCompleted) {
    this->board->reduceNetsOfRouteItems();
    for (int32_t currentNetNumber : *this->netNumbers) this->board->combineTraces(currentNetNumber);
  } else {
    calcNearestTargetPoint(this->prevCorner->toFloat());
  }
  board->optChangedArea(optNetNoArr, tidyClipShape, pullTightAccuracy, nullptr, nullptr,
                        pullTightTimeLimit, okPoint, layer);
  return routeCompleted;
}

bool Route::changeLayer(int32_t toLayer) {
  if (this->layer == toLayer) return true;
  if (toLayer < 0 || toLayer >= this->layerActive->length) {
    FRLogger::warn("Route.change_layer: toLayer out of range");
    return false;
  }
  if (!(*this->layerActive)[toLayer]) return false;
  if (this->viaRule == nullptr) return false;
  this->shoveFailingObstacle = nullptr;
  if (this->viaSnapToSmdCenter) {
    bool snappedToSmdCenter = snapToSmdCenter(toLayer);
    if (!snappedToSmdCenter) snapToSmdCenter(this->layer);
  }
  bool result = true;
  int32_t minLayer = jrt::Math::min(this->layer, toLayer);
  int32_t maxLayer = jrt::Math::max(this->layer, toLayer);
  bool viaFound = false;
  for (int32_t i = 0; i < this->viaRule->viaCount(); i++) {
    ViaInfo* currentViaInfo = this->viaRule->getVia(i);
    Padstack* currentViaPadstack = currentViaInfo->getPadstack();
    if (minLayer < currentViaPadstack->fromLayer() || maxLayer > currentViaPadstack->toLayer()) continue;
    board->generateSnapshot();
    result = board->forcedVia(currentViaInfo, this->prevCorner, this->netNumbers, clearanceClass,
                              penHalfWidthArr, maxShoveTraceRecursionDepth, 0, this->traceTidyWidth,
                              this->pullTightAccuracy, pullTightTimeLimit);
    if (result) {
      viaFound = true;
      break;
    }
    setShoveFailingObstacle(board->getShoveFailingObstacle());
    board->undo(nullptr);
  }
  if (viaFound) this->layer = toLayer;
  return result;
}

bool Route::snapToSmdCenter(int32_t layerIndex) {
  auto* selectionFilter = new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::PINS);
  jrt::Set<Item*>* pickedItems = board->pickItems(this->prevCorner, layerIndex, selectionFilter);
  Pin* foundSmdPin = nullptr;
  for (Item* currentItem : *pickedItems) {
    auto* currentPin = jrt::fast_cast<Pin>(currentItem);
    if (currentPin != nullptr && currentItem->sharesNetNo(this->netNumbers)) {
      if (currentPin->firstLayer() == layerIndex && currentPin->lastLayer() == layerIndex) {
        foundSmdPin = currentPin;
        break;
      }
    }
  }
  if (foundSmdPin == nullptr) return false;
  auto* toCorner = jrt::fast_cast<IntPoint>(foundSmdPin->getCenter());
  if (toCorner == nullptr) return false;
  if (this->connect(this->prevCorner, toCorner)) this->prevCorner = toCorner;
  return true;
}

bool Route::connectToTarget(IntPoint* fromPoint) {
  if (nearestTargetItem != nullptr && targetSet != nullptr && !targetSet->contains(nearestTargetItem)) {
    nearestTargetItem = nullptr;
  }
  if (nearestTargetItem == nullptr || !nearestTargetItem->sharesNetNo(this->netNumbers)) return false;
  bool routeCompleted = false;
  Point* connectionPoint = nullptr;
  if (auto* target = jrt::fast_cast<DrillItem>(nearestTargetItem)) {
    connectionPoint = target->getCenter();
  } else if (auto* trace = jrt::fast_cast<PolylineTrace>(nearestTargetItem)) {
    return board->connectToTrace(fromPoint, trace, (*this->penHalfWidthArr)[layer], this->clearanceClass);
  } else if (jrt::fast_cast<ConductionArea>(nearestTargetItem) != nullptr) {
    connectionPoint = fromPoint;
  }
  if (auto* point = jrt::fast_cast<IntPoint>(connectionPoint)) routeCompleted = connect(fromPoint, point);
  return routeCompleted;
}

bool Route::connect(Point* fromPoint, IntPoint* toPoint) {
  jrt::Array<Point*>* corners = angledConnection(fromPoint, toPoint);
  bool connectionSucceeded = true;
  for (int32_t i = 1; i < corners->length; i++) {
    Point* fromCorner = (*corners)[i - 1];
    Point* toCorner = (*corners)[i];
    auto* timeLimit = new TimeLimit(CHECK_FORCED_TRACE_TIME_LIMIT);
    while (!fromCorner->equals(toCorner)) {
      Point* currentOkPoint = board->insertForcedTraceSegment(
          fromCorner, toCorner, (*penHalfWidthArr)[layer], this->layer, netNumbers, clearanceClass,
          maxShoveTraceRecursionDepth, maxShoveViaRecursionDepth, maxSpringOverRecursionDepth,
          traceTidyWidth, pullTightAccuracy, !isStitchMode, timeLimit);
      if (currentOkPoint == nullptr) {
        board->undo(nullptr);
        return true;
      }
      if (currentOkPoint->equals(fromCorner) && this->withNeckdown) {
        currentOkPoint = tryNeckdownAtEnd(fromCorner, toCorner);
      }
      if (currentOkPoint->equals(fromCorner)) {
        this->prevCorner = fromCorner;
        connectionSucceeded = false;
        break;
      }
      fromCorner = currentOkPoint;
    }
  }
  return connectionSucceeded;
}

int32_t Route::nearestTargetLayer() {
  if (nearestTargetItem == nullptr) return this->layer;
  int32_t firstLayer = nearestTargetItem->firstLayer();
  int32_t lastLayer = nearestTargetItem->lastLayer();
  if (this->layer < firstLayer) return firstLayer;
  return jrt::Math::min(this->layer, lastLayer);
}

jrt::Array<Point*>* Route::angledConnection(Point* fromPoint, Point* toPoint) {
  IntPoint* addCorner = nullptr;
  auto* point = jrt::fast_cast<IntPoint>(fromPoint);
  auto* point1 = jrt::fast_cast<IntPoint>(toPoint);
  if (point != nullptr && point1 != nullptr) {
    AngleRestriction angleRestriction = this->board->rules->getTraceAngleRestriction();
    if (angleRestriction == AngleRestriction::NINETY_DEGREE) {
      addCorner = point->ninetyDegreeCorner(point1, true);
    } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
      addCorner = point->fortyfiveDegreeCorner(point1, true);
    }
  }
  int32_t newCornerCount = 2;
  if (addCorner != nullptr) ++newCornerCount;
  auto* result = new jrt::Array<Point*>(newCornerCount);
  (*result)[0] = fromPoint;
  if (addCorner != nullptr) (*result)[1] = addCorner;
  (*result)[result->length - 1] = toPoint;
  return result;
}

void Route::calculateTargetPointsAndAreas() {
  targetPoints = new jrt::LinkedList<Route_TargetPoint*>();
  targetTracesAndAreas = new jrt::LinkedList<Item*>();
  if (targetSet == nullptr) return;
  for (Item* currentObject : *targetSet) {
    if (auto* item = jrt::fast_cast<DrillItem>(currentObject)) {
      Point* currentPoint = item->getCenter();
      targetPoints->add(new Route_TargetPoint(currentPoint->toFloat(), currentObject));
    } else if (jrt::fast_cast<Trace>(currentObject) != nullptr ||
               jrt::fast_cast<ConductionArea>(currentObject) != nullptr) {
      targetTracesAndAreas->add(currentObject);
    }
  }
}

bool Route::isLayerActive(int32_t layerIndex) {
  if (layerIndex < 0 || layerIndex >= layerActive->length) return false;
  return (*layerActive)[layerIndex];
}

void Route::calcNearestTargetPoint(FloatPoint* fromPoint) {
  double minDist = jrt::Double::MAX_VALUE;
  FloatPoint* nearestPoint = nullptr;
  Item* nearestItem = nullptr;
  for (Route_TargetPoint* currentTargetPoint : *targetPoints) {
    double currentDistance = fromPoint->distance(currentTargetPoint->location);
    if (currentDistance < minDist) {
      minDist = currentDistance;
      nearestPoint = currentTargetPoint->location;
      nearestItem = currentTargetPoint->item;
    }
  }
  for (Item* currentItem : *targetTracesAndAreas) {
    if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
      Polyline* currentPolyline = currentTrace->polyline();
      if (currentPolyline->boundingBox()->distance(fromPoint) < minDist) {
        FloatPoint* currentNearestPoint = currentPolyline->nearestPointApprox(fromPoint);
        double currentDistance = fromPoint->distance(currentNearestPoint);
        if (currentDistance < minDist) {
          minDist = currentDistance;
          nearestPoint = currentNearestPoint;
          nearestItem = currentTrace;
        }
      }
    } else if (auto* currentConductionArea = jrt::fast_cast<ConductionArea>(currentItem);
               currentConductionArea != nullptr && currentItem->tileShapeCount() > 0) {
      Area* currentArea = currentConductionArea->getArea();
      if (currentArea->boundingBox()->distance(fromPoint) < minDist) {
        FloatPoint* currentNearestPoint = currentArea->nearestPointApprox(fromPoint);
        double currentDistance = fromPoint->distance(currentNearestPoint);
        if (currentDistance < minDist) {
          minDist = currentDistance;
          nearestPoint = currentNearestPoint;
          nearestItem = currentConductionArea;
        }
      }
    }
  }
  if (nearestPoint == nullptr) return;  // target set is empty
  nearestTargetPoint = nearestPoint;
  nearestTargetItem = nearestItem;
  board->joinGraphicsUpdateBox(nearestItem->boundingBox());
}

void Route::setShoveFailingObstacle(Item* item) {
  this->shoveFailingObstacle = item;
  if (item != nullptr) this->board->joinGraphicsUpdateBox(item->boundingBox());
}

Point* Route::tryNeckdownAtStart(IntPoint* toCorner) {
  auto* startPin = jrt::fast_cast<Pin>(this->startItem);
  if (startPin == nullptr) return this->prevCorner;
  if (!startPin->isOnLayer(this->layer)) return this->prevCorner;
  FloatPoint* pinCenter = startPin->getCenter()->toFloat();
  double currentClearance = this->board->rules->clearanceMatrix->getValue(
      this->clearanceClass, startPin->clearanceClassIndex(), this->layer, true);
  double pinNeckDownDistance = 2 * (0.5 * startPin->getMaxWidth(this->layer) + currentClearance);
  if (pinCenter->distance(this->prevCorner->toFloat()) >= pinNeckDownDistance) return this->prevCorner;
  int32_t neckDownHalfwidth = startPin->getTraceNeckdownHalfwidth(this->layer);
  if (neckDownHalfwidth >= (*this->penHalfWidthArr)[this->layer]) return this->prevCorner;
  if (!this->prevCorner->equals(startPin->getCenter())) {
    Item* pickedItem = this->board->pickNearestRoutingItem(this->prevCorner, this->layer, nullptr);
    if (auto* trace = jrt::fast_cast<Trace>(pickedItem)) {
      if (trace->getHalfWidth() > neckDownHalfwidth) return this->prevCorner;
    }
  }
  auto* timeLimit = new TimeLimit(CHECK_FORCED_TRACE_TIME_LIMIT);
  return board->insertForcedTraceSegment(prevCorner, toCorner, neckDownHalfwidth, layer, netNumbers,
                                         clearanceClass, maxShoveTraceRecursionDepth,
                                         maxShoveViaRecursionDepth, maxSpringOverRecursionDepth,
                                         traceTidyWidth, pullTightAccuracy, !isStitchMode, timeLimit);
}

Point* Route::tryNeckdownAtEnd(Point* fromCorner, Point* toCorner) {
  auto* targetPin = jrt::fast_cast<Pin>(this->nearestTargetItem);
  if (targetPin == nullptr) return fromCorner;
  if (!targetPin->isOnLayer(this->layer)) return fromCorner;
  FloatPoint* pinCenter = targetPin->getCenter()->toFloat();
  double currentClearance = this->board->rules->clearanceMatrix->getValue(
      this->clearanceClass, targetPin->clearanceClassIndex(), this->layer, true);
  double pinNeckDownDistance = 2 * (0.5 * targetPin->getMaxWidth(this->layer) + currentClearance);
  if (pinCenter->distance(fromCorner->toFloat()) >= pinNeckDownDistance) return fromCorner;
  int32_t neckDownHalfwidth = targetPin->getTraceNeckdownHalfwidth(this->layer);
  if (neckDownHalfwidth >= (*this->penHalfWidthArr)[this->layer]) return fromCorner;
  auto* timeLimit = new TimeLimit(CHECK_FORCED_TRACE_TIME_LIMIT);
  return board->insertForcedTraceSegment(fromCorner, toCorner, neckDownHalfwidth, layer, netNumbers,
                                         clearanceClass, maxShoveTraceRecursionDepth,
                                         maxShoveViaRecursionDepth, maxSpringOverRecursionDepth,
                                         traceTidyWidth, pullTightAccuracy, !isStitchMode, timeLimit);
}

}  // namespace fr
