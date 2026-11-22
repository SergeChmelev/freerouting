// Port of app.freerouting.datastructures.MinAreaTree
#pragma once

#include "datastructures/ArrayStack.h"
#include "datastructures/ShapeTree.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Scratch stack pool supporting re-entrant tree queries without cross-query stack corruption.
 * PORT NOTE: Java keeps one per thread (ThreadLocal); the port is single-threaded, so there is one.
 */
class MinAreaTree_ThreadScratchStacks final : public virtual jrt::Object {
 public:
  MinAreaTree_ThreadScratchStacks();

  ArrayStack<ShapeTree_TreeNode*>* acquire();

  void release(ArrayStack<ShapeTree_TreeNode*>* stack);

 private:
  static constexpr int32_t INITIAL_STACK_CAPACITY = 1024;
  jrt::List<ArrayStack<ShapeTree_TreeNode*>*>* stacks;
  int32_t activeDepth = 0;
};

/**
 * Binary search tree for shapes in the plane. The shapes are stored in the leaves of the tree.
 * Starting from the root go to the child, so that the increase of the bounding shape of that child
 * is minimal after adding the new shape, until you reach a leaf.
 */
class MinAreaTree : public ShapeTree {
 public:
  using ShapeTree::insert;  // C++ name hiding: keep insert(Storable*) / insert(Storable*, int)

  /** Constructor with a fixed set of directions defining the keys and the surrounding shapes. */
  explicit MinAreaTree(ShapeBoundingDirections* directions) : ShapeTree(directions) {}

  /** Calculates the objects in this tree, which overlap with shape, in Leaf order. */
  virtual jrt::List<ShapeTree_Leaf*>* overlaps(RegularTileShape* shape);

  /** Calculates overlaps into result in Leaf order. */
  virtual void overlaps(RegularTileShape* shape, jrt::List<ShapeTree_Leaf*>* result);

  void insert(ShapeTree_Leaf* leaf) override;

  /** Removes an entry from this tree. */
  void removeLeaf(ShapeTree_Leaf* leaf) override;

 protected:
  /** Reusable traversal stack pool for overlap queries (ThreadLocal in Java). */
  static MinAreaTree_ThreadScratchStacks* SCRATCH_STACKS();

  /** Acquires a scratch stack for tree traversal. Must be released via releaseScratchStack. */
  static ArrayStack<ShapeTree_TreeNode*>* acquireScratchStack();

  /** Releases a scratch stack, clearing all node references. */
  static void releaseScratchStack(ArrayStack<ShapeTree_TreeNode*>* stack);

  /** Calculates overlaps while the read lock is already held by the caller. */
  jrt::List<ShapeTree_Leaf*>* overlapsUnlocked(RegularTileShape* shape);

  /** Appends overlapping leaves into result while read lock is held. */
  void overlapsUnlocked(RegularTileShape* shape, jrt::List<ShapeTree_Leaf*>* result);

  /**
   * Appends overlapping leaves into result in traversal order, without sorting them, while the
   * read lock is held.
   */
  void collectOverlapsUnlocked(RegularTileShape* shape, jrt::List<ShapeTree_Leaf*>* result);

  /** Extension hook for testing and instrumentation during tree traversal. */
  virtual void onNodeVisited() {}

 private:
  /** Inserts a leaf while the write lock is already held. */
  void insertUnlocked(ShapeTree_Leaf* leaf);

  ShapeTree_Leaf* positionLocate(ShapeTree_TreeNode* currentNode, ShapeTree_Leaf* leafToInsert);

  /** Removes a leaf while the write lock is already held. */
  void removeLeafUnlocked(ShapeTree_Leaf* leaf);
};

}  // namespace fr
