// Port of app.freerouting.board.model.items.Trace.
#pragma once

#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Class describing functionality required for traces in the plane. */
class Trace : public Item, public virtual Connectable {
 private:
  int32_t halfWidth;  // final; half width of the trace pen
  int32_t layer;      // board layer of the trace

 public:
  Trace(int32_t layer, int32_t halfWidth, jrt::Array<int32_t>* netNumbers,
        int32_t clearanceClassIndex, int32_t id, int32_t groupId, FixedState fixedState,
        BasicBoard* board);

  // --- PORT NOTE: Connectable final overriders forwarding to Item (see Connectable.h) ---
  bool containsNet(int32_t netNumber) override { return Item::containsNet(netNumber); }
  bool sharesNetNo(jrt::Array<int32_t>* netNumbers) override { return Item::sharesNetNo(netNumbers); }
  jrt::Set<Item*>* getAllContacts() override { return Item::getAllContacts(); }
  jrt::Set<Item*>* getAllContacts(int32_t layer) override { return Item::getAllContacts(layer); }
  using Item::getConnectedSet;
  jrt::Set<Item*>* getConnectedSet(int32_t netNumber) override {
    return Item::getConnectedSet(netNumber);
  }

  using Item::isObstacle;

  /** Returns the first corner of the trace. */
  virtual Point* firstCorner() = 0;

  /** Returns the last corner of the trace. */
  virtual Point* lastCorner() = 0;

  int32_t firstLayer() override { return this->layer; }

  int32_t lastLayer() override { return this->layer; }

  virtual int32_t getLayer() { return this->layer; }

  virtual void setLayer(int32_t layer) { this->layer = layer; }

  virtual int32_t getHalfWidth() { return halfWidth; }

  /** Returns the length of this trace. */
  virtual double getLength() = 0;

  /**
   * Returns the half width enlarged by the clearance compensation value for the search tree.
   * Equals getHalfWidth(), if no clearance compensation is used in this tree.
   */
  virtual int32_t getCompensatedHalfWidth(ShapeSearchTree* searchTree);

  bool isObstacle(Item* other) override;

  /** All items with a connection point on the layer of this trace equal to its first corner. */
  virtual jrt::Set<Item*>* getStartContacts() { return getNormalContacts(firstCorner(), false); }

  /** All items with a connection point on the layer of this trace equal to its last corner. */
  virtual jrt::Set<Item*>* getEndContacts() { return getNormalContacts(lastCorner(), false); }

  /** Returns true if the centerline of this trace contains the specified point. */
  virtual bool contains(Point* point) = 0;

  /** Returns true if point lies on the centre line of this trace, its corners included. */
  virtual bool centreLineContains(Point* point) { return contains(point); }

  bool isPreservedRoute() override;

  /**
   * A preserved route cannot be split, so a branch that ends on it is joined where it ends: on
   * such a trace every point of the centre line is a connection point. Another trace connects only
   * at its end corners.
   */
  virtual bool isConnectionPoint(Point* point);

  /** The points away from its end corners where this trace meets its contacts. */
  virtual jrt::List<Point*>* interiorJunctions();

  /** Splits this trace at each of points inside it; a point it cannot be split at is skipped. */
  virtual void splitAt(jrt::List<Point*>* points);

  Point* normalContactPoint(Item* other) override;

  /** (package-private in Java) */
  Point* normalContactPoint(DrillItem* drillItem) override;

  /** (package-private in Java) */
  Point* normalContactPoint(Trace* other) override;

  jrt::Set<Item*>* getNormalContacts() override;

  /**
   * Get a list of all items having a connection point at point on the layer of this trace. If
   * ignoreNet is false, only contacts to items sharing a net with this trace are calculated.
   */
  virtual jrt::Set<Item*>* getNormalContacts(Point* point, bool ignoreNet);

  bool isRoutable() override;

  /** Returns true if an endpoint dangles, including traces with unsplit interior junctions. */
  bool isTail() override;

 private:
  /**
   * Whether the first or last corner touches something. A plane under the end counts only if the
   * end touches its fill.
   */
  bool hasContactAt(bool first);

 public:
  /** Returns whether another trace endpoint or drill center contacts this trace's interior. */
  virtual bool hasInteriorContacts();

 private:
  bool isInteriorPoint(Point* point);

 public:
  bool isDrillable(int32_t netNumber) override;

  /** Looks, if this trace is connected to the same object at its start and its end point. */
  bool isOverlap() override;

  /** Returns true, if it is not allowed to change the location of this item by the push algorithm. */
  bool isShoveFixed() override;

  /** Returns the endpoint of this trace with the shortest distance to fromPoint. */
  virtual Point* nearestEndPoint(Point* fromPoint);

  /** Checks, if this trace can be reached by other items via more than one path. */
  virtual bool isCycle();

  int32_t shapeLayer(int32_t index) override { return layer; }

  jrt::Array<Point*>* getRatsnestCorners() override;

  /**
   * Checks that the connection restrictions to the contact pins are satisfied. If atStart, the
   * start of this trace is checked, else the end. Returns false if a pin restriction is violated.
   */
  virtual bool checkConnectionToPin(bool atStart) = 0;

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  /**
   * Looks up touching pins at the first corner and the last corner of the trace. Used to avoid
   * acid traps.
   */
  virtual jrt::Set<Pin*>* touchingPinsAtEndCorners();

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  std::string getHoverInfo(const std::string& locale) override;

  bool validate() override;

  /** Checks if this trace can be combined with other traces. Returns true if something was combined. */
  virtual bool combine() = 0;

  /**
   * Looks up traces intersecting with this trace and splits them at the intersection points. In
   * case of an overlaps, the traces are split at their first and their last common point.
   * Returns the pieces resulting from splitting. If clipShape != null, splitting is restricted to
   * clipShape.
   */
  virtual jrt::Collection<PolylineTrace*>* split(IntOctagon* clipShape) = 0;

  /**
   * Splits this trace into two at point. Returns the 2 pieces of the split trace, or null if
   * nothing was split.
   */
  virtual jrt::Array<Trace*>* split(Point* point) = 0;

  /**
   * Tries to make this trace shorter according to its rules. Returns true if the geometry of the
   * trace was changed.
   */
  virtual bool pullTight(TraceTightener* pullTightAlgo) = 0;
};

}  // namespace fr
