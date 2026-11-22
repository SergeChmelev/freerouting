#pragma once

#include "autoroute/expansion/ExpandableObject.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An expansion door leading to a start or destination item of the autoroute algorithm. */
class TargetItemExpansionDoor : public virtual ExpandableObject {
 public:
  Item* item;
  int32_t treeEntryNo;
  CompleteExpansionRoom* room;

 private:
  TileShape* shape;
  MazeSearchElement* mazeSearchInfo;

 public:
  /** Creates a new instance of ItemExpansionInfo. */
  TargetItemExpansionDoor(Item* item, int32_t treeEntryNo, CompleteExpansionRoom* room,
                          ShapeSearchTree* searchTree);

  TileShape* getShape() override { return this->shape; }

  int32_t getDimension() override { return 2; }

  /** Returns true if this door leads to a destination item rather than a start item. */
  virtual bool isDestinationDoor();

  /** Returns true if this door leads to a start item. */
  virtual bool isStartDoor();

  CompleteExpansionRoom* otherRoom(CompleteExpansionRoom* room) override { return nullptr; }

  MazeSearchElement* getMazeSearchElement(int32_t index) override { return mazeSearchInfo; }

  int32_t mazeSearchElementCount() override { return 1; }

  void reset() override;

  int32_t getId() override;
};

}  // namespace fr
