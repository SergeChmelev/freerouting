#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Adjustment directions for maze search expansion. */
enum class MazeSearchElement_Adjustment : int32_t { NONE, RIGHT, LEFT };

inline std::string MazeSearchElement_Adjustment_name(MazeSearchElement_Adjustment e) {
  switch (e) {
    case MazeSearchElement_Adjustment::NONE: return "NONE";
    case MazeSearchElement_Adjustment::RIGHT: return "RIGHT";
    case MazeSearchElement_Adjustment::LEFT: return "LEFT";
  }
  return "";
}
inline std::string MazeSearchElement_Adjustment_toString(MazeSearchElement_Adjustment e) {
  return MazeSearchElement_Adjustment_name(e);
}
inline jrt::Array<MazeSearchElement_Adjustment>* MazeSearchElement_Adjustment_values() {
  return jrt::Array<MazeSearchElement_Adjustment>::of({MazeSearchElement_Adjustment::NONE,
                                                       MazeSearchElement_Adjustment::RIGHT,
                                                       MazeSearchElement_Adjustment::LEFT});
}
inline MazeSearchElement_Adjustment MazeSearchElement_Adjustment_valueOf(const std::string& s) {
  if (s == "NONE") return MazeSearchElement_Adjustment::NONE;
  if (s == "RIGHT") return MazeSearchElement_Adjustment::RIGHT;
  if (s == "LEFT") return MazeSearchElement_Adjustment::LEFT;
  throw jrt::IllegalArgumentException("No enum constant MazeSearchElement.Adjustment." + s);
}

/** Describes the structure of a section of an ExpandableObject. Fully implemented here. */
class MazeSearchElement : public virtual jrt::Object {
 public:
  /** True if this door is already occupied by the maze expanding algorithm. */
  bool isOccupied = false;

  /** Used for backtracking in the maze expanding algorithm. */
  ExpandableObject* backtrackDoor = nullptr;

  int32_t sectionNoOfBacktrackDoor = 0;

  bool roomRipped = false;

  MazeSearchElement_Adjustment adjustment = MazeSearchElement_Adjustment::NONE;

  /** The ripup cost paid to enter this door's room via the maze search. Zero when roomRipped is
   * false. */
  int32_t ripupCost = 0;

  /** Resets this MazeSearchElement for autorouting the next connection. */
  virtual void reset() {
    isOccupied = false;
    backtrackDoor = nullptr;
    sectionNoOfBacktrackDoor = 0;
    roomRipped = false;
    adjustment = MazeSearchElement_Adjustment::NONE;
    ripupCost = 0;
  }
};

}  // namespace fr
