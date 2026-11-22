#pragma once

#include "autoroute/expansion/FreeSpaceExpansionRoom.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An expansion room, whose shape is not yet completely calculated. */
class IncompleteFreeSpaceExpansionRoom : public FreeSpaceExpansionRoom {
 private:
  /** A shape which should be contained in the completed shape. */
  TileShape* containedShape;

 public:
  /** If shape == null, it means shape is the whole plane. */
  IncompleteFreeSpaceExpansionRoom(TileShape* shape, int32_t layer, TileShape* containedShape)
      : FreeSpaceExpansionRoom(shape, layer), containedShape(containedShape) {}

  /** Gets the shape that is contained within this room. */
  virtual TileShape* getContainedShape() { return this->containedShape; }

  /** Sets the contained shape for this room. */
  virtual void setContainedShape(TileShape* shape) { this->containedShape = shape; }

  /** Returns an empty list of target doors for incomplete rooms. */
  virtual jrt::Collection<TargetItemExpansionDoor*>* getTargetDoors();

  int32_t getId() override;
};

}  // namespace fr
