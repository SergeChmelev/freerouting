#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Temporary autoroute data stored on the RoutingBoard. */
class AutorouteEngine : public virtual jrt::Object {
 public:
  static constexpr int32_t TRACE_WIDTH_TOLERANCE = 2;

  /** The current search tree used in autorouting. It depends on the trace clearance class used
   * in the autoroute algorithm. */
  ShapeSearchTree* autorouteSearchTree;

  /** If maintainDatabase, the autorouter database is maintained after a connection is completed
   * for performance reasons. */
  bool maintainDatabase;

  /** (Java: package-private) */
  DrillPageArray* drillPageArray;

  /** The PCB-board of this autoroute algorithm. */
  RoutingBoard* board;

  /** To be able to stop the expansion algorithm. */
  Stoppable* stoppableThread = nullptr;

 private:
  /** The net number used for routing in this autoroute algorithm. */
  int32_t netNumber = 0;

  /** To stop the expansion algorithm after a time limit is exceeded. */
  TimeLimit* timeLimit = nullptr;

  /** The list of incomplete expansion rooms on the routing board. */
  // PORT NOTE: declared ArrayList (Java: List, always an ArrayList) for removeIdentical.
  jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>* incompleteExpansionRooms = nullptr;

  /** The list of complete expansion rooms on the routing board. */
  jrt::List<CompleteFreeSpaceExpansionRoom*>* completeExpansionRooms = nullptr;

  /** The count of expansion rooms created so far. */
  int32_t expansionRoomInstanceCount = 0;

  /** Connection-search arena (MEMORY_MODEL.md): holds the kSearch objects of this engine while
   * it does not maintain its database. Released by releaseSearchArena(). */
  jrt::Arena searchArena;
  bool searchArenaReleased = false;
  bool searchArenaEscaped = false;

  /** Set for the rest of the process once an exception escaped a search (see
   * autorouteConnection): from then on all search objects go to the GC heap. */
  static bool& searchArenaDisabled();

  bool usesSearchArena();

 public:
  /** Creates a new instance of AutorouteEngine. If maintainDatabase, the autorouter database is
   * maintained after a connection is completed for performance reasons. */
  AutorouteEngine(RoutingBoard* board, int32_t traceClearanceClassIndex, bool maintainDatabase);

  /** Prepares the engine for routing a connection of netNumber. */
  virtual void initConnection(int32_t netNumber, Stoppable* stoppableThread, TimeLimit* timeLimit);

  /** Autoroutes a connection between startSet and destSet. Returns ALREADY_CONNECTED, ROUTED,
   * FAILED or INSERT_ERROR. */
  virtual AutorouteAttemptResult* autorouteConnection(jrt::Set<Item*>* startSet,
                                                     jrt::Set<Item*>* destSet,
                                                     AutorouteControl* ctrl,
                                                     jrt::TreeSet<Item*>* rippedItemList,
                                                     jrt::Map<Item*, int32_t>* ripupCosts);

  /** Removes the connections of rippedItems; returns the numbers of the changed nets. */
  static jrt::Set<int32_t>* ripConnections(RoutingBoard* board,
                                           jrt::Collection<Item*>* rippedItems,
                                           bool removeUnconnectedVias);

 private:
  bool isDisabledSignalLayer(AutorouteControl* ctrl, int32_t layer);

  static std::string describeConnection(jrt::Set<Item*>* startSet, jrt::Set<Item*>* destSet);

 public:
  /** Returns the net number of the current connection to route. */
  virtual int32_t getNetNumber() { return this->netNumber; }

  /** Returns if the user has stopped the autorouter. */
  virtual bool isStopRequested();

  /** Clears all temporary data. */
  virtual void clear();

  /** Drops all objects of the connection-search arena. Only when the engine is discarded:
   * after clear() (nothing of the arena is referenced by the board any more) or together with
   * its whole board. The engine must not be used afterwards. */
  virtual void releaseSearchArena();

  /** Emits optional diagnostics of the autoroute database. */
  virtual void emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity);

  /** Creates a new FreeSpaceExpansionRoom and adds it to the room list. Its shape is normally
   * unbounded at construction time of this room. */
  virtual IncompleteFreeSpaceExpansionRoom* addIncompleteExpansionRoom(TileShape* shape,
                                                                       int32_t layer,
                                                                       TileShape* containedShape);

  /** Returns the first element in the list of incomplete expansion rooms or null, if the list is
   * empty. */
  virtual IncompleteFreeSpaceExpansionRoom* getFirstIncompleteExpansionRoom();

  /** Removes an incomplete room from the database. */
  virtual void removeIncompleteExpansionRoom(IncompleteFreeSpaceExpansionRoom* room);

  /** Removes a complete expansion room from the database and creates new incomplete expansion
   * rooms for the neighbours. */
  virtual void removeCompleteExpansionRoom(CompleteFreeSpaceExpansionRoom* room);

  /** Completes the shape of the expansion room room and returns the resulting complete rooms. */
  virtual jrt::Collection<CompleteFreeSpaceExpansionRoom*>* completeExpansionRoom(
      IncompleteFreeSpaceExpansionRoom* room);

 private:
  CompleteFreeSpaceExpansionRoom* addCompleteRoom(IncompleteFreeSpaceExpansionRoom* room);

  static std::string describeShapeBounds(TileShape* shape);

  /** Calculates the doors and adds them to the doors of room. Returns the completed room. */
  CompleteExpansionRoom* calculateDoors(ExpansionRoom* room);

 public:
  /** Completes the shapes of the neighbour rooms of room, so that the doors of room will not
   * change later on. */
  virtual void completeNeighbourRooms(CompleteExpansionRoom* room);

  /** Invalidates all drill pages intersecting with shape. */
  virtual void invalidateDrillPages(TileShape* shape);

  /** Removes all doors from room. */
  virtual void removeAllDoors(ExpansionRoom* room);

  /** Returns all complete free space expansion rooms with a target door to an item in items.
   * (Java: package-private) */
  virtual jrt::Set<CompleteFreeSpaceExpansionRoom*>* getRoomsWithTargetItems(
      jrt::Set<Item*>* items);

  /** Checks if the internal data structures are consistent. */
  virtual bool validate();

 private:
  /** Reset all doors for autorouting the next connection. */
  void resetAllDoors();

 public:
  /** Returns a new identification number for a complete expansion room. */
  virtual int32_t generateRoomIdNo();
};

}  // namespace fr
