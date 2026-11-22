// Port of app.freerouting.board.trace.PolylineTrace.
#pragma once

#include "board/model/items/Trace.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Objects of class Trace, whose geometry is described by a Polyline. */
class PolylineTrace : public Trace {
 private:
  // primary data
  Polyline* lines;

 public:
  /** Creates a new instance of a PolylineTrace with the input data. */
  PolylineTrace(Polyline* polyline, int32_t layer, int32_t halfWidth,
                jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex, int32_t id,
                int32_t groupId, FixedState fixedState, BasicBoard* board);

  Item* copy(int32_t id) override;

  /** Checks, if this trace is on layer layer. */
  bool isOnLayer(int32_t layer) override { return getLayer() == layer; }

  /**
   * Returns the first corner of this trace, which is the intersection of the first and second
   * lines of its polyline.
   */
  Point* firstCorner() override;

  /**
   * Returns the last corner of this trace, which is the intersection of the last two lines of its
   * polyline.
   */
  Point* lastCorner() override;

  /** Returns the number of corners of this trace (number of lines of its polyline minus one). */
  virtual int32_t cornerCount();

  double getLength() override;

  IntBox* boundingBox() override;

  bool contains(Point* point) override;

  bool centreLineContains(Point* point) override;

  /** Returns the polyline of this trace. */
  virtual Polyline* polyline() { return lines; }

  /** Replaces the stored polyline for the search-tree adapter (package-private in Java). */
  virtual void setPolyline(Polyline* newPolyline) { lines = newPolyline; }

 protected:
  jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) override;

 public:
  /** Returns the count of tile shapes of this polyline. */
  int32_t tileShapeCount() override;

  void translateBy(Vector* vector) override;

  void turn90Degree(int32_t factor, IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void changePlacementSide(IntPoint* pole) override;

  /**
   * Checks if other traces can be combined with this trace. Returns true, if something has been
   * combined. This trace will be the combined trace, so that only other traces may be deleted.
   */
  bool combine() override;

 private:
  /**
   * Checks if this trace can be combined at its first point with another trace. Returns true if
   * something was combined. The corners of the other trace will be inserted in front of this
   * trace. In case of combine the other trace will be deleted and this trace will remain.
   */
  bool combineAtStart(bool ignoreAreas);

  /**
   * Checks if this trace can be combined at its last point with another trace. Returns true if
   * something was combined. The corners of the other trace will be inserted at the end of this
   * trace. In case of combine the other trace will be deleted and this trace will remain.
   */
  bool combineAtEnd(bool ignoreAreas);

 public:
  /**
   * Looks up traces intersecting with this trace and splits them at the intersection points. In
   * case of an overlaps, the traces are split at their first and their last common point. Returns
   * the pieces resulting from splitting. If clipShape != null, splitting is restricted to
   * clipShape.
   */
  jrt::Collection<PolylineTrace*>* split(IntOctagon* clipShape) override;

  /**
   * Splits this trace into two at point. Returns the 2 pieces of the split trace, or null if
   * nothing was split because for example point is not located on a line segment of the trace.
   */
  jrt::Array<Trace*>* split(Point* point) override;

 private:
  /**
   * Splits this trace at the line with number lineIndex into two by inserting endline as
   * concluding line of the first split piece and as the start line of the second split piece.
   * Returns the 2 pieces of the split trace, or null, if nothing was split.
   */
  jrt::Array<PolylineTrace*>* split(int32_t lineIndex, Line* newEndLine);

  /**
   * Checks, if the intersection of the lineIndex-th line of this trace with line is inside the pad
   * of a pin. In this case the trace will be split only, if the intersection is at the center of
   * the pin.
   */
  bool splitInsideDrillPadProhibited(int32_t lineIndex, Line* line);

 public:
  /**
   * Splits this trace and overlapping traces, and combines this trace. Returns true, if something
   * was changed. If clipShape != null, splitting is restricted to clipShape.
   */
  virtual bool normalize(IntOctagon* clipShape);

  /** Tries to shorten this trace without creating clearance violations. */
  bool pullTight(TraceTightener* pullTightAlgo) override;

  /** Tries to pull this trace tight without creating clearance violations. */
  virtual bool pullTight(bool ownNetOnly, int32_t pullTightAccuracy, Stoppable* stoppableThread);

  /** Tries to smoothen the end corners of this trace, which are at a fork with other traces. */
  virtual bool smoothenEndCornersFork(bool ownNetOnly, int32_t pullTightAccuracy,
                                      Stoppable* stoppableThread);

  TileShape* getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) override;

  // PORT NOTE: write(ObjectOutputStream) dropped (serialization).

  /** Changes the geometry of this trace to newPolyline. */
  virtual void change(Polyline* newPolyline);

  /**
   * Checks that the connection restrictions to the contact pins are satisfied. If atStart, the
   * start of this trace is checked, else the end. Returns false if a pin restriction is violated.
   */
  bool checkConnectionToPin(bool atStart) override;

  /**
   * Tries to correct a connection restriction of this trace. If atStart, the start of the trace
   * polygon is corrected, else the end. Returns true, if this trace was changed.
   */
  virtual bool correctConnectionToPin(bool atStart, AngleRestriction angleRestriction);

  /**
   * Looks, if another pin connection restriction fits better than the current connection
   * restriction and changes this trace in this case. If atStart, the start of the trace polygon
   * is changed, else the end. Returns true, if this trace was changed.
   */
  virtual bool swapConnectionToPin(bool atStart);
};

}  // namespace fr
