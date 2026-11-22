#pragma once

#include "autoroute/expansion/ExpandableObject.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An ExpansionDoor is a common edge between two ExpansionRooms. */
class ExpansionDoor : public virtual ExpandableObject {
 public:
  /** The first room of this door. */
  ExpansionRoom* firstRoom;

  /** The second room of this door. */
  ExpansionRoom* secondRoom;

  /** The dimension of a door may be 1 or 2. */
  int32_t dimension;

  /** Each section of the following array can be expanded separately by the maze search. */
  jrt::Array<MazeSearchElement*>* sectionArr = nullptr;

  /** Creates a new instance of ExpansionDoor. */
  ExpansionDoor(ExpansionRoom* firstRoom, ExpansionRoom* secondRoom, int32_t dimension)
      : firstRoom(firstRoom), secondRoom(secondRoom), dimension(dimension) {}

  /** Creates a new instance of ExpansionDoor (dimension from the room shape intersection). */
  ExpansionDoor(ExpansionRoom* firstRoom, ExpansionRoom* secondRoom);

 private:
  TileShape* cachedShape = nullptr;
  TileShape* cachedFirstRoomShape = nullptr;
  TileShape* cachedSecondRoomShape = nullptr;
  jrt::Array<FloatLine*>* cachedSectionSegments = nullptr;
  TileShape* cachedSectionShape = nullptr;
  double cachedSectionOffset = 0;
  int32_t cachedSectionCount = 0;

 public:
  /** Calculates the intersection of the shapes of the 2 rooms belonging to this door. */
  TileShape* getShape() override;

  /** The dimension of a door may be 1 or 2. 2-dimensional doors can only exist between
   * ObstacleExpansionRooms. */
  int32_t getDimension() override { return this->dimension; }

  /** Returns the other room of this door, or null if room is neither firstRoom nor secondRoom. */
  virtual ExpansionRoom* otherRoom(ExpansionRoom* room);

  /** Returns the other room of this door, or null if room is neither firstRoom nor secondRoom,
   * or if the other room is not a CompleteExpansionRoom. */
  CompleteExpansionRoom* otherRoom(CompleteExpansionRoom* room) override;

  int32_t mazeSearchElementCount() override { return this->sectionArr->length; }

  MazeSearchElement* getMazeSearchElement(int32_t index) override {
    return (*this->sectionArr)[index];
  }

  /** Calculates the Line segments of the sections of this door. */
  virtual jrt::Array<FloatLine*>* getSectionSegments(double offsetParam);

 private:
  /** Calculates a diagonal line of the 2-dimensional doorShape which represents the restraint
   * line between the shapes of firstRoom and secondRoom. */
  FloatLine* calcDoorLineSegment(TileShape* doorShape);

 public:
  /** Resets this ExpandableObject for autorouting the next connection. */
  void reset() override;

  int32_t getId() override;

  /** Allocates and initialises sectionCount sections. (Java: package-private) */
  virtual void allocateSections(int32_t sectionCount);
};

}  // namespace fr
