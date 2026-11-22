// Port of app.freerouting.board.actions.ForcedViaInserter.
#include "board/actions/ForcedViaInserter.h"

#include "board/actions/ForcedPadRouter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "core/library/Padstack.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/HoleSpacing.h"
#include "rules/ViaInfo.h"

namespace fr {

ForcedPadRouter_CheckDrillResult ForcedViaInserter::checkLayer(
    double viaRadius, int32_t clearanceClassIndex, bool attachSmdAllowed, TileShape* roomShape,
    Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t maxRecursionDepth,
    int32_t maxViaRecursionDepth, RoutingBoard* board, int32_t traceHalfWidth,
    int32_t traceClearanceClass) {
  return checkLayer(viaRadius, clearanceClassIndex, attachSmdAllowed, roomShape, location, layer,
                    netNumbers, maxRecursionDepth, maxViaRecursionDepth, board, traceHalfWidth,
                    traceClearanceClass, false);
}

ForcedPadRouter_CheckDrillResult ForcedViaInserter::checkLayer(
    double viaRadius, int32_t clearanceClassIndex, bool attachSmdAllowed, TileShape* roomShape,
    Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t maxRecursionDepth,
    int32_t maxViaRecursionDepth, RoutingBoard* board, int32_t traceHalfWidth,
    int32_t traceClearanceClass, bool circularCopper) {
  if (viaRadius <= 0) {
    return ForcedPadRouter_CheckDrillResult::DRILLABLE;
  }
  if (dynamic_cast<IntPoint*>(location) == nullptr) {
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }
  IntPoint* intLocation = dynamic_cast<IntPoint*>(location);
  ConvexShape* viaShape = new Circle(intLocation, jrt::d2i(jrt::Math::ceil(viaRadius)));

  double checkRadius = viaRadius +
                       0.5 * board->clearanceValue(clearanceClassIndex, clearanceClassIndex, layer) +
                       board->getMinTraceHalfWidth();

  TileShape* tileShape;
  bool is90Degree;
  if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
    tileShape = viaShape->boundingBox();
    is90Degree = true;
  } else {
    tileShape = viaShape->boundingOctagon();
    is90Degree = false;
  }

  tileShape = compensatePadShape(tileShape, clearanceClassIndex, layer, board);
  ShapeEntrySide* fromSide = calculateFromSide(location->toFloat(), tileShape,
                                               roomShape->toSimplex(), checkRadius, is90Degree);
  if (fromSide == nullptr) {
    return ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE;
  }

  ForcedPadRouter* forcedPadRouter = new ForcedPadRouter(board);
  ForcedPadRouter_CheckDrillResult viaResult = forcedPadRouter->checkForcedPad(
      tileShape, fromSide, layer, netNumbers, clearanceClassIndex, attachSmdAllowed, nullptr,
      maxRecursionDepth, maxViaRecursionDepth, false, nullptr, false,
      circularCopper ? viaShape : nullptr);
  if (viaResult == ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
    return viaResult;
  }

  if (traceHalfWidth <= 0) {
    return viaResult;
  }

  Circle* startTraceCircle = new Circle(intLocation, traceHalfWidth);
  TileShape* startTraceShape;
  if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
    startTraceShape = startTraceCircle->boundingBox();
  } else {
    startTraceShape = startTraceCircle->boundingOctagon();
  }

  startTraceShape = compensatePadShape(startTraceShape, traceClearanceClass, layer, board);
  ForcedPadRouter_CheckDrillResult traceResult = forcedPadRouter->checkForcedPad(
      startTraceShape, fromSide, layer, netNumbers, traceClearanceClass, true, nullptr,
      maxRecursionDepth, maxViaRecursionDepth, false, nullptr, false, startTraceCircle);
  if (traceResult == ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
    return traceResult;
  }
  if (viaResult == ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD ||
      traceResult == ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD) {
    return ForcedPadRouter_CheckDrillResult::DRILLABLE_WITH_ATTACH_SMD;
  }
  return ForcedPadRouter_CheckDrillResult::DRILLABLE;
}

bool ForcedViaInserter::check(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                              int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                              RoutingBoard* board, jrt::Array<int32_t>* tracePenHalfwidthArr,
                              int32_t traceClearanceClassIndex) {
  return check(viaInfo, location, netNumbers, maxRecursionDepth, maxViaRecursionDepth, board,
               tracePenHalfwidthArr, traceClearanceClassIndex, std::nullopt);
}

bool ForcedViaInserter::check(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                              int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                              RoutingBoard* board, jrt::Array<int32_t>* tracePenHalfwidthArr,
                              int32_t traceClearanceClassIndex,
                              std::optional<bool> attachSmdAllowed) {
  bool attach = attachSmdAllowed.has_value() ? *attachSmdAllowed : viaInfo->attachSmdAllowed();
  Vector* translateVector = location->differenceBy(Point::ZERO());
  int32_t calcFromSideOffset = board->getMinTraceHalfWidth();
  ForcedPadRouter* forcedPadRouter = new ForcedPadRouter(board);
  Padstack* viaPadstack = viaInfo->getPadstack();
  if (!HoleSpacing::clear(board, viaPadstack, location)) {
    return false;
  }
  Shape* holeShape = holeCheckShape(viaPadstack, location, board);
  for (int32_t i = viaPadstack->fromLayer(); i <= viaPadstack->toLayer(); i++) {
    Shape* currentPadShape = viaPadstack->getShape(i);
    int32_t currentClearanceClassIndex = viaInfo->getClearanceClassIndex();
    bool holeLayer = false;
    if (currentPadShape == nullptr) {
      if (holeShape == nullptr) {
        continue;
      }
      // The drill hole itself must keep hole clearance from copper on this layer.
      currentPadShape = holeShape;
      currentClearanceClassIndex = 0;
      holeLayer = true;
    } else {
      currentPadShape = dynamic_cast<Shape*>(currentPadShape->translateBy(translateVector));
    }
    TileShape* tileShape;
    if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
      tileShape = currentPadShape->boundingBox();
    } else {
      tileShape = currentPadShape->boundingOctagon();
    }
    tileShape = compensatePadShape(tileShape, currentClearanceClassIndex, i, board);
    ShapeEntrySide* fromSide = forcedPadRouter->calcFromSide(
        tileShape, location, i, calcFromSideOffset, currentClearanceClassIndex);
    if (forcedPadRouter->checkForcedPad(tileShape, fromSide, i, netNumbers,
                                        currentClearanceClassIndex, attach, nullptr,
                                        maxRecursionDepth, maxViaRecursionDepth, false, nullptr,
                                        holeLayer, currentPadShape) ==
        ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
      board->setShoveFailingLayer(i);
      return false;
    }
    if (currentClearanceClassIndex != 0 && holeShape != nullptr) {
      // The drill hole must ALSO keep hole clearance from other-net copper on layers where
      // the pad exists - the pad check above only enforces the (smaller) copper clearance.
      TileShape* holeTile;
      if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
        holeTile = holeShape->boundingBox();
      } else {
        holeTile = holeShape->boundingOctagon();
      }
      if (forcedPadRouter->checkForcedPad(holeTile, fromSide, i, netNumbers, 0, attach, nullptr,
                                          maxRecursionDepth, maxViaRecursionDepth, false, nullptr,
                                          true, holeShape) ==
          ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
        board->setShoveFailingLayer(i);
        return false;
      }
    }

    IntPoint* tracePoint = dynamic_cast<IntPoint*>(location);
    if (tracePenHalfwidthArr != nullptr && i < tracePenHalfwidthArr->length &&
        (*tracePenHalfwidthArr)[i] > 0 && tracePoint != nullptr) {
      Circle* startTraceCircle = new Circle(tracePoint, (*tracePenHalfwidthArr)[i]);
      TileShape* startTraceShape;
      if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
        startTraceShape = startTraceCircle->boundingBox();
      } else {
        startTraceShape = startTraceCircle->boundingOctagon();
      }
      startTraceShape = compensatePadShape(startTraceShape, traceClearanceClassIndex, i, board);
      if (forcedPadRouter->checkForcedPad(startTraceShape, fromSide, i, netNumbers,
                                          traceClearanceClassIndex, true, nullptr,
                                          maxRecursionDepth, maxViaRecursionDepth, false, nullptr,
                                          false, startTraceCircle) ==
          ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
        board->setShoveFailingLayer(i);
        return false;
      }
    }
  }
  return true;
}

bool ForcedViaInserter::insert(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                               int32_t traceClearanceClassIndex,
                               jrt::Array<int32_t>* tracePenHalfwidthArr,
                               int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                               RoutingBoard* board) {
  return insert(viaInfo, location, netNumbers, traceClearanceClassIndex, tracePenHalfwidthArr,
                maxRecursionDepth, maxViaRecursionDepth, board, std::nullopt);
}

bool ForcedViaInserter::insert(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                               int32_t traceClearanceClassIndex,
                               jrt::Array<int32_t>* tracePenHalfwidthArr,
                               int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                               RoutingBoard* board, std::optional<bool> attachSmdAllowed) {
  bool attach = attachSmdAllowed.has_value() ? *attachSmdAllowed : viaInfo->attachSmdAllowed();
  Vector* translateVector = location->differenceBy(Point::ZERO());
  int32_t calcFromSideOffset = board->getMinTraceHalfWidth();
  ForcedPadRouter* forcedPadRouter = new ForcedPadRouter(board);
  Padstack* viaPadstack = viaInfo->getPadstack();
  if (!HoleSpacing::clear(board, viaPadstack, location)) {
    return false;
  }
  Shape* holeShape = holeCheckShape(viaPadstack, location, board);
  for (int32_t i = viaPadstack->fromLayer(); i <= viaPadstack->toLayer(); i++) {
    Shape* currentPadShape = viaPadstack->getShape(i);
    int32_t currentClearanceClassIndex = viaInfo->getClearanceClassIndex();
    bool holeLayer = false;
    if (currentPadShape == nullptr) {
      if (holeShape == nullptr) {
        continue;
      }
      currentPadShape = holeShape;
      currentClearanceClassIndex = 0;
      holeLayer = true;
    } else {
      currentPadShape = dynamic_cast<Shape*>(currentPadShape->translateBy(translateVector));
    }
    TileShape* tileShape;
    Circle* startTraceCircle;
    IntPoint* point = dynamic_cast<IntPoint*>(location);
    if ((*tracePenHalfwidthArr)[i] > 0 && point != nullptr) {
      startTraceCircle = new Circle(point, (*tracePenHalfwidthArr)[i]);
    } else {
      startTraceCircle = nullptr;
    }
    TileShape* startTraceShape = nullptr;
    if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
      tileShape = currentPadShape->boundingBox();
      if (startTraceCircle != nullptr) {
        startTraceShape = startTraceCircle->boundingBox();
      }
    } else {
      tileShape = currentPadShape->boundingOctagon();
      if (startTraceCircle != nullptr) {
        startTraceShape = startTraceCircle->boundingOctagon();
      }
    }
    tileShape = compensatePadShape(tileShape, currentClearanceClassIndex, i, board);
    ShapeEntrySide* fromSide = forcedPadRouter->calcFromSide(
        tileShape, location, i, calcFromSideOffset, currentClearanceClassIndex);
    if (!forcedPadRouter->forcedPad(tileShape, fromSide, i, netNumbers,
                                    currentClearanceClassIndex, attach, nullptr,
                                    maxRecursionDepth, maxViaRecursionDepth, holeLayer,
                                    currentPadShape)) {
      board->setShoveFailingLayer(i);
      return false;
    }
    if (currentClearanceClassIndex != 0 && holeShape != nullptr) {
      TileShape* holeTile;
      if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
        holeTile = holeShape->boundingBox();
      } else {
        holeTile = holeShape->boundingOctagon();
      }
      if (!forcedPadRouter->forcedPad(holeTile, fromSide, i, netNumbers, 0, attach, nullptr,
                                      maxRecursionDepth, maxViaRecursionDepth, true, holeShape)) {
        board->setShoveFailingLayer(i);
        return false;
      }
    }
    if (startTraceShape != nullptr) {
      startTraceShape = compensatePadShape(startTraceShape, traceClearanceClassIndex, i, board);
      // necessary in case startTraceShape is bigger than tileShape
      if (!forcedPadRouter->forcedPad(startTraceShape, fromSide, i, netNumbers,
                                      traceClearanceClassIndex, true, nullptr, maxRecursionDepth,
                                      maxViaRecursionDepth, false, startTraceCircle)) {
        board->setShoveFailingLayer(i);
        return false;
      }
    }
  }
  board->insertVia(viaPadstack, location, netNumbers, viaInfo->getClearanceClassIndex(),
                   FixedState::UNFIXED, attach);
  return true;
}

bool ForcedViaInserter::holeInSmdPad(RoutingBoard* board, Padstack* viaPadstack, Point* location,
                                     jrt::Array<int32_t>* netNumbers) {
  double drillRadius = viaPadstack->getDrillRadius();
  IntPoint* center = dynamic_cast<IntPoint*>(location);
  if (drillRadius <= 0 || center == nullptr) {
    return false;
  }
  Circle* hole = new Circle(center, jrt::d2i(jrt::Math::ceil(drillRadius)));
  for (int32_t layer = viaPadstack->fromLayer(); layer <= viaPadstack->toLayer(); layer++) {
    for (SearchTreeObject* found : *board->overlappingObjects(hole, layer)) {
      auto* pin = dynamic_cast<Pin*>(found);
      if (pin != nullptr && pin->drillAllowed() && !pin->viaMayAttach() &&
          pin->sharesNetNo(netNumbers)) {
        Shape* pad = pin->getShapeOnLayer(layer);
        if (pad != nullptr && pad->intersects(hole)) {
          return true;
        }
      }
    }
  }
  return false;
}

TileShape* ForcedViaInserter::compensatePadShape(TileShape* shape, int32_t clearanceClassIndex,
                                                 int32_t layer, RoutingBoard* board) {
  int32_t compensation = board->searchTreeManager->getDefaultTree()->clearanceCompensationValue(
      clearanceClassIndex, layer);
  if (compensation == 0) {
    return shape;
  }
  // Preserve four-sided geometry: side indices and orthogonal shoving depend on IntBox.
  auto* box = dynamic_cast<IntBox*>(shape);
  return box != nullptr ? static_cast<TileShape*>(box->offset(compensation))
                        : dynamic_cast<TileShape*>(shape->enlarge(compensation));
}

Shape* ForcedViaInserter::holeCheckShape(Padstack* padstack, Point* location,
                                         RoutingBoard* board) {
  int32_t holeClearance = board->rules->getHoleClearance();
  IntPoint* center = dynamic_cast<IntPoint*>(location);
  if (holeClearance <= 0 || center == nullptr) {
    return nullptr;
  }
  double drillRadius = padstack->getDrillRadius();
  if (drillRadius <= 0) {
    return nullptr;
  }
  // Inflate by the hole clearance itself and check with the null clearance class (0), so
  // the requirement is exact hole-to-copper spacing regardless of the neighbor's class.
  return new Circle(center, jrt::d2i(jrt::Math::ceil(drillRadius + holeClearance + 10)));
}

ShapeEntrySide* ForcedViaInserter::calculateFromSide(FloatPoint* viaLocation, TileShape* viaShape,
                                                     Simplex* roomShape, double dist,
                                                     bool is90Degree) {
  IntBox* viaBox = viaShape->boundingBox();
  for (int32_t i = 0; i < 4; i++) {
    FloatPoint* checkPoint;
    double borderX;
    double borderY;
    switch (i) {
      case 0:
        checkPoint = new FloatPoint(viaLocation->x, viaLocation->y - dist);
        borderX = viaLocation->x;
        borderY = viaBox->ll->y;
        break;
      case 1:
        checkPoint = new FloatPoint(viaLocation->x + dist, viaLocation->y);
        borderX = viaBox->ur->x;
        borderY = viaLocation->y;
        break;
      case 2:
        checkPoint = new FloatPoint(viaLocation->x, viaLocation->y + dist);
        borderX = viaLocation->x;
        borderY = viaBox->ur->y;
        break;
      default:  // i == 3
        checkPoint = new FloatPoint(viaLocation->x - dist, viaLocation->y);
        borderX = viaBox->ll->x;
        borderY = viaLocation->y;
        break;
    }
    if (roomShape->contains(checkPoint)) {
      int32_t fromSideIndex;
      if (is90Degree) {
        fromSideIndex = i;
      } else {
        fromSideIndex = 2 * i;
      }
      FloatPoint* currentBorderPoint = new FloatPoint(borderX, borderY);
      return new ShapeEntrySide(fromSideIndex, currentBorderPoint);
    }
  }
  if (is90Degree) {
    return nullptr;
  }
  // try the diagonal directions
  dist = dist / Limits::sqrt2;
  double borderDist = viaBox->maxWidth() / (2 * Limits::sqrt2);
  for (int32_t i = 0; i < 4; i++) {
    FloatPoint* checkPoint;
    double borderX;
    double borderY;
    switch (i) {
      case 0:
        checkPoint = new FloatPoint(viaLocation->x + dist, viaLocation->y - dist);
        borderX = viaLocation->x + borderDist;
        borderY = viaLocation->y - borderDist;
        break;
      case 1:
        checkPoint = new FloatPoint(viaLocation->x + dist, viaLocation->y + dist);
        borderX = viaLocation->x + borderDist;
        borderY = viaLocation->y + borderDist;
        break;
      case 2:
        checkPoint = new FloatPoint(viaLocation->x - dist, viaLocation->y + dist);
        borderX = viaLocation->x - borderDist;
        borderY = viaLocation->y + borderDist;
        break;
      default:  // i == 3
        checkPoint = new FloatPoint(viaLocation->x - dist, viaLocation->y - dist);
        borderX = viaLocation->x - borderDist;
        borderY = viaLocation->y - borderDist;
        break;
    }
    if (roomShape->contains(checkPoint)) {
      int32_t fromSideIndex = 2 * i + 1;
      FloatPoint* currentBorderPoint = new FloatPoint(borderX, borderY);
      return new ShapeEntrySide(fromSideIndex, currentBorderPoint);
    }
  }
  return nullptr;
}

bool ForcedViaInserter::crowdsOwnPad(RoutingBoard* board, Padstack* viaPadstack,
                                     int32_t viaClearanceClass, Point* location,
                                     int32_t netNumber) {
  IntPoint* center = dynamic_cast<IntPoint*>(location);
  if (center == nullptr) {
    return false;
  }
  for (int32_t layer = viaPadstack->fromLayer(); layer <= viaPadstack->toLayer(); layer++) {
    ConvexShape* viaShape = viaPadstack->getShape(layer);
    if (viaShape == nullptr) {
      continue;
    }
    int32_t radius = jrt::d2i(jrt::Math::ceil(0.5 * viaShape->maxWidth()));
    int32_t reach = board->rules->clearanceMatrix->maxValue(viaClearanceClass, layer);
    if (reach <= 0) {
      continue;
    }
    for (SearchTreeObject* found :
         *board->overlappingObjects(new Circle(center, radius + reach), layer)) {
      auto* pin = dynamic_cast<Pin*>(found);
      if (pin != nullptr && pin->containsNet(netNumber) && !pin->viaMayAttach()) {
        Shape* pad = pin->getShapeOnLayer(layer);
        if (pad != nullptr) {
          int32_t clearance = board->rules->clearanceMatrix->getValue(
              pin->clearanceClassIndex(), viaClearanceClass, layer, false);
          if (clearance > 0 && pad->intersects(new Circle(center, radius + clearance))) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

}  // namespace fr
