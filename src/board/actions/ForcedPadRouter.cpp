// Port of app.freerouting.board.actions.ForcedPadRouter.
#include "board/actions/ForcedPadRouter.h"

#include "board/actions/DrillItemMover.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/ShapeAndEntrySide.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/optimize/TraceShover.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/searchtree/ShapeTraceEntries.h"
#include "board/state/ChangedArea.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

TileShape* ForcedPadRouter::calcCheckShapeForFromSide(TileShape* shape, Point* shapeCenter,
                                                      Line* borderLine) {
  FloatPoint* shapeCenterFloat = shapeCenter->toFloat();
  FloatPoint* offsetProjection = shapeCenterFloat->projectionApprox(borderLine);
  // Make sure, that direction restrictions are retained.
  jrt::Array<Line*>* lines = new (jrt::kGeom) jrt::Array<Line*>(3);
  Direction* currentDirection = borderLine->direction();
  (*lines)[0] = new (jrt::kGeom) Line(shapeCenter, currentDirection);
  (*lines)[1] = new (jrt::kGeom) Line(shapeCenter, currentDirection->turn45Degree(2));
  (*lines)[2] = new (jrt::kGeom) Line(offsetProjection->round(), currentDirection);
  Polyline* checkLine = new (jrt::kGeom) Polyline(lines);
  return checkLine->offsetShape(1, 0);
}

bool ForcedPadRouter::inFrontOfPad(Line* line, TileShape* padShape, int32_t fromSide,
                                   int32_t width, bool withSides) {
  if (!padShape->isIntOctagon()) {
    // only implemented for octagons
    return true;
  }
  IntOctagon* padOctagon = padShape->boundingOctagon();
  auto* lineA = jrt::fast_cast<IntPoint>(line->a);
  auto* lineB = jrt::fast_cast<IntPoint>(line->b);
  if (!(lineA != nullptr && lineB != nullptr)) {
    // not implemented
    return true;
  }

  double diagWidth = width * jrt::Math::sqrt(2);

  using jrt::Math;
  bool result;
  switch (fromSide) {
    case 0: {
      result = Math::min(lineA->y, lineB->y) >= padOctagon->topY + width ||
               Math::max(lineA->x - lineA->y, lineB->x - lineB->y) <=
                   padOctagon->upperLeftDiagonalX - diagWidth ||
               Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                   padOctagon->upperRightDiagonalX + diagWidth;
      if (withSides && !result) {
        result = (Math::max(lineA->x, lineB->x) <= padOctagon->leftX - width &&
                  Math::min(lineA->x - lineA->y, lineB->x - lineB->y) <=
                      padOctagon->upperLeftDiagonalX - diagWidth) ||
                 (Math::min(lineA->x, lineB->x) >= padOctagon->rightX + width &&
                  Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                      padOctagon->upperRightDiagonalX + diagWidth);
      }
      break;
    }
    case 1: {
      result = Math::min(lineA->y, lineB->y) >= padOctagon->topY + width ||
               Math::max(lineA->x - lineA->y, lineB->x - lineB->y) <=
                   padOctagon->upperLeftDiagonalX - diagWidth ||
               Math::max(lineA->x, lineB->x) <= padOctagon->leftX - width;
      if (withSides && !result) {
        result = (Math::min(lineA->x, lineB->x) <= padOctagon->leftX - width &&
                  Math::max(lineA->x + lineA->y, lineB->x + lineB->y) <=
                      padOctagon->lowerLeftDiagonalX - diagWidth) ||
                 (Math::max(lineA->y, lineB->y) >= padOctagon->topY + width &&
                  Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                      padOctagon->upperRightDiagonalX + diagWidth);
      }
      break;
    }
    case 2: {
      result = Math::max(lineA->x, lineB->x) <= padOctagon->leftX - width ||
               Math::max(lineA->x - lineA->y, lineB->x - lineB->y) <=
                   padOctagon->upperLeftDiagonalX - diagWidth ||
               Math::max(lineA->x + lineA->y, lineB->x + lineB->y) <=
                   padOctagon->lowerLeftDiagonalX - diagWidth;
      if (withSides && !result) {
        result = (Math::max(lineA->y, lineB->y) <= padOctagon->bottomY - width &&
                  Math::min(lineA->x + lineA->y, lineB->x + lineB->y) <=
                      padOctagon->lowerLeftDiagonalX - diagWidth) ||
                 (Math::min(lineA->y, lineB->y) >= padOctagon->topY + width &&
                  Math::min(lineA->x - lineA->y, lineB->x - lineB->y) <=
                      padOctagon->upperLeftDiagonalX - diagWidth);
      }
      break;
    }
    case 3: {
      result = Math::max(lineA->x, lineB->x) <= padOctagon->leftX - width ||
               Math::max(lineA->y, lineB->y) <= padOctagon->bottomY - width ||
               Math::max(lineA->x + lineA->y, lineB->x + lineB->y) <=
                   padOctagon->lowerLeftDiagonalX - diagWidth;
      if (withSides && !result) {
        result = (Math::min(lineA->y, lineB->y) <= padOctagon->bottomY - width &&
                  Math::min(lineA->x - lineA->y, lineB->x - lineB->y) >=
                      padOctagon->lowerRightDiagonalX + diagWidth) ||
                 (Math::min(lineA->x, lineB->x) <= padOctagon->leftX - width &&
                  Math::max(lineA->x - lineA->y, lineB->x - lineB->y) <=
                      padOctagon->upperLeftDiagonalX - diagWidth);
      }
      break;
    }
    case 4: {
      result = Math::max(lineA->y, lineB->y) <= padOctagon->bottomY - width ||
               Math::max(lineA->x + lineA->y, lineB->x + lineB->y) <=
                   padOctagon->lowerLeftDiagonalX - diagWidth ||
               Math::min(lineA->x - lineA->y, lineB->x - lineB->y) >=
                   padOctagon->lowerRightDiagonalX + diagWidth;
      if (withSides && !result) {
        result = (Math::min(lineA->x, lineB->x) >= padOctagon->rightX + width &&
                  Math::max(lineA->x - lineA->y, lineB->x - lineB->y) >=
                      padOctagon->lowerRightDiagonalX + diagWidth) ||
                 (Math::max(lineA->x, lineB->x) <= padOctagon->leftX - width &&
                  Math::min(lineA->x + lineA->y, lineB->x + lineB->y) <=
                      padOctagon->lowerLeftDiagonalX - diagWidth);
      }
      break;
    }
    case 5: {
      result = Math::max(lineA->y, lineB->y) <= padOctagon->bottomY - width ||
               Math::min(lineA->x, lineB->x) >= padOctagon->rightX + width ||
               Math::min(lineA->x - lineA->y, lineB->x - lineB->y) >=
                   padOctagon->lowerRightDiagonalX + diagWidth;
      if (withSides && !result) {
        result = (Math::max(lineA->x, lineB->x) >= padOctagon->rightX + width &&
                  Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                      padOctagon->upperRightDiagonalX + diagWidth) ||
                 (Math::min(lineA->y, lineB->y) <= padOctagon->bottomY - width &&
                  Math::max(lineA->x + lineA->y, lineB->x + lineB->y) <=
                      padOctagon->lowerLeftDiagonalX - diagWidth);
      }
      break;
    }
    case 6: {
      result = Math::min(lineA->x, lineB->x) >= padOctagon->rightX + width ||
               Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                   padOctagon->upperRightDiagonalX + diagWidth ||
               Math::min(lineA->x - lineA->y, lineB->x - lineB->y) >=
                   padOctagon->lowerRightDiagonalX + diagWidth;
      if (withSides && !result) {
        result = (Math::max(lineA->y, lineB->y) <= padOctagon->bottomY - width &&
                  Math::max(lineA->x - lineA->y, lineB->x - lineB->y) >=
                      padOctagon->lowerRightDiagonalX + diagWidth) ||
                 (Math::min(lineA->y, lineB->y) >= padOctagon->topY + width &&
                  Math::max(lineA->x + lineA->y, lineB->x + lineB->y) >=
                      padOctagon->upperRightDiagonalX + diagWidth);
      }
      break;
    }
    case 7: {
      result = Math::min(lineA->y, lineB->y) >= padOctagon->topY + width ||
               Math::min(lineA->x + lineA->y, lineB->x + lineB->y) >=
                   padOctagon->upperRightDiagonalX + diagWidth ||
               Math::min(lineA->x, lineB->x) >= padOctagon->rightX + width;
      if (withSides && !result) {
        result = (Math::max(lineA->y, lineB->y) >= padOctagon->topY + width &&
                  Math::max(lineA->x - lineA->y, lineB->x - lineB->y) <=
                      padOctagon->upperLeftDiagonalX - diagWidth) ||
                 (Math::max(lineA->x, lineB->x) >= padOctagon->rightX + width &&
                  Math::min(lineA->x - lineA->y, lineB->x - lineB->y) >=
                      padOctagon->lowerRightDiagonalX + diagWidth);
      }
      break;
    }
    default: {
      FRLogger::warn("ForcedPadAlgo.in_front_of_pad: fromSide out of range");
      result = true;
      break;
    }
  }

  return result;
}

ForcedPadRouter_CheckDrillResult ForcedPadRouter::checkForcedPad(
    TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
    int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
    int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
    TimeLimit* timeLimit) {
  return checkForcedPad(padShape, fromSide, layer, netNumbers, clearanceClassIndex,
                        copperSharingAllowed, ignoreItems, maxRecursionDepth,
                        maxViaRecursionDepth, checkOnlyFront, timeLimit, false);
}

ForcedPadRouter_CheckDrillResult ForcedPadRouter::checkForcedPad(
    TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
    int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
    int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
    TimeLimit* timeLimit, bool holeOnly) {
  return checkForcedPad(padShape, fromSide, layer, netNumbers, clearanceClassIndex,
                        copperSharingAllowed, ignoreItems, maxRecursionDepth,
                        maxViaRecursionDepth, checkOnlyFront, timeLimit, holeOnly, nullptr);
}

ForcedPadRouter_CheckDrillResult ForcedPadRouter::checkForcedPad(
    TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
    int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
    int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
    TimeLimit* timeLimit, bool holeOnly, Shape* exactPadShape) {
  if (!padShape->isContainedIn(board->getBoundingBox())) {
    this->board->setShoveFailingObstacle(board->getOutline());
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  ShapeTraceEntries* shapeEntries = new (jrt::kOp) ShapeTraceEntries(padShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  // with the pad's copper (a via's circle), items whose copper clears it are no obstacles.
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      padShape, layer, new (jrt::kOp) jrt::Array<int32_t>(0), clearanceClassIndex, exactPadShape);

  if (ignoreItems != nullptr) {
    obstacles->removeAll(ignoreItems);
  }
  bool obstaclesShovable =
      shapeEntries->storeItems(obstacles, true, copperSharingAllowed, holeOnly);
  if (!obstaclesShovable) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }

  // check, if the obstacle vias can be shoved

  for (Via* currentShoveVia : *shapeEntries->shoveViaList) {
    if (maxViaRecursionDepth <= 0) {
      this->board->setShoveFailingObstacle(currentShoveVia);
      return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
    }
    jrt::Array<IntPoint*>* newViaCenter = DrillItemMover::tryShoveViaPoints(
        padShape, layer, currentShoveVia, clearanceClassIndex, false, board);

    if (newViaCenter->length == 0) {
      this->board->setShoveFailingObstacle(currentShoveVia);
      return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
    }
    Vector* delta = (*newViaCenter)[0]->differenceBy(currentShoveVia->getCenter());
    jrt::Collection<Item*>* checkIgnoreItems = new (jrt::kOp) jrt::LinkedList<Item*>();
    if (!DrillItemMover::check(currentShoveVia, delta, maxRecursionDepth,
                               maxViaRecursionDepth - 1, checkIgnoreItems, this->board,
                               timeLimit)) {
      return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
    }
  }
  ForcedPadRouter_CheckDrillResult result = ForcedPadRouter_CheckDrillResult::DRILLABLE;
  if (copperSharingAllowed) {
    for (Item* currentObstacle : *obstacles) {
      if (jrt::fast_cast<Pin>(currentObstacle) != nullptr) {
        result = ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD;
        break;
      }
    }
  }
  int32_t tracePieceCount = shapeEntries->substituteTraceCount();
  if (tracePieceCount == 0) {
    return result;
  }
  if (maxRecursionDepth <= 0) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }
  if (shapeEntries->stackDepth() > 1) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }
  TraceShover* shoveTraceAlgo = new (jrt::kOp) TraceShover(board);
  bool isOrthogonalMode = jrt::fast_cast<IntBox>(padShape) != nullptr;
  for (;;) {
    PolylineTrace* currentSubstituteTrace = shapeEntries->nextSubstituteTracePiece();
    if (currentSubstituteTrace == nullptr) {
      break;
    }
    for (int32_t i = 0; i < currentSubstituteTrace->tileShapeCount(); i++) {
      Line* currentLine = (*currentSubstituteTrace->polyline()->lines)[i + 1];
      Direction* currentDirection = currentLine->direction();
      bool isInFront;
      if (checkOnlyFront) {
        isInFront = inFrontOfPad(currentLine, padShape, fromSide->no,
                                 currentSubstituteTrace->getHalfWidth(), true);
      } else {
        isInFront = true;
      }
      if (isInFront) {
        ShapeAndEntrySide* current =
            new (jrt::kBoard) ShapeAndEntrySide(currentSubstituteTrace, i, isOrthogonalMode, true);
        if (!shoveTraceAlgo->check(current->shape, current->fromSide, currentDirection, layer,
                                   currentSubstituteTrace->netNumbers,
                                   currentSubstituteTrace->clearanceClassIndex(),
                                   maxRecursionDepth - 1, maxViaRecursionDepth, 0, timeLimit)) {
          return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
        }
      }
    }
  }
  return result;
}

bool ForcedPadRouter::forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                                jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                                int32_t maxRecursionDepth, int32_t maxViaRecursionDepth) {
  return forcedPad(padShape, fromSide, layer, netNumbers, clearanceClassIndex,
                   copperSharingAllowed, ignoreItems, maxRecursionDepth, maxViaRecursionDepth,
                   false);
}

bool ForcedPadRouter::forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                                jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                                int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                                bool holeOnly) {
  return forcedPad(padShape, fromSide, layer, netNumbers, clearanceClassIndex,
                   copperSharingAllowed, ignoreItems, maxRecursionDepth, maxViaRecursionDepth,
                   holeOnly, nullptr);
}

bool ForcedPadRouter::forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                                jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                                int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                                bool holeOnly, Shape* exactPadShape) {
  if (padShape->isEmpty()) {
    FRLogger::warn("ShoveTraceAux.forced_pad: padShape is empty");
    return true;
  }
  if (!padShape->isContainedIn(board->getBoundingBox())) {
    this->board->setShoveFailingObstacle(board->getOutline());
    return false;
  }
  if (!DrillItemMover::shoveVias(padShape, fromSide, layer, netNumbers, clearanceClassIndex,
                                 ignoreItems, maxRecursionDepth, maxViaRecursionDepth, false,
                                 this->board, exactPadShape)) {
    return false;
  }
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  ShapeTraceEntries* shapeEntries = new (jrt::kOp) ShapeTraceEntries(padShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      padShape, layer, new (jrt::kOp) jrt::Array<int32_t>(0), clearanceClassIndex, exactPadShape);
  if (ignoreItems != nullptr) {
    obstacles->removeAll(ignoreItems);
  }
  bool obstaclesShovable =
      shapeEntries->storeItems(obstacles, true, copperSharingAllowed, holeOnly) &&
      shapeEntries->shoveViaList->isEmpty();
  if (!obstaclesShovable) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  int32_t tracePieceCount = shapeEntries->substituteTraceCount();
  if (tracePieceCount == 0) {
    return true;
  }
  if (maxRecursionDepth <= 0) {
    this->board->setShoveFailingObstacle(shapeEntries->getFoundObstacle());
    return false;
  }
  bool tailsExistBefore = board->containsTraceTails(obstacles, netNumbers);
  shapeEntries->cutoutTraces(obstacles);
  bool isOrthogonalMode = jrt::fast_cast<IntBox>(padShape) != nullptr;
  TraceShover* shoveTraceAlgo = new (jrt::kOp) TraceShover(this->board);
  for (;;) {
    PolylineTrace* currentSubstituteTrace = shapeEntries->nextSubstituteTracePiece(true);
    if (currentSubstituteTrace == nullptr) {
      break;
    }
    if (currentSubstituteTrace->firstCorner()->equals(currentSubstituteTrace->lastCorner())) {
      continue;
    }
    jrt::Array<int32_t>* currentNetNumbers = currentSubstituteTrace->netNumbers;
    for (int32_t i = 0; i < currentSubstituteTrace->tileShapeCount(); i++) {
      ShapeAndEntrySide* current =
          new (jrt::kBoard) ShapeAndEntrySide(currentSubstituteTrace, i, isOrthogonalMode, false);
      if (!shoveTraceAlgo->insert(current->shape, current->fromSide, layer, currentNetNumbers,
                                  currentSubstituteTrace->clearanceClassIndex(), ignoreItems,
                                  maxRecursionDepth - 1, maxViaRecursionDepth, 0)) {
        return false;
      }
    }
    for (int32_t i = 0; i < currentSubstituteTrace->cornerCount(); i++) {
      board->joinChangedArea(currentSubstituteTrace->polyline()->cornerApprox(i), layer);
    }
    jrt::Array<Point*>* endCorners = nullptr;
    if (!tailsExistBefore) {
      endCorners = new (jrt::kGeom) jrt::Array<Point*>(2);
      (*endCorners)[0] = currentSubstituteTrace->firstCorner();
      (*endCorners)[1] = currentSubstituteTrace->lastCorner();
    }
    board->insertItem(currentSubstituteTrace);
    IntOctagon* optArea;
    if (board->changedArea != nullptr) {
      optArea = board->changedArea->getArea(layer);
    } else {
      optArea = nullptr;
    }

    try {
      currentSubstituteTrace->normalize(optArea);
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

ShapeEntrySide* ForcedPadRouter::calcFromSide(TileShape* shape, Point* shapeCenter, int32_t layer,
                                              int32_t offset, int32_t clearanceClassIndex) {
  jrt::Array<int32_t>* emptyArr = new (jrt::kOp) jrt::Array<int32_t>(0);
  TileShape* offsetShape = jrt::fast_cast<TileShape>(shape->offset(offset));
  for (int32_t i = 0; i < offsetShape->borderLineCount(); i++) {
    TileShape* checkShape = calcCheckShapeForFromSide(shape, shapeCenter, offsetShape->borderLine(i));

    if (board->checkTraceShape(checkShape, layer, emptyArr, clearanceClassIndex, nullptr)) {
      return new (jrt::kBoard) ShapeEntrySide(i, nullptr);
    }
  }
  // try second check without clearance
  for (int32_t i = 0; i < offsetShape->borderLineCount(); i++) {
    TileShape* checkShape = calcCheckShapeForFromSide(shape, shapeCenter, offsetShape->borderLine(i));
    if (board->checkTraceShape(checkShape, layer, emptyArr, 0, nullptr)) {
      return new (jrt::kBoard) ShapeEntrySide(i, nullptr);
    }
  }
  return ShapeEntrySide::NOT_CALCULATED();
}

}  // namespace fr
