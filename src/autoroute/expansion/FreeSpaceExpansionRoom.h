#pragma once

#include "autoroute/expansion/ExpansionRoom.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Expansion Areas used by the maze search algorithm. */
class FreeSpaceExpansionRoom : public virtual ExpansionRoom {
 private:
  /** The layer of this room. */
  int32_t layer;

  /** The shape of this room. */
  TileShape* shape;

  /** The list of doors to neighbour expansion rooms (ArrayList for cache locality). */
  jrt::List<ExpansionDoor*>* doors;

 protected:
  /**
   * Creates a new instance of FreeSpaceExpansionRoom. The shape is normally unbounded at
   * construction time of this room. The final (completed) shape will be a subshape of the start
   * shape, which does not overlap with any obstacle, and is as big as possible.
   */
  FreeSpaceExpansionRoom(TileShape* shape, int32_t layer);

 public:
  /** Adds door to the list of doors of this room. */
  void addDoor(ExpansionDoor* door) override;

  /** Returns the list of doors of this room to neighbour expansion rooms. */
  jrt::List<ExpansionDoor*>* getDoors() override { return this->doors; }

  /** Removes all doors from this room. */
  void clearDoors() override;

  void resetDoors() override;

  bool removeDoor(ExpandableObject* door) override;

  /** Gets the shape of this room. */
  TileShape* getShape() override { return this->shape; }

  /** Sets the shape of this room. */
  virtual void setShape(TileShape* shape) { this->shape = shape; }

  int32_t getLayer() override { return this->layer; }

  /** Checks if this room already has a door to other. */
  bool doorExists(ExpansionRoom* other) override;
};

}  // namespace fr
