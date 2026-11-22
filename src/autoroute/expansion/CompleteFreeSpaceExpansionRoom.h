#pragma once

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/FreeSpaceExpansionRoom.h"
#include "board/searchtree/SearchTreeObject.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * An expansion room, whose shape is completely calculated, so that it can be stored in a shape
 * tree.
 */
class CompleteFreeSpaceExpansionRoom : public FreeSpaceExpansionRoom,
                                       public virtual CompleteExpansionRoom,
                                       public virtual SearchTreeObject {
 private:
  /** Identification number for implementing the Comparable interface. */
  int32_t id;

  /** The array of entries in the SearchTree. Consists of just one element. */
  jrt::Array<ShapeTree_Leaf*>* treeEntries = nullptr;

  /** The list of doors to items of the own net. */
  jrt::Collection<TargetItemExpansionDoor*>* targetDoors;

  bool roomIsNetDependent = false;

 public:
  /** Creates a new instance of CompleteFreeSpaceExpansionRoom. */
  CompleteFreeSpaceExpansionRoom(TileShape* shape, int32_t layer, int32_t id);

  void setSearchTreeEntries(jrt::Array<ShapeTree_Leaf*>* entries, ShapeTree* tree) override {
    treeEntries = entries;
  }

  int32_t compareTo(jrt::Object* other) override;

  /** Removes the tree entries of this room from shapeTree. */
  virtual void removeFromTree(ShapeTree* shapeTree);

  int32_t treeShapeCount(ShapeTree* shapeTree) override { return 1; }

  TileShape* getTreeShape(ShapeTree* shapeTree, int32_t index) override { return this->getShape(); }

  int32_t shapeLayer(int32_t index) override { return this->getLayer(); }

  bool isObstacle(int32_t netNumber) override { return true; }

  using SearchTreeObject::isTraceObstacle;
  bool isTraceObstacle(int32_t netNumber) override { return true; }

  /** Will be called when the room overlaps with net dependent objects. */
  virtual void setNetDependent() { this->roomIsNetDependent = true; }

  /** Returns if the room overlaps with net dependent objects. In this case it cannot be retained
   * when the net number changes in autorouting. */
  virtual bool isNetDependent() { return this->roomIsNetDependent; }

  int32_t getId() override { return id; }

  /** Returns the list doors to target items of this room. */
  jrt::Collection<TargetItemExpansionDoor*>* getTargetDoors() override { return this->targetDoors; }

  /** Adds door to the list of target doors of this room. */
  virtual void addTargetDoor(TargetItemExpansionDoor* door);

  bool removeDoor(ExpandableObject* door) override;

  SearchTreeObject* getObject() override { return this; }

  /** Calculates the doors to the start and destination items of the autoroute algorithm. */
  virtual void calculateTargetDoors(ShapeTree_TreeEntry* ownNetObject, int32_t netNumber,
                                    ShapeSearchTree* autorouteSearchTree);

  /** Emits the shape of this room for an optional diagnostic consumer. */
  void emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) override;

  /** Check if this FreeSpaceExpansionRoom is valid. */
  virtual bool validate(AutorouteEngine* autorouteEngine);

  /** Removes all doors and target doors from this room. */
  void clearDoors() override;

  void resetDoors() override;
};

}  // namespace fr
