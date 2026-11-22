#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Type of a single item in the result list connection_items. Used to create a new PolylineTrace.
 * (Java: protected static class) */
class FoundConnectionLocator_ResultItem : public virtual jrt::Object {
 public:
  jrt::Array<IntPoint*>* corners;
  int32_t layer;

  FoundConnectionLocator_ResultItem(jrt::Array<IntPoint*>* corners, int32_t layer)
      : corners(corners), layer(layer) {}
};

/** Type of the elements of the list returned by backtrack(). Next_room is the common room of the
 * current door and the next door in the backtrack list. (Java: protected static final class) */
class FoundConnectionLocator_BacktrackElement final : public virtual jrt::Object {
 public:
  ExpandableObject* door;
  int32_t sectionNoOfDoor;
  CompleteExpansionRoom* nextRoom;

  // PORT NOTE: private in Java (constructed only by FoundConnectionLocator); public here.
  FoundConnectionLocator_BacktrackElement(ExpandableObject* door, int32_t sectionNoOfDoor,
                                          CompleteExpansionRoom* room)
      : door(door), sectionNoOfDoor(sectionNoOfDoor), nextRoom(room) {}
};

/**
 * Calculates from the backtrack list the location of the traces and vias, which realize a
 * connection found by the maze search algorithm.
 *
 * PORT NOTE: the Java constructor calls the abstract calculateNextTraceCorners() (via
 * calculateNextTrace()), which does not dispatch to the subclass inside a C++ constructor. The
 * C++ constructors therefore only store ctrl, searchTree and angleRestriction; the rest of the
 * Java constructor body is in initialize(), which getInstance() calls right after `new`.
 */
class FoundConnectionLocator : public virtual jrt::Object {
 public:
  jrt::Collection<FoundConnectionLocator_ResultItem*>* connectionItems = nullptr;
  Item* startItem = nullptr;
  int32_t startLayer = 0;
  Item* targetItem = nullptr;
  int32_t targetLayer = 0;

 protected:
  /** The array of backtrack doors from the destination to the start of a found connection of
   * the maze search algorithm. */
  jrt::Array<FoundConnectionLocator_BacktrackElement*>* backtrackArray = nullptr;
  AutorouteControl* ctrl;
  ShapeSearchTree* searchTree;
  AngleRestriction angleRestriction;
  TargetItemExpansionDoor* startDoor = nullptr;
  FloatPoint* currentFromPoint = nullptr;
  FloatPoint* previousFromPoint = nullptr;
  int32_t currentTraceLayer = 0;
  int32_t currentFromDoorIndex = 0;
  int32_t currentToDoorIndex = 0;
  int32_t currentTargetDoorIndex = 0;
  TileShape* currentTargetShape = nullptr;

  /** Creates a new instance of FoundConnectionLocator (first phase, see PORT NOTE). */
  FoundConnectionLocator(MazeSearchEngine_Result* mazeSearchResult, AutorouteControl* ctrl,
                         ShapeSearchTree* searchTree, AngleRestriction angleRestriction,
                         jrt::TreeSet<Item*>* rippedItemList,
                         jrt::Map<Item*, int32_t>* ripupCosts)
      : ctrl(ctrl), searchTree(searchTree), angleRestriction(angleRestriction) {}

  /** Second construction phase: the body of the Java constructor. */
  void initialize(MazeSearchEngine_Result* mazeSearchResult, jrt::TreeSet<Item*>* rippedItemList,
                  jrt::Map<Item*, int32_t>* ripupCosts);

 public:
  /** Returns a new Instance of FoundConnectionLocator or null, if destinationDoor is null. */
  static FoundConnectionLocator* getInstance(MazeSearchEngine_Result* mazeSearchResult,
                                             AutorouteControl* ctrl, ShapeSearchTree* searchTree,
                                             AngleRestriction angleRestriction,
                                             jrt::TreeSet<Item*>* rippedItemList,
                                             jrt::Map<Item*, int32_t>* ripupCosts);

 private:
  /** Calculates the width-safe target shape for a target door within its room. */
  static TileShape* calculateWidthSafeTargetShape(TargetItemExpansionDoor* door,
                                                  TileShape* targetShape, AutorouteControl* ctrl);

  static TileShape* insideConductionArea(Item* item, TileShape* shape);

  /** Calculates the starting point of the next trace on fromDoor. */
  static FloatPoint* calculateStartingPoint(TargetItemExpansionDoor* fromDoor,
                                            ShapeSearchTree* searchTree, AutorouteControl* ctrl);

  /** Creates a list of doors by backtracking from the destination door to the start door. */
  static jrt::Collection<FoundConnectionLocator_BacktrackElement*>* backtrack(
      MazeSearchEngine_Result* mazeSearchResult, jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* ripupCosts, int32_t netNumber);

  /** Calculates an additional corner, so that for the lines from fromPoint to the result corner
   * and from the result corner to toPoint the angle restriction is 90 degree. */
  static FloatPoint* ninetyDegreeCorner(FloatPoint* fromPoint, FloatPoint* toPoint,
                                        bool horizontalFirst);

  /** Calculates an additional corner for the 45 degree angle restriction. */
  static FloatPoint* fortyfiveDegreeCorner(FloatPoint* fromPoint, FloatPoint* toPoint,
                                           bool horizontalFirst);

 public:
  /** Calculates an additional corner according to the angle restriction. (Java:
   * package-private) */
  static FloatPoint* calculateAdditionalCorner(FloatPoint* fromPoint, FloatPoint* toPoint,
                                               bool horizontalFirst,
                                               AngleRestriction angleRestriction);

 private:
  /** Calculates the next trace on the current layer. */
  FoundConnectionLocator_ResultItem* calculateNextTrace(bool layerChanged, bool atFanoutEnd);

 protected:
  /** Calculates the corners of the next trace on the current layer. */
  virtual jrt::Collection<FloatPoint*>* calculateNextTraceCorners() = 0;

 public:
  /** Emits optional diagnostics. */
  virtual void emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity);

 private:
  FloatPoint* adjustStartCorner();

  IntPoint* padEndWithClearStub(Pin* pin, TileShape* copper, TileShape* shrunkRoom,
                                IntPoint* usualEnd, int32_t layer, bool fromPad);

 public:
  /** Whether unshovable copper of another net overlaps the pad. */
  static bool unshovableCopperOverlapsPad(BasicBoard* board, Pin* pin, TileShape* copper,
                                          int32_t layer, int32_t netNumber);

 private:
  static bool endBlockedByUnshovableCopper(BasicBoard* board, TileShape* shape, int32_t layer,
                                           int32_t netNumber, int32_t clearanceClassIndex);
};

}  // namespace fr
