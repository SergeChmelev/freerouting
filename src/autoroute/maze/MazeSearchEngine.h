#pragma once

#include "autoroute/maze/MazeSearchElement.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The result type of MazeSearchEngine.findConnection(). */
class MazeSearchEngine_Result : public virtual jrt::Object {
 public:
  ExpandableObject* destinationDoor;
  int32_t sectionNoOfDoor;

  /** (Java: package-private constructor) */
  MazeSearchEngine_Result(ExpandableObject* destinationDoor, int32_t sectionNoOfDoor)
      : destinationDoor(destinationDoor), sectionNoOfDoor(sectionNoOfDoor) {}
};

/** Result of shoving a trace room (Java: package-private static class). */
class MazeSearchEngine_ShoveResult : public virtual jrt::Object {
 public:
  ExpansionDoor* oppositeDoor;
  jrt::Collection<ExpansionDoor*>* sideDoors;
  FloatPoint* fromDoorPassingPoint;
  FloatPoint* oppositeDoorPassingPoint;

  MazeSearchEngine_ShoveResult(ExpansionDoor* oppositeDoor,
                               jrt::Collection<ExpansionDoor*>* sideDoors,
                               FloatPoint* fromDoorPassingPoint,
                               FloatPoint* oppositeDoorPassingPoint)
      : oppositeDoor(oppositeDoor),
        sideDoors(sideDoors),
        fromDoorPassingPoint(fromDoorPassingPoint),
        oppositeDoorPassingPoint(oppositeDoorPassingPoint) {}
};

/**
 * Class for autorouting an incomplete connection via a maze search algorithm.
 *
 * Package-private members are public here (used by MazeExpansionEngine, MazeRipupResolver, ...).
 * PORT NOTE: mazeExpansionList is a jrt::TreeSet ordered by MazeListElement::compareTo; Java
 * creates it as an anonymous TreeSet subclass overriding add() (fanout escape window filter) ->
 * a local subclass of jrt::TreeSet<MazeListElement*> overriding add() in the .cpp.
 */
class MazeSearchEngine : public virtual jrt::Object {
 public:
  static constexpr int32_t ALREADY_RIPPED_COSTS = 1;

  AutorouteEngine* autorouteEngine;
  AutorouteControl* ctrl;

  /** The queue of expanded elements used in this search algorithm. */
  jrt::TreeSet<MazeListElement*>* mazeExpansionList;

  /** Used for calculating of a good lower bound for the distance between a new
   * MazeExpansionElement and the destination set of the expansion. */
  DestinationDistance* destinationDistance;

  /** The search tree for expanding. It is the tree compensated for the current net. */
  ShapeSearchTree* searchTree;

  jrt::Random* randomGenerator = new jrt::Random();

  MazeFanoutDiagnostics* fanoutDiagnostics;

 private:
  MazeExpansionEngine* expansionEngine;
  MazeRipupResolver* ripupResolver;

  /** The destination door found by the expanding algorithm. */
  ExpandableObject* destinationDoor = nullptr;

  int32_t sectionNoOfDestinationDoor = 0;

 public:
  /** Creates a new instance of MazeSearchEngine. (Java: package-private) */
  MazeSearchEngine(AutorouteEngine* autorouteEngine, AutorouteControl* ctrl);

  /** Initializes a new instance of MazeSearchEngine for a one step connection. Returns null, if
   * the initialisation failed. */
  static MazeSearchEngine* getInstance(jrt::Set<Item*>* startItems,
                                       jrt::Set<Item*>* destinationItems,
                                       AutorouteEngine* autorouteDatabase, AutorouteControl* ctrl);

 private:
  /** Reduces the trace shapes of own-net items at tie pins. */
  static void reduceTraceShapesAtTiePins(jrt::Collection<Item*>* itemList, int32_t ownNetNo,
                                         ShapeSearchTree* autorouteTree);

  static FloatLine* segmentProjection(FloatLine* fromSegment, FloatLine* toSegment);

 public:
  /** (Java: package-private) */
  static std::string describeExpandable(ExpandableObject* door);

 private:
  static std::string safeMazeSectionCount(ExpandableObject* door);

  static std::string describeExpandableBounds(ExpandableObject* door);

 public:
  /** (Java: package-private) */
  static std::string describeRoom(CompleteExpansionRoom* room);

 private:
  static jrt::Array<Point*>* toImpactedPoints(FloatLine* shapeEntry);

 public:
  /** Finds a connection between the start and destination set. Returns null, if no connection
   * was found. */
  virtual MazeSearchEngine_Result* findConnection();

  /** Expands the next element in the maze expansion list. Returns false, if the expansion list
   * is exhausted or the destination is reached. */
  virtual bool occupyNextElement();

 private:
  bool fanoutMayStopAt(MazeListElement* listElement);

  bool everyViaThroughPlaneReachesAnActiveLayer(int32_t fromLayer, int32_t planeLayer);

  bool viaReachesActiveLayer(AutorouteControl_ViaMask* via, int32_t excludedLayer);

 public:
  /** (Java: package-private) */
  static bool closesBacktrackCycle(MazeListElement* listElement, MazeSearchElement* section);

 private:
  bool expandToRoomDoors(MazeListElement* listElement);

  bool expandToTargetDoors(MazeListElement* listElement, bool nextRoomIsThick,
                           bool currentDoorIsSmall, FloatPoint* shapeEntryMiddle);

  bool expandToDoor(ExpansionDoor* toDoor, MazeListElement* listElement, int32_t addCosts,
                    bool nextRoomIsThick, MazeSearchElement_Adjustment adjustment);

  /** Return true, if the width of the door is smaller than traceWidth. */
  bool doorIsSmall(ExpansionDoor* door, double traceWidth);

  bool expandToDoorSection(ExpandableObject* door, int32_t sectionIndex, FloatLine* shapeEntry,
                           MazeListElement* fromElement, int32_t addCosts,
                           MazeSearchElement_Adjustment adjustment);

  bool init(jrt::Set<Item*>* startItems, jrt::Set<Item*>* destinationItems);

  /** Checks, if the room is big enough for a trace of the current net. */
  bool roomShapeIsThick(ObstacleExpansionRoom* obstacleRoom);

  /** Checks, if the shove room of trace can be expanded. */
  bool shoveTraceRoom(MazeListElement* listElement, ObstacleExpansionRoom* obstacleRoom);

 public:
  /** (Java: package-private) */
  TileShape* getWidthSafeTargetShape(TargetItemExpansionDoor* targetDoor);

 private:
  /** Checks, if the next room contains a destination pin, where eventually neckdown is
   * necessary. Returns the neck down trace half width, if neckdown is necessary, else 0. */
  double checkNeckDownAtDestPin(CompleteExpansionRoom* room);
};

}  // namespace fr
