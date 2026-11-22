// Port of app.freerouting.board.model.structure.BoardOutline
#pragma once

#include "board/model/items/Item.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * The stretch of the outline next to one edge pin on one layer: the pin's bounding box, widened by
 * the clearance and the trace half width of the pin's nets. The outline lines are cut at its
 * borders, and the pieces inside it do not block traces of these nets on this layer. (Private Java
 * record.)
 */
class BoardOutline_EdgeWindow : public virtual jrt::Object {
 public:
  BoardOutline_EdgeWindow(IntBox* box, int32_t layer, jrt::Array<int32_t>* nets)
      : box_(box), layer_(layer), nets_(nets) {}

  IntBox* box() { return box_; }
  int32_t layer() { return layer_; }
  jrt::Array<int32_t>* nets() { return nets_; }

  /** Record semantics (the int[] component compares by identity, like Java). */
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  IntBox* box_;
  int32_t layer_;
  jrt::Array<int32_t>* nets_;
};

/** Class describing a board outline. */
class BoardOutline : public Item {
 public:
  using Item::isObstacle;  // keep Item::isObstacle(int) visible next to isObstacle(Item*)

  /** Creates a new instance of BoardOutline. */
  BoardOutline(jrt::Array<PolylineShape*>* shapes, int32_t clearanceClassIndex, int32_t id,
               BasicBoard* board);

  int32_t tileShapeCount() override;

  int32_t shapeLayer(int32_t index) override;

  /**
   * Returns the smallest gap, in board units, between the copper of any pin and the outline lines:
   * 0 if a pin touches or crosses the outline, +infinity if the board has no pins. Pad corners are
   * sampled (exact for polygonal pads, conservative for round pads).
   */
  double minimumPinGap();

  /** Forgets the cached edge pin nets (recomputed on demand). */
  void invalidateEdgePinNets();

  bool isTraceObstacle(int32_t netNumber) override;

  /**
   * Whether the tree shape with shapeIndex of this outline is an obstacle for traces of netNumber:
   * pieces next to an edge pin of the net are exempt.
   */
  bool isTraceObstacle(int32_t netNumber, int32_t shapeIndex) override;

  /**
   * Whether the tree shape with the given index is exempt for a trace with all the given nets, so
   * that the clearance check accepts what the router was allowed to do.
   */
  bool exemptsTrace(int32_t shapeIndex, jrt::Array<int32_t>* netNumbers);

  /**
   * A trace may cross the outline only when every one of its nets is an edge-pin net. A tie trace
   * that also carries an ordinary net stays blocked.
   */
  bool blocksNets(jrt::Array<int32_t>* netNumbers);

  /**
   * Like blocksNets, for a trace of the given nets whose shape overlaps this outline on the given
   * layer: false if every tree shape of this outline that the trace shape reaches, with its
   * clearance, lies next to an edge pin of these nets.
   */
  bool blocksTraceAt(TileShape* traceShape, int32_t layer, jrt::Array<int32_t>* netNumbers,
                     int32_t clearanceClassIndex);

  bool isObstacle(Item* other) override;

  IntBox* boundingBox() override;

  int32_t firstLayer() override;

  int32_t lastLayer() override;

  bool isOnLayer(int32_t layer) override;

  void translateBy(Vector* vector) override;

  void turn90Degree(int32_t factor, IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void changePlacementSide(IntPoint* pole) override;

  /** Returns the number of outline shapes. */
  int32_t shapeCount();

  /** Returns the outline shape with the given index. */
  PolylineShape* getShape(int32_t index);

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  /**
   * The board shape outside the outline curves, where a keepout will be generated. The outline
   * curves are holes of the keepoutArea.
   */
  Area* getKeepoutArea();

  /**
   * Returns the convex pieces of the keepout area that go into the search trees, cut finer where
   * the bounding octagon of a piece would reach into the board (TileShape::splitToTightOctagons).
   */
  jrt::Array<TileShape*>* getKeepoutTiles();

  /** package-private in Java. */
  jrt::Array<TileShape*>* getKeepoutLines();

  Item* copy(int32_t id) override;

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  // PORT NOTE: write(ObjectOutputStream) is dropped (Java serialization).

  /**
   * Returns, if keepout is generated outside the board outline. Otherwise, only the line shapes of
   * the outlines are inserted as keepout.
   */
  bool keepoutOutsideOutlineGenerated() { return keepoutOutsideOutline; }

  /**
   * Makes the area outside this Outline to Keepout, if value = true. Reinserts this Outline into
   * the search trees, if the value changes.
   */
  void generateKeepoutOutside(bool value);

  /** Returns the sum of the lines of all outline polygons. */
  int32_t lineCount();

  /**
   * Returns the shape of the outline line with the given index, counted over all outline polygons,
   * widened by halfWidth on both sides and cut by its neighbouring lines.
   */
  TileShape* lineShape(int32_t lineIndex, int32_t halfWidth);

  /**
   * Returns in how many slices the outline line with the given index goes into the search trees,
   * within MAX_LINE_SLICES. The count does not depend on the layer.
   */
  int32_t lineSlices(int32_t lineIndex);

  /**
   * Splits lineShape (the shape of the line with lineIndex, widened by widening) into the slices of
   * the line, cut at the edge windows.
   */
  jrt::Array<TileShape*>* splitLine(int32_t lineIndex, TileShape* lineShape, int32_t widening);

  /** Whether the copper of the pin touches or crosses the outline. */
  bool isEdgePin(Pin* pin);

  /**
   * Cuts the tree shapes of this outline again if the smallest trace half width of the board, net
   * classes included, changed since they were cut, and reinserts the outline into the search trees.
   */
  void updateSlicesToTraceWidths();

  /** Returns the number of line slices of all outline polygons on one layer. */
  int32_t lineSliceCount();

  /** Returns the half width of the lines of this outline. */
  int32_t getHalfWidth() { return HALF_WIDTH; }

  /** Returns whether the given point is contained within the board outline shapes. */
  bool contains(Point* point);

  /** Returns whether the given float point is contained within the board outline shapes. */
  bool contains(FloatPoint* point);

  /**
   * Erik Gavriluk (freerouting.net): whether a net with a pin on or beyond the outline may route
   * across the outline everywhere on the board. Default false: only the stretch of the outline next
   * to an edge pin is exempt, for the nets of that pin.
   */
  bool edgePinNetsCrossOutline = false;

 protected:
  jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) override;

 private:
  static constexpr int32_t HALF_WIDTH = 100;

  /** Upper bound of the slices of one outline line. */
  static constexpr int32_t MAX_LINE_SLICES = 256;

  /** Nets whose pins touch the outline. */
  jrt::Set<int32_t>* getEdgePinNets();

  double cornerGap(Shape* padShape);

  double distanceToOutline(FloatPoint* point);

  bool exemptsPiece(int32_t shapeIndex, int32_t netNumber);

  /** Forgets the cuts at the edge windows; they are made again with the line slices. */
  void clearLineCuts();

  /**
   * Cuts every outline line that an edge window touches at the window borders, and at the borders
   * of its equal slices, and records which pieces lie inside which windows. Updates slices.
   */
  void cutAtEdgeWindows(jrt::Array<int32_t>* slices,
                        jrt::Array<BoardOutline_EdgeWindow*>* windows);

  jrt::Array<BoardOutline_EdgeWindow*>* computeEdgeWindows();

  static bool sameWindows(jrt::Array<BoardOutline_EdgeWindow*>* a,
                          jrt::Array<BoardOutline_EdgeWindow*>* b);

  /** The tolerance of the current slices, chosen from the trace widths when first cut. */
  int32_t sliceTolerance();

  int32_t currentSliceTolerance();

  /** The board shapes inside the outline curves. */
  jrt::Array<PolylineShape*>* shapes;

  /** The board shape outside the outline curves (outline curves are holes). */
  Area* keepoutArea = nullptr;

  /** Used instead of keepoutArea if only the line shapes of the outlines are inserted as keepout. */
  jrt::Array<TileShape*>* keepoutLines = nullptr;

  bool keepoutOutsideOutline = false;

  /** Convex pieces of the keepout area (rebuilt on demand). */
  jrt::Array<TileShape*>* keepoutTiles = nullptr;

  /**
   * Number of slices per outline line (rebuilt on demand).
   * PORT NOTE: renamed from lineSlices (clashes with the method of that name).
   */
  jrt::Array<int32_t>* lineSlices_ = nullptr;

  /**
   * The tolerance keepoutTiles and lineSlices were cut with, 0 before they are cut.
   * PORT NOTE: renamed from sliceTolerance (clashes with the method of that name).
   */
  int32_t sliceTolerance_ = 0;

  jrt::Set<int32_t>* edgePinNets = nullptr;
  int32_t cachedItemCount = -1;
  int32_t countedPins = -1;
  int64_t countedVersion = -1;
  UndoableObjects* countedList = nullptr;

  /** Edge pin windows the line pieces were cut with; rebuilt with lineSlices. */
  jrt::Array<BoardOutline_EdgeWindow*>* edgeWindows = nullptr;

  /**
   * Per outline line, the coordinates its tree shapes are cut at along lineCutAlongX, or null if no
   * edge window touches the line.
   */
  jrt::Array<jrt::Array<int32_t>*>* lineCuts = nullptr;

  jrt::Array<bool>* lineCutAlongX = nullptr;

  /** Per line piece of one layer, the indices of the edge windows that contain it. */
  jrt::Array<jrt::Array<int32_t>*>* pieceWindows = nullptr;

  int32_t lineSliceTotal = -1;
};

}  // namespace fr
