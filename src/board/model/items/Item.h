// Port of app.freerouting.board.model.items.Item (+ nested enum StopConnectionOption).
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "board/searchtree/SearchTreeObject.h"
#include "datastructures/UndoableObjects.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Used as parameter of get_connection to control, that the connection stops at the next fanout
 * via or at any via.
 */
enum class Item_StopConnectionOption : int32_t { NONE, FANOUT_VIA, VIA };

inline std::string Item_StopConnectionOption_name(Item_StopConnectionOption e) {
  static const char* const kNames[] = {"NONE", "FANOUT_VIA", "VIA"};
  return kNames[static_cast<int32_t>(e)];
}
inline std::string Item_StopConnectionOption_toString(Item_StopConnectionOption e) {
  return Item_StopConnectionOption_name(e);
}
inline jrt::Array<Item_StopConnectionOption>* Item_StopConnectionOption_values() {
  using E = Item_StopConnectionOption;
  return jrt::Array<E>::of({E::NONE, E::FANOUT_VIA, E::VIA});
}
inline Item_StopConnectionOption Item_StopConnectionOption_valueOf(const std::string& s) {
  for (auto e : *Item_StopConnectionOption_values()) {
    if (Item_StopConnectionOption_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.board.model.items.Item.StopConnectionOption." + s);
}

/**
 * Basic class of the items on a board.
 *
 * Java: `abstract class Item implements SearchTreeObject, ItemInfoPrinter.Printable,
 * UndoableObjects.Storable, Serializable`. SearchTreeObject extends ShapeTree.Storable; both
 * Storable interfaces extend Comparable<Object> (-> compareTo(jrt::Object*)), UndoableObjects.Storable
 * declares clone().
 *
 * Item does NOT override equals/hashCode (identity semantics); it implements compareTo by id
 * (`other.id - this.id`, i.e. DESCENDING id order in TreeSet<Item>).
 *
 * PORT NOTE: Java package-private members are public in C++. Serialization (`transient`,
 * `write(ObjectOutputStream)`) is dropped: the abstract `write` method and all its overrides are
 * not ported.
 */
class Item : public virtual SearchTreeObject,
             public virtual ItemInfoPrinter_Printable,
             public virtual UndoableObjects_Storable {
 private:
  static constexpr double PROTECT_FANOUT_LENGTH = 400;
  int32_t id;  // final

 public:
  /** The board this Item is on. */
  BasicBoard* board;

  double smallestClearance = -1.0;

 protected:
  /** Not 0, if this item belongs to a component. */
  int32_t componentId;

 public:
  /** The nets, to which this item belongs. */
  jrt::Array<int32_t>* netNumbers;

 private:
  /**
   * The index in the clearance matrix describing the required spacing to other items.
   * PORT NOTE: renamed from clearanceClassIndex (clashes with method clearanceClassIndex()).
   */
  int32_t clearanceClassIndex_;

  /** Points to the entries of this item in the ShapeSearchTrees. */
  ItemSearchTreesInfo* searchTreesInfo = nullptr;

  // PORT NOTE: Java's FixedState field is nullable in theory (some null checks), but every
  // constructor caller passes a value and isUserFixed() dereferences it unconditionally; the port
  // keeps a plain enum and treats the `!= null` checks as always true.
  FixedState fixedState;

  /** False, if the item is deleted or not inserted into the board. */
  bool onTheBoard = false;

  /** Temporary data used in the autoroute algorithm. */
  ItemAutorouteInfo* autorouteInfo = nullptr;

  /**
   * Set while a plane pad searches its own way into its plane: this item of the same net is then
   * an obstacle like copper of another net, so the pad cannot reach the plane through it.
   */
  bool planeEscapeObstacle = false;

 public:
  Item(jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex, int32_t id,
       int32_t componentId, FixedState fixedState, BasicBoard* board);

  /** Implements the comparable interface. */
  int32_t compareTo(jrt::Object* other) final;

  /**
   * PORT NOTE: not in Java. a->compareTo(b) for two items (compareTo is final): jrt::compareOf
   * calls it for item pointers, without the dynamic type test of b.
   */
  static int32_t jrtCompareSame(Item* a, Item* b) { return b->id - a->id; }

  /**
   * PORT NOTE: Java's Item does not override equals: Object identity, made final here so that
   * jrt collections compare item pointers directly (jrt::equalsOf, jrt_identity_equals).
   */
  bool equals(jrt::Object* other) final { return this == other; }
  using jrt_identity_equals = void;

  /** Returns the unique identification number of this item. */
  int32_t getId() override { return id; }

  /** Returns the neutral semantic category of this board item. */
  virtual BoardItemType getBoardItemType();

  /** Returns true if the net number array of this item contains netNumber. */
  virtual bool containsNet(int32_t netNumber);

  bool isObstacle(int32_t netNumber) override { return !containsNet(netNumber); }

  /** Returns, if this item in not allowed to overlap with other. */
  virtual bool isObstacle(Item* other) = 0;

  using SearchTreeObject::isTraceObstacle;  // keep isTraceObstacle(int, int) visible
  bool isTraceObstacle(int32_t netNumber) override {
    return planeEscapeObstacle || !containsNet(netNumber);
  }

  /** See planeEscapeObstacle. */
  virtual bool isPlaneEscapeObstacle() { return planeEscapeObstacle; }

  /** See planeEscapeObstacle. */
  virtual void setPlaneEscapeObstacle(bool value) { this->planeEscapeObstacle = value; }

  /** Returns true if the net number arrays of this and other have a common number. */
  virtual bool sharesNet(Item* other) { return this->sharesNetNo(other->netNumbers); }

  /** Returns true if the net number array of this and netNumbers have a common number. */
  virtual bool sharesNetNo(jrt::Array<int32_t>* netNumbers);

  /** Returns the number of shapes of this item after decomposition into convex polygonal shapes. */
  virtual int32_t tileShapeCount() = 0;

  /** Returns the index-th shape of this item after decomposition into convex polygonal shapes. */
  virtual TileShape* getTileShape(int32_t index);

  int32_t treeShapeCount(ShapeTree* tree) override;

  TileShape* getTreeShape(ShapeTree* tree, int32_t index) override;

 private:
  jrt::Array<TileShape*>* getPrecalculatedTreeShapes(ShapeTree* tree);

 protected:
  /** Calculates the tree shapes for this item for searchTree. */
  virtual jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) = 0;

 public:
  /** Returns false, if this item is deleted oor not inserted into the board. */
  virtual bool isOnTheBoard() { return this->onTheBoard; }

  virtual void setOnTheBoard(bool value) { this->onTheBoard = value; }

  /**
   * Creates a copy of this item with ID id. If id <= 0, the id of the new item is generated
   * internally.
   *
   * PORT NOTE (board deep copy): copy(id) is a faithful port of Java's copy and, like it, does NOT
   * carry these non-transient Item fields that Java serialization would keep: smallestClearance
   * (reset to -1), onTheBoard (only clone() copies it). The copy is created on this->board (the
   * source board); a deep copy must re-point `board` to the copied board.
   */
  virtual Item* copy(int32_t id) = 0;

  /** copy(getId()) with onTheBoard copied (UndoableObjects.Storable.clone). */
  jrt::Object* clone() override;

  /** Returns true, if the layer range of this item contains layer. */
  virtual bool isOnLayer(int32_t layer) = 0;

  /** Returns the number of the first layer containing geometry of this item. */
  virtual int32_t firstLayer() = 0;

  /** Returns the number of the last layer containing geometry of this item. */
  virtual int32_t lastLayer() = 0;

  // PORT NOTE: `abstract boolean write(ObjectOutputStream)` dropped (serialization).

  /** Translates the shapes of this item by vector. Does not move the item in the board. */
  virtual void translateBy(Vector* vector) = 0;

  /** Turns this Item by factor times 90 degree around pole. Does not update the item in the board. */
  virtual void turn90Degree(int32_t factor, IntPoint* pole) = 0;

  /** Rotates this Item by angleInDegree around pole. Does not update the item in the board. */
  virtual void rotateApprox(double angleInDegree, FloatPoint* pole) = 0;

  /**
   * Changes the placement side of this Item and mirrors it at the vertical line through pole. Does
   * not update the item in the board.
   */
  virtual void changePlacementSide(IntPoint* pole) = 0;

  /** Returns a box containing the geometry of this item. */
  virtual IntBox* boundingBox() = 0;

  /** Translates this item by vector in the board. */
  virtual void moveBy(Vector* vector);

  /** Returns true, if some shapes of this item and other are on the same layer. */
  virtual bool sharesLayer(Item* other);

  /** Returns the first layer, where both this item and other have a shape, or -1. */
  virtual int32_t firstCommonLayer(Item* other);

  /** Returns the last layer, where both this item and other have a shape, or -1. */
  virtual int32_t lastCommonLayer(Item* other);

  /** Return the name of the component of this item or null (nullopt). */
  virtual std::optional<std::string> componentName();

  /** Returns the count of clearance violations of this item with other items. */
  virtual int32_t clearanceViolationCount();

  /**
   * Returns a list of all clearance violations of this item with other items. The firstItem in
   * such an object is always this item.
   */
  virtual jrt::Collection<ClearanceViolation*>* clearanceViolations();

 private:
  static bool overlapsBeyond(TileShape* shape1, TileShape* shape2, double depth);

  double calculateClearanceBetweenTwoShapes(TileShape* rawShape1, TileShape* rawShape2,
                                            double minimumClearance, int32_t clComp1,
                                            int32_t clComp2);

  /**
   * Returns true if every corner of tileShape is contained within at least one of the
   * BoardOutline's polygon shapes.
   */
  static bool outlineContainsTileShape(BoardOutline* outline, TileShape* tileShape);

 public:
  /**
   * Returns all connectable Items with a direct contacts to this item. The result will be empty,
   * if this item is not connectable.
   */
  virtual jrt::Set<Item*>* getAllContacts();

  /** Returns all connectable Items with a direct contacts to this item on the input layer. */
  virtual jrt::Set<Item*>* getAllContacts(int32_t layer);

  /** Checks, if this item is electrically connected to another connectable item. */
  virtual bool isConnected();

  /** Checks, if this item is electrically connected to another connectable item on layer. */
  virtual bool isConnectedOnLayer(int32_t layer);

  /** Default implementation to be overwritten in the Connectable subclasses. */
  virtual jrt::Set<Item*>* getNormalContacts();

 private:
  /**
   * The normal contacts, for a walk over the connected items that only reads them: they are reused
   * while no item of the board changes.
   */
  jrt::Collection<Item*>* normalContactsForWalk();

 public:
  /**
   * Returns the contact point, if this item and other are Connectable and have a unique normal
   * contact. Returns null otherwise.
   */
  virtual Point* normalContactPoint(Item* other) { return nullptr; }

  /** Auxiliary function (package-private in Java). */
  virtual Point* normalContactPoint(Trace* other) { return nullptr; }

  /** Auxiliary function (package-private in Java). */
  virtual Point* normalContactPoint(DrillItem* other) { return nullptr; }

  /**
   * Returns the set of all Connectable items of the net with number netNumber which can be
   * reached recursively via normal contacts from this item. If netNumber <= 0, the net number is
   * ignored.
   */
  virtual jrt::Set<Item*>* getConnectedSet(int32_t netNumber) {
    return getConnectedSet(netNumber, false);
  }

  /**
   * As above. If stopAtPlane, the recursive algorithm stops, when a conduction area is reached,
   * which does not belong to a component.
   */
  virtual jrt::Set<Item*>* getConnectedSet(int32_t netNumber, bool stopAtPlane);

  /** Recursive part of get_connected_set (package-private in Java). */
  virtual void getConnectedSetRecu(jrt::Set<Item*>* result, int32_t netNumber, bool stopAtPlane);

  /** Returns true, if this item contains some overlap to be cleaned. */
  virtual bool isOverlap() { return false; }

  /**
   * Recursive part of Trace.is_cycle. If ignoreAreas is true, cycles where conduction areas are
   * involved are ignored (package-private in Java).
   */
  virtual bool isCycleRecu(jrt::Set<Item*>* visitedItems, Item* searchItem, Item* comeFromItem,
                           bool ignoreAreas);

  /**
   * Returns the set of all Connectable items belonging to the net with number netNumber, which are
   * not in the connected set of this item.
   */
  virtual jrt::Set<Item*>* getUnconnectedSet(int32_t netNumber);

  /** Returns all traces and vias from this item until the next fork or terminal item. */
  virtual jrt::Set<Item*>* getConnectionItems() {
    return getConnectionItems(Item_StopConnectionOption::NONE);
  }

  /**
   * Returns all traces and vias from this item until the next fork or terminal item. Stops at the
   * next fanout via (FANOUT_VIA) or at any via (VIA).
   */
  virtual jrt::Set<Item*>* getConnectionItems(Item_StopConnectionOption stopOption);

  /** Function to be overwritten by classes Trace and Via. */
  virtual bool isTail() { return false; }

  /** Returns all corners of this item, which are used for displaying the ratsnest. */
  virtual jrt::Array<Point*>* getRatsnestCorners() { return new (jrt::kGeom) jrt::Array<Point*>(0); }

  /** Test function checking the item for inconsistencies. */
  virtual bool validate();

  /** Returns for this item the layer of the shape with index index. */
  int32_t shapeLayer(int32_t index) override = 0;

  /** Returns true, if it is not allowed to change this item except shoving the item. */
  virtual bool isUserFixed();

  /** Returns true, if it is not allowed to delete this item. */
  virtual bool isDeletionForbidden();

  /** Returns true, if it is not allowed to change the location of this item by the push algorithm. */
  virtual bool isShoveFixed();

  /** Returns the fixed state of this Item. */
  virtual FixedState getFixedState() { return this->fixedState; }

  /** Fixes the item. */
  virtual void setFixedState(FixedState fixedState);

  /** Returns false, if this item is an obstacle for vias with the input net number. */
  virtual bool isDrillable(int32_t netNumber) { return false; }

  /**
   * Unfixes the item, if it is not fixed by the system or the input. A preserved route on the
   * board is also split where other copper of its net ends inside it.
   */
  virtual void unfix();

 private:
  void noteIfPreservedChanged(FixedState oldState);

 public:
  /** Returns whether this item is a preserved route (user-fixed or input-fixed). */
  virtual bool isPreservedRoute();

  /** Returns true, if this item is an unfixed trace or via, so it can be routed by auto-router. */
  virtual bool isRoutable() { return false; }

  /** Returns, if this item can be routed to. */
  virtual bool isConnectable();

  /** Returns the count of nets this item belongs to. */
  virtual int32_t netCount() { return netNumbers->length; }

  /** Returns the no-th net number of this item for 0 <= no < netCount(). */
  virtual int32_t getNetNumber(int32_t no) { return (*netNumbers)[no]; }

  /** Return the component ID of this item or 0, if it does not belong to a component. */
  virtual int32_t getComponentId() { return componentId; }

  /** Removes netNumber from the net number array. Returns false, if it was not contained. */
  virtual bool removeFromNet(int32_t netNumber);

  /** Returns the index in the clearance matrix describing the required spacing to other items. */
  virtual int32_t clearanceClassIndex() { return clearanceClassIndex_; }

  /** Sets the index in the clearance matrix describing the required spacing to other items. */
  virtual void setClearanceClassIndex(int32_t index);

  /** Changes the clearance class of this item and updates the search tree. */
  virtual void changeClearanceClassIndex(int32_t index);

  /** Assigns this item to the component with the input component ID. */
  virtual void assignComponentId(int32_t id);

  /**
   * Makes this item connectable and assigns it to the input net. If netNumber < 0, the net items
   * net number will be removed and the item will no longer be connectable.
   */
  virtual void assignNetNo(int32_t netNumber);

  /** Assigns multiple net numbers to this item (e.g., for net ties). */
  virtual void assignNetNumbers(jrt::Array<int32_t>* netNumbers);

  /** Returns true, if item is contained in the input filter. */
  virtual bool isSelectedByFilter(ItemSelectionFilter* filter) = 0;

 protected:
  /** Internally used for implementing the function is_selected_by_filter. */
  virtual bool isSelectedByFixedFilter(ItemSelectionFilter* filter);

 public:
  /** Sets the item tree entries for the tree with identification number treeNo. */
  void setSearchTreeEntries(jrt::Array<ShapeTree_Leaf*>* treeEntries, ShapeTree* tree) override;

  /** Returns the tree entries for the tree, or null, if for this tree no entries are inserted. */
  virtual jrt::Array<ShapeTree_Leaf*>* getSearchTreeEntries(ShapeSearchTree* tree);

  /** Sets the precalculated tree shapes for the tree. */
  virtual void setPrecalculatedTreeShapes(jrt::Array<TileShape*>* shapes, ShapeSearchTree* tree);

  /** Sets the search tree entries of this item to null. */
  // PORT NOTE (stage C): the item owns its search-tree info; callers have removed the leaves
  // from the trees (or handed them to another trace) before.
  virtual void clearSearchTreeEntries();
  ItemSearchTreesInfo* newSearchTreesInfo();

  /** Gets the information for the autoroute algorithm. Creates it, if it does not yet exist. */
  virtual ItemAutorouteInfo* getAutorouteInfo();

  /** Gets the information for the autoroute algorithm. */
  virtual ItemAutorouteInfo* getAutorouteInfoPur() { return autorouteInfo; }

  /** Clears the data allocated for the autoroute algorithm. */
  virtual void clearAutorouteInfo() { autorouteInfo = nullptr; }

  /** PORT NOTE: not in Java. Whether clearAutorouteInfo would clear anything. */
  virtual bool holdsAutorouteInfo() { return autorouteInfo != nullptr; }

  /** Clear all cached or derived data, so that they have to be recalculated. */
  virtual void clearDerivedData();

  /** Gets the information for hover event to display. */
  virtual std::string getHoverInfo(const std::string& locale) { return ""; }

  /** Internal function used in the implementation of get_hover_info. */
  virtual std::string getConnectableItemHoverInfo(const std::string& locale) {
    return this->getNetHoverInfo(locale);
  }

  /** Internal function used in the implementation of get_hover_info. */
  virtual std::string getNetHoverInfo(const std::string& locale);

 protected:
  /** Internal functions used in the implementation of print_info. */
  virtual void printNetInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printClearanceInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printFixedInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printContactInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printClearanceViolationInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printConnectableItemInfo(ItemInfoPrinter* printer, const std::string& locale);
  virtual void printItemInfo(ItemInfoPrinter* printer, const std::string& locale);

 public:
  /** Checks, if all nets of this items are normal. */
  virtual bool netsNormal();

  /** Checks, if this item and other contain exactly the same net numbers. */
  virtual bool netsEqual(Item* other) { return netsEqual(other->netNumbers); }

  /** Checks, if this item contains exactly the nets in netNumbers. */
  virtual bool netsEqual(jrt::Array<int32_t>* netNumbers);

  /**
   * Returns true, if the via is directly ob by a trace connected to a nearby SMD-pin. If
   * ignoreItems != null, contact traces in ignoreItems are ignored.
   */
  virtual bool isFanoutVia(jrt::Set<Item*>* ignoreItems);

  /** Checks if the item has a net that must be ignored by the auto-router. */
  virtual bool hasIgnoredNets();

  /** Lower-case simple class name, plus " of component #<id>" if componentId > 0. */
  std::string toString() override;

  /** GetAllNets. */
  virtual jrt::List<Net*>* getAllNets();

  /** Net names joined by ",", or "no nets". */
  virtual std::string getAllNetNames();
};

/**
 * PORT NOTE (not a Java type): a search key for a board's item list, which is ordered by
 * Item::compareTo (by id): compares like an item with this id, so UndoableObjects::findVisible
 * finds the item with an id in O(log n) instead of walking the list.
 */
class ItemIdProbe final : public virtual UndoableObjects_Storable {
 public:
  explicit ItemIdProbe(int32_t id) : id(id) {}
  const int32_t id;
  /** Like Item::compareTo: other.id - this.id. */
  int32_t compareTo(jrt::Object* other) override;
  jrt::Object* clone() override { return nullptr; }
};

}  // namespace fr
