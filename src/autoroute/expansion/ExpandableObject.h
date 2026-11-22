#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An object, which can be expanded by the maze expansion algorithm. */
class ExpandableObject : public virtual jrt::Object {
 public:
  /** Calculates the intersection of the shapes of the 2 objects belonging to this door. */
  virtual TileShape* getShape() = 0;

  /** Returns the dimension of the intersection of the shapes of the 2 objects of this door. */
  virtual int32_t getDimension() = 0;

  /** Returns the other room to room if this is a door and the other room is a
   * CompleteExpansionRoom. Else null is returned. */
  virtual CompleteExpansionRoom* otherRoom(CompleteExpansionRoom* room) = 0;

  /** Returns the count of MazeSearchElements in this expandable object. */
  virtual int32_t mazeSearchElementCount() = 0;

  /** Returns the index-th MazeSearchElements in this expandable object. */
  virtual MazeSearchElement* getMazeSearchElement(int32_t index) = 0;

  /** Resets this ExpandableObject for autorouting the next connection. */
  virtual void reset() = 0;

  /** Returns a unique identification number for this object to allow deterministic sorting. */
  virtual int32_t getId() = 0;
};

}  // namespace fr
