#include "board/optimize/TraceTightener.h"

#include "autoroute/maze/AutorouteControl.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/FixedState.h"
#include "board/optimize/TraceShover.h"
#include "board/optimize/TraceTightener45.h"
#include "board/optimize/TraceTightener90.h"
#include "board/optimize/TraceTightenerAnyAngle.h"
#include "board/optimize/ViaMoveHistory.h"
#include "board/optimize/ViaOptimizer.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/ChangedArea.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/Signum.h"
#include "datastructures/Stoppable.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

double TraceTightener::getMinContactChamferRoom() {
  return jrt::Math::max(2.0, 0.25 * (Limits::sqrt2 - 1.0) * this->currentHalfWidth);
}

TraceTightener::TraceTightener(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                               Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                               int32_t keepPointLayer)
    : board(board), onlyNetNoArr(onlyNetNoArr), stoppableThread(stoppableThread) {
  if (timeLimit > 0) {
    this->timeLimit = new TimeLimit(timeLimit);
  } else {
    this->timeLimit = nullptr;
  }
  this->keepPoint = keepPoint;
  this->keepPointLayer = keepPointLayer;
}

TraceTightener* TraceTightener::getInstance(RoutingBoard* board,
                                            jrt::Array<int32_t>* onlyNetNoArr,
                                            IntOctagon* clipShape, int32_t minTranslateDist,
                                            Stoppable* stoppableThread, int32_t timeLimit,
                                            Point* keepPoint, int32_t keepPointLayer) {
  TraceTightener* result;
  AngleRestriction angleRestriction = board->rules->getTraceAngleRestriction();
  if (angleRestriction == AngleRestriction::NINETY_DEGREE) {
    result = new TraceTightener90(board, onlyNetNoArr, stoppableThread, timeLimit, keepPoint,
                                  keepPointLayer);
  } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
    result = new TraceTightener45(board, onlyNetNoArr, stoppableThread, timeLimit, keepPoint,
                                  keepPointLayer);
  } else {
    result = new TraceTightenerAnyAngle(board, onlyNetNoArr, stoppableThread, timeLimit,
                                        keepPoint, keepPointLayer);
  }
  result->currentClipShape = clipShape;
  result->minTranslateDist = jrt::Math::max(minTranslateDist, 100);
  return result;
}

void TraceTightener::optChangedArea(
    jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts) {
  if (board->changedArea == nullptr) {
    return;
  }
  bool somethingChanged = true;
  ViaMoveHistory* viaMoves = new ViaMoveHistory();
  // starting with curr_min_translate_dist big is a try to
  // avoid fine approximation at the beginning to avoid
  // problems with dog ears
  for (int32_t rounds = 0; somethingChanged && !mustStopAfter(rounds); ++rounds) {
    somethingChanged = false;
    for (int32_t i = 0; i < board->getLayerCount(); i++) {
      IntOctagon* changedRegion = board->changedArea->getArea(i);
      if (changedRegion->isEmpty()) {
        continue;
      }
      board->changedArea->setEmpty(i);
      board->joinGraphicsUpdateBox(changedRegion->boundingBox());
      double changedAreaOffset = 1.5 * (board->rules->clearanceMatrix->maxValue(i) +
                                        2 * board->rules->getMaxTraceHalfWidth());
      changedRegion = changedRegion->enlarge(changedAreaOffset);
      // search in the ShapeSearchTree for all overlapping traces
      // with clipShape on layer i
      jrt::Collection<SearchTreeObject*>* items = board->overlappingObjects(changedRegion, i);
      for (SearchTreeObject* currentObject : *items) {
        if (this->isStopRequested()) {
          return;
        }
        if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentObject)) {
          if (currentTrace->pullTight(this)) {
            somethingChanged = true;
            if (this->splitTracesAtKeepPoint()) {
              break;
            }
          } else if (smoothenEndCornersAtTrace(currentTrace)) {
            somethingChanged = true;
            break;  // because items may be removed
          }
        } else if (auto* via = jrt::fast_cast<Via>(currentObject);
                   via != nullptr && traceCosts != nullptr) {
          if (ViaOptimizer::optViaLocation(this->board, via, traceCosts, this->minTranslateDist,
                                           10, viaMoves)) {
            somethingChanged = true;
          }
        }
      }
    }
  }
}

Polyline* TraceTightener::pullTight(Polyline* polyline, int32_t layer, int32_t halfWidth,
                                    jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                    jrt::Set<Pin*>* contactPins) {
  currentLayer = layer;
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  currentHalfWidth = halfWidth + searchTree->clearanceCompensationValue(clearanceClassIndex, layer);
  currentNetNumbers = netNumbers;
  currentClearanceClassIndex = clearanceClassIndex;
  this->contactPins = contactPins;
  return pullTight(polyline);
}

bool TraceTightener::mustStopAfter(int32_t rounds) {
  return rounds >= MAX_ROUNDS || isStopRequested();
}

bool TraceTightener::isStopRequested() {
  if (this->stoppableThread != nullptr && this->stoppableThread->isStopRequested()) {
    return true;
  }
  if (this->timeLimit == nullptr) {
    return false;
  }
  bool timeLimitExceeded = this->timeLimit->limitExceeded();
  if (timeLimitExceeded) {
    if (this->board == nullptr) {
      FRLogger::error("TraceTightener.is_stop_requested: board is null", nullptr);
    }

    FRLogger::debug("TraceTightener.is_stop_requested: time limit exceeded");
  }
  return timeLimitExceeded;
}

Polyline* TraceTightener::repositionLines(Polyline* polyline) {
  if (polyline->lines->length < 5) {
    return polyline;
  }
  for (int32_t i = 2; i < polyline->lines->length - 2; i++) {
    Line* newLine = repositionLine(polyline->lines, i);
    if (newLine != nullptr) {
      jrt::Array<Line*>* lines = new jrt::Array<Line*>(polyline->lines->length);
      jrt::arraycopy(polyline->lines, 0, lines, 0, lines->length);
      (*lines)[i] = newLine;
      Polyline* result = new Polyline(lines);
      return skipSegmentsOfLength0(result);
    }
  }
  return polyline;
}

Line* TraceTightener::repositionLine(jrt::Array<Line*>* lines, int32_t no) {
  if (lines->length - no < 3) {
    return nullptr;
  }
  if (currentClipShape != nullptr) {
    // check, that the corners of the line to translate are inside the clip shape
    for (int32_t i = -1; i < 1; i++) {
      Point* currentCorner = (*lines)[no + i]->intersection((*lines)[no + i + 1]);
      if (currentClipShape->isOutside(currentCorner)) {
        return nullptr;
      }
    }
  }
  Line* translateLine = (*lines)[no];
  Point* prevCorner = (*lines)[no - 2]->intersection((*lines)[no - 1]);
  Point* nextCorner = (*lines)[no + 1]->intersection((*lines)[no + 2]);
  double prevDist = translateLine->signedDistance(prevCorner->toFloat());
  double nextDist = translateLine->signedDistance(nextCorner->toFloat());
  if (Signum::of(prevDist) != Signum::of(nextDist)) {
    // the 2 corners are at different sides of translateLine
    return nullptr;
  }
  Point* nearestPoint;
  double maxTranslateDist;
  if (jrt::Math::abs(prevDist) < jrt::Math::abs(nextDist)) {
    nearestPoint = prevCorner;
    maxTranslateDist = prevDist;
  } else {
    nearestPoint = nextCorner;
    maxTranslateDist = nextDist;
  }
  double translateDist = maxTranslateDist;
  double deltaDist = maxTranslateDist;
  Side* sideOfNearestPoint = translateLine->sideOf(nearestPoint);
  int32_t sign = Signum::asInt(maxTranslateDist);
  Line* newLine = nullptr;
  jrt::Array<Line*>* checkLines = new jrt::Array<Line*>(3);
  (*checkLines)[0] = (*lines)[no - 1];
  (*checkLines)[2] = (*lines)[no + 1];
  bool firstTime = true;
  while (firstTime || jrt::Math::abs(deltaDist) > minTranslateDist) {
    if (firstTime && jrt::fast_cast<IntPoint>(nearestPoint) != nullptr) {
      (*checkLines)[1] = Line::getInstance(nearestPoint, translateLine->direction());
    } else {
      (*checkLines)[1] = translateLine->translate(-translateDist);
    }
    if ((*checkLines)[1]->equals(translateLine)) {
      // may happen at first time if nearestPoint is not an IntPoint
      return nullptr;
    }
    Side* newLineSideOfNearestPoint = (*checkLines)[1]->sideOf(nearestPoint);
    if (newLineSideOfNearestPoint != sideOfNearestPoint &&
        newLineSideOfNearestPoint != Side::COLLINEAR()) {
      // moved a little bit to far at the first time
      // because of numerical inaccuracy;
      // may happen if nearestPoint is not an IntPoint
      double shortenValue = sign * 0.5;
      maxTranslateDist -= shortenValue;
      translateDist -= shortenValue;
      deltaDist -= shortenValue;
      continue;
    }
    Polyline* tmp = new Polyline(checkLines);

    bool checkOk = false;
    if (tmp->lines->length == 3) {
      TileShape* shapeToCheck = tmp->offsetShape(currentHalfWidth, 0);
      checkOk = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                       currentClearanceClassIndex, this->contactPins);
    }
    deltaDist /= 2;
    if (checkOk) {
      newLine = (*checkLines)[1];
      if (firstTime) {
        // biggest possible change
        break;
      }
      translateDist += deltaDist;
    } else {
      translateDist -= deltaDist;
    }
    firstTime = false;
  }
  if (newLine != nullptr && board->changedArea != nullptr) {
    // mark the changed area
    board->changedArea->join((*checkLines)[0]->intersectionApprox(newLine), currentLayer);
    board->changedArea->join((*checkLines)[2]->intersectionApprox(newLine), currentLayer);
    board->changedArea->join((*lines)[no - 1]->intersectionApprox((*lines)[no]), currentLayer);
    board->changedArea->join((*lines)[no]->intersectionApprox((*lines)[no + 1]), currentLayer);
  }
  return newLine;
}

Polyline* TraceTightener::skipSegmentsOfLength0(Polyline* polyline) {
  bool polylineChanged = false;
  Polyline* currentPolyline = polyline;
  for (int32_t i = 1; i < currentPolyline->lines->length - 1; i++) {
    bool trySkip;
    if (i == 1 || i == currentPolyline->lines->length - 2) {
      // the position of the first corner and the last corner
      //  must be retained exactly
      Point* prevCorner = currentPolyline->corner(i - 1);
      Point* currentCorner = currentPolyline->corner(i);
      trySkip = currentCorner->equals(prevCorner);
    } else {
      FloatPoint* prevCorner = currentPolyline->cornerApprox(i - 1);
      FloatPoint* currentCorner = currentPolyline->cornerApprox(i);
      trySkip = currentCorner->distanceSquare(prevCorner) < c_min_corner_dist_square;
    }

    if (trySkip) {
      // check, if skipping the line of length 0 does not
      // result in a clearance violation
      jrt::Array<Line*>* currentLines = new jrt::Array<Line*>(currentPolyline->lines->length - 1);
      jrt::arraycopy(currentPolyline->lines, 0, currentLines, 0, i);
      jrt::arraycopy(currentPolyline->lines, i + 1, currentLines, i, currentLines->length - i);
      Polyline* tmp = new Polyline(currentLines);
      bool checkOk = tmp->lines->length == currentLines->length;
      if (checkOk && !(*currentPolyline->lines)[i]->isMultipleOf45Degree()) {
        // no check necessary for skipping 45 degree lines, because the check is
        // performance critical and the line shapes
        // are intersected with the bounding octagon anyway.
        if (i > 1) {
          TileShape* shapeToCheck = tmp->offsetShape(currentHalfWidth, i - 2);
          checkOk = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                           currentClearanceClassIndex, this->contactPins);
        }
        if (checkOk && (i < currentPolyline->lines->length - 2)) {
          TileShape* shapeToCheck = tmp->offsetShape(currentHalfWidth, i - 1);
          checkOk = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                           currentClearanceClassIndex, this->contactPins);
        }
      }
      if (checkOk) {
        polylineChanged = true;
        currentPolyline = tmp;
        --i;
      }
    }
  }
  if (!polylineChanged) {
    return polyline;
  }
  return currentPolyline;
}

bool TraceTightener::smoothenEndCornersAtTrace(PolylineTrace* trace) {
  if (this->onlyNetNoArr->length > 0 && !trace->netsEqual(this->onlyNetNoArr)) {
    return false;
  }
  currentLayer = trace->getLayer();
  currentHalfWidth = trace->getHalfWidth();
  currentNetNumbers = trace->netNumbers;
  currentClearanceClassIndex = trace->clearanceClassIndex();
  return smoothenEndCornersAtTrace1(trace);
}

bool TraceTightener::smoothenEndCornersAtTrace1(PolylineTrace* trace) {
  // try to improve the connection to other traces
  if (trace->isShoveFixed()) {
    return false;
  }
  jrt::Set<Pin*>* savedContactPins = this->contactPins;
  // to allow the trace to slide to the end point of a contact trace, if the contact trace ends at
  // a pin.
  this->contactPins = nullptr;
  bool result = false;
  bool connectionToTraceImproved = true;
  PolylineTrace* currentTrace = trace;
  for (int32_t rounds = 0; connectionToTraceImproved && !mustStopAfter(rounds); ++rounds) {
    connectionToTraceImproved = false;
    Polyline* adjustedPolyline = smoothenEndCornersAtTrace2(currentTrace);
    if (adjustedPolyline != nullptr) {
      int32_t traceLayer = currentTrace->getLayer();
      int32_t currentClClass = currentTrace->clearanceClassIndex();
      FixedState currentFixedState = currentTrace->getFixedState();
      board->removeItem(currentTrace);
      PolylineTrace* adjInsTrace =
          board->insertTraceWithoutCleaning(adjustedPolyline, traceLayer, currentHalfWidth,
                                            currentTrace->netNumbers, currentClClass,
                                            currentFixedState);
      if (adjInsTrace != nullptr) {
        result = true;
        connectionToTraceImproved = true;
        board->removeItem(currentTrace);
        currentTrace = adjInsTrace;
        for (int32_t currentNetNumber : *currentTrace->netNumbers) {
          board->splitTraces(adjustedPolyline->firstCorner(), traceLayer, currentNetNumber);
          board->splitTraces(adjustedPolyline->lastCorner(), traceLayer, currentNetNumber);

          try {
            board->normalizeTraces(currentNetNumber);
          } catch (const jrt::Exception& e) {
            FRLogger::error("The normalization of net '" +
                                board->rules->nets->get(currentNetNumber)->name + "' failed.",
                            &e);
          }

          if (splitTracesAtKeepPoint()) {
            return true;
          }
        }
      }
    }
  }
  this->contactPins = savedContactPins;
  return result;
}

bool TraceTightener::splitTracesAtKeepPoint() {
  if (this->keepPoint == nullptr) {
    return false;
  }
  ItemSelectionFilter* filter =
      new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
  jrt::Collection<Item*>* pickedItems =
      this->board->pickItems(this->keepPoint, this->keepPointLayer, filter);
  for (Item* currentItem : *pickedItems) {
    jrt::Array<Trace*>* splitPieces = jrt::fast_cast<Trace>(currentItem)->split(this->keepPoint);
    if (splitPieces != nullptr) {
      return true;
    }
  }
  return false;
}

Polyline* TraceTightener::smoothenEndCornersAtTrace2(PolylineTrace* trace) {
  if (trace == nullptr || !trace->isOnTheBoard()) {
    return nullptr;
  }
  Polyline* result = smoothenStartCornerAtTrace(trace);
  if (result == nullptr) {
    result = smoothenEndCornerAtTrace(trace);
    if (result != nullptr && board->changedArea != nullptr) {
      // mark the changed area
      board->changedArea->join(result->cornerApprox(result->cornerCount() - 1), currentLayer);
    }
  } else if (board->changedArea != nullptr) {
    // mark the changed area
    board->changedArea->join(result->cornerApprox(0), currentLayer);
  }
  if (result != nullptr) {
    this->contactPins = trace->touchingPinsAtEndCorners();
    result = skipSegmentsOfLength0(result);
  }
  return result;
}

Polyline* TraceTightener::avoidAcidTraps(Polyline* polyline) {
  if (true) {
    return polyline;
  }
  Polyline* result = polyline;
  TraceShover* shoveTraceAlgo = new TraceShover(this->board);
  Polyline* newPolyline =
      shoveTraceAlgo->springOverObstacles(polyline, currentHalfWidth, currentLayer,
                                          currentNetNumbers, currentClearanceClassIndex,
                                          contactPins);
  if (newPolyline != nullptr && newPolyline != polyline) {
    if (this->board->checkPolylineTrace(newPolyline, currentLayer, currentHalfWidth,
                                        currentNetNumbers, currentClearanceClassIndex)) {
      result = newPolyline;
    }
  }
  return result;
}

}  // namespace fr
