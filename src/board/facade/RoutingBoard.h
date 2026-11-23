#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "board/facade/BasicBoard.h"

namespace fr {

/**
 * Java: local record `PinNetAssignment(Pin pin, int net)` declared inside
 * RoutingBoard.connectNetlessPads (hoisted). equals/hashCode/toString like a Java record (defined
 * in RoutingBoard.cpp).
 */
class RoutingBoard_PinNetAssignment : public virtual jrt::Object {
 private:
  Pin* pin_;
  int32_t net_;

 public:
  RoutingBoard_PinNetAssignment(Pin* pin, int32_t net) : pin_(pin), net_(net) {}

  Pin* pin() { return pin_; }
  int32_t net() { return net_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * The class that represents the board that is being routed. It contains all the data for the
 * board and the methods to manipulate it.
 *
 * PORT NOTE: [transient] marks Java transient fields (Java default value on a deserialized copy;
 * note shoveFailingLayer is 0, not -1, on a copy).
 */
class RoutingBoard : public BasicBoard {
 public:
  RoutingFailureLog* failureLog;

  /** The area marked for optimizing the route. [transient] */
  ChangedArea* changedArea = nullptr;

 private:
  /** Contains the database for the auto-route algorithm. [transient] */
  AutorouteEngine* autorouteEngine = nullptr;

  Item* shoveFailingObstacle = nullptr;           // [transient]
  int32_t shoveFailingLayer = -1;                 // [transient] (0 on a copy!)
  int64_t shoveFailingObstacleWrites = 0;         // [transient]
  int64_t shoveFailingLayerWrites = 0;            // [transient]
  jrt::List<Item*>* shoveCacheObstacles = nullptr;  // [transient]
  RoutingBoardOperations* operations = nullptr;   // [transient]
  RoutingBoardSearchFacade* searchFacade = nullptr;  // [transient]
  RoutingBoardUndoFacade* undoFacade = nullptr;   // [transient]

 public:
  /**
   * Creates a new instance of a routing Board with surrounding box boundingBox. rules contains the
   * restrictions to obey when inserting items, e.g. a clearance matrix.
   */
  RoutingBoard(IntBox* boundingBox, LayerStructure* layerStructure,
               jrt::Array<PolylineShape*>* outlineShapes, int32_t outlineClClassNo,
               BoardRules* rules, Communication* boardCommunication);

 protected:
  /**
   * PORT NOTE: replaces Java deserialization (see BasicBoard(BasicBoard_CopyTag, ...) and
   * RoutingBoardUndoFacade.h): copies failureLog (deep), transient members at Java defaults.
   */
  RoutingBoard(BasicBoard_CopyTag tag, RoutingBoard* source);

 public:
  /** Maintains the auto-router database after item is inserted, changed, or deleted. */
  void additionalUpdateAfterChange(Item* item) override;

  /**
   * Removes the items in itemList and pulls the nearby rubber traces tight. Returns false, if some
   * items could not be removed, because they were fixed.
   */
  virtual bool removeItemsAndPullTight(jrt::Collection<Item*>* itemList, int32_t tidyWidth,
                                       int32_t pullTightAccuracy);

  /** Starts marking the changed areas for optimizing traces. */
  virtual void startMarkingChangedArea();

  /** Enlarges the changed area on layer, so that it contains point. */
  virtual void joinChangedArea(FloatPoint* point, int32_t layer);

  /** Marks the whole board as changed. */
  virtual void markAllChangedArea();

  /**
   * Optimizes the route in the internally marked area without a time limit, so that the result
   * does not depend on the speed or load of the machine.
   */
  virtual void optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                              int32_t accuracy,
                              jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                              Stoppable* stoppableThread);

  /**
   * Optimizes the route in the internally marked area. If clipShape != null the optimizing is
   * restricted to clipShape.
   */
  virtual void optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                              int32_t accuracy,
                              jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                              Stoppable* stoppableThread, int32_t timeLimit);

  /**
   * Optimizes the route in the internally marked area. If keepPoint != null, traces on layer
   * keepPointLayer with a corner at keepPoint are not optimized.
   */
  virtual void optChangedArea(jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape,
                              int32_t accuracy,
                              jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                              Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                              int32_t keepPointLayer);

  /**
   * Checks if a rectangular boxed trace line segment can be inserted without conflict. If a
   * conflict exists, the result length is the maximal line length which can be inserted.
   */
  virtual double checkTraceSegment(Point* fromPoint, Point* toPoint, int32_t layer,
                                   jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                                   int32_t clClassNo, bool onlyNotShovableObstacles);

  /** Checks if a trace shape around the input parameters can be inserted without conflict. */
  virtual double checkTraceSegment(LineSegment* lineSegment, int32_t layer,
                                   jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                                   int32_t clClassNo, bool onlyNotShovableObstacles);

  /** Checks, if item can be translated by vector without producing overlaps or violations. */
  virtual bool checkMoveItem(Item* item, Vector* vector, jrt::Collection<Item*>* ignoreItems);

  /** Checks, if the net number of item can be changed without producing clearance violations. */
  virtual bool checkChangeNet(Item* item, int32_t newNetNo);

  /**
   * Translates drillItem by vector and shoves obstacle traces aside. Returns false, if that was
   * not possible without creating clearance violations (the database may then be damaged).
   */
  virtual bool moveDrillItem(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                             int32_t maxViaRecursionDepth, int32_t tidyWidth,
                             int32_t pullTightAccuracy, int32_t pullTightTimeLimit);

  /**
   * Checks, if there is an item nearby sharing a net with netNumbers, from where a routing can
   * start, or where the routing can connect to.
   */
  virtual Item* pickNearestRoutingItem(Point* location, int32_t layer, Item* fromItem);

  /**
   * Shoves aside traces, so that a via with the input parameters can be inserted without
   * clearance violations. If the shove failed, the database may be damaged.
   */
  virtual bool forcedVia(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                         int32_t traceClearanceClassIndex,
                         jrt::Array<int32_t>* tracePenHalfwidthArr, int32_t maxRecursionDepth,
                         int32_t maxViaRecursionDepth, int32_t tidyWidth,
                         int32_t pullTightAccuracy, int32_t pullTightTimeLimit);

  /**
   * Tries to insert a trace line from fromCorner to toCorner while shoving aside obstacle traces
   * and vias. Returns the last point between fromCorner and toCorner, to which the shove
   * succeeded. Returns null, if the check was inaccurate and an error occurred while inserting.
   */
  virtual Point* insertForcedTraceSegment(Point* fromCorner, Point* toCorner, int32_t halfWidth,
                                          int32_t layer, jrt::Array<int32_t>* netNumbers,
                                          int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                          int32_t maxViaRecursionDepth,
                                          int32_t maxSpringOverRecursionDepth, int32_t tidyWidth,
                                          int32_t pullTightAccuracy, bool withCheck,
                                          TimeLimit* timeLimit);

  /** Checks, if a trace polyline can be inserted while shoving aside obstacle traces and vias. */
  virtual bool checkForcedTracePolyline(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                        jrt::Array<int32_t>* netNumbers,
                                        int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                        int32_t maxViaRecursionDepth,
                                        int32_t maxSpringOverRecursionDepth);

  /**
   * Tries to insert a trace polyline while shoving aside obstacle traces and vias. Returns the
   * last corner on the polyline, to which the shove succeeded. Returns null, if the check was
   * inaccurate and an error occurred while inserting.
   */
  virtual Point* insertForcedTracePolyline(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                           jrt::Array<int32_t>* netNumbers,
                                           int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                                           int32_t maxViaRecursionDepth,
                                           int32_t maxSpringOverRecursionDepth, int32_t tidyWidth,
                                           int32_t pullTightAccuracy, bool withCheck,
                                           TimeLimit* timeLimit);

  /**
   * Initialises the auto-route database for routing a connection. If retainAutorouteDatabase, the
   * auto-route database is retained and maintained after the algorithm for performance reasons.
   */
  virtual AutorouteEngine* initAutoroute(int32_t netNumber, int32_t traceClearanceClassIndex,
                                         Stoppable* stoppableThread, TimeLimit* timeLimit,
                                         bool retainAutorouteDatabase);

  /** Clears the auto-route database in case it was retained. */
  virtual void finishAutoroute();

  /** Frees the connection-search arena of the current engine without clearing this board.
   * Only for a board that is dropped as a whole (optimizer worker board). */
  virtual void discardAutorouteEngineWithBoard();

  /**
   * Drops maze/search scratch that snapshot undo does not restore. Optimizer worker-board reuse
   * must call this after each candidate.
   */
  virtual void clearTransientAutorouteState();

  /**
   * Routes automatically item to another item of the same net, to which it is not yet
   * electrically connected.
   */
  virtual AutorouteAttemptResult* autoroute(Item* item, RouterSettings* routerSettings,
                                            int32_t viaCosts, Stoppable* stoppableThread,
                                            TimeLimit* timeLimit);

  /**
   * Autoroutes from the input pin until the first via, in case the pin and its connected set has
   * only 1 layer. Ripup is allowed if ripupCosts is >= 0.
   */
  virtual AutorouteAttemptResult* fanout(Pin* pin, RouterSettings* routerSettings,
                                         int32_t ripupCosts, Stoppable* stoppableThread,
                                         TimeLimit* timeLimit);

  /**
   * The copper of its net a plane pad must not reach its plane through, while it searches its own
   * way in (plane_as_priority): the pads of other packages and everything joined to them.
   */
  virtual jrt::Set<Item*>* planeEscapeObstacles(Pin* pin, int32_t netNo,
                                                jrt::Set<Item*>* connectedSet,
                                                jrt::Set<Item*>* destinations);

 private:
  bool isPlaneOnPowerLayer(Item* item);

 public:
  /** netViaClearance: Java nullable Integer. */
  static ViaRule* fanoutFallbackViaRule(ViaRule* netRule, ViaRule* boardRule,
                                        std::optional<int32_t> netViaClearance,
                                        BoardRules* rules);

  /**
   * Inserts a trace from fromPoint to the nearest point on toTrace. Returns false, if that is not
   * possible without clearance violation.
   */
  virtual bool connectToTrace(IntPoint* fromPoint, Trace* toTrace, int32_t penHalfWidth,
                              int32_t clearanceClassIndex);

  /**
   * Checks, if the list items contains traces, which have no contact at their start or end point.
   * Traces with a net number in exceptNetNoArr are ignored.
   */
  virtual bool containsTraceTails(jrt::Collection<Item*>* items,
                                  jrt::Array<int32_t>* exceptNetNoArr);

  /**
   * Removes all trace tails of the input net. If netNumber <= 0, the tails of all nets are removed.
   * Returns true, if something was removed.
   */
  virtual bool removeTraceTails(int32_t netNumber, Item_StopConnectionOption stopConnectionOption);

  /** Clear all item temporary autoroute data. */
  virtual void clearAllItemTemporaryAutorouteData();

  /** Sets, if all conduction areas (power planes) are obstacles for route of foreign nets. */
  virtual void changePlaneAsObstacle(bool value);

  /** Deprecated alias of changePlaneAsObstacle. */
  virtual void changeConductionIsObstacle(bool value);

  /**
   * Tries to reduce the nets of traces and vias, so that the nets are a subset of the nets of the
   * contact items (traces and vias with more than 1 net connected to tie pins).
   */
  virtual bool reduceNetsOfRouteItems();

  /**
   * Connects netless pads to their footprints' net (FR-102). tiePadsOut[0] receives the number of
   * tie pads (if not null).
   */
  virtual int32_t connectNetlessPadsToTheirFootprintsNet(jrt::Array<int32_t>* tiePadsOut);

  /** Connects netless pads to their footprints' net (FR-102). */
  virtual int32_t connectNetlessPadsToTheirFootprintsNet();

  /** Connects netless pads as the input specifies (KiCad boards keep them netless by default). */
  virtual int32_t connectNetlessPads(bool kicadBoard, bool fixKicadFootprints,
                                     jrt::Array<int32_t>* tiePadsOut);

 private:
  static int32_t findRoot(jrt::Array<int32_t>* parent, int32_t i);

 public:
  /** Identifies physically overlapping pins within each footprint and clusters them into net-ties. */
  virtual int32_t clusterNetTies();

 private:
  static bool pinsOverlapCopper(Pin* p1, Pin* p2);

 public:
  /** Returns the obstacle responsible for the last shove to fail. */
  virtual Item* getShoveFailingObstacle() { return shoveFailingObstacle; }

  virtual void setShoveFailingObstacle(Item* item) {
    shoveFailingObstacle = item;
    shoveFailingObstacleWrites++;
  }

  virtual int32_t getShoveFailingLayer() { return shoveFailingLayer; }

  virtual void setShoveFailingLayer(int32_t layer) {
    shoveFailingLayer = layer;
    shoveFailingLayerWrites++;
  }

 private:
  void clearShoveFailingObstacle() {
    shoveFailingObstacle = nullptr;
    shoveFailingLayer = -1;
    shoveFailingObstacleWrites++;
    shoveFailingLayerWrites++;
  }

 public:
  virtual int64_t getShoveFailingObstacleWrites() { return shoveFailingObstacleWrites; }

  virtual int64_t getShoveFailingLayerWrites() { return shoveFailingLayerWrites; }

  virtual jrt::List<Item*>* getShoveCacheObstacles();

  /** Java: ShoveCheckCache.forThread() (one cache in the single-threaded port). */
  virtual ShoveCheckCache* getShoveChecks();

  /**
   * Returns, if the auto-route database is maintained outside the auto-route algorithm while
   * changing items on the board. (Java package-private.)
   */
  virtual bool isMaintainingAutorouteDatabase() { return this->autorouteEngine != nullptr; }

  /**
   * Sets, if the auto-route database has to be maintained outside the auto-route algorithm while
   * changing items on the board. (Java package-private; true is a no-op.)
   */
  virtual void setMaintainingAutorouteDatabase(bool value) {
    if (!value) {
      this->autorouteEngine = nullptr;
    }
  }

  bool undo(jrt::Set<int32_t>* changedNets) override;

  bool redo(jrt::Set<int32_t>* changedNets) override;

  virtual BoardStatistics* getStatistics();

  /**
   * Create a deep copy of the routing board. Similar to BasicBoard.clone, but it copies the
   * routing related values as well (see RoutingBoardUndoFacade::deepCopy).
   */
  virtual RoutingBoard* deepCopy();

  /** PORT NOTE: see BasicBoard::copyAsDeserialized; creates a RoutingBoard. */
  BasicBoard* copyAsDeserialized(jrt::IdentityHashMap<Item*, Item*>* itemMap) override;

 private:
  RoutingBoardOperations* getOperations();

  RoutingBoardSearchFacade* getSearchFacade();

  RoutingBoardUndoFacade* getUndoFacade();
};

}  // namespace fr
