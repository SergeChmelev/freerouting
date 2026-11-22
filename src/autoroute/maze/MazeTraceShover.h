#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A section of an expansion door reached by shoving a trace. */
class MazeTraceShover_DoorSection : public virtual jrt::Object {
 public:
  ExpansionDoor* door;
  int32_t sectionIndex;
  FloatLine* sectionLine;

  /** (Java: package-private constructor) */
  MazeTraceShover_DoorSection(ExpansionDoor* door, int32_t sectionIndex, FloatLine* sectionLine)
      : door(door), sectionIndex(sectionIndex), sectionLine(sectionLine) {}
};

/** Auxiliary functions for shoving traces in the maze search algorithm. */
class MazeTraceShover final : public virtual jrt::Object {
 private:
  MazeTraceShover() = default;

 public:
  /**
   * Checks if the shove of the trace line of obstacleRoom is possible to the left or right.
   * Returns false if shoving is not possible; otherwise the reachable door sections are added to
   * toDoorList.
   */
  static bool checkShoveTraceLine(MazeListElement* listElement,
                                  ObstacleExpansionRoom* obstacleRoom, RoutingBoard* board,
                                  AutorouteControl* ctrl, bool shoveToTheLeft,
                                  jrt::Collection<MazeTraceShover_DoorSection*>* toDoorList);

 private:
  static bool endPointsMatching(PolylineTrace* trace, Item* fromItem);
};

}  // namespace fr
