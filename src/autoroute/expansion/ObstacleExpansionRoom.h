#pragma once

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Expansion Room used for pushing and ripping obstacles in the autoroute algorithm. */
class ObstacleExpansionRoom : public virtual CompleteExpansionRoom {
 private:
  Item* item;
  int32_t indexInItem;
  TileShape* shape;

  /** The list of doors to neighbour expansion rooms. */
  jrt::List<ExpansionDoor*>* doors;

  bool doorsCalculated = false;

 public:
  /** Creates a new instance of ObstacleExpansionRoom. */
  ObstacleExpansionRoom(Item* item, int32_t indexInItem, ShapeSearchTree* shapeTree);

  /** Returns the index of this shape within the item. */
  virtual int32_t getIndexInItem() { return this->indexInItem; }

  int32_t getLayer() override;

  TileShape* getShape() override { return this->shape; }

  int32_t getId() override;

  /** Checks if this room already has a 1-dimensional door to other. */
  bool doorExists(ExpansionRoom* other) override;

  /** Adds a door to the door list of this room. */
  void addDoor(ExpansionDoor* door) override;

  /**
   * Creates a 2-dim door with the other obstacle room if that is useful for the autoroute
   * algorithm. It is assumed that this room and other have a 2-dimensional overlap. Returns false
   * if no door was created.
   */
  virtual bool createOverlapDoor(ObstacleExpansionRoom* other);

 private:
  /** Whether two traces meet at a common end lying on a pin, as two traces of a net leaving the
   * same pad. */
  static bool meetOnPin(Item* first, Item* second);

 public:
  /** Returns the list of doors of this room to neighbour expansion rooms. */
  jrt::List<ExpansionDoor*>* getDoors() override { return this->doors; }

  /** Removes all doors from this room. */
  void clearDoors() override;

  void resetDoors() override;

  jrt::Collection<TargetItemExpansionDoor*>* getTargetDoors() override;

  /** Returns the item associated with this obstacle room. */
  virtual Item* getItem() { return this->item; }

  SearchTreeObject* getObject() override;

  bool removeDoor(ExpandableObject* door) override;

  /** Returns if all doors to the neighbour rooms are calculated. */
  virtual bool allDoorsCalculated() { return this->doorsCalculated; }

  virtual void setDoorsCalculated(bool value) { this->doorsCalculated = value; }

  /** Emits the shape of this room for an optional diagnostic consumer. */
  void emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) override;
};

}  // namespace fr
