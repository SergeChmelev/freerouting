#include "board/optimize/TraceShover.h"

#include "board/actions/DrillItemMover.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/ShapeAndEntrySide.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/optimize/ShoveCheckCache.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/searchtree/ShapeTraceEntries.h"
#include "board/state/ChangedArea.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/IdGenerator.h"
#include "datastructures/TimeLimit.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr {

double TraceShover::check(RoutingBoard* board, LineSegment* lineSegment, bool shoveToTheLeft,
                          int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                          int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                          int32_t maxViaRecursionDepth) {
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  if (searchTree->isClearanceCompensationUsed()) {
    traceHalfWidth += searchTree->clearanceCompensationValue(clearanceClassIndex, layer);
  }
  jrt::Array<TileShape*>* traceShapes = lineSegment->toPolyline()->offsetShapes(traceHalfWidth);
  if (traceShapes->length != 1) {
    FRLogger::warn("TraceShover.check: traceShape count 1 expected");
    return 0;
  }

  TileShape* traceShape = (*traceShapes)[0];
  if (traceShape->isEmpty()) {
    FRLogger::warn("TraceShover.check: traceShape is empty");
    return 0;
  }
  if (!traceShape->isContainedIn(board->getBoundingBox())) {
    return 0;
  }
  ShapeEntrySide* fromSide = new ShapeEntrySide(lineSegment, traceShape, shoveToTheLeft);
  ShapeTraceEntries* shapeEntries = new ShapeTraceEntries(traceShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      traceShape, layer, new jrt::Array<int32_t>(0), clearanceClassIndex);
  bool obstaclesShovable = shapeEntries->storeItems(obstacles, false, true);
  if (!obstaclesShovable || shapeEntries->traceTailsInShape()) {
    return 0;
  }
  int32_t tracePieceCount = shapeEntries->substituteTraceCount();

  if (shapeEntries->stackDepth() > 1) {
    return 0;
  }

  FloatPoint* startCornerAppprox = lineSegment->startPointApprox();
  FloatPoint* endCornerAppprox = lineSegment->endPointApprox();
  double segmentLength = endCornerAppprox->distance(startCornerAppprox);

  ClearanceMatrix* clMatrix = board->rules->clearanceMatrix;

  double result = jrt::Integer::MAX_VALUE;

  // check, if the obstacle vias can be shoved

  for (Via* currentShoveVia : *shapeEntries->shoveViaList) {
    if (currentShoveVia->sharesNetNo(netNumbers)) {
      continue;
    }
    bool shoveViaOk = false;
    if (maxViaRecursionDepth > 0) {
      jrt::Array<IntPoint*>* newViaCenter = DrillItemMover::tryShoveViaPoints(
          traceShape, layer, currentShoveVia, clearanceClassIndex, false, board);

      if (newViaCenter->length == 0) {
        return 0;
      }
      Vector* delta = (*newViaCenter)[0]->differenceBy(currentShoveVia->getCenter());
      jrt::Collection<Item*>* ignoreItems = new jrt::LinkedList<Item*>();
      shoveViaOk = DrillItemMover::check(currentShoveVia, delta, maxRecursionDepth,
                                         maxViaRecursionDepth - 1, ignoreItems, board, nullptr);
    }

    if (!shoveViaOk) {
      FloatPoint* viaCenterAppprox = currentShoveVia->getCenter()->toFloat();
      double projection = startCornerAppprox->scalarProduct(endCornerAppprox, viaCenterAppprox);
      projection /= segmentLength;
      IntBox* viaBox = currentShoveVia->getTreeShapeOnLayer(searchTree, layer)->boundingBox();
      double viaRadius = 0.5 * viaBox->maxWidth();
      double currentOkLength = projection - viaRadius - traceHalfWidth;
      currentOkLength -= searchTree->additionalClearance(
          clearanceClassIndex, currentShoveVia->clearanceClassIndex(), layer);
      if (currentOkLength <= 0) {
        return 0;
      }
      result = jrt::Math::min(result, currentOkLength);
    }
  }
  if (tracePieceCount == 0) {
    return result;
  }
  if (maxRecursionDepth <= 0) {
    return 0;
  }

  Direction* lineDirection = lineSegment->getLine()->direction();
  for (;;) {
    PolylineTrace* currentSubstituteTrace = shapeEntries->nextSubstituteTracePiece();
    if (currentSubstituteTrace == nullptr) {
      break;
    }
    for (int32_t i = 0; i < currentSubstituteTrace->tileShapeCount(); i++) {
      LineSegment* currentLineSegment = new LineSegment(currentSubstituteTrace->polyline(), i + 1);
      if (shoveToTheLeft) {
        // swap the line segment to get the correct shove length
        // in case it is smaller than the length of the whole line segment.
        currentLineSegment = currentLineSegment->opposite();
      }

      bool isInFront = currentLineSegment->getLine()->direction()->equals(lineDirection);
      if (isInFront) {
        double shoveOkLength =
            check(board, currentLineSegment, shoveToTheLeft, layer,
                  currentSubstituteTrace->netNumbers, currentSubstituteTrace->getHalfWidth(),
                  currentSubstituteTrace->clearanceClassIndex(), maxRecursionDepth - 1,
                  maxViaRecursionDepth);
        if (shoveOkLength < jrt::Integer::MAX_VALUE) {
          if (shoveOkLength <= 0) {
            return 0;
          }
          double projection = jrt::Math::min(
              startCornerAppprox->scalarProduct(endCornerAppprox,
                                                currentLineSegment->startPointApprox()),
              startCornerAppprox->scalarProduct(endCornerAppprox,
                                                currentLineSegment->endPointApprox()));
          projection /= segmentLength;
          double currentOkLength =
              shoveOkLength + projection - traceHalfWidth - currentSubstituteTrace->getHalfWidth();
          if (searchTree->isClearanceCompensationUsed()) {
            currentOkLength -=
                searchTree->clearanceCompensationValue(
                    currentSubstituteTrace->clearanceClassIndex(), layer) +
                searchTree->additionalClearance(
                    clearanceClassIndex, currentSubstituteTrace->clearanceClassIndex(), layer);
          } else {
            currentOkLength -= clMatrix->getValue(
                clearanceClassIndex, currentSubstituteTrace->clearanceClassIndex(), layer, true);
          }
          if (currentOkLength <= 0) {
            return 0;
          }
          result = jrt::Math::min(currentOkLength, result);
        }
        break;
      }
    }
  }
  return result;
}

bool TraceShover::check(TileShape* traceShape, ShapeEntrySide* fromSide, Direction* dir,
                        int32_t layer, jrt::Array<int32_t>* netNumbers,
                        int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                        int32_t maxViaRecursionDepth, int32_t maxSpringOverRecursionDepth,
                        TimeLimit* timeLimit) {
  ShoveCheckCache* cache = board->getShoveChecks();
  if (cache == nullptr || timeLimit != nullptr ||
      !ShoveCheckCache::supported(traceShape, dir, netNumbers) || traceShape->isEmpty()) {
    return checkUncached(traceShape, fromSide, dir, layer, netNumbers, clearanceClassIndex,
                         maxRecursionDepth, maxViaRecursionDepth, maxSpringOverRecursionDepth,
                         timeLimit);
  }
  int64_t stamp = board->itemList->getContentVersion();
  ShoveCheckCache_Key* key = new ShoveCheckCache_Key(
      traceShape, fromSide, dir, layer, netNumbers, clearanceClassIndex, maxRecursionDepth,
      maxViaRecursionDepth, maxSpringOverRecursionDepth, false);
  ShoveCheckCache_Entry* hit = cache->tryGet(board, stamp, key);
  if (hit != nullptr) {
    cache->countHit();
    for (int32_t i = 0; i < hit->ids; i++) {
      board->communication->idGenerator->newId();
    }
    if (hit->wroteObstacle) {
      board->setShoveFailingObstacle(
          hit->obstacle < 0 ? nullptr : board->getShoveCacheObstacles()->get(hit->obstacle));
    }
    if (hit->wroteLayer) {
      board->setShoveFailingLayer(hit->layer);
    }
    return hit->result;
  }
  cache->countMiss();
  return measuredCheck(cache, key, traceShape, fromSide, dir, layer, netNumbers,
                       clearanceClassIndex, maxRecursionDepth, maxViaRecursionDepth,
                       maxSpringOverRecursionDepth, stamp);
}

bool TraceShover::measuredCheck(ShoveCheckCache* cache, ShoveCheckCache_Key* key,
                                TileShape* traceShape, ShapeEntrySide* fromSide, Direction* dir,
                                int32_t layer, jrt::Array<int32_t>* netNumbers,
                                int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                int32_t maxViaRecursionDepth,
                                int32_t maxSpringOverRecursionDepth, int64_t stamp) {
  int32_t ids = board->communication->idGenerator->maxGeneratedId();
  int64_t obstacleWrites = board->getShoveFailingObstacleWrites();
  int64_t layerWrites = board->getShoveFailingLayerWrites();

  bool result =
      checkUncached(traceShape, fromSide, dir, layer, netNumbers, clearanceClassIndex,
                    maxRecursionDepth, maxViaRecursionDepth, maxSpringOverRecursionDepth, nullptr);

  int32_t idsUsed = board->communication->idGenerator->maxGeneratedId() - ids;
  bool wroteObstacle = board->getShoveFailingObstacleWrites() != obstacleWrites;
  Item* obstacle = wroteObstacle ? board->getShoveFailingObstacle() : nullptr;

  if (board->itemList->getContentVersion() != stamp || idsUsed < 0 ||
      (obstacle != nullptr && !obstacle->isOnTheBoard())) {
    return result;
  }

  bool wroteLayer = board->getShoveFailingLayerWrites() != layerWrites;
  int32_t obstacleIndex = (wroteObstacle && obstacle != nullptr)
                              ? ShoveCheckCache::obstacleIndex(board, obstacle)
                              : -1;
  ShoveCheckCache_Entry* entry =
      new ShoveCheckCache_Entry(result, idsUsed, wroteObstacle, obstacleIndex, wroteLayer,
                                wroteLayer ? board->getShoveFailingLayer() : -1);
  cache->put(board, stamp, key, entry);
  return result;
}

namespace {

/** The [shove_*_obstacles] trace log line of checkUncached and insert. */
std::string obstacleLogLine(const char* tag, jrt::Array<int32_t>* netNumbers,
                            TileShape* traceShape, int32_t tracePieceCount,
                            jrt::Collection<Item*>* obstacles) {
  std::string obstacleLog;
  obstacleLog += tag;
  obstacleLog += " net=";
  obstacleLog += jrt::str((*netNumbers)[0]);
  obstacleLog += ", shape_bb=";
  obstacleLog += jrt::str(traceShape->boundingBox());
  obstacleLog += ", tracePieceCount=";
  obstacleLog += jrt::str(tracePieceCount);
  obstacleLog += ", obstacles=[";
  bool first = true;
  for (Item* obs : *obstacles) {
    if (!first) {
      obstacleLog += ", ";
    }
    first = false;
    obstacleLog += "{id=";
    obstacleLog += jrt::str(obs->getId());
    obstacleLog += ",type=";
    obstacleLog += jrt::className(obs);
    if (auto* pt = jrt::fast_cast<PolylineTrace>(obs)) {
      obstacleLog += ",nets=";
      obstacleLog += jrt::Arrays::toString(pt->netNumbers);
      obstacleLog += ",fc=";
      obstacleLog += jrt::str(pt->firstCorner());
      obstacleLog += ",lc=";
      obstacleLog += jrt::str(pt->lastCorner());
    }
    obstacleLog += "}";
  }
  obstacleLog += "]";
  return obstacleLog;
}

}  // namespace

bool TraceShover::checkUncached(TileShape* traceShape, ShapeEntrySide* fromSide, Direction* dir,
                                int32_t layer, jrt::Array<int32_t>* netNumbers,
                                int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                int32_t maxViaRecursionDepth,
                                int32_t maxSpringOverRecursionDepth, TimeLimit* timeLimit) {
  if (timeLimit != nullptr && timeLimit->limitExceeded()) {
    return false;
  }

  if (traceShape->isEmpty()) {
    FRLogger::warn("ShoveTraceAux.check: traceShape is empty");
    return true;
  }
  if (!traceShape->isContainedIn(board->getBoundingBox())) {
    this->board->setShoveFailingObstacle(board->getOutline());
    return false;
  }
  ShapeTraceEntries* shapeEntries = new ShapeTraceEntries(traceShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      traceShape, layer, new jrt::Array<int32_t>(0), clearanceClassIndex);
  obstacles->removeAll(getIgnoreItemsAtTiePins(traceShape, layer, netNumbers));
  bool obstaclesShovable = shapeEntries->storeItems(obstacles, false, true);
  if (!obstaclesShovable) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  int32_t tracePieceCount = shapeEntries->substituteTraceCount();

  if (FRLogger::isTraceEnabled() && netNumbers != nullptr && netNumbers->length > 0 &&
      !obstacles->isEmpty()) {
    FRLogger::trace(obstacleLogLine(
        tracePieceCount > 0 ? "[shove_check_obstacles]" : "[shove_check_obstacles_zero]",
        netNumbers, traceShape, tracePieceCount, obstacles));
  }

  if (shapeEntries->stackDepth() > 1) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  double shapeRadius = 0.5 * traceShape->boundingBox()->minWidth();

  // check, if the obstacle vias can be shoved

  for (Via* currentShoveVia : *shapeEntries->shoveViaList) {
    if (currentShoveVia->sharesNetNo(netNumbers)) {
      continue;
    }
    if (maxViaRecursionDepth <= 0) {
      this->board->setShoveFailingObstacle(currentShoveVia);
      return false;
    }
    FloatPoint* currentShoveViaCenter = currentShoveVia->getCenter()->toFloat();
    jrt::Array<IntPoint*>* tryViaCenters = DrillItemMover::tryShoveViaPoints(
        traceShape, layer, currentShoveVia, clearanceClassIndex, true, board);

    double maxDist =
        0.5 * currentShoveVia->getShapeOnLayer(layer)->boundingBox()->maxWidth() + shapeRadius;
    double maxDistSquare = maxDist * maxDist;
    bool shoveViaOk = false;
    for (int32_t i = 0; i < tryViaCenters->length; i++) {
      if (i == 0 ||
          currentShoveViaCenter->distanceSquare((*tryViaCenters)[i]->toFloat()) <=
              maxDistSquare) {
        Vector* delta = (*tryViaCenters)[i]->differenceBy(currentShoveVia->getCenter());
        jrt::Collection<Item*>* ignoreItems = new jrt::LinkedList<Item*>();
        if (DrillItemMover::check(currentShoveVia, delta, maxRecursionDepth,
                                  maxViaRecursionDepth - 1, ignoreItems, this->board,
                                  timeLimit)) {
          shoveViaOk = true;
          break;
        }
      }
    }
    if (!shoveViaOk) {
      return false;
    }
  }

  if (tracePieceCount == 0) {
    return true;
  }
  if (maxRecursionDepth <= 0) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }

  bool isOrthogonalMode = jrt::fast_cast<IntBox>(traceShape) != nullptr;
  for (;;) {
    PolylineTrace* currentSubstituteTrace = shapeEntries->nextSubstituteTracePiece();
    if (currentSubstituteTrace == nullptr) {
      break;
    }
    if (maxSpringOverRecursionDepth > 0) {
      Polyline* newPolyline =
          springOver(currentSubstituteTrace->polyline(),
                     currentSubstituteTrace->getCompensatedHalfWidth(searchTree), layer,
                     currentSubstituteTrace->netNumbers,
                     currentSubstituteTrace->clearanceClassIndex(), false,
                     maxSpringOverRecursionDepth, nullptr);
      if (newPolyline == nullptr) {
        // spring_over did not work
        return false;
      }
      if (newPolyline != currentSubstituteTrace->polyline()) {
        // spring_over changed something
        --maxSpringOverRecursionDepth;
        currentSubstituteTrace->change(newPolyline);
      }
    }
    for (int32_t i = 0; i < currentSubstituteTrace->tileShapeCount(); i++) {
      Direction* currentDirection =
          (*currentSubstituteTrace->polyline()->lines)[i + 1]->direction();
      bool isInFront = dir == nullptr || dir->equals(currentDirection);
      if (isInFront) {
        ShapeAndEntrySide* current =
            new ShapeAndEntrySide(currentSubstituteTrace, i, isOrthogonalMode, true);
        if (!this->check(current->shape, current->fromSide, currentDirection, layer,
                         currentSubstituteTrace->netNumbers,
                         currentSubstituteTrace->clearanceClassIndex(), maxRecursionDepth - 1,
                         maxViaRecursionDepth, maxSpringOverRecursionDepth, timeLimit)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool TraceShover::insert(TileShape* traceShape, ShapeEntrySide* fromSide, int32_t layer,
                         jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                         jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                         int32_t maxViaRecursionDepth, int32_t maxSpringOverRecursionDepth) {
  if (traceShape->isEmpty()) {
    FRLogger::warn("ShoveTraceAux.insert: traceShape is empty");
    return true;
  }
  if (!traceShape->isContainedIn(board->getBoundingBox())) {
    this->board->setShoveFailingObstacle(board->getOutline());
    return false;
  }
  if (!DrillItemMover::shoveVias(traceShape, fromSide, layer, netNumbers, clearanceClassIndex,
                                 ignoreItems, maxRecursionDepth, maxViaRecursionDepth, true,
                                 this->board)) {
    return false;
  }
  ShapeTraceEntries* shapeEntries = new ShapeTraceEntries(traceShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      traceShape, layer, new jrt::Array<int32_t>(0), clearanceClassIndex);
  obstacles->removeAll(getIgnoreItemsAtTiePins(traceShape, layer, netNumbers));
  bool obstaclesShovable = shapeEntries->storeItems(obstacles, false, true);
  if (!shapeEntries->shoveViaList->isEmpty()) {
    obstaclesShovable = false;
    this->board->setShoveFailingObstacle(shapeEntries->shoveViaList->iterator()->next());
    return false;
  }
  if (!obstaclesShovable) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  int32_t tracePieceCount = shapeEntries->substituteTraceCount();
  if (FRLogger::isTraceEnabled() && netNumbers != nullptr && netNumbers->length > 0 &&
      !obstacles->isEmpty()) {
    FRLogger::trace(obstacleLogLine(
        tracePieceCount > 0 ? "[shove_insert_obstacles]" : "[shove_insert_obstacles_zero]",
        netNumbers, traceShape, tracePieceCount, obstacles));
  }
  if (tracePieceCount == 0) {
    return true;
  }
  if (maxRecursionDepth <= 0) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  bool tailsExistBefore = board->containsTraceTails(obstacles, netNumbers);
  shapeEntries->cutoutTraces(obstacles);
  bool isOrthogonalMode = jrt::fast_cast<IntBox>(traceShape) != nullptr;
  for (;;) {
    PolylineTrace* currentSubstituteTrace = shapeEntries->nextSubstituteTracePiece();
    if (currentSubstituteTrace == nullptr) {
      break;
    }
    if (currentSubstituteTrace->firstCorner()->equals(currentSubstituteTrace->lastCorner())) {
      continue;
    }
    if (maxSpringOverRecursionDepth > 0) {
      Polyline* newPolyline =
          springOver(currentSubstituteTrace->polyline(),
                     currentSubstituteTrace->getCompensatedHalfWidth(searchTree), layer,
                     currentSubstituteTrace->netNumbers,
                     currentSubstituteTrace->clearanceClassIndex(), false,
                     maxSpringOverRecursionDepth, nullptr);

      if (newPolyline == nullptr) {
        // spring_over did not work
        return false;
      }
      if (newPolyline != currentSubstituteTrace->polyline()) {
        // spring_over changed something
        --maxSpringOverRecursionDepth;
        currentSubstituteTrace->change(newPolyline);
      }
    }
    jrt::Array<int32_t>* currentNetNumbers = currentSubstituteTrace->netNumbers;
    for (int32_t i = 0; i < currentSubstituteTrace->tileShapeCount(); i++) {
      ShapeAndEntrySide* current =
          new ShapeAndEntrySide(currentSubstituteTrace, i, isOrthogonalMode, false);
      if (!this->insert(current->shape, current->fromSide, layer, currentNetNumbers,
                        currentSubstituteTrace->clearanceClassIndex(), ignoreItems,
                        maxRecursionDepth - 1, maxViaRecursionDepth,
                        maxSpringOverRecursionDepth)) {
        return false;
      }
    }
    for (int32_t i = 0; i < currentSubstituteTrace->cornerCount(); i++) {
      board->joinChangedArea(currentSubstituteTrace->polyline()->cornerApprox(i), layer);
    }
    jrt::Array<Point*>* endCorners = nullptr;
    if (!tailsExistBefore) {
      endCorners = new jrt::Array<Point*>(2);
      (*endCorners)[0] = currentSubstituteTrace->firstCorner();
      (*endCorners)[1] = currentSubstituteTrace->lastCorner();
    }
    board->insertItem(currentSubstituteTrace);

    try {
      currentSubstituteTrace->normalize(board->changedArea->getArea(layer));
    } catch (const jrt::Exception& e) {
      FRLogger::error("Couldn't normalize trace.", &e);
    }

    if (!tailsExistBefore) {
      for (int32_t i = 0; i < 2; i++) {
        Trace* tail = board->getTraceTail((*endCorners)[i], layer, currentNetNumbers);
        if (tail != nullptr) {
          board->removeItems(tail->getConnectionItems(Item_StopConnectionOption::VIA));
          for (int32_t currentNetNumber : *currentNetNumbers) {
            board->combineTraces(currentNetNumber);
          }
        }
      }
    }
  }
  return true;
}

jrt::Collection<Item*>* TraceShover::getIgnoreItemsAtTiePins(TileShape* traceShape, int32_t layer,
                                                             jrt::Array<int32_t>* netNumbers) {
  jrt::Collection<SearchTreeObject*>* overlaps = this->board->overlappingObjects(traceShape, layer);
  jrt::Set<Item*>* result = new jrt::TreeSet<Item*>();
  for (SearchTreeObject* currentObject : *overlaps) {
    if (auto* currentPin = jrt::fast_cast<Pin>(currentObject)) {
      if (currentPin->sharesNetNo(netNumbers)) {
        result->addAll(currentPin->getAllContacts(layer));
      }
    }
  }
  return result;
}

Polyline* TraceShover::springOver(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                  jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                  bool overConnectedPins, int32_t recursionDepth,
                                  jrt::Set<Pin*>* contactPins) {
  Item* foundObstacle = nullptr;
  IntBox* foundObstacleBoundingBox = nullptr;
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  jrt::Array<int32_t>* checkNetNoArr;
  if (contactPins == nullptr) {
    checkNetNoArr = netNumbers;
  } else {
    checkNetNoArr = new jrt::Array<int32_t>(0);
  }
  for (int32_t i = 0; i < polyline->lines->length - 2; i++) {
    TileShape* currentShape = polyline->offsetShape(halfWidth, i);
    jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
        currentShape, layer, checkNetNoArr, clearanceClassIndex);
    for (Item* currentItem : *obstacles) {
      bool isObstacle;
      if (currentItem->sharesNetNo(netNumbers)) {
        // to avoid acid traps
        isObstacle = jrt::fast_cast<Pin>(currentItem) != nullptr && contactPins != nullptr &&
                     !contactPins->contains(jrt::fast_cast<Pin>(currentItem));
      } else if (auto* area = jrt::fast_cast<ConductionArea>(currentItem)) {
        isObstacle = area->getIsObstacle();
      } else if (auto* outline = jrt::fast_cast<BoardOutline>(currentItem)) {
        isObstacle = outline->blocksTraceAt(currentShape, layer, netNumbers, clearanceClassIndex);
      } else if (jrt::fast_cast<ViaObstacleArea>(currentItem) != nullptr ||
                 jrt::fast_cast<ComponentObstacleArea>(currentItem) != nullptr) {
        isObstacle = false;
      } else if (jrt::fast_cast<PolylineTrace>(currentItem) != nullptr) {
        if (currentItem->isShoveFixed()) {
          isObstacle = true;
          // check for a shove fixed trace exit stub, which has to be ignored at a tie pin.
          jrt::Collection<Item*>* currentContacts = currentItem->getNormalContacts();
          for (Item* currentContact : *currentContacts) {
            if (currentContact->sharesNetNo(netNumbers)) {
              isObstacle = false;
            }
          }
        } else {
          // an unfixed trace can be pushed aside eventually
          isObstacle = false;
        }
      } else {
        // an unfixed via can be pushed aside eventually
        isObstacle = !currentItem->isRoutable();
      }

      if (isObstacle) {
        if (foundObstacle == nullptr) {
          foundObstacle = currentItem;
          foundObstacleBoundingBox = currentItem->boundingBox();
        } else if (foundObstacle != currentItem) {
          // check, if 1 obstacle is contained in the other obstacle and take
          // the bigger obstacle in this case.
          // That may happen in case of fixed vias inside of pins.
          IntBox* currentItemBoundingBox = currentItem->boundingBox();
          if (foundObstacleBoundingBox->intersects(currentItemBoundingBox)) {
            if (currentItemBoundingBox->contains(foundObstacleBoundingBox)) {
              foundObstacle = currentItem;
              foundObstacleBoundingBox = currentItemBoundingBox;
            } else if (!foundObstacleBoundingBox->contains(currentItemBoundingBox)) {
              return nullptr;
            }
          }
        }
      }
    }
    if (foundObstacle != nullptr) {
      break;
    }
  }
  if (foundObstacle == nullptr) {
    // no obstacle in the way, nothing to do
    return polyline;
  }

  auto* foundOutline = jrt::fast_cast<BoardOutline>(foundObstacle);
  if (recursionDepth <= 0 || (foundOutline != nullptr && foundOutline->blocksNets(netNumbers)) ||
      (jrt::fast_cast<Trace>(foundObstacle) != nullptr && !foundObstacle->isShoveFixed())) {
    this->board->setShoveFailingObstacle(foundObstacle);
    return nullptr;
  }
  bool trySpringOver = true;
  if (!overConnectedPins) {
    // Check if the obstacle has a trace contact on layer
    jrt::Collection<Item*>* contactsOnLayer = foundObstacle->getAllContacts(layer);
    for (Item* currentContact : *contactsOnLayer) {
      if (jrt::fast_cast<Trace>(currentContact) != nullptr) {
        trySpringOver = false;
        break;
      }
    }
  }
  ConvexShape* obstacleShape = nullptr;
  if (trySpringOver) {
    if (jrt::fast_cast<ObstacleArea>(foundObstacle) != nullptr ||
        jrt::fast_cast<Trace>(foundObstacle) != nullptr) {
      if (foundObstacle->treeShapeCount(searchTree) == 1) {
        obstacleShape = foundObstacle->getTreeShape(searchTree, 0);
      } else if (auto* area = jrt::fast_cast<ObstacleArea>(foundObstacle)) {
        // Search-tree sections are an indexing detail, not necessarily a non-convex obstacle.
        // Recover the whole convex area, with the same compensation applied before partitioning
        // in ShapeSearchTree.calculateTreeShapes. Do not bridge holes or concave areas.
        jrt::Array<TileShape*>* convexShapes = area->splitToConvex();
        if (convexShapes != nullptr && convexShapes->length == 1) {
          obstacleShape = jrt::fast_cast<ConvexShape>((*convexShapes)[0]->enlarge(
              searchTree->clearanceCompensationValue(area->clearanceClassIndex(), layer)));
        } else {
          trySpringOver = false;
        }
      } else {
        trySpringOver = false;
      }
    } else if (auto* foundDrillItem = jrt::fast_cast<DrillItem>(foundObstacle)) {
      obstacleShape = foundDrillItem->getTreeShapeOnLayer(searchTree, layer);
    }
  }
  if (!trySpringOver) {
    this->board->setShoveFailingObstacle(foundObstacle);
    return nullptr;
  }
  TileShape* offsetShape;
  if (searchTree->isClearanceCompensationUsed()) {
    int32_t offset = halfWidth + 1 +
                     searchTree->additionalClearance(clearanceClassIndex,
                                                     foundObstacle->clearanceClassIndex(), layer);
    offsetShape = jrt::fast_cast<TileShape>(obstacleShape->enlarge(offset));
  } else {
    int32_t offset = halfWidth + 1;
    double halfClOffset =
        0.5 * board->clearanceValue(foundObstacle->clearanceClassIndex(), clearanceClassIndex,
                                    layer);
    auto* tile = jrt::fast_cast<TileShape>(obstacleShape);
    if (tile != nullptr && !tile->isIntOctagon()) {
      // Match the checker's separately rounded half-clearance and width expansions. Combining
      // width and half-clearance can consume the one-unit guard because Line.translate rounds
      // the scaled displacement along a coordinate axis. Keeping width separate leaves a gap.
      offsetShape = jrt::fast_cast<TileShape>(obstacleShape->enlarge(halfClOffset));
      offsetShape = jrt::fast_cast<TileShape>(offsetShape->enlarge(halfClOffset));
      offsetShape = jrt::fast_cast<TileShape>(offsetShape->enlarge(offset));
    } else {
      // Preserve established offsets for boxes and octagons.
      offsetShape = jrt::fast_cast<TileShape>(obstacleShape->enlarge(offset + halfClOffset));
      offsetShape = jrt::fast_cast<TileShape>(offsetShape->enlarge(halfClOffset));
    }
  }
  if (this->board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
    offsetShape = offsetShape->boundingBox();
  } else if (this->board->rules->getTraceAngleRestriction() ==
             AngleRestriction::FORTYFIVE_DEGREE) {
    offsetShape = offsetShape->boundingOctagon();
  }

  if (offsetShape->containsInside(polyline->firstCorner()) ||
      offsetShape->containsInside(polyline->lastCorner())) {
    // can happen with clearance compensation off because of asymmetry in calculations with the
    // offset shapes
    this->board->setShoveFailingObstacle(foundObstacle);
    return nullptr;
  }
  jrt::Array<jrt::Array<int32_t>*>* entries = offsetShape->entrancePoints(polyline);
  if (entries->length == 0) {
    return polyline;  // no obstacle
  }

  if (entries->length < 2) {
    this->board->setShoveFailingObstacle(foundObstacle);
    return nullptr;
  }
  int32_t firstIntersectionSideNo = (*(*entries)[0])[1];
  int32_t lastIntersectionSideNo = (*(*entries)[entries->length - 1])[1];
  int32_t firstIntersectionLineNo = (*(*entries)[0])[0];
  int32_t lastIntersectionLineNo = (*(*entries)[entries->length - 1])[0];
  int32_t sideDiff = lastIntersectionSideNo - firstIntersectionSideNo;
  if (sideDiff < 0) {
    sideDiff += offsetShape->borderLineCount();
  } else if (sideDiff == 0) {
    FloatPoint* compareCorner = offsetShape->cornerApprox(firstIntersectionSideNo);
    FloatPoint* firstIntersection =
        (*polyline->lines)[firstIntersectionLineNo]->intersectionApprox(
            offsetShape->borderLine(firstIntersectionSideNo));
    FloatPoint* secondIntersection =
        (*polyline->lines)[lastIntersectionLineNo]->intersectionApprox(
            offsetShape->borderLine(lastIntersectionSideNo));
    if (compareCorner->distance(secondIntersection) < compareCorner->distance(firstIntersection)) {
      sideDiff += offsetShape->borderLineCount();
    }
  }
  jrt::Array<Line*>* substituteLines = new jrt::Array<Line*>(sideDiff + 3);
  (*substituteLines)[0] = (*polyline->lines)[firstIntersectionLineNo];
  int32_t currentEdgeLineNo = firstIntersectionSideNo;

  for (int32_t i = 1; i <= sideDiff + 1; i++) {
    (*substituteLines)[i] = offsetShape->borderLine(currentEdgeLineNo);
    if (currentEdgeLineNo == offsetShape->borderLineCount() - 1) {
      currentEdgeLineNo = 0;
    } else {
      ++currentEdgeLineNo;
    }
  }
  (*substituteLines)[sideDiff + 2] = (*polyline->lines)[lastIntersectionLineNo];
  Polyline* substitutePolyline = new Polyline(substituteLines);
  jrt::Array<Polyline*>* pieces = offsetShape->cutout(
      polyline);  // build a circuit around the offsetShape in counter clock sense
  // from the first intersection point to the second intersection point
  Polyline* result = substitutePolyline;

  if (pieces->length > 0) {
    result = (*pieces)[0]->combine(substitutePolyline);
  }
  if (pieces->length > 1) {
    result = result->combine((*pieces)[1]);
  }
  return springOver(result, halfWidth, layer, netNumbers, clearanceClassIndex, overConnectedPins,
                    recursionDepth - 1, contactPins);
}

Polyline* TraceShover::springOverObstacles(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                           jrt::Array<int32_t>* netNumbers,
                                           int32_t clearanceClassIndex,
                                           jrt::Set<Pin*>* contactPins) {
  const int32_t maxSpringOverRecursionDepth = 20;
  Polyline* counterClockWiseResult =
      springOver(polyline, halfWidth, layer, netNumbers, clearanceClassIndex, true,
                 maxSpringOverRecursionDepth, contactPins);
  if (counterClockWiseResult == polyline) {
    return polyline;  // no obstacle
  }

  Polyline* clockWiseResult =
      springOver(polyline->reverse(), halfWidth, layer, netNumbers, clearanceClassIndex, true,
                 maxSpringOverRecursionDepth, contactPins);
  Polyline* result = nullptr;
  if (clockWiseResult != nullptr && counterClockWiseResult != nullptr) {
    if (clockWiseResult->lengthApprox() <= counterClockWiseResult->lengthApprox()) {
      result = clockWiseResult->reverse();
    } else {
      result = counterClockWiseResult;
    }

  } else if (clockWiseResult != nullptr) {
    result = clockWiseResult->reverse();
  } else if (counterClockWiseResult != nullptr) {
    result = counterClockWiseResult;
  }

  return result;
}

}  // namespace fr
