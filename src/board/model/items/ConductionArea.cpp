// Port of app.freerouting.board.model.items.ConductionArea.
#include "board/model/items/ConductionArea.h"

#include "autoroute/plane/FastPlaneConnectivityGrid.h"
#include "autoroute/plane/PlaneOutlineSmoothing.h"
#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/Communication.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PlanarArea.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "util/TextManager.h"

namespace fr {

void ConductionArea::setFillRules(int32_t clearance, int32_t minWidth) {
  this->fillClearance = jrt::Math::max(0, clearance);
  this->fillMinWidth = jrt::Math::max(0, minWidth);
  this->clearDerivedData();
}

void ConductionArea::setPadConnectionRules(std::optional<PadPlaneConnection_Mode> padConnection,
                                           int32_t thermalGap, int32_t thermalSpokeWidth) {
  this->padConnection = padConnection;
  this->thermalGap = jrt::Math::max(0, thermalGap);
  this->thermalSpokeWidth = jrt::Math::max(0, thermalSpokeWidth);
  this->clearDerivedData();
}

void ConductionArea::setFillEdgeClearance(int32_t edgeClearance) {
  this->fillEdgeClearance = jrt::Math::max(0, edgeClearance);
  this->clearDerivedData();
}

void ConductionArea::setFillCornerSmoothing(bool fillets, int32_t distance) {
  this->cornerFillets = fillets;
  this->cornerSmoothing = jrt::Math::max(0, distance);
  this->smoothedArea = nullptr;
  this->clearDerivedData();
}

Area* ConductionArea::getFillOutline() {
  Area* area = getArea();
  if (this->cornerSmoothing <= 0 || area == nullptr) {
    return area;
  }
  if (this->smoothedArea == nullptr) {
    this->smoothedArea = PlaneOutlineSmoothing::smooth(area, this->cornerFillets, this->cornerSmoothing);
  }
  return this->smoothedArea;
}

void ConductionArea::setIsFilled(bool value) {
  this->isFilled = value;
  this->clearDerivedData();
}

ConductionArea::ConductionArea(Area* area, int32_t layer, Vector* translation,
                               double rotationInDegree, bool sideChanged,
                               jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                               int32_t id, int32_t groupId, const std::optional<std::string>& name,
                               bool isObstacle, FixedState fixedState, BasicBoard* board)
    : ObstacleArea(area, layer, translation, rotationInDegree, sideChanged, netNumbers,
                   clearanceClassIndex, id, groupId, name, fixedState, board) {
  this->isObstacle_ = isObstacle;
}

void ConductionArea::clearDerivedData() {
  ObstacleArea::clearDerivedData();
  this->cachedBoardRevision = -1;
  this->cachedBoardFillArea = nullptr;
  this->cachedFractureRevision = -1;
  this->cachedNetAssignmentStamp = -1;
  this->cachedFractureResult = nullptr;
  this->fractureMemo = nullptr;
}

void ConductionArea::warmDetailedFillCache() {
  if (!this->isFilled) {
    return;
  }
  int32_t layerIndex = this->getLayer();
  double maxClearanceLookupBoard = 2000.0 * this->board->communication->getResolution(Unit::UM);
  if (this->board->rules != nullptr && this->board->rules->clearanceMatrix != nullptr) {
    double maxMatrixClearance =
        this->board->rules->clearanceMatrix->maxValue(this->clearanceClassIndex(), layerIndex);
    maxClearanceLookupBoard = jrt::Math::max(
        maxClearanceLookupBoard,
        maxMatrixClearance + 100.0 * this->board->communication->getResolution(Unit::UM));
  }
  ensureDetailedFillCache(maxClearanceLookupBoard, layerIndex);
}

PlanarArea* ConductionArea::getDetailedFillArea() {
  warmDetailedFillCache();
  return cachedBoardFillArea;
}

PlanarArea* ConductionArea::getDetailedFillArea(double maxClearanceLookupBoard,
                                                int32_t layerIndex) {
  ensureDetailedFillCache(maxClearanceLookupBoard, layerIndex);
  return cachedBoardFillArea;
}

void ConductionArea::ensureDetailedFillCache(double maxClearanceLookupBoard, int32_t layerIndex) {
  bool boardChanged = this->board->getRevision() != cachedBoardRevision;
  if (cachedBoardFillArea != nullptr && !boardChanged) {
    return;
  }

  PlanarArea* fillArea = PlanarArea::fromArea(this->getArea());
  if (fillArea == nullptr) {
    fillArea = PlanarArea::empty();
  }
  if (!fillArea->isEmpty()) {
    IntBox* bbox = this->boundingBox();
    double spokeWidth = 400.0 * this->board->communication->getResolution(Unit::UM);
    int32_t maxCl = static_cast<int32_t>(jrt::Math::round(maxClearanceLookupBoard));
    IntBox* inflatedBbox = new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(bbox->ll->x - maxCl, bbox->ll->y - maxCl),
                                      new (jrt::kGeom) IntPoint(bbox->ur->x + maxCl, bbox->ur->y + maxCl));

    jrt::List<PlanarArea*>* foreignClearances = new (jrt::kOp) jrt::ArrayList<PlanarArea*>();
    jrt::List<PlanarArea*>* sameNetClearances = new (jrt::kOp) jrt::ArrayList<PlanarArea*>();

    jrt::Set<SearchTreeObject*>* overlaps = this->board->overlappingObjects(inflatedBbox, layerIndex);
    for (SearchTreeObject* ob : *overlaps) {
      auto* currentItem = jrt::fast_cast<Item>(ob);
      if (currentItem == nullptr || currentItem == this) {
        continue;
      }
      if (!currentItem->sharesLayer(this)) {
        continue;
      }

      if (jrt::fast_cast<Trace>(currentItem) != nullptr ||
          jrt::fast_cast<ConductionArea>(currentItem) != nullptr) {
        if (currentItem->sharesNet(this)) {
          continue;
        }
      }

      int32_t clClass1 = this->clearanceClassIndex();
      int32_t clClass2 = currentItem->clearanceClassIndex();
      double clearanceDist = this->board->clearanceValue(clClass1, clClass2, layerIndex);

      if (currentItem->sharesNet(this)) {
        if (auto* drillItem = jrt::fast_cast<DrillItem>(currentItem)) {
          FloatPoint* center = drillItem->getCenter()->toFloat();
          Shape* shape = drillItem->getShapeOnLayer(layerIndex);
          if (shape == nullptr) {
            continue;
          }

          Shape* enlargedShape = shape->enlarge(clearanceDist);
          PlanarArea* clearanceArea = PlanarArea::fromShape(enlargedShape);
          if (clearanceArea == nullptr || clearanceArea->isEmpty()) {
            continue;
          }

          IntBox* itemBbox = drillItem->boundingBox();
          double expansionRadiusBoard =
              jrt::Math::hypot(itemBbox->width() / 2.0, itemBbox->height() / 2.0) + clearanceDist +
              spokeWidth;
          double spokeLength = 2 * expansionRadiusBoard;

          PlanarArea* spoke1 = PlanarArea::createRotatedSpoke(center->x, center->y, spokeWidth,
                                                              spokeLength, jrt::Math::PI / 4.0);
          PlanarArea* spoke2 = PlanarArea::createRotatedSpoke(center->x, center->y, spokeWidth,
                                                              spokeLength, -jrt::Math::PI / 4.0);
          PlanarArea* spokes = spoke1->union_(spoke2);

          PlanarArea* thermalCutout = clearanceArea->difference(spokes);
          if (!thermalCutout->isEmpty()) {
            sameNetClearances->add(thermalCutout);
          }
        }
      } else {
        if (auto* drillItem = jrt::fast_cast<DrillItem>(currentItem)) {
          Shape* shape = drillItem->getShapeOnLayer(layerIndex);
          if (shape != nullptr) {
            Shape* enlargedShape = shape->enlarge(clearanceDist);
            PlanarArea* clearanceArea = PlanarArea::fromShape(enlargedShape);
            if (clearanceArea != nullptr && !clearanceArea->isEmpty()) {
              foreignClearances->add(clearanceArea);
            }
          }
        } else {
          int32_t shapeCount = currentItem->tileShapeCount();
          for (int32_t i = 0; i < shapeCount; i++) {
            if (currentItem->shapeLayer(i) == layerIndex) {
              TileShape* tileShape = currentItem->getTileShape(i);
              if (tileShape != nullptr) {
                Shape* enlargedShape = tileShape->enlarge(clearanceDist);
                PlanarArea* clearanceArea = PlanarArea::fromShape(enlargedShape);
                if (clearanceArea != nullptr && !clearanceArea->isEmpty()) {
                  foreignClearances->add(clearanceArea);
                }
              }
            }
          }
        }
      }
    }

    fillArea = fillArea->subtractAll(foreignClearances);
    fillArea = fillArea->subtractAll(sameNetClearances);
  }
  cachedBoardFillArea = fillArea;
  cachedBoardRevision = this->board->getRevision();
}

Item* ConductionArea::copy(int32_t id) {
  ConductionArea* result =
      new (jrt::kBoardData) ConductionArea(getRelativeArea(), getLayer(), getTranslation(), getRotationInDegree(),
                         getSideChanged(), netNumbers, clearanceClassIndex(), id, getComponentId(),
                         this->name, isObstacle_, getFixedState(), board);
  result->fillClearance = this->fillClearance;
  result->fillMinWidth = this->fillMinWidth;
  result->padConnection = this->padConnection;
  result->thermalGap = this->thermalGap;
  result->thermalSpokeWidth = this->thermalSpokeWidth;
  result->fillEdgeClearance = this->fillEdgeClearance;
  result->cornerFillets = this->cornerFillets;
  result->cornerSmoothing = this->cornerSmoothing;
  // PORT NOTE (deep copy): like Java, isFilled is NOT carried by copy() (the copy starts filled);
  // Java serialization would keep it. See the report of the items bucket.
  return result;
}

bool ConductionArea::isDeadCopper() {
  if (board == nullptr) {
    return false;
  }
  return getNormalContacts()->isEmpty();
}

jrt::Set<Item*>* ConductionArea::getNormalContacts() {
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  // A plane spans most of its layer, often in thousands of tiles: rather than one tree query per
  // tile, look up the tiles that contain the ends of the traces and the centres of the vias, and
  // that overlap the pins of the net.
  jrt::Set<SearchTreeObject*>* overlaps = defaultTree->overlappingObjectsWithPointIn(
      this, getLayer(), [this](SearchTreeObject* object) { return this->contactPoints(object); },
      [this](SearchTreeObject* object) { return this->isPinCandidate(object); });
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (overlaps == nullptr) {
    // Not in the tree: query tile by tile.
    for (int32_t i = 0; i < tileShapeCount(); i++) {
      TileShape* tile = getTileShape(i);
      for (SearchTreeObject* currentObject : *board->overlappingObjects(tile, getLayer())) {
        if (isContactInTile(currentObject, tile)) {
          result->add(jrt::fast_cast<Item>(currentObject));
        }
      }
    }
    return result;
  }
  for (SearchTreeObject* currentObject : *overlaps) {
    result->add(jrt::fast_cast<Item>(currentObject));
  }
  return result;
}

jrt::Array<Point*>* ConductionArea::contactPoints(SearchTreeObject* object) {
  auto* item = jrt::fast_cast<Item>(object);
  if (item == nullptr || item == this || !item->sharesNet(this) || !item->sharesLayer(this)) {
    return nullptr;
  }
  if (auto* trace = jrt::fast_cast<Trace>(item)) {
    return jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()});
  }
  auto* via = jrt::fast_cast<Via>(item);
  return via != nullptr ? jrt::Array<Point*>::of({via->getCenter()}) : nullptr;
}

bool ConductionArea::isPinCandidate(SearchTreeObject* object) {
  auto* pin = jrt::fast_cast<Pin>(object);
  return pin != nullptr && pin->sharesNet(this) && pin->sharesLayer(this);
}

bool ConductionArea::isContactInTile(SearchTreeObject* object, TileShape* tile) {
  if (isPinCandidate(object)) {
    return overlapsTile(jrt::fast_cast<Pin>(object), tile);
  }
  jrt::Array<Point*>* points = contactPoints(object);
  if (points != nullptr) {
    for (Point* point : *points) {
      if (tile->contains(point)) {
        return true;
      }
    }
  }
  return false;
}

bool ConductionArea::overlapsTile(DrillItem* drillItem, TileShape* tile) {
  TileShape* drillShape =
      drillItem->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), getLayer());
  return drillShape != nullptr && tile->intersection(drillShape)->dimension() == 2;
}

bool ConductionArea::overlapsCopperOf(DrillItem* drillItem) {
  if (!isPinCandidate(drillItem)) {
    return false;
  }
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  TileShape* drillShape = drillItem->getTreeShapeOnLayer(defaultTree, getLayer());
  if (drillShape == nullptr) {
    return false;
  }
  if (getSearchTreeEntries(defaultTree) == nullptr) {
    for (int32_t i = 0; i < tileShapeCount(); i++) {
      if (getTileShape(i)->intersection(drillShape)->dimension() == 2) {
        return true;
      }
    }
    return false;
  }
  jrt::Collection<ShapeTree_TreeEntry*>* entries = new (jrt::kOp) jrt::ArrayList<ShapeTree_TreeEntry*>();
  defaultTree->overlappingTreeEntries(drillShape, getLayer(), entries);
  for (ShapeTree_TreeEntry* entry : *entries) {
    if (static_cast<jrt::Object*>(entry->object) == static_cast<jrt::Object*>(this) &&
        getTreeShape(defaultTree, entry->shapeIndexInObject)
                ->intersection(drillShape)
                ->dimension() == 2) {
      return true;
    }
  }
  return false;
}

bool ConductionArea::coversAllPlanesOfNet(jrt::Collection<Item*>* connectedSet,
                                          jrt::Collection<Item*>* unconnectedSet, int32_t netNo) {
  bool onPlane = false;
  bool hasPin = false;
  for (Item* item : *connectedSet) {
    auto* ca = jrt::fast_cast<ConductionArea>(item);
    if (ca != nullptr && !ca->isFractured()) {
      onPlane = true;
    } else if (jrt::fast_cast<Pin>(item) != nullptr) {
      hasPin = true;
    }
  }
  if (!onPlane) {
    return false;
  }
  if (!hasPin) {
    // a floating fragment, or copper that only hangs on one: nothing needs it
    return true;
  }
  for (Item* item : *unconnectedSet) {
    auto* ca = jrt::fast_cast<ConductionArea>(item);
    if (ca != nullptr && !ca->isFloatingFragment(netNo)) {
      return false;
    }
  }
  for (Item* item : *connectedSet) {
    auto* ca = jrt::fast_cast<ConductionArea>(item);
    if (ca != nullptr && ca->hasUnreachedPinIsland(connectedSet)) {
      return false;
    }
  }
  return true;
}

bool ConductionArea::isFloatingFragment(int32_t netNo) {
  if (isFractured()) {
    return !hasPinOnAnyIsland(netNo);
  }
  return !reachesPinOfNet(netNo);
}

bool ConductionArea::hasPinOnAnyIsland(int32_t netNo) {
  FastPlaneConnectivityGrid_PlaneFractureResult* res = getFractureResult();
  if (res == nullptr || !res->isFractured) {
    return false;
  }
  for (jrt::List<Item*>* island : *res->islandItems->values()) {
    for (Item* item : *island) {
      if (jrt::fast_cast<Pin>(item) != nullptr && (netNo <= 0 || item->containsNet(netNo))) {
        return true;
      }
    }
  }
  return false;
}

bool ConductionArea::hasUnreachedPinIsland(jrt::Collection<Item*>* connectedSet) {
  FastPlaneConnectivityGrid_PlaneFractureResult* res = getFractureResult();
  if (res == nullptr || !res->isFractured) {
    return false;
  }
  for (jrt::List<Item*>* island : *res->islandItems->values()) {
    for (Item* item : *island) {
      if (jrt::fast_cast<Pin>(item) != nullptr && !connectedSet->contains(item)) {
        return true;
      }
    }
  }
  return false;
}

bool ConductionArea::reachesPinOfNet(int32_t netNo) {
  for (Item* item : *getConnectedSet(netNo)) {
    if (jrt::fast_cast<Pin>(item) != nullptr) {
      return true;
    }
  }
  return false;
}

void ConductionArea::collectPinsOnFracturedIslands(jrt::Collection<Pin*>* out) {
  FastPlaneConnectivityGrid_PlaneFractureResult* res = getFractureResult();
  if (res == nullptr || !res->isFractured) {
    return;
  }
  for (jrt::List<Item*>* island : *res->islandItems->values()) {
    for (Item* item : *island) {
      if (auto* pin = jrt::fast_cast<Pin>(item)) {
        out->add(pin);
        break;
      }
    }
  }
}

bool ConductionArea::isFractured() {
  FastPlaneConnectivityGrid_PlaneFractureResult* res = getFractureResult();
  return res != nullptr && res->isFractured;
}

FastPlaneConnectivityGrid_FractureMemo* ConductionArea::fractureMemoForCopy() {
  FastPlaneConnectivityGrid_FractureMemo* memo = this->fractureMemo;
  if (memo == nullptr || this->board == nullptr) {
    return nullptr;
  }
  return memo->detach(getArea());
}

void ConductionArea::adoptFractureMemo(FastPlaneConnectivityGrid_FractureMemo* detached) {
  if (detached == nullptr || this->board == nullptr) {
    return;
  }
  BasicBoard* copyBoard = this->board;
  this->fractureMemo =
      detached->attach(getArea(), [copyBoard](int32_t id) { return copyBoard->getItem(id); });
}

FastPlaneConnectivityGrid_PlaneFractureResult* ConductionArea::getFractureResult() {
  if (this->board == nullptr) {
    return nullptr;
  }
  int32_t currentRevision = this->board->getRevision();
  int64_t currentNetAssignmentStamp = this->board->getNetAssignmentChangeCount();
  if (this->cachedFractureResult != nullptr &&
      this->cachedNetAssignmentStamp == currentNetAssignmentStamp) {
    if (this->cachedFractureRevision == currentRevision) {
      return this->cachedFractureResult;
    }
    // the board changed, but nowhere the result depends on: it still holds
    IntBox* dependencyBox = this->cachedFractureResult->dependencyBox;
    if (dependencyBox != nullptr &&
        !this->board->changedSince(this->cachedFractureRevision, getLayer(), dependencyBox)) {
      this->cachedFractureRevision = currentRevision;
      return this->cachedFractureResult;
    }
  }
  FastPlaneConnectivityGrid_FractureMemo* memo = this->fractureMemo;
  if (memo == nullptr) {
    memo = new (jrt::kOp) FastPlaneConnectivityGrid_FractureMemo();
    this->fractureMemo = memo;
  }
  FastPlaneConnectivityGrid* connectivityGrid = new (jrt::kOp) FastPlaneConnectivityGrid();
  jrt::Collection<Item*>* contacts = getNormalContacts();
  this->cachedFractureResult = connectivityGrid->evaluateFracture(this, this->board, contacts, memo);
  this->cachedFractureRevision = currentRevision;
  this->cachedNetAssignmentStamp = currentNetAssignmentStamp;
  return this->cachedFractureResult;
}

void ConductionArea::getConnectedSetRecu(jrt::Set<Item*>* result, int32_t netNumber,
                                         bool stopAtPlane) {
  if (this->board == nullptr) {
    ObstacleArea::getConnectedSetRecu(result, netNumber, stopAtPlane);
    return;
  }

  FastPlaneConnectivityGrid_PlaneFractureResult* fractureResult = getFractureResult();
  if (fractureResult == nullptr || !fractureResult->isFractured) {
    ObstacleArea::getConnectedSetRecu(result, netNumber, stopAtPlane);
    return;
  }

  // Determine which islands have already been contacted by items in result. A trace whose ends
  // touch two islands is in both.
  jrt::Set<int32_t>* reachedIslands = new (jrt::kOp) jrt::HashSet<int32_t>();
  for (Item* resItem : *result) {
    if (resItem != this && resItem->sharesNet(this) && resItem->sharesLayer(this)) {
      for (auto* entry : *fractureResult->islandItems->entrySet()) {
        if (entry->getValue()->contains(resItem)) {
          reachedIslands->add(entry->getKey());
        }
      }
    }
  }

  if (reachedIslands->isEmpty()) {
    // If this conduction area itself is the start item, only visit island 0 so severed islands
    // are not falsely bridged
    reachedIslands->add(0);
  }

  for (int32_t islandId : *reachedIslands) {
    jrt::List<Item*>* reachableItems = fractureResult->islandItems->get(islandId);
    if (reachableItems != nullptr) {
      for (Item* item : *reachableItems) {
        if (item != this && (netNumber <= 0 || item->containsNet(netNumber))) {
          if (result->add(item)) {
            item->getConnectedSetRecu(result, netNumber, stopAtPlane);
          }
        }
      }
    }
  }
}

bool ConductionArea::touchesEndOf(Trace* trace, bool first) {
  FastPlaneConnectivityGrid_PlaneFractureResult* fractureResult = getFractureResult();
  if (fractureResult == nullptr) {
    return true;
  }
  jrt::Array<int32_t>* islands = fractureResult->endIslands(trace, first);
  return islands == nullptr || islands->length > 0;
}

bool ConductionArea::joinsEndsOf(Trace* trace) {
  FastPlaneConnectivityGrid_PlaneFractureResult* fractureResult = getFractureResult();
  if (fractureResult == nullptr || !fractureResult->isFractured) {
    return true;
  }
  jrt::Array<int32_t>* first = fractureResult->endIslands(trace, true);
  jrt::Array<int32_t>* last = fractureResult->endIslands(trace, false);
  if (first == nullptr || last == nullptr) {
    return true;
  }
  for (int32_t a : *first) {
    for (int32_t b : *last) {
      if (a == b) {
        return true;
      }
    }
  }
  return false;
}

void ConductionArea::addLaterReachedIslands(jrt::Set<Item*>* result, int32_t netNumber,
                                            bool stopAtPlane) {
  bool changed = true;
  while (changed) {
    changed = false;
    jrt::List<ConductionArea*>* fractured = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
    for (Item* item : *result) {
      auto* ca = jrt::fast_cast<ConductionArea>(item);
      if (ca != nullptr && ca->board != nullptr && ca->isFractured()) {
        fractured->add(ca);
      }
    }
    for (ConductionArea* ca : *fractured) {
      for (jrt::List<Item*>* island : *ca->getFractureResult()->islandItems->values()) {
        bool reached = false;
        for (Item* item : *island) {
          if (item != ca && result->contains(item)) {
            reached = true;
            break;
          }
        }
        if (!reached) {
          continue;
        }
        for (Item* item : *island) {
          if (item != ca && (netNumber <= 0 || item->containsNet(netNumber)) &&
              result->add(item)) {
            item->getConnectedSetRecu(result, netNumber, stopAtPlane);
            changed = true;
          }
        }
      }
    }
  }
}

bool ConductionArea::isCycleRecu(jrt::Set<Item*>* visitedItems, Item* searchItem,
                                 Item* comeFromItem, bool ignoreAreas) {
  if (ignoreAreas) {
    return false;
  }
  if (this->board == nullptr) {
    return ObstacleArea::isCycleRecu(visitedItems, searchItem, comeFromItem, ignoreAreas);
  }
  FastPlaneConnectivityGrid_PlaneFractureResult* fractureResult = getFractureResult();
  if (fractureResult == nullptr || !fractureResult->isFractured) {
    return ObstacleArea::isCycleRecu(visitedItems, searchItem, comeFromItem, ignoreAreas);
  }

  // The islands the walk enters from comeFromItem. The walk for a cycle through a trace starts
  // at its first corner: from the trace itself, only the islands of that end are entered, and the
  // trace closes a cycle if its last corner touches one of them.
  jrt::Set<int32_t>* fromIslands = nullptr;
  if (comeFromItem != nullptr) {
    jrt::Array<int32_t>* startIslands =
        comeFromItem == searchItem ? fractureResult->endIslands(comeFromItem, true) : nullptr;
    if (startIslands != nullptr) {
      fromIslands = new (jrt::kOp) jrt::HashSet<int32_t>();
      for (int32_t island : *startIslands) {
        fromIslands->add(island);
      }
      for (int32_t island : *fractureResult->endIslands(comeFromItem, false)) {
        if (fromIslands->contains(island)) {
          return true;
        }
      }
    } else {
      for (auto* entry : *fractureResult->islandItems->entrySet()) {
        if (entry->getValue()->contains(comeFromItem)) {
          if (fromIslands == nullptr) {
            fromIslands = new (jrt::kOp) jrt::HashSet<int32_t>();
          }
          fromIslands->add(entry->getKey());
        }
      }
      if (fromIslands == nullptr && fractureResult->traceEndIslands->containsKey(comeFromItem)) {
        // a trace whose ends touch no fill of this area
        return false;
      }
    }
  }

  jrt::Collection<Item*>* contactList = getNormalContacts();
  if (contactList == nullptr) {
    return false;
  }
  for (Item* currentContact : *contactList) {
    if (currentContact == comeFromItem) {
      continue;
    }
    if (fromIslands != nullptr) {
      bool onSameIsland = false;
      for (int32_t island : *fromIslands) {
        jrt::List<Item*>* islandMembers = fractureResult->islandItems->get(island);
        if (islandMembers != nullptr && islandMembers->contains(currentContact)) {
          onSameIsland = true;
          break;
        }
      }
      if (!onSameIsland) {
        continue;
      }
    }
    if (currentContact == searchItem) {
      return true;
    }
    if (visitedItems->add(currentContact)) {
      if (currentContact->isCycleRecu(visitedItems, searchItem, this, ignoreAreas)) {
        return true;
      }
    }
  }
  return false;
}

TileShape* ConductionArea::getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) {
  if (index < 0 || index >= this->treeShapeCount(searchTree)) {
    FRLogger::warn("ConductionArea.get_trace_connection_shape index out of range");
    return nullptr;
  }
  return this->getTreeShape(searchTree, index);
}

jrt::Array<Point*>* ConductionArea::getRatsnestCorners() {
  jrt::Array<Point*>* result;
  jrt::Array<FloatPoint*>* corners = this->getArea()->cornerApproxArr();
  result = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*result)[i] = (*corners)[i]->round();
  }

  return result;
}

bool ConductionArea::isObstacle(Item* other) {
  if (this->isObstacle_) {
    return ObstacleArea::isObstacle(other);
  }
  return false;
}

void ConductionArea::setIsObstacle(bool value) {
  if (this->isObstacle_ != value) {
    this->isObstacle_ = value;
    this->clearDerivedData();
  }
}

bool ConductionArea::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::CONDUCTION);
}

void ConductionArea::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("conductionArea"));
  this->printShapeInfo(printer, locale);
  this->printConnectableItemInfo(printer, locale);
  printer->newline();
}

}  // namespace fr
