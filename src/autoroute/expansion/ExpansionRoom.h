#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Interface representing a room in the expansion tree during maze routing. */
class ExpansionRoom : public virtual jrt::Object {
 public:
  /** Adds door to the list of doors of this room. */
  virtual void addDoor(ExpansionDoor* door) = 0;

  /** Returns the list of doors of this room to neighbour expansion rooms. */
  virtual jrt::List<ExpansionDoor*>* getDoors() = 0;

  /** Removes all doors from this room. */
  virtual void clearDoors() = 0;

  /** Clears the autorouting info of all doors for routing the next connection. */
  virtual void resetDoors() = 0;

  /** Checks if this room already has a door to other. */
  virtual bool doorExists(ExpansionRoom* other) = 0;

  /** Removes door from this room. Returns false if this room did not contain door. */
  virtual bool removeDoor(ExpandableObject* door) = 0;

  /** Gets the shape of this room. */
  virtual TileShape* getShape() = 0;

  /** Returns the layer of this expansion room. */
  virtual int32_t getLayer() = 0;

  /** Returns a unique identification number for this expansion room. */
  virtual int32_t getId() = 0;
};

}  // namespace fr
