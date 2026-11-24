#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "board/actions/ItemSearchTreesInfo.h"

#include <unordered_map>
#include <vector>

namespace fr {

/** Java: BasicBoard.DominantSide. */
enum class BasicBoard_DominantSide : int32_t { FRONT, BACK, NONE };

inline std::string BasicBoard_DominantSide_name(BasicBoard_DominantSide e) {
  switch (e) {
    case BasicBoard_DominantSide::FRONT:
      return "FRONT";
    case BasicBoard_DominantSide::BACK:
      return "BACK";
    case BasicBoard_DominantSide::NONE:
      return "NONE";
  }
  return "";
}
inline std::string BasicBoard_DominantSide_toString(BasicBoard_DominantSide e) {
  return BasicBoard_DominantSide_name(e);
}
inline jrt::Array<BasicBoard_DominantSide>* BasicBoard_DominantSide_values() {
  return jrt::Array<BasicBoard_DominantSide>::of({BasicBoard_DominantSide::FRONT,
                                                   BasicBoard_DominantSide::BACK,
                                                   BasicBoard_DominantSide::NONE});
}
inline BasicBoard_DominantSide BasicBoard_DominantSide_valueOf(const std::string& name) {
  if (name == "FRONT") return BasicBoard_DominantSide::FRONT;
  if (name == "BACK") return BasicBoard_DominantSide::BACK;
  if (name == "NONE") return BasicBoard_DominantSide::NONE;
  throw jrt::IllegalArgumentException("No enum constant BasicBoard.DominantSide." + name);
}

/**
 * PORT NOTE (not a Java type): tag selecting the protected "copy as if deserialized" constructors
 * of BasicBoard / RoutingBoard, which replace Java object serialization (see the design notes in
 * board/facade/RoutingBoardUndoFacade.h).
 */
struct BasicBoard_CopyTag {};

/**
 * Provides basic functionality of a board with geometric items. Contains functions such as
 * inserting, deleting, modifying and picking items and elementary checking functions. A board
 * contains 3 kinds of items: traces, vias and obstacles for routing. Traces and vias can be
 * connected to other items and therefore implement the Connectable interface. Obstacles for
 * routing are pins, component outlines, keepouts etc.
 *
 * PORT NOTE: Serializable dropped. Fields Java declares `transient` are marked [transient]: a copy
 * made "as if deserialized" (copyAsDeserialized) gets the Java DEFAULT value for them (0 / null /
 * false), not their field initializer, exactly like Java deserialization.
 */
/**
 * Stage E: allocates the derived caches an item computes in its board's cache region
 * (BasicBoard::cacheRegion), so that they are freed with the board; for a worker board's item the
 * item's own domain (jrt::DomainScope). Not for what a trace's tree-shape keeper frees itself.
 */
class ItemCacheScope {
 public:
  explicit ItemCacheScope(Item* item);
  ItemCacheScope(const ItemCacheScope&) = delete;
  ItemCacheScope& operator=(const ItemCacheScope&) = delete;

 private:
  std::optional<jrt::DomainScope> domain_;
  std::optional<jrt::GeomTarget> target_;
};

class BasicBoard : public virtual jrt::Object, public TreeShapeKeeper {
 private:
  /**
   * The maximum number of outer-loop iterations in normalizeTraces. Each legitimate split/combine
   * operation converges the board toward a stable state.
   */
  static constexpr int32_t MAX_NORMALIZE_ITERATIONS = 2000;

 public:
  /** List of items inserted into this board (e.g. trace classes). */
  UndoableObjects* itemList;

  /** List of placed components on the board. */
  Components* components;

  /** Rules for items to be inserted into this board, e.g. the clearance matrix. */
  BoardRules* rules;

  /** The library containing padstack masks, packages and other templates used on the board. */
  BoardLibrary* library;

  /** The layer structure of this board. */
  LayerStructure* layerStructure;

  /** For communication with a host system or host design file formats. */
  Communication* communication;

  /** Bounding orthogonal rectangle of this board. */
  IntBox* boundingBox;

  /** Handles the search trees pointing into the items of this board. [transient] */
  SearchTreeManager* searchTreeManager = nullptr;

 private:
  NetRoutingLedger* routingLedger_ = nullptr;  // [transient] field renamed: method routingLedger()

  /** Normal contacts by item, valid while the content version of the item list is unchanged. */
  jrt::Map<Item*, jrt::Set<Item*>*>* normalContactCache = nullptr;  // [transient] IdentityHashMap

  int64_t normalContactCacheVersion = 0;  // [transient]

  jrt::Set<int32_t>* normalizeSuppressedNetNos = nullptr;  // [transient] = new HashSet in ctor
  int32_t revision = 0;                                    // [transient]

  /**
   * The last changes of the board, one per revision: the layers and the box of the item that
   * changed, or no box when the change is not known. A ring of CHANGE_JOURNAL_SIZE. [transient]
   */
  jrt::Array<int32_t>* changeRevisions = nullptr;

  jrt::Array<IntBox*>* changeBoxes = nullptr;         // [transient]
  jrt::Array<int32_t>* changeFirstLayers = nullptr;   // [transient]
  jrt::Array<int32_t>* changeLastLayers = nullptr;    // [transient]
  static constexpr int32_t CHANGE_JOURNAL_SIZE = 1024;

  /** The changes of the search trees since the last revision (noteChange). [transient] */
  IntBox* pendingBox = nullptr;

  int32_t pendingFirstLayer = jrt::Integer::MAX_VALUE;  // [transient] (0 on a copy!)
  int32_t pendingLastLayer = jrt::Integer::MIN_VALUE;   // [transient] (0 on a copy!)
  bool pendingUnknown = false;                          // [transient]

  /** Whether the journal is kept: from the first plane that asks for it (changedSince). */
  bool journalEnabled = false;  // [transient]

  int64_t netAssignmentChangeCount = 0;                   // [transient]
  BoardItemRepository* itemRepository = nullptr;          // [transient]
  BoardConnectivityQueries* connectivityQueries = nullptr;  // [transient]
  BoardSnapshotManager* snapshotManager = nullptr;        // [transient]

 public:
  int32_t preExistingClearanceViolationsCount = 0;
  int32_t unfixableClearanceViolationsCount = 0;   // [transient]
  jrt::Set<int32_t>* ruleBlockedPins = nullptr;    // [transient]
  jrt::Set<int32_t>* tolerancePins = nullptr;      // [transient]
  ClearanceBaseline* clearanceBaseline = nullptr;  // [transient]

  // PORT NOTE: Java `public transient CompletableFuture<Void> postLoadFuture` dropped: it is only
  // set by HeadlessBoardManager (out of scope); single-threaded, post-load work runs synchronously.

 private:
  /** The rectangle, where the graphics may be not up-to-date. [transient] = IntBox.EMPTY in ctor */
  IntBox* updateBox = nullptr;
  /** PORT NOTE (stage E): box itself, or a copy when it lies in an operation region. */
  IntBox* keepBox(IntBox* box);

  /** The biggest half width of all traces on the board. */
  int32_t maxTraceHalfWidth = 1000;

  /** The smallest half width of all traces on the board. */
  int32_t minTraceHalfWidth = 10000;

 public:
  /**
   * Creates a new instance of a routing Board with surrounding box boundingBox. rules contains the
   * restrictions to obey when inserting items. Inserts the outline (outlineShapes,
   * outlineClClassNo).
   */
  BasicBoard(IntBox* boundingBox, LayerStructure* layerStructure,
             jrt::Array<PolylineShape*>* outlineShapes, int32_t outlineClClassNo,
             BoardRules* rules, Communication* communication);

 protected:
  /**
   * PORT NOTE: replaces Java deserialization. Copies the non-transient board state of source
   * (see RoutingBoardUndoFacade.h for which members are copied vs shared); transient members get
   * Java default values. Items are NOT copied here: see copyItemListFrom / afterDeserialize.
   */
  BasicBoard(BasicBoard_CopyTag tag, BasicBoard* source);

 public:
  /**
   * Deserialize. PORT NOTE: Java takes a byte[] produced by serialize(); the C++ "serialized form"
   * is a frozen board snapshot (see serialize). Returns a fresh, independent copy of snapshot (of
   * the same dynamic type) as Java deserialization would produce it; nullptr for nullptr.
   */
  static BasicBoard* deserialize(BasicBoard* snapshot);

  /**
   * Returns the normal contacts of item, computed once until an item of the board changes (the
   * content version of the item list moves). The set returned must not be modified.
   */
  virtual jrt::Set<Item*>* cachedNormalContacts(Item* item);

  virtual NetRoutingLedger* routingLedger();

  virtual int32_t getRevision() { return revision; }

  /**
   * Increment revision. The change is the one of the search trees since the last revision
   * (noteChange); without any, its place on the board is not known.
   */
  virtual void incrementRevision();

  /** Increment revision, for the insertion, removal or restoration of this item. */
  virtual void incrementRevision(Item* item);

  /**
   * Notes that the item changes on the board: the change counts at the next revision, inside the
   * box of the item as it is now. Called before and after a change in place.
   */
  virtual void noteChange(Item* item);

  /** Notes a change on these layers inside the box, counted at the next revision. */
  virtual void noteChange(IntBox* box, int32_t firstLayer, int32_t lastLayer);

  /** Notes a change whose place is not known: every cache is renewed at the next revision. */
  virtual void noteUnknownChange();

 private:
  void journalChange(IntBox* box, int32_t firstLayer, int32_t lastLayer);

 public:
  /**
   * Whether the board may have changed on the layer inside the box since the revision: true if a
   * change after it touches the box on that layer, or is not known, or is too old to be told.
   */
  virtual bool changedSince(int32_t sinceRevision, int32_t layer, IntBox* box);

  /**
   * Incremented whenever an item's nets change without a search-tree update (e.g. assignNetNo,
   * removeFromNet), for caches of net-dependent queries.
   */
  virtual int64_t getNetAssignmentChangeCount() { return netAssignmentChangeCount; }

  virtual void noteNetAssignmentChanged();

  /**
   * Serialize. PORT NOTE: returns a frozen, fully independent snapshot copy of this board (made
   * like copyAsDeserialized) instead of a byte[]. Callers keep it as an opaque value and only
   * pass it to BasicBoard::deserialize. basicProfile is accepted for signature parity; the C++
   * snapshot is always the complete board (the basic profile only fed getHash in Java).
   */
  virtual BasicBoard* serialize(bool basicProfile);

  /** Clone (Java Object.clone override): deserialize(serialize(false)). */
  virtual BasicBoard* clone();

  /** Returns an MD5 hash of the board trace state (see BoardSnapshotManager::getHash). */
  virtual std::string getHash();

  /** Returns the number of trace ID differences compared to another board. */
  virtual int32_t diffTraces(BasicBoard* compareTo);

  /**
   * Inserts a trace into the board, whose geometry is described by a Polyline. clearanceClass is
   * the index in the clearanceMatrix. Returns the inserted trace (no cleanup).
   */
  virtual PolylineTrace* insertTraceWithoutCleaning(Polyline* polyline, int32_t layer,
                                                    int32_t halfWidth,
                                                    jrt::Array<int32_t>* netNumbers,
                                                    int32_t clearanceClass, FixedState fixedState);

  /** Inserts a trace into the board, whose geometry is described by a Polyline, and cleans up. */
  virtual void insertTrace(Polyline* polyline, int32_t layer, int32_t halfWidth,
                           jrt::Array<int32_t>* netNumbers, int32_t clearanceClass,
                           FixedState fixedState);

  /** Inserts a trace described by an array of points, and cleans up the net. */
  virtual void insertTrace(jrt::Array<Point*>* points, int32_t layer, int32_t halfWidth,
                           jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                           FixedState fixedState);

  /**
   * Inserts a via into the board. attachAllowed indicates, if the via may overlap with smd pins of
   * the same net.
   */
  virtual Via* insertVia(Padstack* padstack, Point* center, jrt::Array<int32_t>* netNumbers,
                         int32_t clearanceClassIndex, FixedState fixedState, bool attachAllowed);

  /**
   * Inserts a special escape via (fanout escalation phase) directly on top of an SMD pin on
   * smdLayer to provide a layer transition to an inner routing layer.
   */
  virtual Via* insertEscapeVia(Padstack* padstack, Point* center, jrt::Array<int32_t>* netNumbers,
                               int32_t clearanceClassIndex, FixedState fixedState,
                               int32_t smdLayer);

  /** Inserts a pin into the board. pinIndex is the index of this pin in its library package. */
  virtual Pin* insertPin(int32_t componentId, int32_t pinIndex, jrt::Array<int32_t>* netNumbers,
                         int32_t clearanceClassIndex, FixedState fixedState);

  /** Inserts an obstacle into the board, whose geometry is described by a polygonal shape. */
  virtual ObstacleArea* insertObstacle(Area* area, int32_t layer, int32_t clearanceClassIndex,
                                       FixedState fixedState);

  /** Inserts an obstacle belonging to a component and associated with specific nets. */
  virtual ObstacleArea* insertObstacle(Area* area, int32_t layer, jrt::Array<int32_t>* netNumbers,
                                       int32_t clearanceClassIndex, int32_t componentId,
                                       const std::optional<std::string>& name, FixedState fixedState);

  /** Inserts an obstacle belonging to a component (name identifies it in the package). */
  virtual ObstacleArea* insertObstacle(Area* area, int32_t layer, Vector* translation,
                                       double rotationInDegree, bool sideChanged,
                                       int32_t clearanceClassIndex, int32_t componentId,
                                       const std::optional<std::string>& name, FixedState fixedState);

  /** Inserts a via obstacle area into the board. */
  virtual ViaObstacleArea* insertViaObstacle(Area* area, int32_t layer,
                                             int32_t clearanceClassIndex, FixedState fixedState);

  /** Inserts a via obstacle belonging to a component into the board. */
  virtual ViaObstacleArea* insertViaObstacle(Area* area, int32_t layer, Vector* translation,
                                             double rotationInDegree, bool sideChanged,
                                             int32_t clearanceClassIndex, int32_t componentId,
                                             const std::optional<std::string>& name, FixedState fixedState);

  /** Inserts a wire obstacle area (vias allowed, traces prohibited). */
  virtual WireObstacleArea* insertWireObstacle(Area* area, int32_t layer,
                                               int32_t clearanceClassIndex, FixedState fixedState);

  /** Inserts a wire obstacle belonging to a component into the board. */
  virtual WireObstacleArea* insertWireObstacle(Area* area, int32_t layer, Vector* translation,
                                               double rotationInDegree, bool sideChanged,
                                               int32_t clearanceClassIndex, int32_t componentId,
                                               const std::optional<std::string>& name, FixedState fixedState);

  /** Inserts a component obstacle area into the board. */
  virtual ComponentObstacleArea* insertComponentObstacle(Area* area, int32_t layer,
                                                         int32_t clearanceClassIndex,
                                                         FixedState fixedState);

  /** Inserts a component obstacle belonging to a component into the board. */
  virtual ComponentObstacleArea* insertComponentObstacle(
      Area* area, int32_t layer, Vector* translation, double rotationInDegree, bool sideChanged,
      int32_t clearanceClassIndex, int32_t componentId, const std::optional<std::string>& name,
      FixedState fixedState);

  /** Inserts a component outline into the board. */
  virtual ComponentOutline* insertComponentOutline(Area* area, bool isFront, Vector* translation,
                                                   double rotationInDegree, int32_t componentId,
                                                   bool isCourtyard, bool isFabrication,
                                                   bool isClosed, FixedState fixedState);

  /**
   * Inserts a conduction area into the board. If isObstacle is false, it is possible to route
   * through the conduction area with traces and vias of foreign nets.
   */
  virtual ConductionArea* insertConductionArea(Area* area, int32_t layer,
                                               jrt::Array<int32_t>* netNumbers,
                                               int32_t clearanceClassIndex, bool isObstacle,
                                               FixedState fixedState);

  /** Inserts an Outline into the board. */
  virtual BoardOutline* insertOutline(jrt::Array<PolylineShape*>* outlineShapes,
                                      int32_t clearanceClassIndex);

  /**
   * Expands the board's bounding box so that all placed items are fully contained within the
   * routable bounding box, with a minimum margin.
   */
  virtual void expandBoundingBoxToIncludeAllItems();

  /** Returns the outline of the board. */
  virtual BoardOutline* getOutline();

  /** Drops the outline's cached edge-pin net set and refreshes outline slices after a mutation. */
  virtual void invalidateEdgePinNetCache();

  /** Removes an item from the board. */
  virtual void removeItem(Item* item);

  /** Detaches a movable item, including a component pin, within an existing move snapshot. */
  virtual void detachItemForMove(Item* item);

  /** Searches for an item with the specified ID on the board; null if none. */
  virtual Item* getItem(int32_t id);

  /** Returns the list of all items on the board. */
  virtual jrt::Collection<Item*>* getItems();

  /** Returns all connectable items on the board containing netNumber. */
  virtual jrt::Collection<Item*>* getConnectableItems(int32_t netNumber);

  /** Returns the count of connectable items of the net with number netNumber. */
  virtual int32_t connectableItemCount(int32_t netNumber);

  /** Returns all items with the input component ID. */
  virtual jrt::Collection<Item*>* getComponentItems(int32_t componentId);

  /** Returns all pins with the input component ID. */
  virtual jrt::Collection<Pin*>* getComponentPins(int32_t componentId);

  /** Returns the pin with the input component ID and pin index, or null. */
  virtual Pin* getPin(int32_t componentId, int32_t pinIndex);

  /** Removes the items in itemList. Returns false, if some items could not be removed (fixed). */
  virtual bool removeItems(jrt::Collection<Item*>* itemList);

  /** Returns the list of all conduction areas on the board. */
  virtual jrt::Collection<ConductionArea*>* getConductionAreas();

  /**
   * Returns the conduction areas among items that are planes on a power layer, the planes a pad of
   * a plane net drops into through a via.
   */
  virtual jrt::Set<Item*>* powerLayerPlanes(jrt::Collection<Item*>* items);

  /** Returns the list of all pins on the board. */
  virtual jrt::Collection<Pin*>* getPins();

  /** Returns the count of all pins on the board without collecting them. */
  virtual int32_t getPinCount();

  /** Returns the list of all pins on the board with only 1 layer. */
  virtual jrt::Collection<Pin*>* getSmdPins();

  /** Returns the list of all vias on the board. */
  virtual jrt::Collection<Via*>* getVias();

  /** Returns the count of all vias on the board without collecting them. */
  virtual int32_t getViaCount();

  /** Returns the list of all traces on the board. */
  virtual jrt::Collection<Trace*>* getTraces();

  /** Returns the cumulative length of all traces on the board. */
  virtual double cumulativeTraceLength();

  /**
   * Combines the connected traces of this net, which have only 1 contact at the connection point.
   * If netNumber < 0 traces of all nets are combined.
   */
  virtual bool combineTraces(int32_t netNumber);

  /** Normalizes the traces of this net. */
  virtual bool normalizeTraces(int32_t netNumber);

  /** Normalizes the traces of all nets on the board in a single optimized pass. */
  virtual bool normalizeAllTraces();

  /**
   * Looks for traces of the input net on the input layer, so that location is on the trace
   * polygon, and splits these traces. Returns false, if no trace was split.
   */
  virtual bool splitTraces(Point* location, int32_t layer, int32_t netNumber);

  /** Returns a Collection of Collections of items forming a connected set. */
  virtual jrt::Collection<jrt::Collection<Item*>*>* getConnectedSets(int32_t netNumber);

  /** Returns all SearchTreeObjects on layer layer, which overlap with shape (layer < 0: all). */
  virtual jrt::Set<SearchTreeObject*>* overlappingObjects(ConvexShape* shape, int32_t layer);

  /**
   * Returns items overlapping a shape prepared for the default search tree, including clearance.
   * When the default tree is compensated, shape must already include its compensation.
   */
  virtual jrt::Set<Item*>* overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                         jrt::Array<int32_t>* ignoreNetNos,
                                                         int32_t clearanceClass);

  /** Returns all items on layer layer, which overlap with area (layer < 0: all). */
  virtual jrt::Set<Item*>* overlappingItems(Area* area, int32_t layer);

  /**
   * Checks, if an object with shape shape and net nos netNumbers and clearance class
   * clearanceClassIndex can be inserted on layer layer without clearance violation.
   */
  virtual bool checkShape(Area* shape, int32_t layer, jrt::Array<int32_t>* netNumbers,
                          int32_t clearanceClassIndex);

  /**
   * Checks, if a trace line with shape shape can be inserted on layer layer without clearance
   * violation. If contactPins != null, all pins not contained in contactPins are regarded as
   * obstacles, even if they are of the own net.
   */
  virtual bool checkTraceShape(TileShape* shape, int32_t layer, jrt::Array<int32_t>* netNumbers,
                               int32_t clearanceClassIndex, jrt::Set<Pin*>* contactPins);

  /** Checks, if a polyline trace can be inserted without clearance violations. */
  virtual bool checkPolylineTrace(Polyline* polyline, int32_t layer, int32_t penHalfWidth,
                                  jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex);

  /** Returns the layer count of this board. */
  virtual int32_t getLayerCount();

  /**
   * Returns the items on the board whose shape on layer layer contains the point at location
   * (layer < 0: all). If filter != null, only items of types selected by the filter are returned.
   */
  virtual jrt::Set<Item*>* pickItems(Point* location, int32_t layer, ItemSelectionFilter* filter);

  /** Checks, if point is contained in the bounding box of this board. */
  virtual bool contains(Point* point);

  /** Returns the minimum clearance requested between items of clearance class class1 and class2. */
  virtual int32_t clearanceValue(int32_t class1, int32_t class2, int32_t layer);

  /** Returns the biggest half width of all traces on the board. */
  virtual int32_t getMaxTraceHalfWidth() { return maxTraceHalfWidth; }

  /** Returns the smallest half width of all traces on the board. */
  virtual int32_t getMinTraceHalfWidth() { return minTraceHalfWidth; }

  /** Returns a surrounding box of the geometry of this board. */
  virtual IntBox* getBoundingBox() { return boundingBox; }

  /** Returns a box containing all items in itemList. */
  virtual IntBox* getBoundingBox(jrt::Collection<Item*>* itemList);

  /** Resets the rectangle, where a graphics update is needed. */
  virtual void resetGraphicsUpdateBox();

  /** Gets the rectangle, where a graphics update is needed on the screen. */
  virtual IntBox* getGraphicsUpdateBox() { return updateBox; }

  /** Enlarges the graphics update box, so that it contains box. */
  virtual void joinGraphicsUpdateBox(IntBox* box);

  /** Starts notifying the observers of any change in the objects list. */
  virtual void startNotifyObservers();

  /** Ends notifying the observers of changes in the objects list. */
  virtual void endNotifyObservers();

  /** Returns, if the observer of the board items is activated. */
  virtual bool observersActive();

  /**
   * Turns an obstacle area into a conduction area with net number netNumber. If it is convex and
   * has no holes, it is turned into a Pin, else into a conduction area.
   */
  virtual Connectable* makeConductive(ObstacleArea* area, int32_t netNumber);

  /** Inserts an item into the board database. */
  virtual void insertItem(Item* item);

  /**
   * Stub function overwritten in class RoutingBoard to maintain the autorouter database if
   * necessary.
   */
  virtual void additionalUpdateAfterChange(Item* item) { (void)item; }

  /**
   * Restores the situation at the previous snapshot. Returns false, if no more undo is possible.
   * Puts the numbers of the changed nets into the set changedNets, if changedNets != null.
   */
  virtual bool undo(jrt::Set<int32_t>* changedNets);

  /**
   * Restores the situation before the last undo. Returns false, if no more redo is possible. Puts
   * the numbers of the changed nets into the set changedNets, if changedNets != null.
   */
  virtual bool redo(jrt::Set<int32_t>* changedNets);

 private:
  void applyUndoRedoSideEffects(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
                                jrt::Collection<UndoableObjects_Storable*>* restoredObjects,
                                jrt::Set<int32_t>* changedNets);

 public:
  /** Makes the current board situation restorable by undo. */
  virtual void generateSnapshot();

  /**
   * Removes the top snapshot from the undo stack, so that its situation cannot be restored
   * anymore. Returns false, if no more snapshot could be popped.
   */
  virtual bool popSnapshot();

  /**
   * Looks if at the input position ends a trace with the input net number, which has no normal
   * contact at that position. Returns null, if no tail is found.
   */
  virtual Trace* getTraceTail(Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers);

  /**
   * Checks, if item item is part of a cycle and remove it together with its connection in this
   * case.
   */
  virtual bool removeIfCycle(Trace* trace);

 private:
  /**
   * getConnectionItems also returns the via at a fork that ends the connection. Such a via still
   * joins the other items it touches: it is taken out of connectionItems.
   */
  static void keepJunctionVias(jrt::Set<Item*>* connectionItems);

  static bool touchesItemOutside(Via* via, jrt::Set<Item*>* items);

  BoardItemRepository* getItemRepository();

  BoardConnectivityQueries* getConnectivityQueries();

  BoardSnapshotManager* getSnapshotManager();

 protected:
  /**
   * PORT NOTE: body of Java's private readObject(ObjectInputStream) after defaultReadObject():
   * min-hole-to-hole rule fixup, new SearchTreeManager, normalizeSuppressedNetNos = new HashSet,
   * facade caches reset, then for every item of getItems() in order: item->board = this and
   * searchTreeManager->insert(item). Called on a board built by the copy constructor after
   * copyItemListFrom.
   */
  void afterDeserialize();

  /**
   * PORT NOTE: copies the item list of source into this board (fresh copy of the
   * UndoableObjects state, every Storable mapped through Item::copy(sameId) with board = this,
   * preserving ids, item-list order, snapshot levels and the undo stacks). Fills itemMap
   * (original -> copy) when it is not null.
   */
  void copyItemListFrom(BasicBoard* source, jrt::IdentityHashMap<Item*, Item*>* itemMap);

 public:
  /**
   * PORT NOTE: replacement for "deserialize(serialize(board))". Returns a fully independent copy
   * of this board of the same dynamic type, in the state Java deserialization produces (transient
   * members at Java defaults, afterDeserialize() run). No deepCopy post-steps (baseline, ledger,
   * plane memos) are applied here. Fills itemMap (original item -> copy) if not null.
   * Overridden by RoutingBoard to create a RoutingBoard.
   */
  virtual BasicBoard* copyAsDeserialized(jrt::IdentityHashMap<Item*, Item*>* itemMap);

  /**
   * PORT NOTE (stage E, board teardown): ends the life of a board that its only owner drops (a
   * board copy: optimizer incumbent / worker / replaced boards, history snapshots). Frees what
   * the board owns: the items (all versions in the undo history) with their search-tree info,
   * the undo-list nodes, the search trees, the components, the contact cache, the autoroute
   * engine (RoutingBoard) and the board object; forgets the caches keyed by board identity.
   * Not freed: what a copy shares with its source (library, layer structure, bounding box,
   * clearance baseline, item geometry and net-number arrays, padstacks) and parts not owned yet
   * (rules, communication, item geometry caches). Nothing may use the board or its items
   * afterwards; never call it for the job's input board (owned by the caller).
   */
  static void discard(BasicBoard* board, const char* site = "other");
  /** Stage E: discard() for the last board of a family (the input board and all its copies are
   * gone): also frees what they shared - library, layer structure, the DSN parser info and
   * coordinate transform. */
  static void discardLast(BasicBoard* board);
  /** False when FR_NO_BOARD_DISCARD switches the drop site `site` off (diagnostics). */
  static bool discardEnabled(const char* site);
  /** Diagnostics (FR_MEM_STATS): board copies made / freed by discard() so far. */
  static int64_t copiesMade();
  /** Lock held while a copy reads this board (Java: synchronized (board) in deepCopy). */
  static std::mutex& copyLock(const BasicBoard* board);
  static int64_t copiesDiscarded();
  /** Diagnostics (FR_MEM_STATS): deferred-disposal counters. */
  static std::string graveyardStats();

 protected:
  /** The teardown steps of discard() before the board object itself is freed. */
  virtual void releaseOwnedData();

 public:
  /** Stage E: the region that holds the derived caches of this board's items (shapes of pins and
   * vias, areas of outlines and obstacles, tree shapes of items other than traces, exact copper,
   * autoroute info): freed with the board. Created on first use; only for boards whose items
   * live on the heap (a worker board's items keep their caches in its candidate region). */
  jrt::GeomRegion* cacheRegion();

 private:
  jrt::GeomRegion* cacheRegion_ = nullptr;

 public:
  // PORT NOTE (stage E): deferred disposal ("graveyard"). Items removed for good (no undo can
  // restore them: reported by the item list's sweep) and precalculated tree shapes of traces that
  // no array refers to any more are kept until releaseGraveyard(): callers still use a removed
  // item after removeItem (pass item lists, ripped-item lists), and the drill pages / obstacle
  // rooms of a live autoroute engine refer to tree shapes.
  void retainTreeShapes(jrt::Array<TileShape*>* shapes) override;
  void releaseTreeShapes(jrt::Array<TileShape*>* shapes) override;
  /** A leaf uses `shape` as its bounding shape: counted if it is a tracked tree shape. */
  void retainLeafShape(RegularTileShape* shape);
  void releaseLeafShape(RegularTileShape* shape);
  /** Frees the graveyard if nothing outside the board can still hold its contents (no live
   * autoroute engine); call only between top-level operations (pass / stage boundaries). */
  void releaseGraveyard();

 protected:
  /** False while something outside the board may still refer to removed items or dropped tree
   * shapes (RoutingBoard: a live autoroute engine). */
  virtual bool graveyardSafe() { return true; }
  void releaseGraveyardNow();

 private:
  void noteDeadObject(UndoableObjects_Storable* object);
  void watchItemList();
  std::unordered_map<TileShape*, int32_t> treeShapeRefs_;
  std::vector<TileShape*> deadTreeShapes_;
  std::vector<jrt::Array<TileShape*>*> deadShapeArrays_;
  std::vector<Item*> deadItems_;

 public:

  /** Deletes all traces and vias from the board. */
  virtual void deleteAllTracksAndVias();

  /**
   * Unfills all conduction zones on the board, leaving only their definitions (boundaries)
   * visible. This disables them as routing obstacles.
   */
  virtual void unfillConductionAreas();

  /** Logs a warning if connectable items exist on inactive routing layers. */
  virtual void areThereItemsOnInactiveLayer(AutorouteControl* ctrl);

  /** Returns the number of traces that are not multiples of 45 degrees. */
  virtual int32_t getNon45DegreeTraceCount();

  /**
   * Waits for any asynchronous post-load processing to complete. PORT NOTE: no-op (single-threaded
   * port, postLoadFuture dropped).
   */
  virtual void awaitPostLoad() {}
};

}  // namespace fr
