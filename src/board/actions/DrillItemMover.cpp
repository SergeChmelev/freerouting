// Port of app.freerouting.board.actions.DrillItemMover.
#include "board/actions/DrillItemMover.h"

#include "board/actions/ForcedPadRouter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/searchtree/ShapeTraceEntries.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "rules/BoardRules.h"
#include "rules/HoleSpacing.h"

namespace fr {

bool DrillItemMover::check(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                           int32_t maxViaRecursionDepth, jrt::Collection<Item*>* ignoreItems,
                           RoutingBoard* board, TimeLimit* timeLimit) {
  if (timeLimit != nullptr && timeLimit->limitExceeded()) {
    return false;
  }
  if (drillItem->isShoveFixed()) {
    return false;
  }

  // Check, that drillitem is only connected to traces.
  jrt::Collection<Item*>* contactList = drillItem->getNormalContacts();
  for (Item* currentContact : *contactList) {
    if (!(dynamic_cast<Trace*>(currentContact) != nullptr ||
          dynamic_cast<ConductionArea*>(currentContact) != nullptr)) {
      return false;
    }
  }
  jrt::Collection<Item*>* effectiveIgnoreItems;
  if (ignoreItems == nullptr) {
    effectiveIgnoreItems = new jrt::LinkedList<Item*>();
  } else {
    effectiveIgnoreItems = ignoreItems;
  }
  effectiveIgnoreItems->add(drillItem);
  ForcedPadRouter* forcedPadRouter = new ForcedPadRouter(board);
  bool attachAllowed = false;
  if (auto* via = dynamic_cast<Via*>(drillItem)) {
    attachAllowed = via->attachAllowed;
    if (via->getPadstack() != nullptr &&
        !HoleSpacing::clear(board, via->getPadstack(), via->getCenter()->translateBy(vector), via,
                            effectiveIgnoreItems)) {
      return false;
    }
  }
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  for (int32_t currentLayer = drillItem->firstLayer(); currentLayer <= drillItem->lastLayer();
       currentLayer++) {
    int32_t currentInd = currentLayer - drillItem->firstLayer();
    TileShape* currentShape = drillItem->getTreeShape(searchTree, currentInd);
    if (currentShape == nullptr) {
      continue;
    }
    ConvexShape* newShape = dynamic_cast<ConvexShape*>(currentShape->translateBy(vector));
    TileShape* currentTileShape;
    if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
      currentTileShape = newShape->boundingBox();
    } else {
      currentTileShape = newShape->boundingOctagon();
    }
    ShapeEntrySide* fromSide = new ShapeEntrySide(drillItem->getCenter(), currentTileShape);
    if (forcedPadRouter->checkForcedPad(
            currentTileShape, fromSide, currentLayer, drillItem->netNumbers,
            drillItem->clearanceClassIndex(), attachAllowed, effectiveIgnoreItems,
            maxRecursionDepth, maxViaRecursionDepth, true, timeLimit, false,
            board->rules->isExactCopper()
                ? movedCopper(drillItem, currentInd, vector, searchTree)
                : nullptr) == ForcedPadRouter_CheckDrillResult::NOT_DRILLABLE) {
      return false;
    }
  }
  return true;
}

bool DrillItemMover::insert(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                            int32_t maxViaRecursionDepth, IntOctagon* tidyRegion,
                            RoutingBoard* board) {
  jrt::Array<IntOctagon*>* holder =
      tidyRegion != nullptr ? jrt::Array<IntOctagon*>::of({tidyRegion}) : nullptr;
  return insertJoiningTidyRegion(drillItem, vector, maxRecursionDepth, maxViaRecursionDepth,
                                 holder, board);
}

bool DrillItemMover::insertJoiningTidyRegion(DrillItem* drillItem, Vector* vector,
                                             int32_t maxRecursionDepth,
                                             int32_t maxViaRecursionDepth,
                                             jrt::Array<IntOctagon*>* tidyRegionHolder,
                                             RoutingBoard* board) {
  if (drillItem->isShoveFixed()) {
    return false;
  }

  bool attachAllowed = false;
  if (auto* via = dynamic_cast<Via*>(drillItem)) {
    attachAllowed = via->attachAllowed;
    if (via->getPadstack() != nullptr &&
        !HoleSpacing::clear(board, via->getPadstack(), via->getCenter()->translateBy(vector),
                            via)) {
      return false;
    }
  }
  ForcedPadRouter* forcedPadRouter = new ForcedPadRouter(board);
  jrt::Collection<Item*>* ignoreItems = new jrt::LinkedList<Item*>();
  ignoreItems->add(drillItem);
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  for (int32_t currentLayer = drillItem->firstLayer(); currentLayer <= drillItem->lastLayer();
       currentLayer++) {
    int32_t currentInd = currentLayer - drillItem->firstLayer();
    TileShape* currentShape = drillItem->getTreeShape(searchTree, currentInd);
    if (currentShape == nullptr) {
      continue;
    }
    ConvexShape* newShape = dynamic_cast<ConvexShape*>(currentShape->translateBy(vector));
    TileShape* currentTileShape;
    if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
      currentTileShape = newShape->boundingBox();
    } else {
      currentTileShape = newShape->boundingOctagon();
    }
    if (tidyRegionHolder != nullptr && tidyRegionHolder->length > 0 &&
        (*tidyRegionHolder)[0] != nullptr) {
      (*tidyRegionHolder)[0] = (*tidyRegionHolder)[0]->union_(currentTileShape->boundingOctagon());
    }
    ShapeEntrySide* fromSide = new ShapeEntrySide(drillItem->getCenter(), currentTileShape);
    if (!forcedPadRouter->forcedPad(currentTileShape, fromSide, currentLayer,
                                    drillItem->netNumbers, drillItem->clearanceClassIndex(),
                                    attachAllowed, ignoreItems, maxRecursionDepth,
                                    maxViaRecursionDepth, false,
                                    board->rules->isExactCopper()
                                        ? movedCopper(drillItem, currentInd, vector, searchTree)
                                        : nullptr)) {
      return false;
    }
    IntBox* currentBoundingBox = currentShape->boundingBox();
    for (int32_t j = 0; j < 4; j++) {
      board->joinChangedArea(currentBoundingBox->cornerApprox(j), currentLayer);
    }
  }
  drillItem->moveBy(vector);
  return true;
}

Shape* DrillItemMover::movedCopper(DrillItem* drillItem, int32_t index, Vector* vector,
                                   ShapeSearchTree* tree) {
  Shape* copper = drillItem->getShape(index);
  if (dynamic_cast<Circle*>(copper) == nullptr && dynamic_cast<TileShape*>(copper) == nullptr) {
    return nullptr;
  }
  int32_t layer = drillItem->shapeLayer(index);
  int32_t grow = tree->clearanceCompensationValue(drillItem->clearanceClassIndex(), layer) +
                 tree->drillHoleClearanceDelta(drillItem, copper, layer);
  if (auto* circle = dynamic_cast<Circle*>(copper)) {
    return circle->translateBy(vector)->enlarge(grow);
  }
  return grow == 0 ? static_cast<Shape*>(dynamic_cast<TileShape*>(copper)->translateBy(vector))
                   : nullptr;
}

bool DrillItemMover::shoveVias(TileShape* obstacleShape, ShapeEntrySide* fromSide, int32_t layer,
                               jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                               jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                               int32_t maxViaRecursionDepth, bool copperSharingAllowed,
                               RoutingBoard* board) {
  return shoveVias(obstacleShape, fromSide, layer, netNumbers, clearanceClassIndex, ignoreItems,
                   maxRecursionDepth, maxViaRecursionDepth, copperSharingAllowed, board, nullptr);
}

bool DrillItemMover::shoveVias(TileShape* obstacleShape, ShapeEntrySide* fromSide, int32_t layer,
                               jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                               jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                               int32_t maxViaRecursionDepth, bool copperSharingAllowed,
                               RoutingBoard* board, Shape* exactShape) {
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  ShapeTraceEntries* shapeEntries = new ShapeTraceEntries(obstacleShape, layer, netNumbers,
                                                          clearanceClassIndex, fromSide, board);
  jrt::Collection<Item*>* obstacles = searchTree->overlappingItemsWithClearance(
      obstacleShape, layer, new jrt::Array<int32_t>(0), clearanceClassIndex, exactShape);

  if (!shapeEntries->storeItems(obstacles, false, copperSharingAllowed)) {
    return true;
  }
  if (ignoreItems != nullptr) {
    // PORT NOTE: Java shoveViaList.removeAll(ignoreItems) on a LinkedList<Via> with a
    // Collection<Item>; the element types differ in C++, so AbstractCollection.removeAll is
    // written out (iterate this list, remove the elements contained in ignoreItems).
    for (auto it = shapeEntries->shoveViaList->iterator(); it->hasNext();) {
      Item* current = it->next();
      if (ignoreItems->contains(current)) {
        it->remove();
      }
    }
  }
  if (shapeEntries->shoveViaList->isEmpty()) {
    return true;
  }
  double shapeRadius = 0.5 * obstacleShape->boundingBox()->minWidth();
  for (Via* currentVia : *shapeEntries->shoveViaList) {
    if (currentVia->sharesNetNo(netNumbers)) {
      continue;
    }
    if (maxViaRecursionDepth <= 0) {
      return true;
    }
    jrt::Array<IntPoint*>* tryViaCenters =
        tryShoveViaPoints(obstacleShape, layer, currentVia, clearanceClassIndex, true, board);
    IntPoint* newViaCenter = nullptr;
    double maxDist =
        0.5 * currentVia->getShapeOnLayer(layer)->boundingBox()->maxWidth() + shapeRadius;
    double maxDistSquare = maxDist * maxDist;
    IntPoint* currentViaCenter = dynamic_cast<IntPoint*>(currentVia->getCenter());
    FloatPoint* checkViaCenter = currentViaCenter->toFloat();
    Vector* relCoor = nullptr;
    for (int32_t i = 0; i < tryViaCenters->length; i++) {
      if (i == 0 ||
          checkViaCenter->distanceSquare((*tryViaCenters)[i]->toFloat()) <= maxDistSquare) {
        jrt::Collection<Item*>* localIgnoreItems = new jrt::LinkedList<Item*>();
        if (ignoreItems != nullptr) {
          localIgnoreItems->addAll(ignoreItems);
        }
        relCoor = (*tryViaCenters)[i]->differenceBy(currentViaCenter);
        // No time limit here because the item database is already changed.
        bool shoveOk = check(currentVia, relCoor, maxRecursionDepth, maxViaRecursionDepth - 1,
                             localIgnoreItems, board, nullptr);
        if (shoveOk) {
          newViaCenter = (*tryViaCenters)[i];
          break;
        }
      }
    }
    if (newViaCenter == nullptr) {
      continue;
    }
    if (!insert(currentVia, relCoor, maxRecursionDepth, maxViaRecursionDepth - 1, nullptr,
                board)) {
      return false;
    }
  }
  return true;
}

jrt::Array<IntPoint*>* DrillItemMover::tryShoveViaPoints(TileShape* obstacleShape, int32_t layer,
                                                         Via* via, int32_t clearanceClassIndex,
                                                         bool extendedCheck, RoutingBoard* board) {
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  TileShape* currentViaShape = via->getTreeShapeOnLayer(searchTree, layer);
  if (currentViaShape == nullptr) {
    return new jrt::Array<IntPoint*>(0);
  }
  bool isIntOctagon = obstacleShape->isIntOctagon();
  double clearanceValue =
      searchTree->additionalClearance(clearanceClassIndex, via->clearanceClassIndex(), layer);
  double shoveDistance;
  if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE ||
      isIntOctagon) {
    shoveDistance = 0.5 * currentViaShape->boundingBox()->maxWidth();
    shoveDistance += clearanceValue;
  } else {
    // a different algorithm is used for calculating the new via centers
    shoveDistance = 0;
    // Split only the clearance not already represented by the two prepared shapes.
    shoveDistance += 0.5 * clearanceValue;
  }

  // The additional constant 2 is an empirical value for the tolerance in case of diagonal
  // shoving.
  shoveDistance += 2;

  IntPoint* currentViaCenter = dynamic_cast<IntPoint*>(via->getCenter());
  jrt::Array<IntPoint*>* tryViaCenters;
  int32_t tryCount = 1;
  if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
    IntBox* currentOffsetBox = obstacleShape->boundingBox()->offset(shoveDistance);
    if (extendedCheck) {
      tryCount = 2;
    }
    tryViaCenters = currentOffsetBox->nearestBorderProjections(currentViaCenter, tryCount);
  } else if (isIntOctagon) {
    IntOctagon* currentOffsetOctagon = obstacleShape->boundingOctagon()->enlarge(shoveDistance);
    if (extendedCheck) {
      tryCount = 4;
    }

    tryViaCenters = currentOffsetOctagon->nearestBorderProjections(currentViaCenter, tryCount);
  } else {
    TileShape* currentOffsetShape = dynamic_cast<TileShape*>(obstacleShape->enlarge(shoveDistance));
    currentViaShape = dynamic_cast<TileShape*>(currentViaShape->enlarge(0.5 * clearanceValue));
    if (extendedCheck) {
      tryCount = 4;
    }
    jrt::Array<FloatPoint*>* shoveDeltas =
        currentOffsetShape->nearestRelativeOutsideLocations(currentViaShape, tryCount);
    tryViaCenters = new jrt::Array<IntPoint*>(shoveDeltas->length);
    for (int32_t i = 0; i < tryViaCenters->length; i++) {
      Vector* currentDelta = (*shoveDeltas)[i]->round()->differenceBy(Point::ZERO());
      (*tryViaCenters)[i] = dynamic_cast<IntPoint*>(currentViaCenter->translateBy(currentDelta));
    }
  }
  return tryViaCenters;
}

}  // namespace fr
