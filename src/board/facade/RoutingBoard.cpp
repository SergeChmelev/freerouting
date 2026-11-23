// Port of app.freerouting.board.facade.RoutingBoard
#include "board/facade/RoutingBoard.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/RoutingFailureLog.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "board/actions/DrillItemMover.h"
#include "board/actions/ForcedViaInserter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoardOperations.h"
#include "board/facade/RoutingBoardSearchFacade.h"
#include "board/facade/RoutingBoardUndoFacade.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/optimize/ShoveCheckCache.h"
#include "board/optimize/TraceShover.h"
#include "board/optimize/TraceTightener.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/ChangedArea.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/scoring/BoardStatistics.h"
#include "datastructures/IdGenerator.h"
#include "datastructures/Stoppable.h"
#include "datastructures/TimeLimit.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"
#include "settings/AutorouterSettings.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// RoutingBoard_PinNetAssignment (Java local record)
// ---------------------------------------------------------------------------------------------

bool RoutingBoard_PinNetAssignment::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* other = jrt::fast_cast<RoutingBoard_PinNetAssignment>(o);
  if (other == nullptr) {
    return false;
  }
  return jrt::equalsOf<Pin*>(pin_, other->pin_) && net_ == other->net_;
}

int32_t RoutingBoard_PinNetAssignment::hashCode() {
  int32_t h = jrt::hashOf<Pin*>(pin_);
  h = jrt::hashCombine(h, jrt::Integer::hashCode(net_));
  return h;
}

std::string RoutingBoard_PinNetAssignment::toString() {
  return "PinNetAssignment[pin=" + jrt::str(pin_) + ", net=" + jrt::str(net_) + "]";
}

// ---------------------------------------------------------------------------------------------
// RoutingBoard
// ---------------------------------------------------------------------------------------------

RoutingBoard::RoutingBoard(IntBox* boundingBox, LayerStructure* layerStructure,
                           jrt::Array<PolylineShape*>* outlineShapes, int32_t outlineClClassNo,
                           BoardRules* rules, Communication* boardCommunication)
    : BasicBoard(boundingBox, layerStructure, outlineShapes, outlineClClassNo, rules,
                 boardCommunication) {
  this->failureLog = new (jrt::kBoard) RoutingFailureLog();
}

RoutingBoard::RoutingBoard(BasicBoard_CopyTag tag, RoutingBoard* source)
    : BasicBoard(tag, source) {
  // PORT NOTE: replaces Java deserialization. failureLog is non-transient: deep copy. Transient
  // fields get the Java default values.
  this->failureLog = source->failureLog != nullptr ? source->failureLog->deepCopy() : nullptr;
  changedArea = nullptr;
  autorouteEngine = nullptr;
  shoveFailingObstacle = nullptr;
  shoveFailingLayer = 0;
  shoveFailingObstacleWrites = 0;
  shoveFailingLayerWrites = 0;
  shoveCacheObstacles = nullptr;
  operations = nullptr;
  searchFacade = nullptr;
  undoFacade = nullptr;
}

void RoutingBoard::additionalUpdateAfterChange(Item* item) {
  if (item == nullptr) {
    return;
  }
  if (this->autorouteEngine == nullptr || !this->autorouteEngine->maintainDatabase) {
    return;
  }
  // Invalidate the free space expansion rooms touching a shape of item.
  int32_t shapeCount = item->treeShapeCount(this->autorouteEngine->autorouteSearchTree);
  for (int32_t i = 0; i < shapeCount; i++) {
    TileShape* currentShape = item->getTreeShape(this->autorouteEngine->autorouteSearchTree, i);
    this->autorouteEngine->invalidateDrillPages(currentShape);
    int32_t currentLayer = item->shapeLayer(i);
    jrt::Collection<SearchTreeObject*>* overlaps =
        this->autorouteEngine->autorouteSearchTree->overlappingObjects(currentShape, currentLayer);
    for (SearchTreeObject* currentObject : *overlaps) {
      if (auto* room = jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(currentObject)) {
        this->autorouteEngine->removeCompleteExpansionRoom(room);
      }
    }
  }
  item->clearAutorouteInfo();
}

bool RoutingBoard::removeItemsAndPullTight(jrt::Collection<Item*>* itemList, int32_t tidyWidth,
                                           int32_t pullTightAccuracy) {
  return getOperations()->removeItemsAndPullTight(itemList, tidyWidth, pullTightAccuracy);
}

void RoutingBoard::startMarkingChangedArea() { getOperations()->startMarkingChangedArea(); }

void RoutingBoard::joinChangedArea(FloatPoint* point, int32_t layer) {
  getOperations()->joinChangedArea(point, layer);
}

void RoutingBoard::markAllChangedArea() { getOperations()->markAllChangedArea(); }

void RoutingBoard::optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                                  int32_t accuracy,
                                  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                                  Stoppable* stoppableThread) {
  optChangedArea(onlyNetNoArr, clipShape, accuracy, traceCosts, stoppableThread, 0);
}

void RoutingBoard::optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                                  int32_t accuracy,
                                  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                                  Stoppable* stoppableThread, int32_t timeLimit) {
  getOperations()->optChangedArea(onlyNetNoArr, clipShape, accuracy, traceCosts, stoppableThread,
                                  timeLimit, nullptr, 0);
}

void RoutingBoard::optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                                  int32_t accuracy,
                                  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                                  Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                                  int32_t keepPointLayer) {
  getOperations()->optChangedArea(onlyNetNoArr, clipShape, accuracy, traceCosts, stoppableThread,
                                  timeLimit, keepPoint, keepPointLayer);
}

double RoutingBoard::checkTraceSegment(Point* fromPoint, Point* toPoint, int32_t layer,
                                       jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                                       int32_t clClassNo, bool onlyNotShovableObstacles) {
  return getSearchFacade()->checkTraceSegment(fromPoint, toPoint, layer, netNumbers,
                                              traceHalfWidth, clClassNo,
                                              onlyNotShovableObstacles);
}

double RoutingBoard::checkTraceSegment(LineSegment* lineSegment, int32_t layer,
                                       jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                                       int32_t clClassNo, bool onlyNotShovableObstacles) {
  return getSearchFacade()->checkTraceSegment(lineSegment, layer, netNumbers, traceHalfWidth,
                                              clClassNo, onlyNotShovableObstacles);
}

bool RoutingBoard::checkMoveItem(Item* item, Vector* vector, jrt::Collection<Item*>* ignoreItems) {
  return getSearchFacade()->checkMoveItem(item, vector, ignoreItems);
}

bool RoutingBoard::checkChangeNet(Item* item, int32_t newNetNo) {
  return getSearchFacade()->checkChangeNet(item, newNetNo);
}

bool RoutingBoard::moveDrillItem(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                                 int32_t maxViaRecursionDepth, int32_t tidyWidth,
                                 int32_t pullTightAccuracy, int32_t pullTightTimeLimit) {
  clearShoveFailingObstacle();
  // unfix the connected shove fixed traces.
  jrt::Collection<Item*>* contactList = drillItem->getNormalContacts();
  for (Item* currentContact : *contactList) {
    if (currentContact->getFixedState() == FixedState::SHOVE_FIXED) {
      currentContact->setFixedState(FixedState::UNFIXED);
    }
  }

  jrt::Array<IntOctagon*>* tidyRegion = new (jrt::kGeom) jrt::Array<IntOctagon*>(1);
  bool calculateTidyRegion;
  if (tidyWidth < jrt::Integer::MAX_VALUE) {
    (*tidyRegion)[0] = IntOctagon::EMPTY();
    calculateTidyRegion = tidyWidth > 0;
  } else {
    (*tidyRegion)[0] = nullptr;
    calculateTidyRegion = false;
  }
  startMarkingChangedArea();
  if (!DrillItemMover::insertJoiningTidyRegion(drillItem, vector, maxRecursionDepth,
                                               maxViaRecursionDepth, tidyRegion, this)) {
    return false;
  }
  IntOctagon* finalTidyRegion = (*tidyRegion)[0];
  if (calculateTidyRegion && finalTidyRegion != nullptr) {
    finalTidyRegion = finalTidyRegion->enlarge(tidyWidth);
  }
  jrt::Array<int32_t>* optNetNoArr;
  if (maxRecursionDepth <= 0) {
    jrt::Array<int32_t>* netNumbers = drillItem->netNumbers;
    optNetNoArr = netNumbers;
  } else {
    optNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(0);
  }
  optChangedArea(optNetNoArr, finalTidyRegion, pullTightAccuracy, nullptr, nullptr,
                 pullTightTimeLimit);
  return true;
}

Item* RoutingBoard::pickNearestRoutingItem(Point* location, int32_t layer, Item* fromItem) {
  return getSearchFacade()->pickNearestRoutingItem(location, layer, fromItem);
}

bool RoutingBoard::forcedVia(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                             int32_t traceClearanceClassIndex,
                             jrt::Array<int32_t>* tracePenHalfwidthArr, int32_t maxRecursionDepth,
                             int32_t maxViaRecursionDepth, int32_t tidyWidth,
                             int32_t pullTightAccuracy, int32_t pullTightTimeLimit) {
  clearShoveFailingObstacle();
  this->startMarkingChangedArea();
  bool result = ForcedViaInserter::insert(viaInfo, location, netNumbers, traceClearanceClassIndex,
                                          tracePenHalfwidthArr, maxRecursionDepth,
                                          maxViaRecursionDepth, this);
  if (result) {
    IntOctagon* tidyClipShape;
    if (tidyWidth < jrt::Integer::MAX_VALUE) {
      tidyClipShape = location->surroundingOctagon()->enlarge(tidyWidth);
    } else {
      tidyClipShape = nullptr;
    }
    jrt::Array<int32_t>* optNetNoArr;
    if (maxRecursionDepth <= 0) {
      optNetNoArr = netNumbers;
    } else {
      optNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(0);
    }
    this->optChangedArea(optNetNoArr, tidyClipShape, pullTightAccuracy, nullptr, nullptr,
                         pullTightTimeLimit);
  }
  return result;
}

Point* RoutingBoard::insertForcedTraceSegment(Point* fromCorner, Point* toCorner,
                                              int32_t halfWidth, int32_t layer,
                                              jrt::Array<int32_t>* netNumbers,
                                              int32_t clearanceClassIndex,
                                              int32_t maxRecursionDepth,
                                              int32_t maxViaRecursionDepth,
                                              int32_t maxSpringOverRecursionDepth,
                                              int32_t tidyWidth, int32_t pullTightAccuracy,
                                              bool withCheck, TimeLimit* timeLimit) {
  if (fromCorner->equals(toCorner)) {
    return toCorner;
  }
  Polyline* insertPolyline = new (jrt::kGeom) Polyline(fromCorner, toCorner);
  Point* okPoint = insertForcedTracePolyline(
      insertPolyline, halfWidth, layer, netNumbers, clearanceClassIndex, maxRecursionDepth,
      maxViaRecursionDepth, maxSpringOverRecursionDepth, tidyWidth, pullTightAccuracy, withCheck,
      timeLimit);
  Point* result;
  if (okPoint == insertPolyline->firstCorner()) {
    result = fromCorner;
  } else if (okPoint == insertPolyline->lastCorner()) {
    result = toCorner;
  } else {
    result = okPoint;
  }
  return result;
}

bool RoutingBoard::checkForcedTracePolyline(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                            jrt::Array<int32_t>* netNumbers,
                                            int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                            int32_t maxViaRecursionDepth,
                                            int32_t maxSpringOverRecursionDepth) {
  ShapeSearchTree* searchTree = searchTreeManager->getDefaultTree();
  int32_t compensatedHalfWidth =
      halfWidth + searchTree->clearanceCompensationValue(clearanceClassIndex, layer);
  jrt::Array<TileShape*>* traceShapes =
      polyline->offsetShapes(compensatedHalfWidth, 0, polyline->lines->length - 1);
  bool orthogonalMode = rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE;
  TraceShover* shoveTraceAlgo = new (jrt::kOp) TraceShover(this);
  for (int32_t i = 0; i < traceShapes->length; i++) {
    TileShape* currentTraceShape = (*traceShapes)[i];
    if (orthogonalMode) {
      currentTraceShape = currentTraceShape->boundingBox();
    }
    ShapeEntrySide* fromSide = new (jrt::kBoard) ShapeEntrySide(polyline, i + 1, currentTraceShape);

    bool checkShoveOk = shoveTraceAlgo->check(currentTraceShape, fromSide, nullptr, layer,
                                              netNumbers, clearanceClassIndex, maxRecursionDepth,
                                              maxViaRecursionDepth, maxSpringOverRecursionDepth,
                                              nullptr);
    if (!checkShoveOk) {
      return false;
    }
  }
  return true;
}

namespace {

std::string netLayerLabel(jrt::Array<int32_t>* netNumbers, int32_t layer) {
  return "Net #" + jrt::str((*netNumbers)[0]) + ",Layer #" + jrt::str(layer);
}

}  // namespace

Point* RoutingBoard::insertForcedTracePolyline(Polyline* polyline, int32_t halfWidth,
                                               int32_t layer, jrt::Array<int32_t>* netNumbers,
                                               int32_t clearanceClassIndex,
                                               int32_t maxRecursionDepth,
                                               int32_t maxViaRecursionDepth,
                                               int32_t maxSpringOverRecursionDepth,
                                               int32_t tidyWidth, int32_t pullTightAccuracy,
                                               bool withCheck, TimeLimit* timeLimit) {
  // PORT NOTE: the FRLogger.trace payloads below are only built when TRACE logging is enabled
  // (Java builds them unconditionally; the output is the same).
  const bool traceOn = FRLogger::isTraceEnabled();
  clearShoveFailingObstacle();
  Point* fromCorner = polyline->firstCorner();
  Point* toCorner = polyline->lastCorner();
  if (fromCorner == nullptr || toCorner == nullptr) {
    // A degenerate polyline has no well-defined first/last corner. The trace cannot be inserted;
    // return null so the caller treats this segment as not inserted and reroutes.
    FRLogger::trace(
        "RoutingBoard.insert_forced_trace_polyline: degenerate polyline (null corner), skipping");
    return nullptr;
  }
  if (fromCorner->equals(toCorner)) {
    return toCorner;
  }
  if (!(jrt::fast_cast<IntPoint>(fromCorner) != nullptr &&
        jrt::fast_cast<IntPoint>(toCorner) != nullptr)) {
    FRLogger::warn("RoutingBoard.insert_forced_trace_segment: only implemented for IntPoints");
    return fromCorner;
  }
  startMarkingChangedArea();
  // Check, if there ends an item of the same net at fromCorner.
  // If so, its geometry will be used to cut off dog ears of the check shape.
  Trace* pickedTrace = nullptr;
  ItemSelectionFilter* filter =
      new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
  jrt::Set<Item*>* pickedItems = this->pickItems(fromCorner, layer, filter);
  if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
    FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                    ", step=start, pickedSize=" + jrt::str(pickedItems->size()) +
                    ", from=" + jrt::str(fromCorner) + ", to=" + jrt::str(toCorner) +
                    ", idMax=" + jrt::str(communication->idGenerator->maxGeneratedId()));
  }
  if (pickedItems->size() == 1) {
    Trace* currentPickedTrace = jrt::fast_cast<Trace>(pickedItems->iterator()->next());
    if (currentPickedTrace->netsEqual(netNumbers) &&
        currentPickedTrace->getHalfWidth() == halfWidth &&
        currentPickedTrace->clearanceClassIndex() == clearanceClassIndex &&
        (jrt::fast_cast<PolylineTrace>(currentPickedTrace) != nullptr)) {
      // can combine with the picked trace
      pickedTrace = currentPickedTrace;
    }
  }
  ShapeSearchTree* searchTree = searchTreeManager->getDefaultTree();
  int32_t compensatedHalfWidth =
      halfWidth + searchTree->clearanceCompensationValue(clearanceClassIndex, layer);
  TraceShover* shoveTraceAlgo = new (jrt::kOp) TraceShover(this);
  Polyline* newPolyline = shoveTraceAlgo->springOverObstacles(
      polyline, compensatedHalfWidth, layer, netNumbers, clearanceClassIndex, nullptr);
  if (newPolyline == nullptr) {
    if (traceOn && netNumbers != nullptr && netNumbers->length > 0 && (*netNumbers)[0] == 94) {
      FRLogger::trace("RoutingBoard.insert_forced_trace_polyline",
                      "compare_trace_insert_forced_fail", "spring_over_obstacles returned null",
                      netLayerLabel(netNumbers, layer),
                      jrt::Array<Point*>::of({fromCorner, toCorner}));
    }
    return fromCorner;
  }
  Polyline* combinedPolyline;
  if (pickedTrace == nullptr) {
    combinedPolyline = newPolyline;
  } else {
    PolylineTrace* combineTrace = jrt::fast_cast<PolylineTrace>(pickedTrace);
    combinedPolyline = newPolyline->combine(combineTrace->polyline());
  }
  if (combinedPolyline->lines->length < 3) {
    if (traceOn && netNumbers != nullptr && netNumbers->length > 0 && (*netNumbers)[0] == 94) {
      FRLogger::trace("RoutingBoard.insert_forced_trace_polyline",
                      "compare_trace_insert_forced_fail", "combinedPolyline.lines.length < 3",
                      netLayerLabel(netNumbers, layer),
                      jrt::Array<Point*>::of({fromCorner, toCorner}));
    }
    return fromCorner;
  }
  int32_t startShapeNo = combinedPolyline->lines->length - newPolyline->lines->length;
  // calculate the last shapes of combinedPolyline for checking
  jrt::Array<TileShape*>* traceShapes = combinedPolyline->offsetShapes(
      compensatedHalfWidth, startShapeNo, combinedPolyline->lines->length - 1);
  int32_t lastShapeNo = traceShapes->length;
  bool orthogonalMode = rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE;
  int32_t idBeforeShoveLoop = communication->idGenerator->maxGeneratedId();
  for (int32_t i = 0; i < traceShapes->length; i++) {
    TileShape* currentTraceShape = (*traceShapes)[i];
    if (orthogonalMode) {
      currentTraceShape = currentTraceShape->boundingBox();
    }
    ShapeEntrySide* fromSide = new (jrt::kBoard) ShapeEntrySide(
        combinedPolyline, combinedPolyline->cornerCount() - traceShapes->length - 1 + i,
        currentTraceShape);
    if (withCheck) {
      bool checkShoveOk = shoveTraceAlgo->check(
          currentTraceShape, fromSide, nullptr, layer, netNumbers, clearanceClassIndex,
          maxRecursionDepth, maxViaRecursionDepth, maxSpringOverRecursionDepth, timeLimit);
      if (!checkShoveOk) {
        lastShapeNo = i;
        break;
      }
    }
    int32_t idBeforeShove = communication->idGenerator->maxGeneratedId();
    bool insertOk = shoveTraceAlgo->insert(currentTraceShape, fromSide, layer, netNumbers,
                                           clearanceClassIndex, nullptr, maxRecursionDepth,
                                           maxViaRecursionDepth, maxSpringOverRecursionDepth);
    int32_t idAfterShove = communication->idGenerator->maxGeneratedId();
    if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
      FRLogger::trace("compare_trace_shove_shape net=" + jrt::str((*netNumbers)[0]) +
                      ", shapeIdx=" + jrt::str(i) + ", idBefore=" + jrt::str(idBeforeShove) +
                      ", idAfter=" + jrt::str(idAfterShove) +
                      ", delta=" + jrt::str(idAfterShove - idBeforeShove));
    }
    if (!insertOk) {
      return nullptr;
    }
  }
  Point* newCorner = toCorner;
  if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
    FRLogger::trace(
        "compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
        ", step=after_shove_loop, shoveLoopDelta=" +
        jrt::str(communication->idGenerator->maxGeneratedId() - idBeforeShoveLoop) +
        ", lastShapeNo=" + jrt::str(lastShapeNo) +
        ", traceShapes.length=" + jrt::str(traceShapes->length) +
        ", idMax=" + jrt::str(communication->idGenerator->maxGeneratedId()));
  }
  if (lastShapeNo < traceShapes->length) {
    // the shove with index lastShapeNo failed.
    // Sample the shove line to a shorter shove distance and try again.
    TileShape* lastTraceShape = (*traceShapes)[lastShapeNo];
    if (orthogonalMode) {
      lastTraceShape = lastTraceShape->boundingBox();
    }
    int32_t sampleWidth = 2 * this->getMinTraceHalfWidth();
    FloatPoint* lastCorner = newPolyline->cornerApprox(lastShapeNo + 1);
    FloatPoint* prevLastCorner = newPolyline->cornerApprox(lastShapeNo);
    double lastSegmentLength = lastCorner->distance(prevLastCorner);
    if (lastSegmentLength > 100 * sampleWidth) {
      // to many cycles to sample
      if (traceOn && netNumbers != nullptr && netNumbers->length > 0 && (*netNumbers)[0] == 94) {
        FRLogger::trace("RoutingBoard.insert_forced_trace_polyline",
                        "compare_trace_insert_forced_fail", "too many cycles to sample",
                        netLayerLabel(netNumbers, layer),
                        jrt::Array<Point*>::of({fromCorner, toCorner}));
      }
      return fromCorner;
    }
    int32_t shapeIndex = combinedPolyline->cornerCount() - traceShapes->length - 1 + lastShapeNo;
    if (lastSegmentLength > sampleWidth) {
      newPolyline = newPolyline->shorten(
          newPolyline->lines->length - (traceShapes->length - lastShapeNo - 1), sampleWidth);
      Point* currentLastCorner = newPolyline->lastCorner();
      if (jrt::fast_cast<IntPoint>(currentLastCorner) == nullptr) {
        FRLogger::trace("RoutingBoard.insert_forced_trace_polyline: IntPoint expected");
        if (traceOn && netNumbers != nullptr && netNumbers->length > 0 &&
            (*netNumbers)[0] == 94) {
          FRLogger::trace("RoutingBoard.insert_forced_trace_polyline",
                          "compare_trace_insert_forced_fail",
                          "currentLastCorner is not an IntPoint",
                          netLayerLabel(netNumbers, layer),
                          jrt::Array<Point*>::of({fromCorner, toCorner}));
        }
        return fromCorner;
      }
      newCorner = currentLastCorner;
      if (pickedTrace == nullptr) {
        combinedPolyline = newPolyline;
      } else {
        PolylineTrace* combineTrace = jrt::fast_cast<PolylineTrace>(pickedTrace);
        combinedPolyline = newPolyline->combine(combineTrace->polyline());
      }
      if (combinedPolyline->lines->length < 3) {
        return newCorner;
      }
      shapeIndex = combinedPolyline->lines->length - 3;
      lastTraceShape = combinedPolyline->offsetShape(compensatedHalfWidth, shapeIndex);
      if (orthogonalMode) {
        lastTraceShape = lastTraceShape->boundingBox();
      }
    }
    ShapeEntrySide* fromSide = new (jrt::kBoard) ShapeEntrySide(combinedPolyline, shapeIndex, lastTraceShape);
    bool checkShoveOk = shoveTraceAlgo->check(lastTraceShape, fromSide, nullptr, layer, netNumbers,
                                              clearanceClassIndex, maxRecursionDepth,
                                              maxViaRecursionDepth, maxSpringOverRecursionDepth,
                                              timeLimit);
    if (!checkShoveOk) {
      if (traceOn && netNumbers != nullptr && netNumbers->length > 0 && (*netNumbers)[0] == 94) {
        Item* shoveFailingObstacle = this->getShoveFailingObstacle();
        FRLogger::trace("RoutingBoard.insert_forced_trace_polyline",
                        "compare_trace_insert_forced_fail", "checkShoveOk returned false",
                        netLayerLabel(netNumbers, layer),
                        jrt::Array<Point*>::of({fromCorner, toCorner}));
        FRLogger::trace(
            "RoutingBoard.insert_forced_trace_polyline", "compare_trace_insert_forced_obstacle",
            "failing obstacle=" + jrt::str(shoveFailingObstacle),
            "Net #" + jrt::str((*netNumbers)[0]) + ",Layer #" + jrt::str(layer) + ",Obstacle=" +
                (shoveFailingObstacle == nullptr
                     ? std::string("null")
                     : jrt::className(shoveFailingObstacle) + "#" +
                           jrt::str(shoveFailingObstacle->getId())),
            jrt::Array<Point*>::of({fromCorner, toCorner}));
      }
      return fromCorner;
    }
    bool insertOk = shoveTraceAlgo->insert(lastTraceShape, fromSide, layer, netNumbers,
                                           clearanceClassIndex, nullptr, maxRecursionDepth,
                                           maxViaRecursionDepth, maxSpringOverRecursionDepth);
    if (!insertOk) {
      FRLogger::trace("RoutingBoard.insert_forced_trace_polyline: shove trace failed");
      return nullptr;
    }
  }
  // insert the new trace segment
  for (int32_t i = 0; i < newPolyline->cornerCount(); i++) {
    joinChangedArea(newPolyline->cornerApprox(i), layer);
  }
  int32_t idBeforeInsert = communication->idGenerator->maxGeneratedId();
  PolylineTrace* newTrace = insertTraceWithoutCleaning(newPolyline, layer, halfWidth, netNumbers,
                                                       clearanceClassIndex, FixedState::UNFIXED);
  int32_t idAfterInsert = communication->idGenerator->maxGeneratedId();
  bool combineResult = newTrace->combine();
  int32_t idAfterCombine = communication->idGenerator->maxGeneratedId();
  if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
    FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                    ", step=insert_and_combine" +
                    ", insertDelta=" + jrt::str(idAfterInsert - idBeforeInsert) +
                    ", combineDelta=" + jrt::str(idAfterCombine - idAfterInsert) +
                    ", combined=" + jrt::str(combineResult) + ", idMax=" +
                    jrt::str(idAfterCombine));
  }

  IntOctagon* tidyRegion = nullptr;
  if (tidyWidth < jrt::Integer::MAX_VALUE) {
    tidyRegion = newCorner->surroundingOctagon()->enlarge(tidyWidth);
  }
  jrt::Array<int32_t>* optNetNoArr;
  if (maxRecursionDepth <= 0) {
    optNetNoArr = netNumbers;
  } else {
    optNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(0);
  }
  TraceTightener* pullTightAlgo = TraceTightener::getInstance(
      this, optNetNoArr, tidyRegion, pullTightAccuracy, nullptr, -1, newCorner, layer);

  try {
    // Remove evtl. generated cycles because otherwise pullTight may not work correctly.
    int32_t idBeforeNorm = communication->idGenerator->maxGeneratedId();
    bool normalizeResult = newTrace != nullptr && newTrace->normalize(changedArea->getArea(layer));
    int32_t idAfterNorm = communication->idGenerator->maxGeneratedId();
    if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
      FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                      ", step=normalize, result=" + jrt::str(normalizeResult) +
                      ", idBefore=" + jrt::str(idBeforeNorm) + ", idAfter=" +
                      jrt::str(idAfterNorm) + ", delta=" + jrt::str(idAfterNorm - idBeforeNorm));
    }
    if (normalizeResult) {
      int32_t idBeforeSplit = communication->idGenerator->maxGeneratedId();
      pullTightAlgo->splitTracesAtKeepPoint();
      int32_t idAfterSplit = communication->idGenerator->maxGeneratedId();
      if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
        FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                        ", step=split_at_keep, idBefore=" + jrt::str(idBeforeSplit) +
                        ", idAfter=" + jrt::str(idAfterSplit) +
                        ", delta=" + jrt::str(idAfterSplit - idBeforeSplit));
      }
      // otherwise the new corner may no more be contained in the new trace after optimizing
      ItemSelectionFilter* itemFilter =
          new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
      jrt::Set<Item*>* currentPickedItems = this->pickItems(newCorner, layer, itemFilter);
      newTrace = nullptr;
      if (!currentPickedItems->isEmpty()) {
        Item* foundTrace = currentPickedItems->iterator()->next();
        if (auto* trace = jrt::fast_cast<PolylineTrace>(foundTrace)) {
          newTrace = trace;
        }
      }
    }
  } catch (const jrt::Exception& e) {
    // Max normalization depth is hit for geometrically complex or degenerate trace segments.
    // The router skips the segment and continues; affected connections may remain unrouted.
    FRLogger::trace(
        "RoutingBoard.insert_forced_trace_polyline: A trace could not be normalized"
        " and was skipped. Cause: " +
        e.getMessage());
  }

  // To avoid, that a separate handling for moving backwards in the own trace line becomes
  // necessary, pull tight is called here.
  if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
    ItemSelectionFilter* dbgFilter =
        new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
    jrt::Set<Item*>* dbgBefore = this->pickItems(newCorner, layer, dbgFilter);
    FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                    ", step=before_pull_tight, pickedAtEndCorner=" + jrt::str(dbgBefore->size()) +
                    ", new_trace_null=" + jrt::str(newTrace == nullptr) +
                    ", newCorner=" + jrt::str(newCorner));
  }
  if (tidyWidth > 0 && newTrace != nullptr) {
    newTrace->pullTight(pullTightAlgo);
  }
  if (traceOn && netNumbers != nullptr && netNumbers->length > 0) {
    ItemSelectionFilter* dbgFilter =
        new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
    jrt::Set<Item*>* dbgAfter = this->pickItems(newCorner, layer, dbgFilter);
    FRLogger::trace("compare_trace_insert_forced_sub net=" + jrt::str((*netNumbers)[0]) +
                    ", step=after_pull_tight, pickedAtEndCorner=" + jrt::str(dbgAfter->size()) +
                    ", newCorner=" + jrt::str(newCorner));
  }
  return newCorner;
}

AutorouteEngine* RoutingBoard::initAutoroute(int32_t netNumber, int32_t traceClearanceClassIndex,
                                             Stoppable* stoppableThread, TimeLimit* timeLimit,
                                             bool retainAutorouteDatabase) {
  if (this->autorouteEngine == nullptr || !retainAutorouteDatabase ||
      this->autorouteEngine->autorouteSearchTree->compensatedClearanceClassNo !=
          traceClearanceClassIndex) {
    if (this->autorouteEngine != nullptr) {
      // PORT NOTE: the replaced engine is dropped (Java leaves it to the GC): free its arena.
      this->autorouteEngine->releaseSearchArena();
    }
    this->autorouteEngine =
        new (jrt::kBoard) AutorouteEngine(this, traceClearanceClassIndex, retainAutorouteDatabase);
  }
  this->autorouteEngine->initConnection(netNumber, stoppableThread, timeLimit);
  return this->autorouteEngine;
}

void RoutingBoard::finishAutoroute() {
  if (this->autorouteEngine != nullptr) {
    this->autorouteEngine->clear();
    this->autorouteEngine->releaseSearchArena();
  }
  this->autorouteEngine = nullptr;
}

void RoutingBoard::discardAutorouteEngineWithBoard() {
  // Called when this whole board is dropped: the engine's arena objects are referenced only by
  // this board (its search tree, item autoroute data), so no clear() is needed.
  if (this->autorouteEngine != nullptr) {
    this->autorouteEngine->releaseSearchArena();
  }
  this->autorouteEngine = nullptr;
}

void RoutingBoard::clearTransientAutorouteState() {
  finishAutoroute();
  clearAllItemTemporaryAutorouteData();
  changedArea = nullptr;
  shoveFailingObstacle = nullptr;
  shoveFailingLayer = -1;
}

AutorouteAttemptResult* RoutingBoard::autoroute(Item* item, RouterSettings* routerSettings,
                                                int32_t viaCosts, Stoppable* stoppableThread,
                                                TimeLimit* timeLimit) {
  if (jrt::fast_cast<Connectable>(item) == nullptr || item->netCount() == 0) {
    return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::NO_CONNECTIONS,
                                      "The item '" + jrt::str(item) + "' is not connectable.");
  }
  if (item->netCount() > 1) {
    FRLogger::warn("RoutingBoard.autoroute: netCount > 1 not yet implemented");
  }
  int32_t routeNetNo = item->getNetNumber(0);
  AutorouteControl* ctrlSettings = new (jrt::kOp) AutorouteControl(this, routeNetNo, routerSettings, viaCosts,
                                                        routerSettings->getTraceCosts());
  ctrlSettings->removeUnconnectedVias = false;
  jrt::Set<Item*>* routeStartSet = item->getConnectedSet(routeNetNo);
  Net* routeNet = rules->nets->get(routeNetNo);
  jrt::Set<Item*>* routeDestSet = item->getUnconnectedSet(routeNetNo);
  if (routeNet != nullptr && routeNet->containsPlane() &&
      ConductionArea::coversAllPlanesOfNet(routeStartSet, routeDestSet, routeNetNo)) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::CONNECTED_TO_PLANE,
        "The item '" + jrt::str(item) + "' is connected to every plane of its net.");
  }
  if (routeDestSet->isEmpty()) {
    return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::ALREADY_CONNECTED,
                                      "The item '" + jrt::str(item) + "' is already connected.");
  }
  jrt::TreeSet<Item*>* rippedItemList = new (jrt::kOp) jrt::TreeSet<Item*>();
  AutorouteEngine* currentAutorouteEngine =
      initAutoroute(item->getNetNumber(0), ctrlSettings->traceClearanceClassIndex,
                    stoppableThread, timeLimit, false);
  AutorouteAttemptResult* result = currentAutorouteEngine->autorouteConnection(
      routeStartSet, routeDestSet, ctrlSettings, rippedItemList,
      nullptr);  // null: costs not needed here
  if (result->state == AutorouteAttemptState::ROUTED) {
    optChangedArea(jrt::Array<int32_t>::of({routeNetNo}), nullptr,
                   routerSettings->tracePullTightAccuracy.value(), ctrlSettings->traceCosts,
                   stoppableThread);
  }
  return result;
}

AutorouteAttemptResult* RoutingBoard::fanout(Pin* pin, RouterSettings* routerSettings,
                                             int32_t ripupCosts, Stoppable* stoppableThread,
                                             TimeLimit* timeLimit) {
  if (pin->firstLayer() != pin->lastLayer() || pin->netCount() != 1) {
    return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::ALREADY_CONNECTED,
                                      "The pin '" + jrt::str(pin) + "' is already connected.");
  }
  if (pin->hasIgnoredNets()) {
    return new (jrt::kOp) AutorouteAttemptResult(
        AutorouteAttemptState::SKIPPED,
        "The pin '" + jrt::str(pin) + "' belongs to an ignored net class.");
  }
  int32_t pinNetNo = pin->getNetNumber(0);
  int32_t pinLayer = pin->firstLayer();
  jrt::Set<Item*>* pinConnectedSet = pin->getConnectedSet(pinNetNo);
  for (Item* currentItem : *pinConnectedSet) {
    if (currentItem->firstLayer() != pinLayer || currentItem->lastLayer() != pinLayer) {
      return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::ALREADY_CONNECTED,
                                        "The pin '" + jrt::str(pin) + "' is already connected.");
    }
  }
  jrt::Set<Item*>* unconnectedSet = pin->getUnconnectedSet(pinNetNo);
  if (unconnectedSet->isEmpty()) {
    return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::NO_UNCONNECTED_NETS,
                                      "The pin '" + jrt::str(pin) + "' is already connected.");
  }
  FloatPoint* pinCenter = pin->getCenter()->toFloat();
  jrt::List<Item*>* sortedUnconnectedList = new (jrt::kOp) jrt::ArrayList<Item*>(unconnectedSet);
  sortedUnconnectedList->sort([pinCenter](Item* item1, Item* item2) -> int32_t {
    IntBox* box1 = item1->boundingBox();
    double cx1 = (box1->ll->x + box1->ur->x) / 2.0;
    double cy1 = (box1->ll->y + box1->ur->y) / 2.0;
    double dx1 = cx1 - pinCenter->x;
    double dy1 = cy1 - pinCenter->y;
    double distSq1 = dx1 * dx1 + dy1 * dy1;

    IntBox* box2 = item2->boundingBox();
    double cx2 = (box2->ll->x + box2->ur->x) / 2.0;
    double cy2 = (box2->ll->y + box2->ur->y) / 2.0;
    double dx2 = cx2 - pinCenter->x;
    double dy2 = cy2 - pinCenter->y;
    double distSq2 = dx2 * dx2 + dy2 * dy2;

    return jrt::Double::compare(distSq1, distSq2);
  });

  // A pin of a plane net escapes into its plane: the plane is the first target, and the via that
  // drops into it is priced as a plane via, not as a signal layer change.
  Net* pinNet = this->rules->nets->get(pinNetNo);
  bool planeNet = pinNet != nullptr && pinNet->containsPlane();
  if (planeNet) {
    sortedUnconnectedList->sort([this](Item* item1, Item* item2) -> int32_t {
      return jrt::Boolean::compare(isPlaneOnPowerLayer(item2), isPlaneOnPowerLayer(item1));
    });
  }
  bool routedFirst = pinNet != nullptr && pinNet->isRoutedFirst();
  int32_t fanoutViaCosts =
      planeNet ? routerSettings->getPlaneViaCosts()
               : (routedFirst ? routerSettings->getPriorityViaCosts()
                              : routerSettings->getViaCosts());
  AutorouteControl* ctrlSettings = new (jrt::kOp) AutorouteControl(this, pinNetNo, routerSettings,
                                                        fanoutViaCosts,
                                                        routerSettings->getTraceCosts());
  ctrlSettings->isFanout = true;
  if (routerSettings->fanout != nullptr &&
      routerSettings->fanout->fallbackToBoardVias.value_or(false) &&
      ctrlSettings->viaRule != nullptr) {
    NetClass* pinNetClass = this->rules->nets->get(pinNetNo) != nullptr
                                ? this->rules->nets->get(pinNetNo)->getNetClass()
                                : nullptr;
    std::optional<int32_t> netViaClearance =
        pinNetClass != nullptr
            ? std::optional<int32_t>(pinNetClass->defaultItemClearanceClasses->get(
                  DefaultItemClearanceClasses_ItemClass::VIA))
            : std::nullopt;
    ViaRule* boardDefaultViaRule =
        !this->rules->viaRules->isEmpty() ? this->rules->viaRules->get(0) : nullptr;
    ctrlSettings->viaRule =
        fanoutFallbackViaRule(ctrlSettings->viaRule, boardDefaultViaRule, netViaClearance,
                              this->rules);
    ctrlSettings->rebuildViaInfo(this, fanoutViaCosts, pinNetNo);
  }
  Component* pinComponent = this->components->get(pin->getComponentId());
  if (pinComponent != nullptr && pin->name().has_value()) {
    ctrlSettings->fanoutStartPinName = pinComponent->name + "-" + pin->name().value();
  } else {
    ctrlSettings->fanoutStartPinName = pin->toString();
  }
  ctrlSettings->fanoutStartPinCenter = pin->getCenter();
  ctrlSettings->fanoutStartPinLayer = pin->firstLayer();
  ctrlSettings->removeUnconnectedVias = false;
  if (ripupCosts >= 0) {
    ctrlSettings->ripupAllowed = true;
    ctrlSettings->ripupCosts = ripupCosts;
  }
  jrt::TreeSet<Item*>* rippedItemList = new (jrt::kOp) jrt::TreeSet<Item*>();
  AutorouteEngine* currentAutorouteEngine = initAutoroute(
      pinNetNo, ctrlSettings->traceClearanceClassIndex, stoppableThread, timeLimit, false);

  AutorouteAttemptResult* result = nullptr;
  // plane_as_priority: the pin first drops into its plane through a via of its own.
  if (planeNet && routerSettings->autorouter != nullptr &&
      routerSettings->autorouter->planeAsPriority.value_or(false)) {
    jrt::Set<Item*>* planes = new (jrt::kOp) jrt::HashSet<Item*>();
    for (Item* target : *unconnectedSet) {
      if (isPlaneOnPowerLayer(target)) {
        planes->add(target);
      }
    }
    if (!planes->isEmpty()) {
      jrt::Set<Item*>* obstacles = planeEscapeObstacles(pin, pinNetNo, pinConnectedSet, planes);
      obstacles->forEach([](Item* obstacle) { obstacle->setPlaneEscapeObstacle(true); });
      try {
        result = currentAutorouteEngine->autorouteConnection(pinConnectedSet, planes, ctrlSettings,
                                                             rippedItemList, nullptr);
      } catch (...) {
        // Java finally block
        obstacles->forEach([](Item* obstacle) { obstacle->setPlaneEscapeObstacle(false); });
        throw;
      }
      obstacles->forEach([](Item* obstacle) { obstacle->setPlaneEscapeObstacle(false); });
      if (result->state == AutorouteAttemptState::ROUTED) {
        optChangedArea(jrt::Array<int32_t>::of({pinNetNo}), nullptr,
                       routerSettings->tracePullTightAccuracy.value(), ctrlSettings->traceCosts,
                       stoppableThread);
        return result;
      }
      if (result->state == AutorouteAttemptState::ALREADY_CONNECTED) {
        return result;
      }
      result = nullptr;
      // rooms and drill pages of the search above saw the obstacles
      currentAutorouteEngine = initAutoroute(pinNetNo, ctrlSettings->traceClearanceClassIndex,
                                             stoppableThread, timeLimit, false);
    }
  }
  if (sortedUnconnectedList->size() <= 4) {
    if (!sortedUnconnectedList->isEmpty()) {
      // 1. Try to route to the closest target first
      Item* closestTarget = sortedUnconnectedList->get(0);
      result = currentAutorouteEngine->autorouteConnection(
          pinConnectedSet, new (jrt::kOp) jrt::HashSet<Item*>({closestTarget}), ctrlSettings,
          rippedItemList, nullptr);  // null: costs not needed here

      // 2. If that fails and we have other targets, fall back to searching the entire
      // unconnected set at once
      if (result->state != AutorouteAttemptState::ROUTED &&
          result->state != AutorouteAttemptState::ALREADY_CONNECTED &&
          sortedUnconnectedList->size() > 1) {
        result = currentAutorouteEngine->autorouteConnection(pinConnectedSet, unconnectedSet,
                                                             ctrlSettings, rippedItemList, nullptr);
      }
    }
  } else {
    // For large nets (e.g. power/ground/buses), route to the entire unconnected set at once to
    // avoid CPU thrashing
    result = currentAutorouteEngine->autorouteConnection(pinConnectedSet, unconnectedSet,
                                                         ctrlSettings, rippedItemList, nullptr);
  }
  if (result != nullptr && result->state == AutorouteAttemptState::FAILED &&
      routerSettings->fanout != nullptr &&
      routerSettings->fanout->retryWithoutEscapeWindow.value_or(false)) {
    ctrlSettings->ignoreEscapeWindow = true;
    result = currentAutorouteEngine->autorouteConnection(pinConnectedSet, unconnectedSet,
                                                         ctrlSettings, rippedItemList, nullptr);
  }
  if (result == nullptr) {
    result = new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::FAILED,
                                        "No target items to route connection.");
  }

  if (result->state == AutorouteAttemptState::ROUTED) {
    optChangedArea(jrt::Array<int32_t>::of({pinNetNo}), nullptr,
                   routerSettings->tracePullTightAccuracy.value(), ctrlSettings->traceCosts,
                   stoppableThread);
  }
  return result;
}

jrt::Set<Item*>* RoutingBoard::planeEscapeObstacles(Pin* pin, int32_t netNo,
                                                    jrt::Set<Item*>* connectedSet,
                                                    jrt::Set<Item*>* destinations) {
  jrt::Set<Item*>* open = new (jrt::kOp) jrt::HashSet<Item*>(connectedSet);
  open->addAll(destinations);
  for (Item* destination : *destinations) {
    if (auto* sharedPin = jrt::fast_cast<Pin>(destination)) {
      open->addAll(sharedPin->getConnectedSet(netNo, true));
    }
  }
  for (Pin* other : *getPins()) {
    if (other->getComponentId() == pin->getComponentId() && other->containsNet(netNo)) {
      open->addAll(other->getConnectedSet(netNo, true));
    }
  }
  jrt::Set<Item*>* obstacles = new (jrt::kOp) jrt::HashSet<Item*>();
  for (Item* other : *getItems()) {
    if (other->containsNet(netNo) && jrt::fast_cast<ConductionArea>(other) == nullptr &&
        !open->contains(other)) {
      obstacles->add(other);
    }
  }
  return obstacles;
}

bool RoutingBoard::isPlaneOnPowerLayer(Item* item) {
  return jrt::fast_cast<ConductionArea>(item) != nullptr &&
         !(*this->layerStructure->layers)[item->firstLayer()]->isSignal;
}

ViaRule* RoutingBoard::fanoutFallbackViaRule(ViaRule* netRule, ViaRule* boardRule,
                                             std::optional<int32_t> netViaClearance,
                                             BoardRules* rules) {
  ViaRule* combined = new (jrt::kBoard) ViaRule(netRule->name + "_fallback");
  for (int32_t i = 0; i < netRule->viaCount(); i++) {
    combined->appendVia(netRule->getVia(i));
  }
  if (boardRule == nullptr) {
    return combined;
  }
  for (int32_t i = 0; i < boardRule->viaCount(); i++) {
    ViaInfo* boardVia = boardRule->getVia(i);
    if (netViaClearance.has_value() && *netViaClearance != boardVia->getClearanceClassIndex()) {
      bool present = false;
      for (int32_t j = 0; j < combined->viaCount(); j++) {
        ViaInfo* existing = combined->getVia(j);
        if (existing->getPadstack() == boardVia->getPadstack() &&
            existing->getClearanceClassIndex() == *netViaClearance) {
          present = true;
          break;
        }
      }
      if (!present) {
        combined->appendVia(new (jrt::kBoard) ViaInfo(boardVia->getName(), boardVia->getPadstack(),
                                        *netViaClearance, boardVia->attachSmdAllowed(), rules));
      }
    } else if (!combined->contains(boardVia)) {
      combined->appendVia(boardVia);
    }
  }
  return combined;
}

bool RoutingBoard::connectToTrace(IntPoint* fromPoint, Trace* toTrace, int32_t penHalfWidth,
                                  int32_t clearanceClassIndex) {
  auto* polylineTrace = jrt::fast_cast<PolylineTrace>(toTrace);
  if (polylineTrace == nullptr) {
    return false;  // not yet implemented
  }

  if (polylineTrace->polyline()->contains(fromPoint)) {
    // no connection line necessary
    return true;
  }
  LineSegment* projectionLine = polylineTrace->polyline()->projectionLine(fromPoint);
  if (projectionLine == nullptr) {
    return false;
  }
  Polyline* connectionLine = projectionLine->toPolyline();
  if (connectionLine == nullptr || connectionLine->lines->length != 3) {
    return false;
  }
  int32_t traceLayer = toTrace->getLayer();
  if (!this->checkPolylineTrace(connectionLine, traceLayer, penHalfWidth, toTrace->netNumbers,
                                clearanceClassIndex)) {
    return false;
  }
  if (this->changedArea != nullptr) {
    for (int32_t i = 0; i < connectionLine->cornerCount(); i++) {
      this->changedArea->join(connectionLine->cornerApprox(i), traceLayer);
    }
  }

  this->insertTrace(connectionLine, traceLayer, penHalfWidth, toTrace->netNumbers,
                    clearanceClassIndex, FixedState::UNFIXED);

  Point* firstCorner = toTrace->firstCorner();
  Point* lastCorner = toTrace->lastCorner();
  jrt::Array<int32_t>* netNumbers = toTrace->netNumbers;
  if (!fromPoint->equals(firstCorner)) {
    Trace* tail = this->getTraceTail(firstCorner, traceLayer, netNumbers);
    if (tail != nullptr && !tail->isUserFixed()) {
      this->removeItem(tail);
    }
  }
  if (!fromPoint->equals(lastCorner)) {
    Trace* tail = this->getTraceTail(lastCorner, traceLayer, netNumbers);
    if (tail != nullptr && !tail->isUserFixed()) {
      this->removeItem(tail);
    }
  }
  return true;
}

bool RoutingBoard::containsTraceTails(jrt::Collection<Item*>* items,
                                      jrt::Array<int32_t>* exceptNetNoArr) {
  for (Item* currentObject : *items) {
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentObject)) {
      if (!currentTrace->netsEqual(exceptNetNoArr)) {
        if (currentTrace->isTail()) {
          return true;
        }
      }
    }
  }
  return false;
}

bool RoutingBoard::removeTraceTails(int32_t netNumber,
                                    Item_StopConnectionOption stopConnectionOption) {
  jrt::TreeSet<Item*>* stubSet = new (jrt::kOp) jrt::TreeSet<Item*>();
  jrt::Collection<Item*>* boardItems = this->getItems();
  for (Item* currentItem : *boardItems) {
    if (!currentItem->isRoutable()) {
      continue;
    }
    if (currentItem->netCount() != 1) {
      continue;
    }
    if (netNumber > 0 && currentItem->getNetNumber(0) != netNumber) {
      continue;
    }
    if (currentItem->isTail()) {
      // A dangling endpoint is a DRC finding, not permission to delete an entire junction.
      if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
        if (trace->hasInteriorContacts()) {
          continue;
        }
      }
      if (jrt::fast_cast<Via>(currentItem) != nullptr) {
        if (stopConnectionOption == Item_StopConnectionOption::VIA) {
          continue;
        }
        if (stopConnectionOption == Item_StopConnectionOption::FANOUT_VIA) {
          if (currentItem->isFanoutVia(nullptr)) {
            continue;
          }
        }
      }
      stubSet->add(currentItem);
    }
  }
  jrt::TreeSet<Item*>* stubConnections = new (jrt::kOp) jrt::TreeSet<Item*>();
  for (Item* currentItem : *stubSet) {
    int32_t itemContactCount = currentItem->getNormalContacts()->size();
    if (itemContactCount == 1) {
      stubConnections->addAll(currentItem->getConnectionItems(stopConnectionOption));
    } else {
      // the connected items are no stubs for example if a via is only connected on 1 layer,
      // but to several traces.
      stubConnections->add(currentItem);
    }
  }
  if (stubConnections->isEmpty()) {
    return false;
  }
  this->removeItems(stubConnections);
  this->combineTraces(netNumber);
  return true;
}

void RoutingBoard::clearAllItemTemporaryAutorouteData() {
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = this->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    currentItem->clearAutorouteInfo();
  }
}

void RoutingBoard::changePlaneAsObstacle(bool value) {
  bool targetIgnore = !value;
  bool somethingChanged = false;
  // Change the isObstacle property of all conduction areas of the board.
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentConductionArea = jrt::fast_cast<ConductionArea>(currentItem)) {
      Layer* currentLayer = (*layerStructure->layers)[currentConductionArea->getLayer()];
      if (currentLayer->isSignal && currentConductionArea->getIsObstacle() != value) {
        currentConductionArea->setIsObstacle(value);
        somethingChanged = true;
      }
    }
  }
  if (this->rules->getIgnoreConduction() != targetIgnore) {
    this->rules->setIgnoreConduction(targetIgnore);
    somethingChanged = true;
  }
  if (somethingChanged) {
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* clearIt = itemList->startReadObject();
    for (;;) {
      Item* currentItem = jrt::fast_cast<Item>(itemList->readObject(clearIt));
      if (currentItem == nullptr) {
        break;
      }
      if (auto* ca = jrt::fast_cast<ConductionArea>(currentItem)) {
        ca->clearDerivedData();
      }
    }
    this->searchTreeManager->reinsertTreeItems();
  }
}

void RoutingBoard::changeConductionIsObstacle(bool value) { changePlaneAsObstacle(value); }

bool RoutingBoard::reduceNetsOfRouteItems() {
  bool result = false;
  bool somethingChanged = true;
  while (somethingChanged) {
    somethingChanged = false;
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = itemList->startReadObject();
    for (;;) {
      UndoableObjects_Storable* currentObject = itemList->readObject(it);
      if (currentObject == nullptr) {
        break;
      }
      Item* currentItem = jrt::fast_cast<Item>(currentObject);
      if (currentItem->netNumbers->length <= 1 ||
          FixedState_isFix(currentItem->getFixedState())) {
        continue;
      }
      if (jrt::fast_cast<Via>(currentObject) != nullptr) {
        jrt::Collection<Item*>* contacts = currentItem->getNormalContacts();
        // Java iterates the array captured at loop start (removeFromNet replaces the field).
        jrt::Array<int32_t>* netNumbersAtStart = currentItem->netNumbers;
        for (int32_t currentNetNumber : *netNumbersAtStart) {
          for (Item* currentContact : *contacts) {
            if (!currentContact->containsNet(currentNetNumber)) {
              currentItem->removeFromNet(currentNetNumber);
              somethingChanged = true;
              break;
            }
          }
          if (somethingChanged) {
            break;
          }
        }

      } else if (auto* currentTrace = jrt::fast_cast<Trace>(currentObject)) {
        jrt::Collection<Item*>* contacts = currentTrace->getStartContacts();
        for (int32_t i = 0; i < 2; i++) {
          jrt::Array<int32_t>* netNumbersAtStart = currentItem->netNumbers;
          for (int32_t currentNetNumber : *netNumbersAtStart) {
            bool pinFound = false;
            for (Item* currentContact : *contacts) {
              if (jrt::fast_cast<Pin>(currentContact) != nullptr) {
                pinFound = true;
                if (!currentContact->containsNet(currentNetNumber)) {
                  currentItem->removeFromNet(currentNetNumber);
                  somethingChanged = true;
                  break;
                }
              }
            }
            if (!pinFound) {  // at tie pins traces may have different nets
              for (Item* currentContact : *contacts) {
                if (jrt::fast_cast<Pin>(currentContact) == nullptr &&
                    !currentContact->containsNet(currentNetNumber)) {
                  currentItem->removeFromNet(currentNetNumber);
                  somethingChanged = true;
                  break;
                }
              }
            }
          }
          if (somethingChanged) {
            break;
          }
          contacts = currentTrace->getEndContacts();
        }
        if (somethingChanged) {
          break;
        }
      }
      if (somethingChanged) {
        result = true;
        break;
      }
    }
  }
  return result;
}

int32_t RoutingBoard::connectNetlessPadsToTheirFootprintsNet(jrt::Array<int32_t>* tiePadsOut) {
  int32_t assigned = 0;
  int32_t tiePads = 0;
  for (Pin* pin : *new (jrt::kOp) jrt::ArrayList<Pin*>(getPins())) {
    if (pin->netCount() != 0) {
      continue;
    }
    jrt::TreeSet<int32_t>* nets = new (jrt::kOp) jrt::TreeSet<int32_t>();
    bool ownFootprint = true;
    for (Pin* other : *pin->overlappingNettedPins()) {
      ownFootprint &= (other->getComponentId() == pin->getComponentId());
      for (int32_t i = 0; i < other->netCount(); i++) {
        nets->add(other->getNetNumber(i));
      }
    }
    if (nets->isEmpty()) {
      continue;
    }
    if (nets->size() == 1) {
      pin->assignNetNo(nets->iterator()->next());
    } else if (ownFootprint) {
      jrt::Array<int32_t>* netArr = nets->toArray();
      pin->assignNetNumbers(netArr);
      tiePads++;
    } else {
      continue;
    }
    assigned++;
  }
  if (tiePadsOut != nullptr && tiePadsOut->length > 0) {
    (*tiePadsOut)[0] = tiePads;
  }
  return assigned;
}

int32_t RoutingBoard::connectNetlessPadsToTheirFootprintsNet() {
  return connectNetlessPadsToTheirFootprintsNet(new (jrt::kOp) jrt::Array<int32_t>(1));
}

int32_t RoutingBoard::connectNetlessPads(bool kicadBoard, bool fixKicadFootprints,
                                         jrt::Array<int32_t>* tiePadsOut) {
  if (!kicadBoard) {
    return connectNetlessPadsToTheirFootprintsNet(tiePadsOut);
  }
  if (tiePadsOut != nullptr && tiePadsOut->length > 0) {
    (*tiePadsOut)[0] = 0;
  }
  if (!fixKicadFootprints) {
    return 0;
  }
  jrt::List<Pin*>* netless = new (jrt::kOp) jrt::ArrayList<Pin*>();
  for (Pin* p : *getPins()) {
    if (p->netCount() == 0) {
      netless->add(p);
    }
  }
  jrt::Map<Pin*, int32_t>* index = new (jrt::kOp) jrt::IdentityHashMap<Pin*, int32_t>();
  for (int32_t i = 0; i < netless->size(); i++) {
    index->put(netless->get(i), i);
  }
  jrt::Array<int32_t>* parent = new (jrt::kOp) jrt::Array<int32_t>(netless->size());
  for (int32_t i = 0; i < parent->length; i++) {
    (*parent)[i] = i;
  }
  for (int32_t i = 0; i < netless->size(); i++) {
    for (Pin* other : *netless->get(i)->copperOverlappingPins(
             [](Pin* o) { return o->netCount() == 0; })) {
      std::optional<int32_t> otherIdx = index->getOpt(other);
      if (otherIdx.has_value()) {
        int32_t rootI = findRoot(parent, i);
        int32_t rootOther = findRoot(parent, *otherIdx);
        (*parent)[rootI] = rootOther;
      }
    }
  }
  jrt::Map<int32_t, jrt::List<Pin*>*>* groups = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Pin*>*>();
  for (int32_t i = 0; i < netless->size(); i++) {
    int32_t root = findRoot(parent, i);
    groups
        ->computeIfAbsent(root,
                          [](int32_t) -> jrt::List<Pin*>* { return new (jrt::kOp) jrt::ArrayList<Pin*>(); })
        ->add(netless->get(i));
  }
  jrt::List<RoutingBoard_PinNetAssignment*>* assignments =
      new (jrt::kOp) jrt::ArrayList<RoutingBoard_PinNetAssignment*>();
  for (jrt::List<Pin*>* members : *groups->values()) {
    int32_t component = members->getFirst()->getComponentId();
    bool mismatchComponent = false;
    for (Pin* m : *members) {
      if (m->getComponentId() != component) {
        mismatchComponent = true;
        break;
      }
    }
    if (mismatchComponent) {
      continue;
    }
    jrt::List<Pin*>* netted = new (jrt::kOp) jrt::ArrayList<Pin*>();
    for (Pin* m : *members) {
      netted->addAll(m->copperOverlappingPins([](Pin* o) { return o->netCount() != 0; }));
    }
    if (netted->isEmpty()) {
      continue;
    }
    bool valid = true;
    int32_t firstNet = netted->getFirst()->getNetNumber(0);
    for (Pin* o : *netted) {
      if (o->getComponentId() != component || o->netCount() != 1 ||
          o->getNetNumber(0) != firstNet) {
        valid = false;
        break;
      }
    }
    if (valid) {
      for (Pin* m : *members) {
        assignments->add(new (jrt::kBoard) RoutingBoard_PinNetAssignment(m, firstNet));
      }
    }
  }
  for (RoutingBoard_PinNetAssignment* a : *assignments) {
    a->pin()->assignNetNo(a->net());
  }
  return assignments->size();
}

int32_t RoutingBoard::findRoot(jrt::Array<int32_t>* parent, int32_t i) {
  if ((*parent)[i] == i) {
    return i;
  }
  return (*parent)[i] = findRoot(parent, (*parent)[i]);
}

int32_t RoutingBoard::clusterNetTies() {
  // 1. Group pins by component ID (excluding board pins with componentId <= 0)
  jrt::Map<int32_t, jrt::List<Pin*>*>* pinsByComponent =
      new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Pin*>*>();
  for (Pin* pin : *getPins()) {
    if (pin->getComponentId() > 0) {
      pinsByComponent
          ->computeIfAbsent(pin->getComponentId(),
                            [](int32_t) -> jrt::List<Pin*>* { return new (jrt::kOp) jrt::ArrayList<Pin*>(); })
          ->add(pin);
    }
  }

  int32_t nextClusterId = 1;
  int32_t multiPinClusters = 0;

  for (jrt::List<Pin*>* compPins : *pinsByComponent->values()) {
    int32_t count = compPins->size();
    if (count <= 1) {
      continue;
    }

    jrt::Array<int32_t>* parent = new (jrt::kOp) jrt::Array<int32_t>(count);
    for (int32_t i = 0; i < count; i++) {
      (*parent)[i] = i;
    }

    // Check pairwise physical copper overlap between pins of the same component
    for (int32_t i = 0; i < count; i++) {
      Pin* p1 = compPins->get(i);
      IntBox* b1 = p1->boundingBox();
      for (int32_t j = i + 1; j < count; j++) {
        Pin* p2 = compPins->get(j);
        if (!p1->sharesLayer(p2) || !b1->intersects(p2->boundingBox())) {
          continue;
        }
        if (pinsOverlapCopper(p1, p2)) {
          int32_t rootI = findRoot(parent, i);
          int32_t rootJ = findRoot(parent, j);
          if (rootI != rootJ) {
            (*parent)[rootI] = rootJ;
          }
        }
      }
    }

    // Group pin indices by their root representative
    jrt::Map<int32_t, jrt::List<Pin*>*>* groups = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Pin*>*>();
    for (int32_t i = 0; i < count; i++) {
      int32_t root = findRoot(parent, i);
      groups
          ->computeIfAbsent(root,
                            [](int32_t) -> jrt::List<Pin*>* { return new (jrt::kOp) jrt::ArrayList<Pin*>(); })
          ->add(compPins->get(i));
    }

    for (jrt::List<Pin*>* cluster : *groups->values()) {
      if (cluster->size() > 1) {
        int32_t clusterId = nextClusterId++;
        for (Pin* pin : *cluster) {
          pin->setNetTieClusterId(clusterId);
        }
        multiPinClusters++;
      }
    }
  }

  return multiPinClusters;
}

bool RoutingBoard::pinsOverlapCopper(Pin* p1, Pin* p2) {
  for (int32_t i = 0; i < p1->tileShapeCount(); i++) {
    Shape* s1 = p1->getShape(i);
    if (s1 == nullptr) {
      continue;
    }
    int32_t layer1 = p1->shapeLayer(i);
    for (int32_t j = 0; j < p2->tileShapeCount(); j++) {
      if (layer1 == p2->shapeLayer(j)) {
        Shape* s2 = p2->getShape(j);
        if (s2 != nullptr && s1->intersects(s2)) {
          return true;
        }
      }
    }
  }
  return false;
}

jrt::List<Item*>* RoutingBoard::getShoveCacheObstacles() {
  if (shoveCacheObstacles == nullptr) {
    shoveCacheObstacles = new (jrt::kBoard) jrt::ArrayList<Item*>();
  }
  return shoveCacheObstacles;
}

ShoveCheckCache* RoutingBoard::getShoveChecks() { return ShoveCheckCache::forThread(); }

bool RoutingBoard::undo(jrt::Set<int32_t>* changedNets) { return getUndoFacade()->undo(changedNets); }

bool RoutingBoard::redo(jrt::Set<int32_t>* changedNets) { return getUndoFacade()->redo(changedNets); }

BoardStatistics* RoutingBoard::getStatistics() { return new (jrt::kOp) BoardStatistics(this); }

RoutingBoard* RoutingBoard::deepCopy() { return getUndoFacade()->deepCopy(); }

BasicBoard* RoutingBoard::copyAsDeserialized(jrt::IdentityHashMap<Item*, Item*>* itemMap) {
  RoutingBoard* copy = new (jrt::kBoard) RoutingBoard(BasicBoard_CopyTag{}, this);
  if (itemMap == nullptr) itemMap = new (jrt::kOp) jrt::IdentityHashMap<Item*, Item*>();
  copy->copyItemListFrom(this, itemMap);
  // PORT NOTE: Java serialization makes the failure log's records point at the copied items;
  // re-copy it now that the old->new item map exists.
  if (this->failureLog != nullptr) copy->failureLog = this->failureLog->deepCopy(itemMap);
  copy->afterDeserialize();
  return copy;
}

RoutingBoardOperations* RoutingBoard::getOperations() {
  if (operations == nullptr) {
    operations = new (jrt::kBoard) RoutingBoardOperations(this);
  }
  return operations;
}

RoutingBoardSearchFacade* RoutingBoard::getSearchFacade() {
  if (searchFacade == nullptr) {
    searchFacade = new (jrt::kBoard) RoutingBoardSearchFacade(this);
  }
  return searchFacade;
}

RoutingBoardUndoFacade* RoutingBoard::getUndoFacade() {
  if (undoFacade == nullptr) {
    undoFacade = new (jrt::kBoard) RoutingBoardUndoFacade(this);
  }
  return undoFacade;
}

}  // namespace fr
