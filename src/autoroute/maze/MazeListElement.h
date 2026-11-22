#pragma once

#include "autoroute/maze/MazeSearchElement.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Information for the maze expand algorithm contained in an expansion list element.
 * Java: implements Comparable<MazeListElement>; ordered by jrt natural ordering (compareTo) in the
 * maze search priority queue. Package-private fields are public here.
 */
class MazeListElement : public virtual jrt::Object {
 public:
  /** The door or drill belonging to this MazeListElement stored in the expansion list. */
  ExpandableObject* door;

  /** The section number of the door (or the layer of the drill). */
  int32_t sectionNoOfDoor;

  /** The door, from which this door was expanded. */
  ExpandableObject* backtrackDoor;

  /** The section number of the backtrack door. */
  int32_t sectionNoOfBacktrackDoor;

  /** The weighted distance to the start of the expansion. */
  double expansionValue;

  /** The expansion value plus the shortest distance to a destination. */
  double sortingValue;

  /** The next room, which will be expanded from this maze search element. */
  CompleteExpansionRoom* nextRoom;

  /** Point of the region of the expansion door, which has the shortest distance to the backtrack
   * door. */
  FloatLine* shapeEntry;

  bool roomRipped;

  MazeSearchElement_Adjustment adjustment;

  bool alreadyChecked;

  int32_t ripupCost = 0;

  /** Creates a new instance of ExpansionInfo. */
  MazeListElement(ExpandableObject* door, int32_t sectionNoOfDoor, ExpandableObject* backtrackDoor,
                  int32_t sectionNoOfBacktrackDoor, double expansionValue, double sortingValue,
                  CompleteExpansionRoom* nextRoom, FloatLine* shapeEntry, bool roomRipped,
                  MazeSearchElement_Adjustment adjustment, bool alreadyChecked)
      : door(door),
        sectionNoOfDoor(sectionNoOfDoor),
        backtrackDoor(backtrackDoor),
        sectionNoOfBacktrackDoor(sectionNoOfBacktrackDoor),
        expansionValue(expansionValue),
        sortingValue(sortingValue),
        nextRoom(nextRoom),
        shapeEntry(shapeEntry),
        roomRipped(roomRipped),
        adjustment(adjustment),
        alreadyChecked(alreadyChecked) {}

  /** Orders by sortingValue, then expansionValue, door id and section number. */
  virtual int32_t compareTo(MazeListElement* other);
};

}  // namespace fr
