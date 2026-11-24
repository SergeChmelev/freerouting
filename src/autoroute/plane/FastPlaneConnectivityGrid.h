#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Result container holding partitioned same-net items per isolated copper island. */
class FastPlaneConnectivityGrid_PlaneFractureResult : public virtual jrt::Object {
 public:
  /** Whether the conduction area has fractured into multiple disconnected islands with pins. */
  bool isFractured;
  /** Map of island ID (0-based) to list of same-net items residing in that island. */
  jrt::Map<int32_t, jrt::List<Item*>*>* islandItems;
  /** Total count of distinct islands containing electrical contact items. */
  int32_t islandCount;
  /**
   * The box outside of which nothing on the layer of the area changes this result, or nullptr when
   * it is not known: the result then holds for one revision of the board only.
   */
  IntBox* dependencyBox;
  /**
   * For each trace of the net: the islands that its first corner ([0]) and its last corner ([1])
   * touch. A trace is in the item list of each island it touches, without joining them.
   */
  jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* traceEndIslands;

  /** Creates a new fracture result (Java: this(islandItems, null)). */
  explicit FastPlaneConnectivityGrid_PlaneFractureResult(jrt::Map<int32_t, jrt::List<Item*>*>* islandItems);
  /** Holds as long as nothing changes inside the box (Java: this(..., Collections.emptyMap())). */
  FastPlaneConnectivityGrid_PlaneFractureResult(jrt::Map<int32_t, jrt::List<Item*>*>* islandItems, IntBox* dependencyBox);
  /** Null maps are replaced by empty maps (Java Collections.emptyMap()). */
  FastPlaneConnectivityGrid_PlaneFractureResult(
      jrt::Map<int32_t, jrt::List<Item*>*>* islandItems,
      IntBox* dependencyBox,
      jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* traceEndIslands);

  /**
   * The islands that one end of a trace touches, or nullptr if the trace is not known to this
   * result (it was placed in the island of one of its ends).
   */
  virtual jrt::Array<int32_t>* endIslands(Item* trace, bool first);
};

/**
 * What a conduction area keeps between two fracture evaluations, so that a board change that
 * leaves its inputs alone costs no rasterization or flood fill.
 *
 * PORT NOTE: Java `synchronized` methods are plain methods (single-threaded port).
 */
class FastPlaneConnectivityGrid_FractureMemo : public virtual jrt::Object {
  friend class FastPlaneConnectivityGrid;

 private:
  Area* copperArea = nullptr;
  int32_t copperOriginX = 0;
  int32_t copperOriginY = 0;
  int32_t copperCellSize = 0;
  int32_t copperCols = 0;
  int32_t copperRows = 0;
  jrt::Array<int64_t>* copperWords = nullptr;
  int32_t areasOriginX = 0;
  int32_t areasOriginY = 0;
  int32_t areasCellSize = 0;
  int32_t areasCols = 0;
  int32_t areasRows = 0;
  jrt::Array<Item*>* areasItems = nullptr;
  jrt::Array<int32_t>* areasShapeIndices = nullptr;
  jrt::Array<TileShape*>* areasShapes = nullptr;
  jrt::Array<double>* areasClearances = nullptr;
  jrt::Array<int64_t>* areasWords = nullptr;
  Area* partitionArea = nullptr;
  int32_t partitionOriginX = 0;
  int32_t partitionOriginY = 0;
  int32_t partitionCellSize = 0;
  int32_t partitionCols = 0;
  int32_t partitionRows = 0;
  jrt::Array<int64_t>* partitionObstacleWords = nullptr;
  jrt::Array<Item*>* partitionItems = nullptr;
  jrt::Array<IntPoint*>* partitionLocations = nullptr;
  FastPlaneConnectivityGrid_PlaneFractureResult* partitionResult = nullptr;

 public:
  FastPlaneConnectivityGrid_FractureMemo() = default;

  /** Copies the cached copper mask into the grid; returns false if it was not computed for it. */
  virtual bool restoreCopper(FastPlaneConnectivityGrid* grid, Area* area);
  virtual void storeCopper(FastPlaneConnectivityGrid* grid, Area* area);
  /**
   * Returns the raster of these obstacle area shapes if it is cached for this grid (tile shapes are
   * immutable, so the same references with the same clearances give the same raster), else nullptr.
   */
  virtual jrt::Array<int64_t>* lookupObstacleAreas(
      FastPlaneConnectivityGrid* grid, jrt::Array<TileShape*>* shapes, jrt::Array<double>* clearances);
  virtual void storeObstacleAreas(
      FastPlaneConnectivityGrid* grid,
      jrt::Array<Item*>* items,
      jrt::Array<int32_t>* shapeIndices,
      jrt::Array<TileShape*>* shapes,
      jrt::Array<double>* clearances,
      jrt::Array<int64_t>* words);
  /** Returns the previous partition if it was computed from exactly these inputs, else nullptr. */
  virtual FastPlaneConnectivityGrid_PlaneFractureResult* lookupPartition(
      FastPlaneConnectivityGrid* grid, Area* area, jrt::Array<Item*>* items, jrt::Array<IntPoint*>* locations);
  /**
   * Returns what a copy of the conduction area can reuse of this memo, detached from the area it
   * was computed for, or nullptr if nothing is valid for currentArea.
   */
  virtual FastPlaneConnectivityGrid_FractureMemo* detach(Area* currentArea);

 private:
  /** Returns true if every cached obstacle area shape is still the shape of its item. */
  bool areaShapesAreCurrent();

 public:
  /**
   * Returns this detached memo bound to the copy of its conduction area: copyArea is the area of
   * the copy, and copyItemById gives the copy of an item by its id.
   */
  virtual FastPlaneConnectivityGrid_FractureMemo* attach(Area* copyArea, std::function<Item*(int32_t)> copyItemById);
  virtual void storePartition(
      FastPlaneConnectivityGrid* grid,
      Area* area,
      jrt::Array<Item*>* items,
      jrt::Array<IntPoint*>* locations,
      FastPlaneConnectivityGrid_PlaneFractureResult* result);
};

/**
 * High-performance, constant-memory 2D occupancy grid for evaluating copper pour topological
 * connectivity and detecting zone fracturing. Unlike expensive CSG polygon clipping, the pour and
 * the foreign obstacles are rasterized into bit masks and the islands are found by flood fill.
 */
class FastPlaneConnectivityGrid : public virtual jrt::Object {
  friend class FastPlaneConnectivityGrid_FractureMemo;
  friend class FastPlaneConnectivityGrid_PlaneFractureResult;

 public:
  /** Maximum dimension of the grid in either axis (512 * 512 = 256K cells). */
  static constexpr int32_t MAX_GRID_DIM = 512;
  /** Minimum cell size in internal board units (e.g. 10 um at 10 units/um resolution). */
  static constexpr int32_t MIN_CELL_SIZE = 100;
  /** Default cell size in internal board units (e.g. 100 um at 10 units/um resolution). */
  static constexpr int32_t DEFAULT_CELL_SIZE = 1000;

 private:
  int32_t originX = 0;
  int32_t originY = 0;
  int32_t cellSize = 0;
  int32_t cols = 0;
  int32_t rows = 0;
  int32_t wordsPerRow = 0;
  jrt::Array<int64_t>* copperMask;
  jrt::Array<int64_t>* obstacleMask;
  jrt::Array<int64_t>* visitedMask;
  jrt::Array<int16_t>* cellIslandMap;
  jrt::Array<int32_t>* queue;
  bool partitionBuffersReady = false;
  /** The number of points per cell and axis at which the obstacles are sampled (fineMask). */
  static constexpr int32_t FINE = 8;
  /**
   * The obstacles at FINE times the resolution of the cells, when the fill rules of the design
   * are known: a cell is then free of fill only if all its sample points are.
   */
  jrt::Array<int64_t>* fineMask = new (jrt::kOp) jrt::Array<int64_t>(0);
  jrt::Array<bool>* partialCell = new (jrt::kOp) jrt::Array<bool>(0);
  int32_t fineCols = 0;
  int32_t fineRows = 0;
  int32_t fineWordsPerRow = 0;

 public:
  /** Creates a new uninitialized connectivity grid (all masks empty). */
  FastPlaneConnectivityGrid()
      : copperMask(new (jrt::kOp) jrt::Array<int64_t>(0)),
        obstacleMask(new (jrt::kOp) jrt::Array<int64_t>(0)),
        visitedMask(new (jrt::kOp) jrt::Array<int64_t>(0)),
        cellIslandMap(new (jrt::kOp) jrt::Array<int16_t>(0)),
        queue(new (jrt::kOp) jrt::Array<int32_t>(0)) {}

  /** Initializes the grid geometry for the given bounding box and desired cell size. */
  virtual void init(IntBox* bbox, int32_t requestedCellSize);

 private:
  /**
   * Prepares the island map and the flood-fill queue. Only a partition needs them, and they are
   * the largest buffers of the grid.
   */
  void preparePartitionBuffers();

 public:
  /**
   * Evaluates the topological connectivity of a conduction area against foreign routing
   * obstacles. sameNetItems are the same-net items (pins, vias, traces) to partition.
   */
  virtual FastPlaneConnectivityGrid_PlaneFractureResult* evaluateFracture(
      ConductionArea* ca, BasicBoard* board, jrt::Collection<Item*>* sameNetItems);
  /**
   * Same as evaluateFracture(ca, board, sameNetItems), reusing what memo kept from the previous
   * evaluation of the same conduction area when its inputs are unchanged.
   */
  virtual FastPlaneConnectivityGrid_PlaneFractureResult* evaluateFracture(
      ConductionArea* ca,
      BasicBoard* board,
      jrt::Collection<Item*>* sameNetItems,
      FastPlaneConnectivityGrid_FractureMemo* memo);
  /**
   * PORT NOTE (stage E): copy-out of an evaluation run in the operation region `work`. Copies the
   * parts of `result` and of what `memo` stored that lie in `work` into the current domain (maps
   * keep their layout, so they iterate as before) and returns the result to keep.
   */
  static FastPlaneConnectivityGrid_PlaneFractureResult* keepOutside(
      const jrt::GeomRegion& work,
      FastPlaneConnectivityGrid_PlaneFractureResult* result,
      FastPlaneConnectivityGrid_FractureMemo* memo);

 private:
  /**
   * Partitions the items of the net when the design says how the pads are connected to the fill
   * (ConductionArea::setPadConnectionRules).
   */
  FastPlaneConnectivityGrid_PlaneFractureResult* partitionItemsByPadConnection(
      ConductionArea* ca, BasicBoard* board, jrt::Collection<Item*>* sameNetItems);
  /** The largest clearance the fill of the area keeps to any item on its layer. */
  static double maxClearanceOf(ConductionArea* ca, BasicBoard* board);
  /**
   * Numbers the connected pieces of the fill: writes the piece of each conductive cell into
   * pieceOfCell (-1 elsewhere) and returns the number of cells of each piece.
   */
  jrt::Array<int32_t>* labelPieces(jrt::Array<int32_t>* pieceOfCell);
  /** For each cell, whether one of its sample points is marked free of fill. */
  jrt::Array<bool>* partialCells();
  /** Whether the fill passes from a cell to its neighbour in the direction (dr, dc). */
  bool cellsTouch(int32_t row, int32_t col, int32_t dr, int32_t dc);
  static int32_t find(jrt::Array<int32_t>* parent, int32_t node);
  /** Java name `union` is a C++ keyword. */
  static void union_(jrt::Array<int32_t>* parent, int32_t a, int32_t b);
  /** Attaches a solidly connected pad to every piece of the fill that reaches its copper. */
  void attachSolidPad(
      ConductionArea* ca,
      Pin* pin,
      BasicBoard* board,
      double maxKeepout,
      jrt::Array<int32_t>* pieceOfCell,
      jrt::Array<int32_t>* parent,
      int32_t node);
  /**
   * Collects the copper of other nets on the layer of the area that overlaps the box, each shape
   * with the distance the fill keeps free around it. With netPads, also the pads of the net of the
   * area that carry a thermal relief.
   */
  static void collectForeignCopper(
      ConductionArea* ca,
      BasicBoard* board,
      IntBox* query,
      jrt::List<TileShape*>* foreignShapes,
      jrt::List<jrt::Array<double>*>* foreignClearances,
      jrt::List<int32_t>* netPads,
      jrt::Map<Item*, int32_t>* indexOfItem,
      jrt::Array<PadPlaneConnection_Mode>* modes,
      int32_t ownIndex);
  /** Attaches a pad with a thermal relief through the spokes that the fill keeps. */
  void attachThermalPad(
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
      int32_t pieceCount);
  /** Joins the node with every piece of the fill that has a free sample point within reach of the point. */
  void attachPiecesNear(
      FloatPoint* point, double reach, jrt::Array<int32_t>* pieceOfCell, jrt::Array<int32_t>* parent, int32_t node);
  /** Whether the cell has a free sample point no further than limit from the point. */
  bool hasFreeSampleNear(int32_t row, int32_t col, FloatPoint* point, double limit, double fine);
  /**
   * Places each trace of the net in the islands it touches, and returns the islands of each of
   * its ends.
   */
  static jrt::Map<Item*, jrt::Array<jrt::Array<int32_t>*>*>* attachTraces(
      ConductionArea* ca,
      jrt::List<Trace*>* traces,
      jrt::Map<int32_t, jrt::List<Item*>*>* islands,
      std::function<jrt::Set<int32_t>*(Trace*, Point*)> endIslands,
      std::function<jrt::Set<int32_t>*(Trace*)> wayIslands);
  /** The islands of these pieces of fill, in increasing order. */
  static jrt::Set<int32_t>* islandsOf(jrt::Set<int32_t>* pieces, std::function<int32_t(int32_t)> islandOfPiece);
  /** For each item, the islands it is in. */
  static jrt::Map<Item*, jrt::List<int32_t>*>* islandsOfItems(jrt::Map<int32_t, jrt::List<Item*>*>* islands);
  /**
   * The pad or via of the net centred on point, or else whose box contains it, if any; the lowest
   * id wins. nullptr if none.
   */
  static DrillItem* drillItemAt(Point* point, jrt::Collection<Item*>* items);
  /** The pieces of the fill that have a free sample point within reach of the point. */
  jrt::TreeSet<int32_t>* piecesNear(FloatPoint* point, double reach, jrt::Array<int32_t>* pieceOfCell);
  /** The pieces of the fill that the copper of a trace of the net touches on its way. */
  jrt::TreeSet<int32_t>* piecesAlong(Trace* trace, double minWidth, jrt::Array<int32_t>* pieceOfCell);
  /** Whether the cell has a free sample point no further than limit from the segment. */
  bool hasFreeSampleNearSegment(
      int32_t row, int32_t col, FloatPoint* from, FloatPoint* to, double limit, double fine);
  /** Clears the obstacles at the resolution of the sample points. */
  void initFineMask();
  /**
   * Marks as free of fill the sample points outside the area and those inside closer than reach
   * to its border or to one of its holes.
   */
  void rasterizeOutsideOf(Area* area, double reach);
  /** Marks the sample points of a row from startCol to endCol as free of fill. */
  void setFineSpan(int32_t row, int32_t startCol, int32_t endCol);
  /** Sets the bits of the sample points of a row from startCol to endCol in the mask. */
  void setFineSpan(jrt::Array<int64_t>* mask, int32_t row, int32_t startCol, int32_t endCol);
  bool isFineSet(int32_t row, int32_t col);
  /** Marks as free of fill the cells whose sample points all are. */
  void foldFineMask();
  /**
   * Marks as free of fill the sample points that are closer than distance to the copper of a
   * foreign item on the layer.
   */
  void rasterizeForeignItem(Item* item, int32_t layer, double distance);
  /**
   * Marks as free of fill the sample points closer than distance to the pad of the item on the
   * layer, if that pad is round (as a circle, not as its search-tree octagon).
   */
  bool rasterizeRoundPad(DrillItem* drillItem, int32_t layer, double distance);
  /** Marks as free of fill the sample points that are closer than distance to the board edge. */
  void rasterizeOutline(BoardOutline* outline, double distance);
  /** Marks as free of fill the sample points that are closer than reach to the segment, row by row. */
  void rasterizeSegment(FloatPoint* from, FloatPoint* to, double reach);
  /** Marks as free of fill the sample points that are closer to the shape than distance. */
  void rasterizeKeepout(TileShape* shape, double distance);
  /**
   * Marks as free of fill the sample points that are closer than reach to the convex polygon with
   * these corners (a segment with two, a point with one), row by row.
   */
  void rasterizeConvex(jrt::Array<FloatPoint*>* corners, double reach);
  /** Sets in the mask the sample points closer than reach to the convex polygon, or inside it. */
  void rasterizeConvex(jrt::Array<FloatPoint*>* corners, double reach, jrt::Array<int64_t>* mask);
  /** Whether two spokes that end in each other are joined by fill. */
  static bool spokesMeet(
      PadPlaneConnection_Spoke* own,
      PadPlaneConnection_Spoke* their,
      double minWidth,
      jrt::List<TileShape*>* foreignShapes,
      jrt::List<jrt::Array<double>*>* foreignClearances);
  /** The positions of a strip as wide as the minimum width across a spoke: middle, both sides. */
  static jrt::Array<double>* stripOffsets(double halfWidth, double minWidth);
  /** The centre line of a spoke from the edge of the pad to its end, moved sideways by offset. */
  static jrt::Array<FloatPoint*>* shifted(PadPlaneConnection_Spoke* spoke, double offset);
  /** The spokes from the centre of the pad instead of its edge, as the fill draws them. */
  static jrt::Array<PadPlaneConnection_Spoke*>* wholeSpokes(Pin* pin, jrt::Array<PadPlaneConnection_Spoke*>* fromEdge);
  /** Whether the point lies in the rectangle of the spoke, with a tolerance of one unit. */
  static bool isInSpoke(PadPlaneConnection_Spoke* spoke, double x, double y);
  /**
   * The share of the minimum width by which the zone filler of KiCad 10.0.8 is more lenient than
   * the exact distance (clearance plus half the minimum width) when it decides whether fill is left.
   */
  static constexpr double FILL_LENIENCY = 0.06;
  /** Whether foreign copper leaves a part of the spoke between two points of its centre line that the fill keeps. */
  static bool isStripFree(
      FloatPoint* start,
      FloatPoint* end,
      double halfWidth,
      double minWidth,
      TileShape* padShape,
      double gap,
      jrt::List<TileShape*>* foreignShapes,
      jrt::List<jrt::Array<double>*>* foreignClearances);
  /** Whether the far end of a spoke lies in fill, which KiCad asks of a spoke to keep it. */
  static bool isEndInFill(
      FloatPoint* start,
      FloatPoint* end,
      double minWidth,
      TileShape* padShape,
      double gap,
      jrt::List<TileShape*>* foreignShapes,
      jrt::List<jrt::Array<double>*>* foreignClearances);
  /** Whether the segment keeps the clearance of the fill plus half its minimum width, less the leniency. */
  static bool isFree(
      FloatPoint* from,
      FloatPoint* to,
      double leniency,
      jrt::List<TileShape*>* foreignShapes,
      jrt::List<jrt::Array<double>*>* foreignClearances);

 public:
  /** The distance between the segment from-to and a convex shape; 0 if they meet. */
  static double distance(FloatPoint* from, FloatPoint* to, TileShape* shape);

 private:
  static double segmentDistance(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2);
  static bool segmentsCross(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2);
  static double cross(FloatPoint* a, FloatPoint* b, FloatPoint* c);
  static double pointSegmentDistance(FloatPoint* p, FloatPoint* a, FloatPoint* b);

 public:
  /** Rasterizes the convex tile shapes of a conduction area into the copper mask. */
  virtual void rasterizeConductionArea(ConductionArea* ca);

 private:
  /**
   * Rasterizes the tile shapes of foreign obstacle areas, each enlarged by its clearance, into the
   * obstacle mask. The raster of exactly these shapes and clearances is taken from memo when it
   * has it.
   */
  void rasterizeObstacleAreas(
      jrt::List<Item*>* items,
      jrt::List<int32_t>* shapeIndices,
      jrt::List<TileShape*>* shapes,
      jrt::List<double>* clearances,
      FastPlaneConnectivityGrid_FractureMemo* memo);

 public:
  /** Rasterizes an obstacle shape into the obstacle mask. */
  virtual void rasterizeObstacleShape(Shape* shape);

 private:
  void rasterizeObstacleShape(Shape* shape, jrt::Array<int64_t>* mask);

 public:
  /** Rasterizes a thick line segment (capsule) into the obstacle mask (board units). */
  virtual void rasterizeThickSegment(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius);

 private:
  /** Partitions items into isolated connected components using BFS flood fill. */
  FastPlaneConnectivityGrid_PlaneFractureResult* partitionItemsByIsland(
      ConductionArea* ca, jrt::Collection<Item*>* items);
  /**
   * Partitions items into islands, given their locations (locations[i] is getItemLocation of
   * itemArr[i]).
   */
  FastPlaneConnectivityGrid_PlaneFractureResult* partitionItemsByIsland(
      ConductionArea* ca, jrt::Collection<Item*>* items, jrt::Array<Item*>* itemArr, jrt::Array<IntPoint*>* locations);
  /**
   * The conductive cells whose centre is no further from point than halfWidth and half a cell, in
   * row-major order.
   */
  jrt::List<int32_t>* fillCellsAround(IntPoint* point, double halfWidth);
  static constexpr int32_t SEED_UNKNOWN = -2;
  static constexpr int32_t SEED_NONE = -1;
  /** Returns the seed cell index of the item at index, computing it on first use. */
  int32_t seedCell(jrt::Array<int32_t>* seeds, int32_t index, IntPoint* location);
  /** Labels with islandId every conductive, not yet visited cell 4-connected to the start cell. */
  void runFloodFill(int32_t startCol, int32_t startRow, int16_t islandId);
  /** Visits one in-grid neighbour of the flood fill; returns the new queue tail. */
  int32_t visitCell(int32_t r, int32_t c, int32_t cell, int16_t islandId, int32_t tail);
  jrt::Array<int32_t>* findNearestConductiveCell(int32_t col, int32_t row);
  jrt::Array<int32_t>* findNearestConductiveCell(int32_t col, int32_t row, int32_t maxRadius);
  int32_t findNearestMappedIsland(int32_t col, int32_t row, int32_t maxRadius);
  static IntPoint* toIntPoint(Point* p);
  static IntPoint* getItemLocation(Item* item, ConductionArea* ca);

 public:
  /** Returns true if the cell contains conductive copper (inside pour and not blocked by obstacle). */
  virtual bool isConductive(int32_t col, int32_t row);

 private:
  bool isBitSet(jrt::Array<int64_t>* mask, int32_t row, int32_t col);
  void setBit(jrt::Array<int64_t>* mask, int32_t row, int32_t col);
  void setRowSpan(jrt::Array<int64_t>* mask, int32_t row, int32_t startCol, int32_t endCol);

 public:
  /** Returns current grid column count. */
  virtual int32_t getCols() { return cols; }
  /** Returns current grid row count. */
  virtual int32_t getRows() { return rows; }
  /** Returns current cell size in board units. */
  virtual int32_t getCellSize() { return cellSize; }
};

}  // namespace fr
