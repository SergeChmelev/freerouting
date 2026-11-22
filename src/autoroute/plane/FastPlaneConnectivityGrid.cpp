#include "autoroute/plane/FastPlaneConnectivityGrid.h"

#include <cmath>

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/PadPlaneConnection.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr {

using jrt::Math;

namespace {
using IslandMap = jrt::Map<int32_t, jrt::List<Item*>*>;
using TraceEndMap = jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>;
using ModeArr = jrt::Array<PadPlaneConnection_Mode>;
using SpokeArr = jrt::Array<PadPlaneConnection_Spoke*>;

// Java `~0L`
constexpr int64_t ALL_ONES = ~static_cast<int64_t>(0);
}  // namespace

// ---------------------------------------------------------------------------------------------
// PlaneFractureResult
// ---------------------------------------------------------------------------------------------

FastPlaneConnectivityGrid_PlaneFractureResult::FastPlaneConnectivityGrid_PlaneFractureResult(
    jrt::Map<int32_t, jrt::List<Item*>*>* islandItems)
    : FastPlaneConnectivityGrid_PlaneFractureResult(islandItems, nullptr) {}

FastPlaneConnectivityGrid_PlaneFractureResult::FastPlaneConnectivityGrid_PlaneFractureResult(
    jrt::Map<int32_t, jrt::List<Item*>*>* islandItems, IntBox* dependencyBox)
    : FastPlaneConnectivityGrid_PlaneFractureResult(
          islandItems, dependencyBox, jrt::Collections::emptyMap<Item*, jrt::Array<jrt::Array<int32_t>*>*>()) {}

FastPlaneConnectivityGrid_PlaneFractureResult::FastPlaneConnectivityGrid_PlaneFractureResult(
    jrt::Map<int32_t, jrt::List<Item*>*>* islandItems,
    IntBox* dependencyBox,
    jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* traceEndIslands) {
  this->islandItems =
      islandItems != nullptr ? islandItems : jrt::Collections::emptyMap<int32_t, jrt::List<Item*>*>();
  this->traceEndIslands = traceEndIslands != nullptr
                              ? traceEndIslands
                              : jrt::Collections::emptyMap<Item*, jrt::Array<jrt::Array<int32_t>*>*>();
  this->islandCount = this->islandItems->size();
  this->isFractured = this->islandCount > 1;
  this->dependencyBox = dependencyBox;
}

jrt::Array<int32_t>* FastPlaneConnectivityGrid_PlaneFractureResult::endIslands(Item* trace, bool first) {
  jrt::Array<jrt::Array<int32_t>*>* ends = traceEndIslands->get(trace);
  return ends == nullptr ? nullptr : (*ends)[first ? 0 : 1];
}

// ---------------------------------------------------------------------------------------------
// FractureMemo
// ---------------------------------------------------------------------------------------------

bool FastPlaneConnectivityGrid_FractureMemo::restoreCopper(FastPlaneConnectivityGrid* grid, Area* area) {
  if (copperWords == nullptr || area == nullptr || area != copperArea || grid->originX != copperOriginX ||
      grid->originY != copperOriginY || grid->cellSize != copperCellSize || grid->cols != copperCols ||
      grid->rows != copperRows) {
    return false;
  }
  jrt::arraycopy(copperWords, 0, grid->copperMask, 0, copperWords->length);
  return true;
}

void FastPlaneConnectivityGrid_FractureMemo::storeCopper(FastPlaneConnectivityGrid* grid, Area* area) {
  if (area == nullptr) {
    return;
  }
  copperArea = area;
  copperOriginX = grid->originX;
  copperOriginY = grid->originY;
  copperCellSize = grid->cellSize;
  copperCols = grid->cols;
  copperRows = grid->rows;
  copperWords = jrt::Arrays::copyOf(grid->copperMask, grid->rows * grid->wordsPerRow);
}

jrt::Array<int64_t>* FastPlaneConnectivityGrid_FractureMemo::lookupObstacleAreas(
    FastPlaneConnectivityGrid* grid, jrt::Array<TileShape*>* shapes, jrt::Array<double>* clearances) {
  if (areasWords == nullptr || grid->originX != areasOriginX || grid->originY != areasOriginY ||
      grid->cellSize != areasCellSize || grid->cols != areasCols || grid->rows != areasRows ||
      shapes->length != areasShapes->length || !jrt::Arrays::equals(clearances, areasClearances)) {
    return nullptr;
  }
  for (int32_t i = 0; i < shapes->length; i++) {
    if ((*shapes)[i] != (*areasShapes)[i]) {
      return nullptr;
    }
  }
  return areasWords;
}

void FastPlaneConnectivityGrid_FractureMemo::storeObstacleAreas(
    FastPlaneConnectivityGrid* grid,
    jrt::Array<Item*>* items,
    jrt::Array<int32_t>* shapeIndices,
    jrt::Array<TileShape*>* shapes,
    jrt::Array<double>* clearances,
    jrt::Array<int64_t>* words) {
  areasItems = items;
  areasShapeIndices = shapeIndices;
  areasOriginX = grid->originX;
  areasOriginY = grid->originY;
  areasCellSize = grid->cellSize;
  areasCols = grid->cols;
  areasRows = grid->rows;
  areasShapes = shapes;
  areasClearances = clearances;
  areasWords = words;
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid_FractureMemo::lookupPartition(
    FastPlaneConnectivityGrid* grid, Area* area, jrt::Array<Item*>* items, jrt::Array<IntPoint*>* locations) {
  if (partitionResult == nullptr || area == nullptr || area != partitionArea ||
      grid->originX != partitionOriginX || grid->originY != partitionOriginY ||
      grid->cellSize != partitionCellSize || grid->cols != partitionCols || grid->rows != partitionRows ||
      items->length != partitionItems->length) {
    return nullptr;
  }
  for (int32_t i = 0; i < items->length; i++) {
    if ((*items)[i] != (*partitionItems)[i]) {
      return nullptr;
    }
  }
  if (!jrt::Arrays::equals(locations, partitionLocations)) {
    return nullptr;
  }
  int32_t words = grid->rows * grid->wordsPerRow;
  // Java: Arrays.equals(grid.obstacleMask, 0, words, partitionObstacleWords, 0, words)
  if (words > grid->obstacleMask->length || words > partitionObstacleWords->length) {
    throw jrt::ArrayIndexOutOfBoundsException("Arrays.equals: range out of bounds");
  }
  for (int32_t w = 0; w < words; w++) {
    if ((*grid->obstacleMask)[w] != (*partitionObstacleWords)[w]) {
      return nullptr;
    }
  }
  return partitionResult;
}

FastPlaneConnectivityGrid_FractureMemo* FastPlaneConnectivityGrid_FractureMemo::detach(Area* currentArea) {
  if (currentArea == nullptr) {
    return nullptr;
  }
  auto* detached = new (jrt::kOp) FastPlaneConnectivityGrid_FractureMemo();
  bool any = false;
  if (copperWords != nullptr && copperArea == currentArea) {
    detached->copperOriginX = copperOriginX;
    detached->copperOriginY = copperOriginY;
    detached->copperCellSize = copperCellSize;
    detached->copperCols = copperCols;
    detached->copperRows = copperRows;
    // never written after it is stored, so it can be shared
    detached->copperWords = copperWords;
    any = true;
  }
  if (areasWords != nullptr && areaShapesAreCurrent()) {
    detached->areasOriginX = areasOriginX;
    detached->areasOriginY = areasOriginY;
    detached->areasCellSize = areasCellSize;
    detached->areasCols = areasCols;
    detached->areasRows = areasRows;
    detached->areasItems = areasItems;
    detached->areasShapeIndices = areasShapeIndices;
    detached->areasShapes = areasShapes;
    detached->areasClearances = areasClearances;
    detached->areasWords = areasWords;
    any = true;
  }
  if (partitionResult != nullptr && partitionArea == currentArea) {
    detached->partitionOriginX = partitionOriginX;
    detached->partitionOriginY = partitionOriginY;
    detached->partitionCellSize = partitionCellSize;
    detached->partitionCols = partitionCols;
    detached->partitionRows = partitionRows;
    detached->partitionObstacleWords = partitionObstacleWords;
    detached->partitionItems = partitionItems;
    detached->partitionLocations = partitionLocations;
    detached->partitionResult = partitionResult;
    any = true;
  }
  return any ? detached : nullptr;
}

bool FastPlaneConnectivityGrid_FractureMemo::areaShapesAreCurrent() {
  for (int32_t i = 0; i < areasItems->length; i++) {
    Item* item = (*areasItems)[i];
    int32_t index = (*areasShapeIndices)[i];
    if (index >= item->tileShapeCount() || item->getTileShape(index) != (*areasShapes)[i]) {
      return false;
    }
  }
  return true;
}

FastPlaneConnectivityGrid_FractureMemo* FastPlaneConnectivityGrid_FractureMemo::attach(
    Area* copyArea, std::function<Item*(int32_t)> copyItemById) {
  if (copyArea == nullptr) {
    return nullptr;
  }
  auto* attached = new (jrt::kOp) FastPlaneConnectivityGrid_FractureMemo();
  if (copperWords != nullptr) {
    attached->copperArea = copyArea;
    attached->copperOriginX = copperOriginX;
    attached->copperOriginY = copperOriginY;
    attached->copperCellSize = copperCellSize;
    attached->copperCols = copperCols;
    attached->copperRows = copperRows;
    attached->copperWords = copperWords;
  }
  if (areasWords != nullptr) {
    auto* copiedShapes = new (jrt::kGeom) jrt::Array<TileShape*>(areasShapes->length);
    auto* copiedAreaItems = new (jrt::kOp) jrt::Array<Item*>(areasItems->length);
    bool complete = true;
    for (int32_t i = 0; i < areasItems->length && complete; i++) {
      Item* copy = copyItemById((*areasItems)[i]->getId());
      int32_t index = (*areasShapeIndices)[i];
      if (copy == nullptr || typeid(*copy) != typeid(*(*areasItems)[i]) || index >= copy->tileShapeCount()) {
        complete = false;
      } else {
        (*copiedAreaItems)[i] = copy;
        (*copiedShapes)[i] = copy->getTileShape(index);
      }
    }
    if (complete) {
      attached->areasOriginX = areasOriginX;
      attached->areasOriginY = areasOriginY;
      attached->areasCellSize = areasCellSize;
      attached->areasCols = areasCols;
      attached->areasRows = areasRows;
      attached->areasItems = copiedAreaItems;
      attached->areasShapeIndices = areasShapeIndices;
      attached->areasShapes = copiedShapes;
      attached->areasClearances = areasClearances;
      attached->areasWords = areasWords;
    }
  }
  if (partitionResult != nullptr) {
    auto* copies = new (jrt::kOp) jrt::IdentityHashMap<Item*, Item*>();
    auto* copiedItems = new (jrt::kOp) jrt::Array<Item*>(partitionItems->length);
    bool complete = true;
    for (int32_t i = 0; i < partitionItems->length && complete; i++) {
      Item* copy = copyItemById((*partitionItems)[i]->getId());
      if (copy == nullptr || typeid(*copy) != typeid(*(*partitionItems)[i])) {
        complete = false;
      } else {
        (*copiedItems)[i] = copy;
        copies->put((*partitionItems)[i], copy);
      }
    }
    auto* copiedIslands = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    for (auto* island : *partitionResult->islandItems->entrySet()) {
      if (!complete) {
        break;
      }
      auto* copiedIsland = new (jrt::kOp) jrt::ArrayList<Item*>(island->getValue()->size());
      for (Item* item : *island->getValue()) {
        Item* copy = copies->get(item);
        if (copy == nullptr) {
          complete = false;
          break;
        }
        copiedIsland->add(copy);
      }
      copiedIslands->put(island->getKey(), copiedIsland);
    }
    if (complete) {
      attached->partitionArea = copyArea;
      attached->partitionOriginX = partitionOriginX;
      attached->partitionOriginY = partitionOriginY;
      attached->partitionCellSize = partitionCellSize;
      attached->partitionCols = partitionCols;
      attached->partitionRows = partitionRows;
      attached->partitionObstacleWords = partitionObstacleWords;
      attached->partitionItems = copiedItems;
      attached->partitionLocations = partitionLocations;
      attached->partitionResult = new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(copiedIslands);
    }
  }
  return attached;
}

void FastPlaneConnectivityGrid_FractureMemo::storePartition(
    FastPlaneConnectivityGrid* grid,
    Area* area,
    jrt::Array<Item*>* items,
    jrt::Array<IntPoint*>* locations,
    FastPlaneConnectivityGrid_PlaneFractureResult* result) {
  if (area == nullptr) {
    return;
  }
  partitionArea = area;
  partitionOriginX = grid->originX;
  partitionOriginY = grid->originY;
  partitionCellSize = grid->cellSize;
  partitionCols = grid->cols;
  partitionRows = grid->rows;
  partitionObstacleWords = jrt::Arrays::copyOf(grid->obstacleMask, grid->rows * grid->wordsPerRow);
  partitionItems = items;
  partitionLocations = locations;
  partitionResult = result;
}

// ---------------------------------------------------------------------------------------------
// FastPlaneConnectivityGrid
// ---------------------------------------------------------------------------------------------

void FastPlaneConnectivityGrid::init(IntBox* bbox, int32_t requestedCellSize) {
  if (bbox == nullptr) {
    throw jrt::IllegalArgumentException("Bounding box must not be null");
  }
  this->originX = bbox->ll->x;
  this->originY = bbox->ll->y;

  int32_t width = Math::max(1, bbox->ur->x - bbox->ll->x);
  int32_t height = Math::max(1, bbox->ur->y - bbox->ll->y);

  int32_t maxAllowedCell = Math::max(1, Math::min(width, height) / 2);
  int32_t calculatedCellSize = Math::max(1, Math::min(requestedCellSize, maxAllowedCell));
  int32_t c = (width + calculatedCellSize - 1) / calculatedCellSize + 1;
  int32_t r = (height + calculatedCellSize - 1) / calculatedCellSize + 1;

  // Scale cell size up if grid would exceed MAX_GRID_DIM
  if (c > MAX_GRID_DIM || r > MAX_GRID_DIM) {
    double maxDim = Math::max(width, height);
    calculatedCellSize = jrt::d2i(Math::ceil(maxDim / (MAX_GRID_DIM - 2)));
    c = (width + calculatedCellSize - 1) / calculatedCellSize + 1;
    r = (height + calculatedCellSize - 1) / calculatedCellSize + 1;
  }

  this->cellSize = calculatedCellSize;
  this->cols = Math::min(MAX_GRID_DIM, c);
  this->rows = Math::min(MAX_GRID_DIM, r);
  this->wordsPerRow = (this->cols + 63) / 64;

  int32_t totalWords = this->rows * this->wordsPerRow;
  if (this->copperMask->length < totalWords) {
    this->copperMask = new (jrt::kOp) jrt::Array<int64_t>(totalWords);
    this->obstacleMask = new (jrt::kOp) jrt::Array<int64_t>(totalWords);
    this->visitedMask = new (jrt::kOp) jrt::Array<int64_t>(totalWords);
  } else {
    jrt::Arrays::fill<int64_t>(this->copperMask, 0, totalWords, 0);
    jrt::Arrays::fill<int64_t>(this->obstacleMask, 0, totalWords, 0);
    jrt::Arrays::fill<int64_t>(this->visitedMask, 0, totalWords, 0);
  }

  this->partitionBuffersReady = false;
}

void FastPlaneConnectivityGrid::preparePartitionBuffers() {
  if (this->partitionBuffersReady) {
    return;
  }
  int32_t totalCells = this->cols * this->rows;
  if (this->cellIslandMap->length < totalCells) {
    this->cellIslandMap = new (jrt::kOp) jrt::Array<int16_t>(totalCells);
  }
  jrt::Arrays::fill<int16_t>(this->cellIslandMap, 0, totalCells, static_cast<int16_t>(-1));

  int32_t maxQueueSize = this->cols * this->rows;
  if (this->queue->length < maxQueueSize) {
    this->queue = new (jrt::kOp) jrt::Array<int32_t>(maxQueueSize);
  }
  this->partitionBuffersReady = true;
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid::evaluateFracture(
    ConductionArea* ca, BasicBoard* board, jrt::Collection<Item*>* sameNetItems) {
  return evaluateFracture(ca, board, sameNetItems, nullptr);
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid::evaluateFracture(
    ConductionArea* ca,
    BasicBoard* board,
    jrt::Collection<Item*>* sameNetItems,
    FastPlaneConnectivityGrid_FractureMemo* memo) {
  if (ca == nullptr || board == nullptr || sameNetItems == nullptr || sameNetItems->isEmpty()) {
    return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(
        jrt::Collections::emptyMap<int32_t, jrt::List<Item*>*>());
  }

  if (sameNetItems->size() <= 1) {
    auto* singleIsland = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    singleIsland->put(0, new (jrt::kOp) jrt::ArrayList<Item*>(sameNetItems));
    return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(singleIsland);
  }

  IntBox* bbox = ca->boundingBox();
  int32_t netClassNo = ca->netCount() > 0 ? ca->getNetNumber(0) : 0;
  (void)netClassNo;
  int32_t clearanceClass = ca->clearanceClassIndex();
  int32_t layer = ca->getLayer();

  double baseClearance = board->clearanceValue(clearanceClass, clearanceClass, layer);
  if (baseClearance <= 0) {
    baseClearance = DEFAULT_CELL_SIZE;
  }
  double maxClearance = baseClearance;
  if (board->rules != nullptr && board->rules->clearanceMatrix != nullptr) {
    maxClearance = Math::max(
        maxClearance, static_cast<double>(board->rules->clearanceMatrix->maxValue(clearanceClass, layer)));
  }
  // the fill of the zone may keep further away than the clearance classes say
  int32_t clearanceInflate = jrt::d2i(Math::ceil(ca->fillKeepout(maxClearance)));
  IntBox* queryBbox = new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(bbox->ll->x - clearanceInflate, bbox->ll->y - clearanceInflate),
                                 new (jrt::kGeom) IntPoint(bbox->ur->x + clearanceInflate, bbox->ur->y + clearanceInflate));

  int32_t maxDim = Math::max(bbox->width(), bbox->height());
  int32_t desiredCell =
      jrt::l2i(Math::max(static_cast<int64_t>(MIN_CELL_SIZE), Math::round(baseClearance / 2.0)));
  if (maxDim > 0 && maxDim / 64 < desiredCell) {
    desiredCell = Math::max(MIN_CELL_SIZE, maxDim / 64);
  }
  init(bbox, desiredCell);

  // 1. Rasterize conduction area boundary
  Area* area = ca->getArea();
  if (memo == nullptr || !memo->restoreCopper(this, area)) {
    rasterizeConductionArea(ca);
    if (memo != nullptr) {
      memo->storeCopper(this, area);
    }
  }

  // 2. Query spatial index for obstacles overlapping conduction area
  jrt::Set<SearchTreeObject*>* overlaps = board->overlappingObjects(queryBbox, layer);
  bool hasForeignObstacles = false;
  auto* areaItems = new (jrt::kOp) jrt::ArrayList<Item*>();
  auto* areaShapeIndices = new (jrt::kOp) jrt::ArrayList<int32_t>();
  auto* areaShapes = new (jrt::kOp) jrt::ArrayList<TileShape*>();
  auto* areaClearances = new (jrt::kOp) jrt::ArrayList<double>();
  // With the fill rules of the design, a cell stands for the centre of a disc of fill as wide as
  // the minimum width: it is free when that centre keeps its distance to the foreign copper.
  bool exact = ca->getPadConnection().has_value();
  if (exact) {
    initFineMask();
  }
  for (SearchTreeObject* ob : *overlaps) {
    Item* item = jrt::fast_cast<Item>(ob);
    if (item == nullptr || item == ca || !item->sharesLayer(ca)) {
      continue;
    }

    // Foreign traces, vias, and blocking obstacles are obstacles
    bool isForeign = !item->sharesNet(ca);
    if (isForeign) {
      if (auto* foreignCa = jrt::fast_cast<ConductionArea>(item); foreignCa != nullptr && !foreignCa->getIsObstacle()) {
        continue;
      }
      hasForeignObstacles = true;
      int32_t itemClearanceClass = item->clearanceClassIndex();
      double clearance = ca->fillKeepout(board->clearanceValue(clearanceClass, itemClearanceClass, layer));

      if (exact) {
        double leniency = FILL_LENIENCY * ca->getFillMinWidth();
        auto* outline = jrt::fast_cast<BoardOutline>(item);
        if (outline != nullptr && ca->getFillEdgeClearance() > 0) {
          // the fill keeps its own distance to the edge of the board
          rasterizeOutline(outline, ca->getFillEdgeClearance() + ca->getFillMinWidth() / 2.0 - leniency);
        } else {
          rasterizeForeignItem(item, layer, clearance - leniency);
        }
      } else if (auto* trace = jrt::fast_cast<Trace>(item)) {
        int32_t radius = jrt::l2i(Math::round(trace->getHalfWidth() + clearance));
        if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
          jrt::Array<Point*>* corners = polylineTrace->polyline()->corners();
          for (int32_t i = 0; i < corners->length - 1; i++) {
            IntPoint* p1 = toIntPoint((*corners)[i]);
            IntPoint* p2 = toIntPoint((*corners)[i + 1]);
            rasterizeThickSegment(p1->x, p1->y, p2->x, p2->y, radius);
          }
        } else {
          IntPoint* p1 = toIntPoint(trace->firstCorner());
          IntPoint* p2 = toIntPoint(trace->lastCorner());
          rasterizeThickSegment(p1->x, p1->y, p2->x, p2->y, radius);
        }
      } else if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
        IntPoint* center = toIntPoint(drillItem->getCenter());
        IntBox* itemBbox = drillItem->boundingBox();
        int32_t radius =
            jrt::l2i(Math::round(Math::max(itemBbox->width(), itemBbox->height()) / 2.0 + clearance));
        rasterizeThickSegment(center->x, center->y, center->x, center->y, radius);
      } else if (jrt::fast_cast<BoardOutline>(item) != nullptr) {
        // the fill stops at the edge of the board: a plane drawn past the outline (an L-shaped
        // board under a rectangular zone) does not join its pieces outside the board
        double edgeKeepout =
            board->clearanceValue(clearanceClass, itemClearanceClass, layer) + ca->getFillMinWidth() / 2.0;
        for (int32_t i = 0; i < item->tileShapeCount(); i++) {
          if (item->shapeLayer(i) == layer) {
            TileShape* tileShape = item->getTileShape(i);
            if (tileShape != nullptr) {
              rasterizeObstacleShape(tileShape->enlarge(edgeKeepout + (cellSize + 1.0) / 2.0));
            }
          }
        }
      } else if (jrt::fast_cast<ObstacleArea>(item) != nullptr) {
        int32_t shapeCount = item->tileShapeCount();
        for (int32_t i = 0; i < shapeCount; i++) {
          if (item->shapeLayer(i) == layer) {
            TileShape* tileShape = item->getTileShape(i);
            if (tileShape != nullptr) {
              areaItems->add(item);
              areaShapeIndices->add(i);
              areaShapes->add(tileShape);
              areaClearances->add(clearance + (cellSize + 1.0) / 2.0);
            }
          }
        }
      }
    }
  }
  // The enlarged area shapes are the costly obstacles, and they rarely change: their raster is
  // kept apart, then merged into the obstacle mask (a union, so the order does not matter).
  rasterizeObstacleAreas(areaItems, areaShapeIndices, areaShapes, areaClearances, memo);

  if (ca->getPadConnection().has_value()) {
    // The design says how each pad is connected to the fill: a pad can be cut off the plane
    // without any foreign copper cutting the plane itself.
    return partitionItemsByPadConnection(ca, board, sameNetItems);
  }

  // Without foreign obstacles, only the area's own holes can split it.
  if (!hasForeignObstacles && (ca->getArea() == nullptr || ca->getArea()->getHoles()->length == 0)) {
    auto* singleIsland = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    singleIsland->put(0, new (jrt::kOp) jrt::ArrayList<Item*>(sameNetItems));
    return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(singleIsland);
  }

  // 3. Partition same-net items into connected islands using bitwise BFS
  if (memo == nullptr) {
    return partitionItemsByIsland(ca, sameNetItems);
  }
  jrt::Array<Item*>* items = sameNetItems->toArray();
  auto* locations = new (jrt::kGeom) jrt::Array<IntPoint*>(items->length);
  for (int32_t i = 0; i < items->length; i++) {
    (*locations)[i] = getItemLocation((*items)[i], ca);
  }
  FastPlaneConnectivityGrid_PlaneFractureResult* previous = memo->lookupPartition(this, area, items, locations);
  if (previous != nullptr) {
    return previous;
  }
  FastPlaneConnectivityGrid_PlaneFractureResult* result =
      partitionItemsByIsland(ca, sameNetItems, items, locations);
  memo->storePartition(this, area, items, locations, result);
  return result;
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid::partitionItemsByPadConnection(
    ConductionArea* ca, BasicBoard* board, jrt::Collection<Item*>* sameNetItems) {
  int32_t layer = ca->getLayer();
  auto* items = new (jrt::kOp) jrt::ArrayList<Item*>(sameNetItems);
  auto* indexOfItem = new (jrt::kOp) jrt::HashMap<Item*, int32_t>();
  for (int32_t i = 0; i < items->size(); i++) {
    indexOfItem->put(items->get(i), i);
  }

  double minWidth = ca->getFillMinWidth();
  // the far end of a spoke lies 5 um beyond the relief and half the minimum width
  double endSlack = board->communication->getResolution(Unit::UM) * 5.0;
  // PORT NOTE: Java's null entries of the Mode[] array are INHERITED (ordinal 0) here. Every test
  // below compares with THERMAL, NONE or SOLID only, so null and INHERITED behave the same.
  auto* modes = new (jrt::kOp) ModeArr(items->size());
  auto* spokes = new (jrt::kOp) jrt::Array<SpokeArr*>(items->size());
  double maxGap = ca->getThermalGap();
  for (int32_t i = 0; i < items->size(); i++) {
    auto* pin = jrt::fast_cast<Pin>(items->get(i));
    if (pin != nullptr && pin->getPlaneConnection() != nullptr) {
      PadPlaneConnection* connection = pin->getPlaneConnection();
      (*modes)[i] = connection->resolve(*ca->getPadConnection());
      if ((*modes)[i] == PadPlaneConnection_Mode::THERMAL) {
        (*spokes)[i] = connection->spokes(ca->getThermalGap(), ca->getThermalSpokeWidth(), minWidth, endSlack);
        maxGap = Math::max(maxGap, connection->gapOn(ca->getThermalGap()));
      }
    }
  }
  double maxKeepout = ca->fillKeepout(maxClearanceOf(ca, board));

  // The fill stops at the thermal gap of a pad of the net, and at the clearance of a pad it does
  // not connect: a row of such pads can close a passage as foreign copper does.
  for (int32_t i = 0; i < items->size(); i++) {
    if ((*modes)[i] != PadPlaneConnection_Mode::THERMAL && (*modes)[i] != PadPlaneConnection_Mode::NONE) {
      continue;
    }
    Pin* pin = static_cast<Pin*>(items->get(i));
    TileShape* padShape = pin->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
    if (padShape == nullptr) {
      continue;
    }
    double relief = (*modes)[i] == PadPlaneConnection_Mode::THERMAL
                        ? pin->getPlaneConnection()->gapOn(ca->getThermalGap()) + minWidth / 2.0
                        : ca->fillKeepout(
                              board->clearanceValue(ca->clearanceClassIndex(), pin->clearanceClassIndex(), layer));
    if (!rasterizeRoundPad(pin, layer, relief - FILL_LENIENCY * minWidth)) {
      rasterizeKeepout(padShape, relief - FILL_LENIENCY * minWidth);
    }
  }

  // The fill keeps half its minimum width inside the outline of the zone, as from any copper,
  // and follows the outline with its corners smoothed.
  rasterizeOutsideOf(ca->getFillOutline(), minWidth / 2.0 - FILL_LENIENCY * minWidth);

  foldFineMask();
  auto* pieceOfCell = new (jrt::kOp) jrt::Array<int32_t>(cols * rows);
  int32_t pieceCount = labelPieces(pieceOfCell)->length;
  // nodes: the pieces of the fill, then the items
  auto* parent = new (jrt::kOp) jrt::Array<int32_t>(pieceCount + items->size());
  for (int32_t i = 0; i < parent->length; i++) {
    (*parent)[i] = i;
  }
  for (int32_t i = 0; i < items->size(); i++) {
    Item* item = items->get(i);
    int32_t node = pieceCount + i;
    if ((*modes)[i] == PadPlaneConnection_Mode::NONE) {
      continue;
    }
    if ((*modes)[i] == PadPlaneConnection_Mode::SOLID) {
      attachSolidPad(ca, static_cast<Pin*>(item), board, maxKeepout, pieceOfCell, parent, node);
      continue;
    }
    if ((*modes)[i] == PadPlaneConnection_Mode::THERMAL) {
      attachThermalPad(ca, board, items, indexOfItem, modes, spokes, i, maxGap, maxKeepout, pieceOfCell, parent,
                       pieceCount);
      continue;
    }
    // A via is covered by the fill: it is attached to every piece whose copper reaches it, and
    // to none if the fill leaves it out. A trace is placed once the islands are known.
    if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
      IntBox* box = drillItem->boundingBox();
      TileShape* shape = drillItem->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
      if (shape != nullptr) {
        box = shape->boundingBox();
      }
      double reach = Math::min(box->width(), box->height()) / 2.0 + minWidth / 2.0;
      attachPiecesNear(drillItem->getCenter()->toFloat(), reach, pieceOfCell, parent, node);
    }
  }

  // islands: the groups of items other than traces, the largest first, then in the order of the
  // items
  auto* groups = new (jrt::kOp) jrt::LinkedHashMap<int32_t, jrt::List<Item*>*>();
  auto* traces = new (jrt::kOp) jrt::ArrayList<Trace*>();
  for (int32_t i = 0; i < items->size(); i++) {
    if (auto* trace = jrt::fast_cast<Trace>(items->get(i))) {
      traces->add(trace);
      continue;
    }
    groups
        ->computeIfAbsent(find(parent, pieceCount + i),
                          [](int32_t) -> jrt::List<Item*>* { return new (jrt::kOp) jrt::ArrayList<Item*>(); })
        ->add(items->get(i));
  }
  auto* roots = new (jrt::kOp) jrt::ArrayList<int32_t>(groups->keySet());
  int32_t largest = 0;
  for (int32_t i = 1; i < roots->size(); i++) {
    if (groups->get(roots->get(i))->size() > groups->get(roots->get(largest))->size()) {
      largest = i;
    }
  }
  auto* islands = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
  auto* islandOfRoot = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();
  if (!roots->isEmpty()) {
    islandOfRoot->put(roots->get(largest), islands->size());
    islands->put(islands->size(), groups->get(roots->get(largest)));
  }
  for (int32_t i = 0; i < roots->size(); i++) {
    if (i != largest) {
      islandOfRoot->put(roots->get(i), islands->size());
      islands->put(islands->size(), groups->get(roots->get(i)));
    }
  }
  // a piece of fill that no pad or via holds is an island of its own once a trace reaches it
  std::function<int32_t(int32_t)> islandOfPiece = [islandOfRoot, islands, parent](int32_t piece) -> int32_t {
    return islandOfRoot->computeIfAbsent(find(parent, piece), [islands](int32_t) -> int32_t {
      int32_t id = islands->size();
      islands->put(id, new (jrt::kOp) jrt::ArrayList<Item*>());
      return id;
    });
  };
  jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* traceEnds = attachTraces(
      ca,
      traces,
      islands,
      [this, minWidth, pieceOfCell, islandOfPiece](Trace* trace, Point* end) -> jrt::Set<int32_t>* {
        return islandsOf(piecesNear(end->toFloat(), trace->getHalfWidth() + minWidth / 2.0, pieceOfCell),
                         islandOfPiece);
      },
      [this, minWidth, pieceOfCell, islandOfPiece](Trace* trace) -> jrt::Set<int32_t>* {
        return islandsOf(piecesAlong(trace, minWidth, pieceOfCell), islandOfPiece);
      });
  // Everything read above lies within the area, its foreign copper and the reliefs and spokes of
  // the pads of the net, with the copper around them: the result holds while nothing changes
  // there.
  int32_t reach = jrt::d2i(Math::ceil(2.0 * maxKeepout + 2.0 * maxGap + 2.0 * minWidth + 2.0 * cellSize)) + 1;
  IntBox* box = ca->boundingBox();
  for (Item* item : *items) {
    box = box->union_(item->boundingBox());
  }
  IntBox* dependencyBox = new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(box->ll->x - reach, box->ll->y - reach),
                                     new (jrt::kGeom) IntPoint(box->ur->x + reach, box->ur->y + reach));
  return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(islands, dependencyBox, traceEnds);
}

double FastPlaneConnectivityGrid::maxClearanceOf(ConductionArea* ca, BasicBoard* board) {
  int32_t clearanceClass = ca->clearanceClassIndex();
  double result = board->clearanceValue(clearanceClass, clearanceClass, ca->getLayer());
  if (board->rules != nullptr && board->rules->clearanceMatrix != nullptr) {
    result = Math::max(result,
                       static_cast<double>(board->rules->clearanceMatrix->maxValue(clearanceClass, ca->getLayer())));
  }
  return result;
}

jrt::Array<int32_t>* FastPlaneConnectivityGrid::labelPieces(jrt::Array<int32_t>* pieceOfCell) {
  preparePartitionBuffers();
  // -1: fill not yet numbered, -2: no fill
  for (int32_t row = 0; row < rows; row++) {
    for (int32_t col = 0; col < cols; col++) {
      (*pieceOfCell)[row * cols + col] = isConductive(col, row) ? -1 : -2;
    }
  }
  // cells none of whose sample points is marked: two of them that are neighbours always touch
  jrt::Array<bool>* partial = partialCells();
  auto* sizes = new (jrt::kOp) jrt::ArrayList<int32_t>();
  // diagonal neighbours too: what separates two pieces is much thicker than a cell
  static const int32_t dc[] = {1, -1, 0, 0, 1, 1, -1, -1};
  static const int32_t dr[] = {0, 0, 1, -1, 1, -1, 1, -1};
  constexpr int32_t dcLength = 8;
  for (int32_t start = 0; start < cols * rows; start++) {
    if ((*pieceOfCell)[start] != -1) {
      continue;
    }
    int32_t piece = sizes->size();
    int32_t head = 0;
    int32_t tail = 0;
    (*queue)[tail++] = start;
    (*pieceOfCell)[start] = piece;
    while (head < tail) {
      int32_t cell = (*queue)[head++];
      int32_t r = cell / cols;
      int32_t c = cell - r * cols;
      for (int32_t i = 0; i < dcLength; i++) {
        int32_t nc = c + dc[i];
        int32_t nr = r + dr[i];
        if (nc < 0 || nc >= cols || nr < 0 || nr >= rows) {
          continue;
        }
        int32_t neighbour = nr * cols + nc;
        if ((*pieceOfCell)[neighbour] == -1 &&
            ((!(*partial)[cell] && !(*partial)[neighbour]) || cellsTouch(r, c, dr[i], dc[i]))) {
          (*pieceOfCell)[neighbour] = piece;
          (*queue)[tail++] = neighbour;
        }
      }
    }
    sizes->add(tail);
  }
  for (int32_t cell = 0; cell < cols * rows; cell++) {
    if ((*pieceOfCell)[cell] == -2) {
      (*pieceOfCell)[cell] = -1;
    }
  }
  auto* result = new (jrt::kOp) jrt::Array<int32_t>(sizes->size());
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = sizes->get(i);
  }
  return result;
}

jrt::Array<bool>* FastPlaneConnectivityGrid::partialCells() {
  if (partialCell->length < cols * rows) {
    partialCell = new (jrt::kOp) jrt::Array<bool>(cols * rows);
  } else {
    jrt::Arrays::fill<bool>(partialCell, 0, cols * rows, false);
  }
  for (int32_t row = 0; row < rows; row++) {
    for (int32_t word = 0; word < fineWordsPerRow; word++) {
      int64_t any = 0;
      for (int32_t k = 0; k < FINE; k++) {
        any |= (*fineMask)[(FINE * row + k) * fineWordsPerRow + word];
      }
      if (any == 0) {
        continue;
      }
      for (int32_t col = word * 64 / FINE; col < (word + 1) * 64 / FINE && col < cols; col++) {
        if ((any & jrt::shl(jrt::shl(static_cast<int64_t>(1), FINE) - 1, (col * FINE) & 63)) != 0) {
          (*partialCell)[row * cols + col] = true;
        }
      }
    }
  }
  return partialCell;
}

bool FastPlaneConnectivityGrid::cellsTouch(int32_t row, int32_t col, int32_t dr, int32_t dc) {
  if (dr != 0 && dc != 0) {
    int32_t fineRow = dr > 0 ? FINE * row + FINE - 1 : FINE * row;
    int32_t fineCol = dc > 0 ? FINE * col + FINE - 1 : FINE * col;
    return !isFineSet(fineRow, fineCol) && !isFineSet(fineRow + dr, fineCol + dc);
  }
  for (int32_t k = 0; k < FINE; k++) {
    // the sample point of the cell on the border, and the three of the neighbour next to it
    int32_t fineRow = dr == 0 ? FINE * row + k : (dr > 0 ? FINE * row + FINE - 1 : FINE * row);
    int32_t fineCol = dc == 0 ? FINE * col + k : (dc > 0 ? FINE * col + FINE - 1 : FINE * col);
    if (isFineSet(fineRow, fineCol)) {
      continue;
    }
    for (int32_t m = -1; m <= 1; m++) {
      int32_t otherRow = dr == 0 ? fineRow + m : fineRow + dr;
      int32_t otherCol = dc == 0 ? fineCol + m : fineCol + dc;
      // only the points of the neighbour itself
      if (dr == 0 && (otherRow < FINE * row || otherRow >= FINE * row + FINE)) {
        continue;
      }
      if (dc == 0 && (otherCol < FINE * col || otherCol >= FINE * col + FINE)) {
        continue;
      }
      if (!isFineSet(otherRow, otherCol)) {
        return true;
      }
    }
  }
  return false;
}

int32_t FastPlaneConnectivityGrid::find(jrt::Array<int32_t>* parent, int32_t node) {
  while ((*parent)[node] != node) {
    (*parent)[node] = (*parent)[(*parent)[node]];
    node = (*parent)[node];
  }
  return node;
}

void FastPlaneConnectivityGrid::union_(jrt::Array<int32_t>* parent, int32_t a, int32_t b) {
  int32_t rootA = find(parent, a);
  int32_t rootB = find(parent, b);
  if (rootA != rootB) {
    // the smaller index is the root, so that the result does not depend on the order of calls
    (*parent)[Math::max(rootA, rootB)] = Math::min(rootA, rootB);
  }
}

void FastPlaneConnectivityGrid::attachSolidPad(
    ConductionArea* ca,
    Pin* pin,
    BasicBoard* board,
    double maxKeepout,
    jrt::Array<int32_t>* pieceOfCell,
    jrt::Array<int32_t>* parent,
    int32_t node) {
  int32_t layer = ca->getLayer();
  TileShape* padShape = pin->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
  if (padShape == nullptr) {
    return;
  }
  double reach = cellSize * Math::sqrt(0.5) + 1.0;
  IntBox* box = padShape->boundingBox();
  int32_t startCol = Math::max(0, (box->ll->x - originX) / cellSize - 1);
  int32_t endCol = Math::min(cols - 1, (box->ur->x - originX) / cellSize + 1);
  int32_t startRow = Math::max(0, (box->ll->y - originY) / cellSize - 1);
  int32_t endRow = Math::min(rows - 1, (box->ur->y - originY) / cellSize + 1);
  bool attached = false;
  for (int32_t row = startRow; row <= endRow; row++) {
    for (int32_t col = startCol; col <= endCol; col++) {
      int32_t piece = (*pieceOfCell)[row * cols + col];
      if (piece < 0) {
        continue;
      }
      if (find(parent, piece) == find(parent, node)) {
        attached = true;
        continue;
      }
      FloatPoint* centre =
          new (jrt::kGeom) FloatPoint(originX + col * cellSize + cellSize / 2.0, originY + row * cellSize + cellSize / 2.0);
      if (padShape->distance(centre) <= reach) {
        union_(parent, node, piece);
        attached = true;
      }
    }
  }
  if (attached) {
    return;
  }

  double minWidth = ca->getFillMinWidth();
  double margin = maxKeepout + minWidth + 1.0;
  IntBox* query = new (jrt::kGeom) IntBox(
      new (jrt::kGeom) IntPoint(jrt::d2i(Math::floor(box->ll->x - margin)), jrt::d2i(Math::floor(box->ll->y - margin))),
      new (jrt::kGeom) IntPoint(jrt::d2i(Math::ceil(box->ur->x + margin)), jrt::d2i(Math::ceil(box->ur->y + margin))));
  auto* foreignShapes = new (jrt::kOp) jrt::ArrayList<TileShape*>();
  auto* foreignClearances = new (jrt::kOp) jrt::ArrayList<jrt::Array<double>*>();
  collectForeignCopper(ca, board, query, foreignShapes, foreignClearances, nullptr, nullptr, nullptr, -1);
  jrt::Array<FloatPoint*>* corners = padShape->cornerApproxArr();
  if (corners == nullptr || corners->length < 2) {
    return;
  }
  FloatPoint* inside = padShape->centreOfGravity();
  double perimeter = 0;
  for (int32_t i = 0; i < corners->length; i++) {
    perimeter += (*corners)[i]->distance((*corners)[(i + 1) % corners->length]);
  }
  double step = Math::max(Math::max(minWidth / 2.0, perimeter / 64.0), 1.0);
  for (int32_t i = 0; i < corners->length; i++) {
    FloatPoint* from = (*corners)[i];
    FloatPoint* to = (*corners)[(i + 1) % corners->length];
    double length = from->distance(to);
    if (length <= 0) {
      continue;
    }
    // the normal of the side that points away from the pad
    double outX = -(to->y - from->y) / length;
    double outY = (to->x - from->x) / length;
    if (outX * (inside->x - from->x) + outY * (inside->y - from->y) > 0) {
      outX = -outX;
      outY = -outY;
    }
    int32_t count = jrt::d2i(Math::ceil(length / step));
    for (int32_t k = 0; k <= count; k++) {
      double t = static_cast<double>(k) / count;
      FloatPoint* centre = new (jrt::kGeom) FloatPoint(from->x + t * (to->x - from->x) + outX * minWidth / 2.0,
                                          from->y + t * (to->y - from->y) + outY * minWidth / 2.0);
      // the centre of the disc must be a point of the fill: inside the outline as the fill
      // follows it, clear of the reliefs of the net and of foreign copper
      double fine = static_cast<double>(cellSize) / FINE;
      int32_t fineCol = jrt::d2i(Math::floor((centre->x - originX) / fine));
      int32_t fineRow = jrt::d2i(Math::floor((centre->y - originY) / fine));
      if (fineCol < 0 || fineRow < 0 || fineCol >= fineCols || fineRow >= fineRows || isFineSet(fineRow, fineCol) ||
          !isFree(centre, centre, 0, foreignShapes, foreignClearances)) {
        continue;
      }
      int32_t piece = (*pieceOfCell)[(fineRow / FINE) * cols + fineCol / FINE];
      if (piece >= 0) {
        union_(parent, node, piece);
      }
    }
  }
}

void FastPlaneConnectivityGrid::collectForeignCopper(
    ConductionArea* ca,
    BasicBoard* board,
    IntBox* query,
    jrt::List<TileShape*>* foreignShapes,
    jrt::List<jrt::Array<double>*>* foreignClearances,
    jrt::List<int32_t>* netPads,
    jrt::Map<Item*, int32_t>* indexOfItem,
    jrt::Array<PadPlaneConnection_Mode>* modes,
    int32_t ownIndex) {
  int32_t layer = ca->getLayer();
  int32_t clearanceClass = ca->clearanceClassIndex();
  for (SearchTreeObject* object : *board->overlappingObjects(query, layer)) {
    Item* item = jrt::fast_cast<Item>(object);
    if (item == nullptr || item == ca || !item->sharesLayer(ca)) {
      continue;
    }
    if (item->sharesNet(ca)) {
      if (netPads != nullptr) {
        std::optional<int32_t> other = indexOfItem->getOpt(item);
        if (other.has_value() && *other != ownIndex &&
            ((*modes)[*other] == PadPlaneConnection_Mode::THERMAL ||
             (*modes)[*other] == PadPlaneConnection_Mode::NONE)) {
          netPads->add(*other);
        }
      }
      continue;
    }
    if (auto* foreignCa = jrt::fast_cast<ConductionArea>(item); foreignCa != nullptr && !foreignCa->getIsObstacle()) {
      continue;
    }
    if (!(jrt::fast_cast<Trace>(item) != nullptr || jrt::fast_cast<DrillItem>(item) != nullptr ||
          jrt::fast_cast<ObstacleArea>(item) != nullptr)) {
      continue;
    }
    double clearance = board->clearanceValue(clearanceClass, item->clearanceClassIndex(), layer);
    jrt::Array<double>* distances = jrt::Array<double>::of({ca->fillKeepout(clearance)});
    for (int32_t i = 0; i < item->tileShapeCount(); i++) {
      if (item->shapeLayer(i) == layer && item->getTileShape(i) != nullptr) {
        foreignShapes->add(item->getTileShape(i));
        foreignClearances->add(distances);
      }
    }
  }
  if (netPads != nullptr) {
    // the same order on every run
    netPads->sort(nullptr);
  }
}

void FastPlaneConnectivityGrid::attachThermalPad(
    ConductionArea* ca,
    BasicBoard* board,
    jrt::List<Item*>* items,
    jrt::Map<Item*, int32_t>* indexOfItem,
    jrt::Array<PadPlaneConnection_Mode>* modes,
    jrt::Array<jrt::Array<PadPlaneConnection_Spoke*>*>* spokes,
    int32_t index,
    double maxGap,
    double maxKeepout,
    jrt::Array<int32_t>* pieceOfCell,
    jrt::Array<int32_t>* parent,
    int32_t pieceCount) {
  SpokeArr* own = (*spokes)[index];
  if (own == nullptr || own->length == 0) {
    return;
  }
  int32_t layer = ca->getLayer();
  int32_t clearanceClass = ca->clearanceClassIndex();
  double minWidth = ca->getFillMinWidth();

  // one query for the four spokes: the foreign copper and the pads of the net around them
  double minX = jrt::Double::MAX_VALUE;
  double minY = jrt::Double::MAX_VALUE;
  double maxX = -jrt::Double::MAX_VALUE;
  double maxY = -jrt::Double::MAX_VALUE;
  for (PadPlaneConnection_Spoke* spoke : *own) {
    minX = Math::min(minX, Math::min(spoke->startX(), spoke->endX()));
    minY = Math::min(minY, Math::min(spoke->startY(), spoke->endY()));
    maxX = Math::max(maxX, Math::max(spoke->startX(), spoke->endX()));
    maxY = Math::max(maxY, Math::max(spoke->startY(), spoke->endY()));
  }
  double margin = Math::max(maxKeepout, maxGap) + (*own)[0]->halfWidth() + 1.0;
  IntBox* query =
      new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(jrt::d2i(Math::floor(minX - margin)), jrt::d2i(Math::floor(minY - margin))),
                 new (jrt::kGeom) IntPoint(jrt::d2i(Math::ceil(maxX + margin)), jrt::d2i(Math::ceil(maxY + margin))));
  auto* foreignShapes = new (jrt::kOp) jrt::ArrayList<TileShape*>();
  auto* foreignClearances = new (jrt::kOp) jrt::ArrayList<jrt::Array<double>*>();
  auto* netPads = new (jrt::kOp) jrt::ArrayList<int32_t>();
  collectForeignCopper(ca, board, query, foreignShapes, foreignClearances, netPads, indexOfItem, modes, index);

  Pin* ownPin = static_cast<Pin*>(items->get(index));
  SpokeArr* ownWhole = wholeSpokes(ownPin, own);
  TileShape* ownShape = ownPin->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
  double ownGap = ownPin->getPlaneConnection()->gapOn(ca->getThermalGap());
  for (int32_t k = 0; k < own->length; k++) {
    PadPlaneConnection_Spoke* spoke = (*own)[k];
    FloatPoint* start = new (jrt::kGeom) FloatPoint(spoke->startX(), spoke->startY());
    FloatPoint* end = new (jrt::kGeom) FloatPoint(spoke->endX(), spoke->endY());
    // the relief of another pad of the net leaves no fill at the end of the spoke
    int32_t inReliefOf = -1;
    for (int32_t other : *netPads) {
      Pin* otherPin = static_cast<Pin*>(items->get(other));
      TileShape* otherShape = otherPin->getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
      if (otherShape == nullptr) {
        continue;
      }
      double relief = (*modes)[other] == PadPlaneConnection_Mode::THERMAL
                          ? otherPin->getPlaneConnection()->gapOn(ca->getThermalGap())
                          : static_cast<double>(Math::max(
                                board->clearanceValue(clearanceClass, otherPin->clearanceClassIndex(), layer),
                                ca->getFillClearance()));
      if (otherShape->distance(end) < relief) {
        inReliefOf = other;
        break;
      }
    }
    if (inReliefOf >= 0) {
      // Kept if a spoke of that pad ends in this spoke, and this one in that spoke. The two
      // spokes then join the two pads where they meet, if foreign copper leaves them that far.
      SpokeArr* theirs = (*spokes)[inReliefOf];
      if (theirs == nullptr) {
        continue;
      }
      SpokeArr* theirWhole = wholeSpokes(static_cast<Pin*>(items->get(inReliefOf)), theirs);
      for (int32_t m = 0; m < theirs->length; m++) {
        if (isInSpoke((*theirWhole)[m], end->x, end->y) &&
            isInSpoke((*ownWhole)[k], (*theirWhole)[m]->endX(), (*theirWhole)[m]->endY())) {
          if (spokesMeet(spoke, (*theirs)[m], minWidth, foreignShapes, foreignClearances)) {
            union_(parent, pieceCount + index, pieceCount + inReliefOf);
          }
          break;
        }
      }
      continue;
    }
    // the end first: it costs less to tell
    if (!isEndInFill(start, end, minWidth, ownShape, ownGap, foreignShapes, foreignClearances) ||
        !isStripFree(start, end, spoke->halfWidth(), minWidth, ownShape, ownGap, foreignShapes,
                     foreignClearances)) {
      continue;
    }
    IntPoint* endPoint = end->round();
    if (ca->getArea() == nullptr || !ca->getArea()->contains(static_cast<Point*>(endPoint))) {
      continue;
    }
    // the piece of the fill the end lies in; a piece too small for the grid connects nothing
    attachPiecesNear(end, minWidth / 2.0 + FILL_LENIENCY * minWidth, pieceOfCell, parent, pieceCount + index);
  }
}

void FastPlaneConnectivityGrid::attachPiecesNear(
    FloatPoint* point, double reach, jrt::Array<int32_t>* pieceOfCell, jrt::Array<int32_t>* parent, int32_t node) {
  double fine = static_cast<double>(cellSize) / FINE;
  // the sample points are that far apart
  double limit = reach + fine * 0.75;
  int32_t startCol = Math::max(0, jrt::d2i(Math::floor((point->x - limit - originX) / cellSize)));
  int32_t endCol = Math::min(cols - 1, jrt::d2i(Math::floor((point->x + limit - originX) / cellSize)));
  int32_t startRow = Math::max(0, jrt::d2i(Math::floor((point->y - limit - originY) / cellSize)));
  int32_t endRow = Math::min(rows - 1, jrt::d2i(Math::floor((point->y + limit - originY) / cellSize)));
  int32_t root = find(parent, node);
  for (int32_t row = startRow; row <= endRow; row++) {
    for (int32_t col = startCol; col <= endCol; col++) {
      int32_t piece = (*pieceOfCell)[row * cols + col];
      // a piece the node is already joined with needs no second look
      if (piece >= 0 && find(parent, piece) != root && hasFreeSampleNear(row, col, point, limit, fine)) {
        union_(parent, node, piece);
        root = find(parent, node);
      }
    }
  }
}

bool FastPlaneConnectivityGrid::hasFreeSampleNear(int32_t row, int32_t col, FloatPoint* point, double limit,
                                                  double fine) {
  for (int32_t fineRow = FINE * row; fineRow < FINE * row + FINE; fineRow++) {
    double dy = originY + (fineRow + 0.5) * fine - point->y;
    for (int32_t fineCol = FINE * col; fineCol < FINE * col + FINE; fineCol++) {
      double dx = originX + (fineCol + 0.5) * fine - point->x;
      if (dx * dx + dy * dy <= limit * limit && !isFineSet(fineRow, fineCol)) {
        return true;
      }
    }
  }
  return false;
}

jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* FastPlaneConnectivityGrid::attachTraces(
    ConductionArea* ca,
    jrt::List<Trace*>* traces,
    jrt::Map<int32_t, jrt::List<Item*>*>* islands,
    std::function<jrt::Set<int32_t>*(Trace*, Point*)> endIslands,
    std::function<jrt::Set<int32_t>*(Trace*)> wayIslands) {
  if (traces->isEmpty()) {
    return jrt::Collections::emptyMap<Item*, jrt::Array<jrt::Array<int32_t>*>*>();
  }
  jrt::Map<Item*, jrt::List<int32_t>*>* islandsOfItem = islandsOfItems(islands);
  auto* result = new (jrt::kOp) jrt::HashMap<Item*, jrt::Array<jrt::Array<int32_t>*>*>();
  for (Trace* trace : *traces) {
    auto* ends = new (jrt::kOp) jrt::Array<jrt::Array<int32_t>*>(2);
    auto* all = new (jrt::kOp) jrt::TreeSet<int32_t>();
    for (int32_t e = 0; e < 2; e++) {
      Point* end = e == 0 ? trace->firstCorner() : trace->lastCorner();
      auto* touched = new (jrt::kOp) jrt::TreeSet<int32_t>();
      if (end != nullptr && ca->getArea() != nullptr && ca->getArea()->contains(end)) {
        DrillItem* drill = drillItemAt(end, islandsOfItem->keySet());
        if (drill != nullptr) {
          touched->addAll(islandsOfItem->get(drill));
        } else {
          touched->addAll(endIslands(trace, end));
        }
      }
      (*ends)[e] = new (jrt::kOp) jrt::Array<int32_t>(touched->toVector());
      all->addAll(touched);
    }
    all->addAll(wayIslands(trace));
    for (int32_t island : *all) {
      islands->get(island)->add(trace);
    }
    result->put(trace, ends);
  }
  return result;
}

jrt::Set<int32_t>* FastPlaneConnectivityGrid::islandsOf(jrt::Set<int32_t>* pieces,
                                                        std::function<int32_t(int32_t)> islandOfPiece) {
  auto* result = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t piece : *pieces) {
    result->add(islandOfPiece(piece));
  }
  return result;
}

jrt::Map<Item*, jrt::List<int32_t>*>* FastPlaneConnectivityGrid::islandsOfItems(
    jrt::Map<int32_t, jrt::List<Item*>*>* islands) {
  auto* result = new (jrt::kOp) jrt::HashMap<Item*, jrt::List<int32_t>*>();
  for (auto* entry : *islands->entrySet()) {
    for (Item* item : *entry->getValue()) {
      result->computeIfAbsent(item, [](Item*) -> jrt::List<int32_t>* { return new (jrt::kOp) jrt::ArrayList<int32_t>(); })
          ->add(entry->getKey());
    }
  }
  return result;
}

DrillItem* FastPlaneConnectivityGrid::drillItemAt(Point* point, jrt::Collection<Item*>* items) {
  DrillItem* centred = nullptr;
  DrillItem* containing = nullptr;
  IntPoint* p = toIntPoint(point);
  for (Item* item : *items) {
    auto* drill = jrt::fast_cast<DrillItem>(item);
    if (drill == nullptr) {
      continue;
    }
    if (point->equals(drill->getCenter())) {
      if (centred == nullptr || drill->getId() < centred->getId()) {
        centred = drill;
      }
    } else if (drill->boundingBox()->contains(static_cast<Point*>(p)) &&
               (containing == nullptr || drill->getId() < containing->getId())) {
      containing = drill;
    }
  }
  return centred != nullptr ? centred : containing;
}

jrt::TreeSet<int32_t>* FastPlaneConnectivityGrid::piecesNear(FloatPoint* point, double reach,
                                                            jrt::Array<int32_t>* pieceOfCell) {
  double fine = static_cast<double>(cellSize) / FINE;
  // the sample points are that far apart
  double limit = reach + fine * 0.75;
  int32_t startCol = Math::max(0, jrt::d2i(Math::floor((point->x - limit - originX) / cellSize)));
  int32_t endCol = Math::min(cols - 1, jrt::d2i(Math::floor((point->x + limit - originX) / cellSize)));
  int32_t startRow = Math::max(0, jrt::d2i(Math::floor((point->y - limit - originY) / cellSize)));
  int32_t endRow = Math::min(rows - 1, jrt::d2i(Math::floor((point->y + limit - originY) / cellSize)));
  auto* result = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t row = startRow; row <= endRow; row++) {
    for (int32_t col = startCol; col <= endCol; col++) {
      int32_t piece = (*pieceOfCell)[row * cols + col];
      if (piece >= 0 && !result->contains(piece) && hasFreeSampleNear(row, col, point, limit, fine)) {
        result->add(piece);
      }
    }
  }
  return result;
}

jrt::TreeSet<int32_t>* FastPlaneConnectivityGrid::piecesAlong(Trace* trace, double minWidth,
                                                             jrt::Array<int32_t>* pieceOfCell) {
  jrt::Array<Point*>* corners;
  if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
    corners = polylineTrace->polyline()->corners();
  } else {
    corners = jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()});
  }
  double fine = static_cast<double>(cellSize) / FINE;
  // the sample points are that far apart
  double limit = trace->getHalfWidth() + minWidth / 2.0 + fine * 0.75;
  double halfDiagonal = cellSize * Math::sqrt(0.5);
  auto* result = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t i = 0; i < Math::max(1, corners->length - 1); i++) {
    FloatPoint* from = (*corners)[i]->toFloat();
    FloatPoint* to = (*corners)[Math::min(i + 1, corners->length - 1)]->toFloat();
    int32_t startCol = Math::max(0, jrt::d2i(Math::floor((Math::min(from->x, to->x) - limit - originX) / cellSize)));
    int32_t endCol =
        Math::min(cols - 1, jrt::d2i(Math::floor((Math::max(from->x, to->x) + limit - originX) / cellSize)));
    int32_t startRow = Math::max(0, jrt::d2i(Math::floor((Math::min(from->y, to->y) - limit - originY) / cellSize)));
    int32_t endRow =
        Math::min(rows - 1, jrt::d2i(Math::floor((Math::max(from->y, to->y) + limit - originY) / cellSize)));
    for (int32_t r = startRow; r <= endRow; r++) {
      for (int32_t c = startCol; c <= endCol; c++) {
        int32_t piece = (*pieceOfCell)[r * cols + c];
        if (piece < 0 || result->contains(piece)) {
          continue;
        }
        FloatPoint* centre = new (jrt::kGeom) FloatPoint(originX + (c + 0.5) * cellSize, originY + (r + 0.5) * cellSize);
        if (pointSegmentDistance(centre, from, to) > limit + halfDiagonal ||
            !hasFreeSampleNearSegment(r, c, from, to, limit, fine)) {
          continue;
        }
        result->add(piece);
      }
    }
  }
  return result;
}

bool FastPlaneConnectivityGrid::hasFreeSampleNearSegment(int32_t row, int32_t col, FloatPoint* from, FloatPoint* to,
                                                         double limit, double fine) {
  for (int32_t fineRow = FINE * row; fineRow < FINE * row + FINE; fineRow++) {
    double y = originY + (fineRow + 0.5) * fine;
    for (int32_t fineCol = FINE * col; fineCol < FINE * col + FINE; fineCol++) {
      if (isFineSet(fineRow, fineCol)) {
        continue;
      }
      FloatPoint* sample = new (jrt::kGeom) FloatPoint(originX + (fineCol + 0.5) * fine, y);
      if (pointSegmentDistance(sample, from, to) <= limit) {
        return true;
      }
    }
  }
  return false;
}

void FastPlaneConnectivityGrid::initFineMask() {
  this->fineCols = FINE * this->cols;
  this->fineRows = FINE * this->rows;
  this->fineWordsPerRow = (this->fineCols + 63) / 64;
  int32_t words = this->fineRows * this->fineWordsPerRow;
  if (this->fineMask->length < words) {
    this->fineMask = new (jrt::kOp) jrt::Array<int64_t>(words);
  } else {
    jrt::Arrays::fill<int64_t>(this->fineMask, 0, words, 0);
  }
}

void FastPlaneConnectivityGrid::rasterizeOutsideOf(Area* area, double reach) {
  if (area == nullptr) {
    return;
  }
  jrt::Array<TileShape*>* tiles = area->splitToConvex();
  if (tiles == nullptr) {
    return;
  }
  auto* inside = new (jrt::kOp) jrt::Array<int64_t>(fineRows * fineWordsPerRow);
  for (TileShape* tile : *tiles) {
    jrt::Array<FloatPoint*>* corners = tile == nullptr ? nullptr : tile->cornerApproxArr();
    if (corners != nullptr && corners->length > 2) {
      rasterizeConvex(corners, 0.0, inside);
    }
  }
  for (int32_t word = 0; word < inside->length; word++) {
    (*fineMask)[word] |= ~(*inside)[word];
  }
  if (reach <= 0) {
    return;
  }
  auto* rims = new (jrt::kOp) jrt::ArrayList<Shape*>();
  rims->add(area->getBorder());
  for (Shape* hole : *area->getHoles()) {
    rims->add(hole);
  }
  for (Shape* rim : *rims) {
    auto* polylineShape = jrt::fast_cast<PolylineShape>(rim);
    if (polylineShape == nullptr) {
      continue;
    }
    jrt::Array<FloatPoint*>* corners = polylineShape->cornerApproxArr();
    for (int32_t k = 0; corners != nullptr && k < corners->length; k++) {
      rasterizeSegment((*corners)[k], (*corners)[(k + 1) % corners->length], reach);
    }
  }
}

void FastPlaneConnectivityGrid::setFineSpan(int32_t row, int32_t startCol, int32_t endCol) {
  setFineSpan(fineMask, row, startCol, endCol);
}

void FastPlaneConnectivityGrid::setFineSpan(jrt::Array<int64_t>* mask, int32_t row, int32_t startCol,
                                            int32_t endCol) {
  if (row < 0 || row >= fineRows) {
    return;
  }
  int32_t from = Math::max(0, startCol);
  int32_t to = Math::min(fineCols - 1, endCol);
  if (from > to) {
    return;
  }
  int32_t startWord = row * fineWordsPerRow + (from >> 6);
  int32_t endWord = row * fineWordsPerRow + (to >> 6);
  int64_t startBits = jrt::shl(ALL_ONES, from & 63);
  int64_t endBits = jrt::ushr(ALL_ONES, 63 - (to & 63));
  if (startWord == endWord) {
    (*mask)[startWord] |= startBits & endBits;
    return;
  }
  (*mask)[startWord] |= startBits;
  for (int32_t word = startWord + 1; word < endWord; word++) {
    (*mask)[word] = ALL_ONES;
  }
  (*mask)[endWord] |= endBits;
}

bool FastPlaneConnectivityGrid::isFineSet(int32_t row, int32_t col) {
  return ((*fineMask)[row * fineWordsPerRow + (col >> 6)] & jrt::shl(static_cast<int64_t>(1), col & 63)) != 0;
}

void FastPlaneConnectivityGrid::foldFineMask() {
  for (int32_t row = 0; row < rows; row++) {
    for (int32_t word = 0; word < fineWordsPerRow; word++) {
      int64_t all = ALL_ONES;
      for (int32_t k = 0; k < FINE; k++) {
        all &= (*fineMask)[(FINE * row + k) * fineWordsPerRow + word];
      }
      if (all == 0) {
        continue;
      }
      for (int32_t col = word * 64 / FINE; col < (word + 1) * 64 / FINE && col < cols; col++) {
        int64_t cell = jrt::shl(jrt::shl(static_cast<int64_t>(1), FINE) - 1, (col * FINE) & 63);
        if ((all & cell) == cell) {
          setBit(obstacleMask, row, col);
        }
      }
    }
  }
}

void FastPlaneConnectivityGrid::rasterizeForeignItem(Item* item, int32_t layer, double distance) {
  if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
    double reach = trace->getHalfWidth() + distance;
    jrt::Array<Point*>* corners = trace->polyline()->corners();
    for (int32_t i = 0; i < corners->length - 1; i++) {
      rasterizeSegment((*corners)[i]->toFloat(), (*corners)[i + 1]->toFloat(), reach);
    }
    return;
  }
  if (auto* drillItem = jrt::fast_cast<DrillItem>(item); drillItem != nullptr && rasterizeRoundPad(drillItem, layer, distance)) {
    return;
  }
  for (int32_t i = 0; i < item->tileShapeCount(); i++) {
    if (item->shapeLayer(i) == layer && item->getTileShape(i) != nullptr) {
      rasterizeKeepout(item->getTileShape(i), distance);
    }
  }
}

bool FastPlaneConnectivityGrid::rasterizeRoundPad(DrillItem* drillItem, int32_t layer, double distance) {
  if (auto* circle = jrt::fast_cast<Circle>(drillItem->getShapeOnLayer(layer))) {
    rasterizeConvex(jrt::Array<FloatPoint*>::of({circle->center->toFloat()}), circle->radius + distance);
    return true;
  }
  return false;
}

void FastPlaneConnectivityGrid::rasterizeOutline(BoardOutline* outline, double distance) {
  for (int32_t i = 0; i < outline->shapeCount(); i++) {
    jrt::Array<FloatPoint*>* corners = outline->getShape(i)->cornerApproxArr();
    if (corners == nullptr) {
      continue;
    }
    for (int32_t k = 0; k < corners->length; k++) {
      rasterizeSegment((*corners)[k], (*corners)[(k + 1) % corners->length], distance);
    }
  }
}

void FastPlaneConnectivityGrid::rasterizeSegment(FloatPoint* from, FloatPoint* to, double reach) {
  rasterizeConvex(jrt::Array<FloatPoint*>::of({from, to}), reach);
}

void FastPlaneConnectivityGrid::rasterizeKeepout(TileShape* shape, double distance) {
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  if (corners == nullptr || corners->length == 0) {
    return;
  }
  rasterizeConvex(corners, distance);
}

void FastPlaneConnectivityGrid::rasterizeConvex(jrt::Array<FloatPoint*>* corners, double reach) {
  rasterizeConvex(corners, reach, fineMask);
}

void FastPlaneConnectivityGrid::rasterizeConvex(jrt::Array<FloatPoint*>* corners, double reach,
                                                jrt::Array<int64_t>* mask) {
  double fine = static_cast<double>(cellSize) / FINE;
  double minY = jrt::Double::MAX_VALUE;
  double maxY = -jrt::Double::MAX_VALUE;
  for (FloatPoint* corner : *corners) {
    minY = Math::min(minY, corner->y);
    maxY = Math::max(maxY, corner->y);
  }
  int32_t startRow = Math::max(0, jrt::d2i(Math::floor((minY - reach - originY) / fine)));
  int32_t endRow = Math::min(fineRows - 1, jrt::d2i(Math::floor((maxY + reach - originY) / fine)));
  int32_t sides = corners->length > 2 ? corners->length : corners->length - 1;
  // the corners of the band around each side
  auto* bandX = jrt::newArray2<double>(Math::max(sides, 0), 4);
  auto* bandY = jrt::newArray2<double>(Math::max(sides, 0), 4);
  auto* hasBand = new (jrt::kOp) jrt::Array<bool>(Math::max(sides, 0));
  for (int32_t i = 0; i < sides; i++) {
    FloatPoint* from = (*corners)[i];
    FloatPoint* to = (*corners)[(i + 1) % corners->length];
    double dx = to->x - from->x;
    double dy = to->y - from->y;
    double length = Math::hypot(dx, dy);
    if (length <= 0) {
      continue;
    }
    (*hasBand)[i] = true;
    double offsetX = -dy / length * reach;
    double offsetY = dx / length * reach;
    jrt::Array<double>& bx = *(*bandX)[i];
    jrt::Array<double>& by = *(*bandY)[i];
    bx[0] = from->x + offsetX;
    by[0] = from->y + offsetY;
    bx[1] = to->x + offsetX;
    by[1] = to->y + offsetY;
    bx[2] = to->x - offsetX;
    by[2] = to->y - offsetY;
    bx[3] = from->x - offsetX;
    by[3] = from->y - offsetY;
  }
  for (int32_t row = startRow; row <= endRow; row++) {
    double y = originY + (row + 0.5) * fine;
    double low = jrt::Double::MAX_VALUE;
    double high = -jrt::Double::MAX_VALUE;
    for (FloatPoint* corner : *corners) {
      double offset = y - corner->y;
      if (Math::abs(offset) < reach) {
        double halfChord = Math::sqrt(reach * reach - offset * offset);
        low = Math::min(low, corner->x - halfChord);
        high = Math::max(high, corner->x + halfChord);
      }
    }
    for (int32_t i = 0; i < sides; i++) {
      if (!(*hasBand)[i]) {
        continue;
      }
      jrt::Array<double>& bx = *(*bandX)[i];
      jrt::Array<double>& by = *(*bandY)[i];
      for (int32_t k = 0; k < 4; k++) {
        double y1 = by[k];
        double y2 = by[(k + 1) % 4];
        if ((y1 - y) * (y2 - y) <= 0 && y1 != y2) {
          double x = bx[k] + (y - y1) * (bx[(k + 1) % 4] - bx[k]) / (y2 - y1);
          low = Math::min(low, x);
          high = Math::max(high, x);
        }
      }
    }
    if (low < high) {
      setFineSpan(mask, row, jrt::d2i(Math::ceil((low - originX) / fine - 0.5)),
                  jrt::d2i(Math::floor((high - originX) / fine - 0.5)));
    }
  }
}

bool FastPlaneConnectivityGrid::spokesMeet(
    PadPlaneConnection_Spoke* own,
    PadPlaneConnection_Spoke* their,
    double minWidth,
    jrt::List<TileShape*>* foreignShapes,
    jrt::List<jrt::Array<double>*>* foreignClearances) {
  jrt::Array<double>* ownOffsets = stripOffsets(own->halfWidth(), minWidth);
  jrt::Array<double>* theirOffsets = stripOffsets(their->halfWidth(), minWidth);
  for (double ownOffset : *ownOffsets) {
    jrt::Array<FloatPoint*>* a = shifted(own, ownOffset);
    for (double theirOffset : *theirOffsets) {
      jrt::Array<FloatPoint*>* b = shifted(their, theirOffset);
      double dx = (*a)[1]->x - (*a)[0]->x;
      double dy = (*a)[1]->y - (*a)[0]->y;
      double ex = (*b)[1]->x - (*b)[0]->x;
      double ey = (*b)[1]->y - (*b)[0]->y;
      double denominator = dx * ey - dy * ex;
      FloatPoint* meet;
      if (Math::abs(denominator) > 1e-6 * Math::hypot(dx, dy) * Math::hypot(ex, ey)) {
        double qx = (*b)[0]->x - (*a)[0]->x;
        double qy = (*b)[0]->y - (*a)[0]->y;
        double t = (qx * ey - qy * ex) / denominator;
        double u = (qx * dy - qy * dx) / denominator;
        if (t > 1 || u > 1) {
          continue;
        }
        // a crossing under a pad: the strips already touch at the edges of the pads
        t = Math::max(0.0, t);
        meet = new (jrt::kGeom) FloatPoint((*a)[0]->x + t * dx, (*a)[0]->y + t * dy);
      } else {
        meet = new (jrt::kGeom) FloatPoint(((*a)[0]->x + (*b)[0]->x) / 2.0, ((*a)[0]->y + (*b)[0]->y) / 2.0);
        double length = Math::hypot(dx, dy);
        double across = length > 0 ? Math::abs((meet->x - (*a)[0]->x) * dy - (meet->y - (*a)[0]->y) * dx) / length : 0;
        if (across > 1.0) {
          // parallel strips that are not on one line
          continue;
        }
      }
      if (isFree((*a)[0], meet, 0, foreignShapes, foreignClearances) &&
          isFree((*b)[0], meet, 0, foreignShapes, foreignClearances)) {
        return true;
      }
    }
  }
  return false;
}

jrt::Array<double>* FastPlaneConnectivityGrid::stripOffsets(double halfWidth, double minWidth) {
  double side = Math::max(0.0, halfWidth - minWidth / 2.0);
  return side > 0 ? jrt::Array<double>::of({0, side, -side}) : jrt::Array<double>::of({0});
}

jrt::Array<FloatPoint*>* FastPlaneConnectivityGrid::shifted(PadPlaneConnection_Spoke* spoke, double offset) {
  double dx = spoke->endX() - spoke->startX();
  double dy = spoke->endY() - spoke->startY();
  double length = Math::hypot(dx, dy);
  double shiftX = length > 0 ? -dy / length * offset : 0;
  double shiftY = length > 0 ? dx / length * offset : 0;
  return jrt::Array<FloatPoint*>::of({new (jrt::kGeom) FloatPoint(spoke->startX() + shiftX, spoke->startY() + shiftY),
                                      new (jrt::kGeom) FloatPoint(spoke->endX() + shiftX, spoke->endY() + shiftY)});
}

jrt::Array<PadPlaneConnection_Spoke*>* FastPlaneConnectivityGrid::wholeSpokes(
    Pin* pin, jrt::Array<PadPlaneConnection_Spoke*>* fromEdge) {
  PadPlaneConnection* connection = pin->getPlaneConnection();
  auto* result = new (jrt::kOp) SpokeArr(fromEdge->length);
  for (int32_t i = 0; i < fromEdge->length; i++) {
    (*result)[i] = new (jrt::kBoard) PadPlaneConnection_Spoke(connection->centerX(), connection->centerY(), (*fromEdge)[i]->endX(),
                                                (*fromEdge)[i]->endY(), (*fromEdge)[i]->halfWidth());
  }
  return result;
}

bool FastPlaneConnectivityGrid::isInSpoke(PadPlaneConnection_Spoke* spoke, double x, double y) {
  double dx = spoke->endX() - spoke->startX();
  double dy = spoke->endY() - spoke->startY();
  double length = Math::hypot(dx, dy);
  if (length <= 0) {
    return false;
  }
  double along = ((x - spoke->startX()) * dx + (y - spoke->startY()) * dy) / length;
  double across = Math::abs((x - spoke->startX()) * dy - (y - spoke->startY()) * dx) / length;
  return along >= -1.0 && along <= length + 1.0 && across <= spoke->halfWidth() + 1.0;
}

bool FastPlaneConnectivityGrid::isStripFree(
    FloatPoint* start,
    FloatPoint* end,
    double halfWidth,
    double minWidth,
    TileShape* padShape,
    double gap,
    jrt::List<TileShape*>* foreignShapes,
    jrt::List<jrt::Array<double>*>* foreignClearances) {
  if (foreignShapes->isEmpty()) {
    return true;
  }
  double length = start->distance(end);
  for (double offset : *stripOffsets(halfWidth, minWidth)) {
    double shiftX = length > 0 ? -(end->y - start->y) / length * offset : 0;
    double shiftY = length > 0 ? (end->x - start->x) / length * offset : 0;
    FloatPoint* from = new (jrt::kGeom) FloatPoint(start->x + shiftX, start->y + shiftY);
    FloatPoint* to = new (jrt::kGeom) FloatPoint(end->x + shiftX, end->y + shiftY);
    if (isFree(from, to, 0, foreignShapes, foreignClearances)) {
      return true;
    }
  }
  double side = halfWidth - minWidth / 2.0;
  if (side < 0 || length <= 0 || minWidth <= 0 || padShape == nullptr) {
    return false;
  }
  // Points every eighth of the minimum width, as centres of a disc of that width. Within the
  // spoke the disc must stay between its sides; past the relief of the pad it is a disc of the
  // fill around the spoke. The spoke holds if such discs lead from the edge of the pad into the
  // fill.
  double step = minWidth / 8.0;
  int32_t half = jrt::d2i(Math::ceil((side + minWidth / 2.0) / step));
  int32_t across = 2 * half + 1;
  int32_t along = jrt::d2i(Math::ceil((length + minWidth / 2.0) / step)) + 1;
  double alongX = (end->x - start->x) / length;
  double alongY = (end->y - start->y) / length;
  double fillDistance = gap + minWidth / 2.0 - 1.0;
  // only the foreign copper that can reach one of these points
  FloatPoint* farEnd =
      new (jrt::kGeom) FloatPoint(start->x + alongX * (along - 1) * step, start->y + alongY * (along - 1) * step);
  auto* nearShapes = new (jrt::kOp) jrt::ArrayList<TileShape*>();
  auto* nearClearances = new (jrt::kOp) jrt::ArrayList<jrt::Array<double>*>();
  for (int32_t i = 0; i < foreignShapes->size(); i++) {
    if (FastPlaneConnectivityGrid::distance(start, farEnd, foreignShapes->get(i)) <
        (*foreignClearances->get(i))[0] + half * step) {
      nearShapes->add(foreignShapes->get(i));
      nearClearances->add(foreignClearances->get(i));
    }
  }
  auto* reached = new (jrt::kOp) jrt::Array<bool>(across);
  for (int32_t m = 0; m < along; m++) {
    double distance = m * step;
    auto* free = new (jrt::kOp) jrt::Array<bool>(across);
    auto* inFill = new (jrt::kOp) jrt::Array<bool>(across);
    for (int32_t n = 0; n < across; n++) {
      double offset = (n - half) * step;
      FloatPoint* point =
          new (jrt::kGeom) FloatPoint(start->x + alongX * distance - alongY * offset, start->y + alongY * distance + alongX * offset);
      (*inFill)[n] = padShape->distance(point) >= fillDistance;
      (*free)[n] = ((*inFill)[n] || Math::abs(offset) <= side + 1e-6) && isFree(point, point, 0, nearShapes, nearClearances);
    }
    auto* next = new (jrt::kOp) jrt::Array<bool>(across);
    for (int32_t n = 0; n < across; n++) {
      (*next)[n] = (*free)[n] &&
                   (m == 0 || (*reached)[n] || (n > 0 && (*reached)[n - 1]) || (n < across - 1 && (*reached)[n + 1]));
    }
    for (int32_t n = 1; n < across; n++) {
      (*next)[n] = (*next)[n] || ((*free)[n] && (*next)[n - 1]);
    }
    for (int32_t n = across - 2; n >= 0; n--) {
      (*next)[n] = (*next)[n] || ((*free)[n] && (*next)[n + 1]);
    }
    bool any = false;
    for (int32_t n = 0; n < across; n++) {
      if ((*next)[n] && (*inFill)[n]) {
        return true;
      }
      any = any || (*next)[n];
    }
    if (!any) {
      return false;
    }
    reached = next;
  }
  return false;
}

bool FastPlaneConnectivityGrid::isEndInFill(
    FloatPoint* start,
    FloatPoint* end,
    double minWidth,
    TileShape* padShape,
    double gap,
    jrt::List<TileShape*>* foreignShapes,
    jrt::List<jrt::Array<double>*>* foreignClearances) {
  if (foreignShapes->isEmpty()) {
    return true;
  }
  double length = start->distance(end);
  jrt::Array<double>* slides =
      length > 0 && minWidth > 0
          ? jrt::Array<double>::of({0, minWidth / 4.0, -minWidth / 4.0, minWidth / 2.0, -minWidth / 2.0})
          : jrt::Array<double>::of({0});
  for (double slide : *slides) {
    FloatPoint* centre = end;
    if (slide != 0) {
      centre = new (jrt::kGeom) FloatPoint(end->x - (end->y - start->y) / length * slide,
                              end->y + (end->x - start->x) / length * slide);
      if (padShape != nullptr && padShape->distance(centre) < gap + minWidth / 2.0 - FILL_LENIENCY * minWidth) {
        continue;
      }
    }
    if (isFree(centre, centre, FILL_LENIENCY * minWidth, foreignShapes, foreignClearances)) {
      return true;
    }
  }
  return false;
}

bool FastPlaneConnectivityGrid::isFree(
    FloatPoint* from,
    FloatPoint* to,
    double leniency,
    jrt::List<TileShape*>* foreignShapes,
    jrt::List<jrt::Array<double>*>* foreignClearances) {
  for (int32_t i = 0; i < foreignShapes->size(); i++) {
    if (distance(from, to, foreignShapes->get(i)) < (*foreignClearances->get(i))[0] - leniency) {
      return false;
    }
  }
  return true;
}

double FastPlaneConnectivityGrid::distance(FloatPoint* from, FloatPoint* to, TileShape* shape) {
  if (shape->contains(from) || shape->contains(to)) {
    return 0;
  }
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  if (corners == nullptr || corners->length == 0) {
    return jrt::Double::MAX_VALUE;
  }
  double result = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < corners->length; i++) {
    FloatPoint* a = (*corners)[i];
    FloatPoint* b = (*corners)[(i + 1) % corners->length];
    result = Math::min(result, segmentDistance(from, to, a, b));
    if (result <= 0) {
      return 0;
    }
  }
  return result;
}

double FastPlaneConnectivityGrid::segmentDistance(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2) {
  if (segmentsCross(p1, p2, q1, q2)) {
    return 0;
  }
  return Math::min(Math::min(pointSegmentDistance(p1, q1, q2), pointSegmentDistance(p2, q1, q2)),
                   Math::min(pointSegmentDistance(q1, p1, p2), pointSegmentDistance(q2, p1, p2)));
}

bool FastPlaneConnectivityGrid::segmentsCross(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2) {
  double d1 = cross(q1, q2, p1);
  double d2 = cross(q1, q2, p2);
  double d3 = cross(p1, p2, q1);
  double d4 = cross(p1, p2, q2);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

double FastPlaneConnectivityGrid::cross(FloatPoint* a, FloatPoint* b, FloatPoint* c) {
  return (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
}

double FastPlaneConnectivityGrid::pointSegmentDistance(FloatPoint* p, FloatPoint* a, FloatPoint* b) {
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  double lengthSquare = dx * dx + dy * dy;
  double t = lengthSquare > 0 ? ((p->x - a->x) * dx + (p->y - a->y) * dy) / lengthSquare : 0;
  t = Math::max(0.0, Math::min(1.0, t));
  return Math::hypot(p->x - (a->x + t * dx), p->y - (a->y + t * dy));
}

void FastPlaneConnectivityGrid::rasterizeConductionArea(ConductionArea* ca) {
  if (ca == nullptr || ca->getArea() == nullptr) {
    return;
  }
  jrt::Array<TileShape*>* pieces = ca->getArea()->splitToConvex();
  if (pieces == nullptr) {
    return;
  }
  for (TileShape* tile : *pieces) {
    if (tile == nullptr) {
      continue;
    }
    IntBox* box = tile->boundingBox();
    int32_t startCol = Math::max(0, (box->ll->x - originX) / cellSize);
    int32_t endCol = Math::min(cols - 1, (box->ur->x - originX) / cellSize);
    int32_t startRow = Math::max(0, (box->ll->y - originY) / cellSize);
    int32_t endRow = Math::min(rows - 1, (box->ur->y - originY) / cellSize);

    if (startCol > endCol || startRow > endRow) {
      continue;
    }

    bool tileMarked = false;
    if (tile->isIntBox()) {
      for (int32_t r = startRow; r <= endRow; r++) {
        setRowSpan(copperMask, r, startCol, endCol);
        tileMarked = true;
      }
    } else {
      // Precise point-in-convex-tile test along cell centers
      for (int32_t r = startRow; r <= endRow; r++) {
        int32_t cellY = originY + r * cellSize + cellSize / 2;
        int32_t spanStart = -1;
        for (int32_t c = startCol; c <= endCol; c++) {
          int32_t cellX = originX + c * cellSize + cellSize / 2;
          if (tile->contains(static_cast<Point*>(new (jrt::kGeom) IntPoint(cellX, cellY)))) {
            if (spanStart == -1) {
              spanStart = c;
            }
          } else if (spanStart != -1) {
            setRowSpan(copperMask, r, spanStart, c - 1);
            spanStart = -1;
            tileMarked = true;
          }
        }
        if (spanStart != -1) {
          setRowSpan(copperMask, r, spanStart, endCol);
          tileMarked = true;
        }
      }
    }

    // Fallback for thin or tiny features whose cell centers fall outside the shape:
    // mark the cells containing the tile corners and center of gravity
    if (!tileMarked) {
      jrt::Array<FloatPoint*>* corners = tile->cornerApproxArr();
      if (corners != nullptr) {
        for (FloatPoint* corner : *corners) {
          int32_t c = Math::max(
              0, Math::min(cols - 1, jrt::d2i(Math::floor((corner->x - originX) / static_cast<double>(cellSize)))));
          int32_t r = Math::max(
              0, Math::min(rows - 1, jrt::d2i(Math::floor((corner->y - originY) / static_cast<double>(cellSize)))));
          setBit(copperMask, r, c);
        }
      }
      FloatPoint* cog = tile->centreOfGravity();
      if (cog != nullptr) {
        int32_t c =
            Math::max(0, Math::min(cols - 1, jrt::d2i(Math::floor((cog->x - originX) / static_cast<double>(cellSize)))));
        int32_t r =
            Math::max(0, Math::min(rows - 1, jrt::d2i(Math::floor((cog->y - originY) / static_cast<double>(cellSize)))));
        setBit(copperMask, r, c);
      }
    }
  }
}

void FastPlaneConnectivityGrid::rasterizeObstacleAreas(
    jrt::List<Item*>* items,
    jrt::List<int32_t>* shapeIndices,
    jrt::List<TileShape*>* shapes,
    jrt::List<double>* clearances,
    FastPlaneConnectivityGrid_FractureMemo* memo) {
  if (shapes->isEmpty()) {
    return;
  }
  if (memo == nullptr) {
    for (int32_t i = 0; i < shapes->size(); i++) {
      rasterizeObstacleShape(shapes->get(i)->enlarge(clearances->get(i)), obstacleMask);
    }
    return;
  }
  jrt::Array<TileShape*>* shapeArr = shapes->toArray();
  auto* clearanceArr = new (jrt::kOp) jrt::Array<double>(clearances->size());
  for (int32_t i = 0; i < clearanceArr->length; i++) {
    (*clearanceArr)[i] = clearances->get(i);
  }
  int32_t words = rows * wordsPerRow;
  jrt::Array<int64_t>* areaWords = memo->lookupObstacleAreas(this, shapeArr, clearanceArr);
  if (areaWords == nullptr) {
    areaWords = new (jrt::kOp) jrt::Array<int64_t>(words);
    for (int32_t i = 0; i < shapeArr->length; i++) {
      rasterizeObstacleShape((*shapeArr)[i]->enlarge((*clearanceArr)[i]), areaWords);
    }
    auto* indexArr = new (jrt::kOp) jrt::Array<int32_t>(shapeIndices->size());
    for (int32_t i = 0; i < indexArr->length; i++) {
      (*indexArr)[i] = shapeIndices->get(i);
    }
    memo->storeObstacleAreas(this, items->toArray(), indexArr, shapeArr, clearanceArr, areaWords);
  }
  for (int32_t w = 0; w < words; w++) {
    (*obstacleMask)[w] |= (*areaWords)[w];
  }
}

void FastPlaneConnectivityGrid::rasterizeObstacleShape(Shape* shape) {
  rasterizeObstacleShape(shape, obstacleMask);
}

void FastPlaneConnectivityGrid::rasterizeObstacleShape(Shape* shape, jrt::Array<int64_t>* mask) {
  if (shape == nullptr || shape->isEmpty()) {
    return;
  }
  IntBox* box = shape->boundingBox();
  int32_t startCol = Math::max(0, (box->ll->x - originX) / cellSize);
  int32_t endCol = Math::min(cols - 1, (box->ur->x - originX) / cellSize);
  int32_t startRow = Math::max(0, (box->ll->y - originY) / cellSize);
  int32_t endRow = Math::min(rows - 1, (box->ur->y - originY) / cellSize);

  if (startCol > endCol || startRow > endRow) {
    return;
  }

  if (jrt::fast_cast<IntBox>(shape) != nullptr) {
    for (int32_t r = startRow; r <= endRow; r++) {
      setRowSpan(mask, r, startCol, endCol);
    }
  } else {
    for (int32_t r = startRow; r <= endRow; r++) {
      int32_t cellY = originY + r * cellSize + cellSize / 2;
      int32_t spanStart = -1;
      for (int32_t c = startCol; c <= endCol; c++) {
        int32_t cellX = originX + c * cellSize + cellSize / 2;
        if (shape->contains(static_cast<Point*>(new (jrt::kGeom) IntPoint(cellX, cellY)))) {
          if (spanStart == -1) {
            spanStart = c;
          }
        } else if (spanStart != -1) {
          setRowSpan(mask, r, spanStart, c - 1);
          spanStart = -1;
        }
      }
      if (spanStart != -1) {
        setRowSpan(mask, r, spanStart, endCol);
      }
    }
  }
}

void FastPlaneConnectivityGrid::rasterizeThickSegment(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                                      int32_t radius) {
  // Conservative dilation: include cell half-diagonal so any cell touching the obstacle is marked
  int32_t dilatedRadius = radius + (cellSize + 1) / 2;
  int32_t minX = Math::min(x1, x2) - dilatedRadius;
  int32_t maxX = Math::max(x1, x2) + dilatedRadius;
  int32_t minY = Math::min(y1, y2) - dilatedRadius;
  int32_t maxY = Math::max(y1, y2) + dilatedRadius;

  int32_t startCol = Math::max(0, (minX - originX) / cellSize);
  int32_t endCol = Math::min(cols - 1, (maxX - originX) / cellSize);
  int32_t startRow = Math::max(0, (minY - originY) / cellSize);
  int32_t endRow = Math::min(rows - 1, (maxY - originY) / cellSize);

  if (startCol > endCol || startRow > endRow) {
    return;
  }

  int64_t radiusSq = static_cast<int64_t>(dilatedRadius) * dilatedRadius;
  int64_t dx = x2 - x1;
  int64_t dy = y2 - y1;

  // Fast-path: Horizontal line segment or point (dx arbitrary, dy == 0)
  if (dy == 0) {
    int32_t segMinX = Math::min(x1, x2);
    int32_t segMaxX = Math::max(x1, x2);
    for (int32_t r = startRow; r <= endRow; r++) {
      int32_t cellY = originY + r * cellSize + cellSize / 2;
      int64_t distY = Math::abs(cellY - y1);
      if (distY <= dilatedRadius) {
        int32_t deltaX = jrt::d2i(Math::sqrt(static_cast<double>(radiusSq - distY * distY)));
        int32_t spanStartCol = Math::max(startCol, (segMinX - deltaX - originX) / cellSize);
        int32_t spanEndCol = Math::min(endCol, (segMaxX + deltaX - originX) / cellSize);
        if (spanStartCol <= spanEndCol) {
          setRowSpan(obstacleMask, r, spanStartCol, spanEndCol);
        }
      }
    }
    return;
  }

  // Fast-path: Vertical line segment (dx == 0, dy != 0)
  if (dx == 0) {
    int32_t segMinY = Math::min(y1, y2);
    int32_t segMaxY = Math::max(y1, y2);
    for (int32_t r = startRow; r <= endRow; r++) {
      int32_t cellY = originY + r * cellSize + cellSize / 2;
      int32_t clampedY = Math::max(segMinY, Math::min(segMaxY, cellY));
      int64_t distY = Math::abs(cellY - clampedY);
      if (distY <= dilatedRadius) {
        int32_t deltaX = jrt::d2i(Math::sqrt(static_cast<double>(radiusSq - distY * distY)));
        int32_t sCol = Math::max(startCol, (x1 - deltaX - originX) / cellSize);
        int32_t eCol = Math::min(endCol, (x1 + deltaX - originX) / cellSize);
        if (sCol <= eCol) {
          setRowSpan(obstacleMask, r, sCol, eCol);
        }
      }
    }
    return;
  }

  int64_t lenSq = dx * dx + dy * dy;

  for (int32_t r = startRow; r <= endRow; r++) {
    int32_t cellY = originY + r * cellSize + cellSize / 2;
    int32_t spanStart = -1;
    int32_t spanEnd = -1;

    for (int32_t c = startCol; c <= endCol; c++) {
      int32_t cellX = originX + c * cellSize + cellSize / 2;

      // Distance from (cellX, cellY) to segment (x1, y1)-(x2, y2)
      bool inside;
      if (lenSq == 0) {
        int64_t ddx = cellX - x1;
        int64_t ddy = cellY - y1;
        inside = (ddx * ddx + ddy * ddy) <= radiusSq;
      } else {
        int64_t dot = static_cast<int64_t>(cellX - x1) * dx + static_cast<int64_t>(cellY - y1) * dy;
        if (dot <= 0) {
          int64_t ddx = cellX - x1;
          int64_t ddy = cellY - y1;
          inside = (ddx * ddx + ddy * ddy) <= radiusSq;
        } else if (dot >= lenSq) {
          int64_t ddx = cellX - x2;
          int64_t ddy = cellY - y2;
          inside = (ddx * ddx + ddy * ddy) <= radiusSq;
        } else {
          // Perpendicular projection
          double t = static_cast<double>(dot) / lenSq;
          double projX = x1 + t * dx;
          double projY = y1 + t * dy;
          double distSq = (cellX - projX) * (cellX - projX) + (cellY - projY) * (cellY - projY);
          inside = distSq <= static_cast<double>(radiusSq);
        }
      }

      if (inside) {
        if (spanStart == -1) {
          spanStart = c;
        }
        spanEnd = c;
      } else if (spanStart != -1) {
        setRowSpan(obstacleMask, r, spanStart, spanEnd);
        spanStart = -1;
        spanEnd = -1;
      }
    }
    if (spanStart != -1) {
      setRowSpan(obstacleMask, r, spanStart, spanEnd);
    }
  }
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid::partitionItemsByIsland(
    ConductionArea* ca, jrt::Collection<Item*>* items) {
  jrt::Array<Item*>* itemArr = items->toArray();
  auto* locations = new (jrt::kGeom) jrt::Array<IntPoint*>(itemArr->length);
  for (int32_t i = 0; i < itemArr->length; i++) {
    (*locations)[i] = getItemLocation((*itemArr)[i], ca);
  }
  return partitionItemsByIsland(ca, items, itemArr, locations);
}

FastPlaneConnectivityGrid_PlaneFractureResult* FastPlaneConnectivityGrid::partitionItemsByIsland(
    ConductionArea* ca, jrt::Collection<Item*>* items, jrt::Array<Item*>* itemArr, jrt::Array<IntPoint*>* locations) {
  preparePartitionBuffers();
  auto* islands = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
  auto* itemIslandMap = new (jrt::kOp) jrt::HashMap<Item*, int32_t>();
  auto* unseededItems = new (jrt::kOp) jrt::ArrayList<Item*>();
  auto* unseededSet = new (jrt::kOp) jrt::HashSet<Item*>();
  // seed cell index of each item: SEED_UNKNOWN until computed, SEED_NONE if there is none
  auto* seeds = new (jrt::kOp) jrt::Array<int32_t>(itemArr->length);
  jrt::Arrays::fill<int32_t>(seeds, SEED_UNKNOWN);
  int32_t nextIslandId = 0;
  auto newList = [](int32_t) -> jrt::List<Item*>* { return new (jrt::kOp) jrt::ArrayList<Item*>(); };

  auto* traces = new (jrt::kOp) jrt::ArrayList<Trace*>();
  for (int32_t i = 0; i < itemArr->length; i++) {
    Item* item = (*itemArr)[i];
    if (auto* trace = jrt::fast_cast<Trace>(item)) {
      // a trace is placed by its ends once the islands are known (attachTraces)
      traces->add(trace);
      continue;
    }
    if (itemIslandMap->containsKey(item)) {
      continue;
    }

    if ((*locations)[i] == nullptr) {
      unseededItems->add(item);
      unseededSet->add(item);
      continue;
    }

    int32_t seedIndex = seedCell(seeds, i, (*locations)[i]);
    if (seedIndex == SEED_NONE) {
      unseededItems->add(item);
      unseededSet->add(item);
      continue;
    }

    int32_t existingIsland = (*cellIslandMap)[seedIndex];
    if (existingIsland >= 0) {
      islands->computeIfAbsent(existingIsland, newList)->add(item);
      itemIslandMap->put(item, existingIsland);
      continue;
    }

    // Start new BFS flood fill for this island
    int32_t currentIslandId = nextIslandId++;
    islands->computeIfAbsent(currentIslandId, newList)->add(item);
    itemIslandMap->put(item, currentIslandId);

    runFloodFill(seedIndex % cols, seedIndex / cols, static_cast<int16_t>(currentIslandId));

    // Check remaining unassigned items to see if they are reached by this flood fill
    for (int32_t j = 0; j < itemArr->length; j++) {
      Item* other = (*itemArr)[j];
      if (jrt::fast_cast<Trace>(other) == nullptr && !itemIslandMap->containsKey(other) &&
          !unseededSet->contains(other)) {
        if ((*locations)[j] != nullptr) {
          int32_t otherIndex = seedCell(seeds, j, (*locations)[j]);
          if (otherIndex != SEED_NONE && (*cellIslandMap)[otherIndex] == currentIslandId) {
            islands->get(currentIslandId)->add(other);
            itemIslandMap->put(other, currentIslandId);
          }
        }
      }
    }
  }

  if (nextIslandId == 0 && traces->isEmpty()) {
    auto* single = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    single->put(0, new (jrt::kOp) jrt::ArrayList<Item*>(items));
    return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(single);
  }
  int32_t islandCounter = nextIslandId;

  // The copper at the end of a trace touches the cells of fill whose centre is no further from
  // the end than the half width of the trace and half a cell. A piece of fill that no pad or via
  // holds is an island of its own once a trace reaches it.
  jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* traceEnds = attachTraces(
      ca,
      traces,
      islands,
      [this, &islandCounter, islands](Trace* trace, Point* end) -> jrt::Set<int32_t>* {
        auto* result = new (jrt::kOp) jrt::TreeSet<int32_t>();
        for (int32_t cell : *fillCellsAround(toIntPoint(end), trace->getHalfWidth())) {
          if ((*cellIslandMap)[cell] < 0) {
            int32_t id = islandCounter++;
            islands->put(id, new (jrt::kOp) jrt::ArrayList<Item*>());
            runFloodFill(cell % cols, cell / cols, static_cast<int16_t>(id));
          }
          result->add(static_cast<int32_t>((*cellIslandMap)[cell]));
        }
        return result;
      },
      [](Trace*) -> jrt::Set<int32_t>* { return jrt::Collections::emptySet<int32_t>(); });

  if (islandCounter == 0) {
    auto* single = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Item*>*>();
    single->put(0, new (jrt::kOp) jrt::ArrayList<Item*>(items));
    return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(single, nullptr, traceEnds);
  }

  for (Item* unseeded : *unseededItems) {
    if (itemIslandMap->containsKey(unseeded)) {
      continue;
    }
    IntPoint* pt = getItemLocation(unseeded, ca);
    int32_t assignedIsland = 0;
    if (pt != nullptr) {
      int32_t targetCol = (pt->x - originX) / cellSize;
      int32_t targetRow = (pt->y - originY) / cellSize;
      int32_t nearestIsland = findNearestMappedIsland(targetCol, targetRow, 10);
      if (nearestIsland >= 0) {
        assignedIsland = nearestIsland;
      }
    }
    islands->computeIfAbsent(assignedIsland, newList)->add(unseeded);
    itemIslandMap->put(unseeded, assignedIsland);
  }

  return new (jrt::kOp) FastPlaneConnectivityGrid_PlaneFractureResult(islands, nullptr, traceEnds);
}

jrt::List<int32_t>* FastPlaneConnectivityGrid::fillCellsAround(IntPoint* point, double halfWidth) {
  double reach = halfWidth + cellSize / 2.0;
  int32_t minCol = Math::max(0, jrt::d2i(Math::floor((point->x - reach - originX) / static_cast<double>(cellSize))));
  int32_t maxCol =
      Math::min(cols - 1, jrt::d2i(Math::floor((point->x + reach - originX) / static_cast<double>(cellSize))));
  int32_t minRow = Math::max(0, jrt::d2i(Math::floor((point->y - reach - originY) / static_cast<double>(cellSize))));
  int32_t maxRow =
      Math::min(rows - 1, jrt::d2i(Math::floor((point->y + reach - originY) / static_cast<double>(cellSize))));
  auto* result = new (jrt::kOp) jrt::ArrayList<int32_t>();
  for (int32_t row = minRow; row <= maxRow; row++) {
    double cellY = originY + row * static_cast<double>(cellSize) + cellSize / 2.0;
    for (int32_t col = minCol; col <= maxCol; col++) {
      double cellX = originX + col * static_cast<double>(cellSize) + cellSize / 2.0;
      if (Math::hypot(cellX - point->x, cellY - point->y) <= reach && isConductive(col, row)) {
        result->add(row * cols + col);
      }
    }
  }
  return result;
}

int32_t FastPlaneConnectivityGrid::seedCell(jrt::Array<int32_t>* seeds, int32_t index, IntPoint* location) {
  if ((*seeds)[index] == SEED_UNKNOWN) {
    jrt::Array<int32_t>* seed =
        findNearestConductiveCell((location->x - originX) / cellSize, (location->y - originY) / cellSize);
    (*seeds)[index] = seed == nullptr ? SEED_NONE : (*seed)[1] * cols + (*seed)[0];
  }
  return (*seeds)[index];
}

void FastPlaneConnectivityGrid::runFloodFill(int32_t startCol, int32_t startRow, int16_t islandId) {
  int32_t head = 0;
  int32_t tail = 0;

  int32_t startCell = startRow * cols + startCol;
  (*queue)[tail++] = startCell;
  setBit(visitedMask, startRow, startCol);
  (*cellIslandMap)[startCell] = islandId;

  while (head < tail) {
    int32_t cell = (*queue)[head++];
    int32_t r = cell / cols;
    int32_t c = cell - r * cols;
    if (c + 1 < cols) {
      tail = visitCell(r, c + 1, cell + 1, islandId, tail);
    }
    if (c > 0) {
      tail = visitCell(r, c - 1, cell - 1, islandId, tail);
    }
    if (r + 1 < rows) {
      tail = visitCell(r + 1, c, cell + cols, islandId, tail);
    }
    if (r > 0) {
      tail = visitCell(r - 1, c, cell - cols, islandId, tail);
    }
  }
}

int32_t FastPlaneConnectivityGrid::visitCell(int32_t r, int32_t c, int32_t cell, int16_t islandId, int32_t tail) {
  int32_t wordIdx = r * wordsPerRow + (c >> 6);
  int64_t bit = jrt::shl(static_cast<int64_t>(1), c & 63);
  if (((*visitedMask)[wordIdx] & bit) == 0 && ((*copperMask)[wordIdx] & bit) != 0 &&
      ((*obstacleMask)[wordIdx] & bit) == 0) {
    (*visitedMask)[wordIdx] |= bit;
    (*cellIslandMap)[cell] = islandId;
    (*queue)[tail] = cell;
    return tail + 1;
  }
  return tail;
}

jrt::Array<int32_t>* FastPlaneConnectivityGrid::findNearestConductiveCell(int32_t col, int32_t row) {
  // Search expanding neighborhood up to radius 4
  return findNearestConductiveCell(col, row, 4);
}

jrt::Array<int32_t>* FastPlaneConnectivityGrid::findNearestConductiveCell(int32_t col, int32_t row,
                                                                          int32_t maxRadius) {
  if (col >= 0 && col < cols && row >= 0 && row < rows && isConductive(col, row)) {
    return jrt::Array<int32_t>::of({col, row});
  }
  for (int32_t radius = 1; radius <= maxRadius; radius++) {
    for (int32_t dr = -radius; dr <= radius; dr++) {
      for (int32_t dc = -radius; dc <= radius; dc++) {
        int32_t nc = col + dc;
        int32_t nr = row + dr;
        if (nc >= 0 && nc < cols && nr >= 0 && nr < rows && isConductive(nc, nr)) {
          return jrt::Array<int32_t>::of({nc, nr});
        }
      }
    }
  }
  return nullptr;
}

int32_t FastPlaneConnectivityGrid::findNearestMappedIsland(int32_t col, int32_t row, int32_t maxRadius) {
  for (int32_t r = 1; r <= maxRadius; r++) {
    for (int32_t dr = -r; dr <= r; dr++) {
      for (int32_t dc = -r; dc <= r; dc++) {
        int32_t nc = col + dc;
        int32_t nr = row + dr;
        if (nc >= 0 && nc < cols && nr >= 0 && nr < rows) {
          int32_t island = (*cellIslandMap)[nr * cols + nc];
          if (island >= 0) {
            return island;
          }
        }
      }
    }
  }
  return -1;
}

IntPoint* FastPlaneConnectivityGrid::toIntPoint(Point* p) {
  if (auto* ip = jrt::fast_cast<IntPoint>(p)) {
    return ip;
  }
  return p->toFloat()->round();
}

IntPoint* FastPlaneConnectivityGrid::getItemLocation(Item* item, ConductionArea* ca) {
  if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
    IntPoint* center = toIntPoint(drillItem->getCenter());
    if (ca != nullptr && ca->getArea() != nullptr && !ca->getArea()->contains(static_cast<Point*>(center))) {
      IntBox* box = drillItem->boundingBox();
      if (box != nullptr) {
        jrt::Array<IntPoint*>* corners = jrt::Array<IntPoint*>::of(
            {box->ll, box->ur, new (jrt::kGeom) IntPoint(box->ll->x, box->ur->y), new (jrt::kGeom) IntPoint(box->ur->x, box->ll->y)});
        for (IntPoint* cp : *corners) {
          if (ca->getArea()->contains(static_cast<Point*>(cp))) {
            return cp;
          }
        }
      }
    }
    return center;
  } else if (auto* trace = jrt::fast_cast<Trace>(item)) {
    if (ca != nullptr && ca->getArea() != nullptr) {
      if (ca->getArea()->contains(trace->firstCorner())) {
        return toIntPoint(trace->firstCorner());
      }
      if (ca->getArea()->contains(trace->lastCorner())) {
        return toIntPoint(trace->lastCorner());
      }
      if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
        jrt::Array<Point*>* corners = polylineTrace->polyline()->corners();
        for (Point* c : *corners) {
          if (ca->getArea()->contains(c)) {
            return toIntPoint(c);
          }
        }
      }
    }
    return toIntPoint(trace->firstCorner());
  }
  return nullptr;
}

bool FastPlaneConnectivityGrid::isConductive(int32_t col, int32_t row) {
  if (col < 0 || col >= cols || row < 0 || row >= rows) {
    return false;
  }
  int32_t wordIdx = row * wordsPerRow + (col >> 6);
  int64_t bit = jrt::shl(static_cast<int64_t>(1), col & 63);
  return ((*copperMask)[wordIdx] & bit) != 0 && ((*obstacleMask)[wordIdx] & bit) == 0;
}

bool FastPlaneConnectivityGrid::isBitSet(jrt::Array<int64_t>* mask, int32_t row, int32_t col) {
  if (col < 0 || col >= cols || row < 0 || row >= rows) {
    return false;
  }
  int32_t wordIdx = row * wordsPerRow + (col >> 6);
  return ((*mask)[wordIdx] & jrt::shl(static_cast<int64_t>(1), col & 63)) != 0;
}

void FastPlaneConnectivityGrid::setBit(jrt::Array<int64_t>* mask, int32_t row, int32_t col) {
  if (col >= 0 && col < cols && row >= 0 && row < rows) {
    int32_t wordIdx = row * wordsPerRow + (col >> 6);
    (*mask)[wordIdx] |= jrt::shl(static_cast<int64_t>(1), col & 63);
  }
}

void FastPlaneConnectivityGrid::setRowSpan(jrt::Array<int64_t>* mask, int32_t row, int32_t startCol, int32_t endCol) {
  if (row < 0 || row >= rows || startCol > endCol || startCol >= cols || endCol < 0) {
    return;
  }
  int32_t s = Math::max(0, startCol);
  int32_t e = Math::min(cols - 1, endCol);

  int32_t startWord = row * wordsPerRow + (s >> 6);
  int32_t endWord = row * wordsPerRow + (e >> 6);

  int32_t startBit = s & 63;
  int32_t endBit = e & 63;

  if (startWord == endWord) {
    int64_t range = jrt::shl(jrt::ushr(ALL_ONES, 63 - (endBit - startBit)), startBit);
    (*mask)[startWord] |= range;
  } else {
    (*mask)[startWord] |= jrt::shl(ALL_ONES, startBit);
    for (int32_t w = startWord + 1; w < endWord; w++) {
      (*mask)[w] = ALL_ONES;
    }
    (*mask)[endWord] |= jrt::ushr(ALL_ONES, 63 - endBit);
  }
}

}  // namespace fr
