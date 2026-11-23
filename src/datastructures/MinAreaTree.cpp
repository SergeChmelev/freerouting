// Port of app.freerouting.datastructures.MinAreaTree
#include "datastructures/MinAreaTree.h"

#include <algorithm>

#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Shape.h"
#include "logger/FRLogger.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// MinAreaTree_ThreadScratchStacks
// ---------------------------------------------------------------------------------------------

MinAreaTree_ThreadScratchStacks::MinAreaTree_ThreadScratchStacks()
    : stacks(new (jrt::kOp) jrt::ArrayList<ArrayStack<ShapeTree_TreeNode*>*>(2)) {}

ArrayStack<ShapeTree_TreeNode*>* MinAreaTree_ThreadScratchStacks::acquire() {
  if (activeDepth == stacks->size()) {
    stacks->add(new (jrt::kOp) ArrayStack<ShapeTree_TreeNode*>(INITIAL_STACK_CAPACITY));
  }
  ArrayStack<ShapeTree_TreeNode*>* stack = stacks->get(activeDepth++);
  stack->reset();
  return stack;
}

void MinAreaTree_ThreadScratchStacks::release(ArrayStack<ShapeTree_TreeNode*>* stack) {
  stack->reset();
  if (activeDepth > 0) {
    activeDepth--;
  }
}

// ---------------------------------------------------------------------------------------------
// MinAreaTree
// ---------------------------------------------------------------------------------------------

MinAreaTree_ThreadScratchStacks* MinAreaTree::SCRATCH_STACKS() {
  static MinAreaTree_ThreadScratchStacks* v = new (jrt::kBoard) MinAreaTree_ThreadScratchStacks();
  return v;
}

ArrayStack<ShapeTree_TreeNode*>* MinAreaTree::acquireScratchStack() {
  return SCRATCH_STACKS()->acquire();
}

void MinAreaTree::releaseScratchStack(ArrayStack<ShapeTree_TreeNode*>* stack) {
  SCRATCH_STACKS()->release(stack);
}

jrt::List<ShapeTree_Leaf*>* MinAreaTree::overlaps(RegularTileShape* shape) {
  return overlapsUnlocked(shape);
}

void MinAreaTree::overlaps(RegularTileShape* shape, jrt::List<ShapeTree_Leaf*>* result) {
  overlapsUnlocked(shape, result);
}

jrt::List<ShapeTree_Leaf*>* MinAreaTree::overlapsUnlocked(RegularTileShape* shape) {
  jrt::List<ShapeTree_Leaf*>* foundOverlaps = new (jrt::kOp) jrt::ArrayList<ShapeTree_Leaf*>();
  overlapsUnlocked(shape, foundOverlaps);
  return foundOverlaps;
}

void MinAreaTree::overlapsUnlocked(RegularTileShape* shape, jrt::List<ShapeTree_Leaf*>* result) {
  int32_t first = result->size();
  collectOverlapsUnlocked(shape, result);
  if (first == 0) {
    jrt::Collections::sort<ShapeTree_Leaf*>(result);
  } else {
    // result.subList(first, result.size()).sort(null): stable sort of the appended part only
    int32_t n = result->size();
    std::vector<ShapeTree_Leaf*> tail;
    tail.reserve(n - first);
    for (int32_t i = first; i < n; i++) {
      tail.push_back(result->get(i));
    }
    std::stable_sort(tail.begin(), tail.end(), [](ShapeTree_Leaf* a, ShapeTree_Leaf* b) {
      return a->compareTo(b) < 0;
    });
    for (int32_t i = first; i < n; i++) {
      result->set(i, tail[i - first]);
    }
  }
}

void MinAreaTree::collectOverlapsUnlocked(RegularTileShape* shape,
                                          jrt::List<ShapeTree_Leaf*>* result) {
  if (this->root == nullptr) {
    return;
  }
  ArrayStack<ShapeTree_TreeNode*>* stack = acquireScratchStack();
  try {
    stack->push(this->root);
    ShapeTree_TreeNode* currentNode;
    while ((currentNode = stack->pop()) != nullptr) {
      onNodeVisited();
      if (currentNode->boundingShape->intersects(static_cast<Shape*>(shape))) {
        if (auto* leaf = jrt::fast_cast<ShapeTree_Leaf>(currentNode)) {
          result->add(leaf);
        } else {
          auto* inner = static_cast<ShapeTree_InnerNode*>(currentNode);
          stack->push(inner->firstChild);
          stack->push(inner->secondChild);
        }
      }
    }
  } catch (...) {
    releaseScratchStack(stack);
    throw;
  }
  releaseScratchStack(stack);
}

void MinAreaTree::insert(ShapeTree_Leaf* leaf) { insertUnlocked(leaf); }

void MinAreaTree::insertUnlocked(ShapeTree_Leaf* leaf) {
  ++this->leafCount;

  // Tree is empty - just insert the new leaf
  if (root == nullptr) {
    root = leaf;
    return;
  }

  // Non-empty tree - do a recursive location for leaf replacement
  ShapeTree_Leaf* leafToReplace = positionLocate(root, leaf);

  // Construct a new node - whenever a leaf is added so is a new node
  RegularTileShape* newBounds = leaf->boundingShape->union_(leafToReplace->boundingShape);
  ShapeTree_InnerNode* currentParent = leafToReplace->parent;
  auto* newNode = new (jrt::kBoardData) ShapeTree_InnerNode(newBounds, currentParent);

  if (leafToReplace->parent != nullptr) {
    // Replace the pointer from the parent to the leaf with our new node
    if (leafToReplace == currentParent->firstChild) {
      currentParent->firstChild = newNode;
    } else {
      currentParent->secondChild = newNode;
    }
  }
  // Update the parent pointers of the old leaf and new leaf to point to new node
  leafToReplace->parent = newNode;
  leaf->parent = newNode;

  // Insert the children in any order.
  newNode->firstChild = leafToReplace;
  newNode->secondChild = leaf;

  if (root == leafToReplace) {
    root = newNode;
  }
}

ShapeTree_Leaf* MinAreaTree::positionLocate(ShapeTree_TreeNode* currentNode,
                                            ShapeTree_Leaf* leafToInsert) {
  ShapeTree_TreeNode* node = currentNode;

  while (jrt::fast_cast<ShapeTree_Leaf>(node) == nullptr) {
    auto* currentInnerNode = static_cast<ShapeTree_InnerNode*>(node);
    currentInnerNode->boundingShape =
        leafToInsert->boundingShape->union_(currentInnerNode->boundingShape);

    // Choose the child, so that the area increase of that child after taking the union
    // with the shape of leafToInsert is minimal.

    RegularTileShape* firstChildShape = currentInnerNode->firstChild->boundingShape;
    RegularTileShape* unionWithFirstChildShape = leafToInsert->boundingShape->union_(firstChildShape);
    double firstAreaIncrease = unionWithFirstChildShape->area() - firstChildShape->area();

    RegularTileShape* secondChildShape = currentInnerNode->secondChild->boundingShape;
    RegularTileShape* unionWithSecondChildShape =
        leafToInsert->boundingShape->union_(secondChildShape);
    double secondAreaIncrease = unionWithSecondChildShape->area() - secondChildShape->area();

    if (firstAreaIncrease <= secondAreaIncrease) {
      node = currentInnerNode->firstChild;
    } else {
      node = currentInnerNode->secondChild;
    }
  }
  return static_cast<ShapeTree_Leaf*>(node);
}

void MinAreaTree::removeLeaf(ShapeTree_Leaf* leaf) { removeLeafUnlocked(leaf); }

void MinAreaTree::removeLeafUnlocked(ShapeTree_Leaf* leaf) {
  if (leaf == nullptr) {
    return;
  }
  if (leaf->parent == nullptr && root != leaf) {
    return;
  }
  // remove the leaf node
  ShapeTree_InnerNode* parent = leaf->parent;
  // Keep payload fields valid for callers that retained this Leaf from an earlier query; only
  // structural ownership is cleared by detaching the leaf.
  leaf->parent = nullptr;
  --this->leafCount;
  if (parent == nullptr) {
    // tree gets empty
    root = nullptr;
    return;
  }
  // find the other leaf of the parent
  ShapeTree_TreeNode* otherLeaf;
  if (parent->secondChild == leaf) {
    otherLeaf = parent->firstChild;
  } else if (parent->firstChild == leaf) {
    otherLeaf = parent->secondChild;
  } else {
    FRLogger::warn("MinAreaTree.remove_leaf: parent inconsistent");
    otherLeaf = nullptr;
  }
  // link the other leaf to the grandParent and remove the parent node
  ShapeTree_InnerNode* grandParent = parent->parent;
  if (otherLeaf == nullptr) {
    throw jrt::NullPointerException("MinAreaTree.remove_leaf: otherLeaf is null");
  }
  otherLeaf->parent = grandParent;
  if (grandParent == nullptr) {
    // only one leaf left in the tree
    root = otherLeaf;
  } else {
    if (grandParent->secondChild == parent) {
      grandParent->secondChild = otherLeaf;
    } else if (grandParent->firstChild == parent) {
      grandParent->firstChild = otherLeaf;
    } else {
      FRLogger::warn("MinAreaTree.remove_leaf: grandParent inconsistent");
    }
  }
  parent->parent = nullptr;
  parent->firstChild = nullptr;
  parent->secondChild = nullptr;
  parent->boundingShape = nullptr;

  // recalculate the bounding shapes of the ancestors
  // as long as it gets smaller after removing leaf
  ShapeTree_InnerNode* nodeToRecalculate = grandParent;
  while (nodeToRecalculate != nullptr) {
    RegularTileShape* newBounds = nodeToRecalculate->secondChild->boundingShape->union_(
        nodeToRecalculate->firstChild->boundingShape);
    if (newBounds->contains(nodeToRecalculate->boundingShape)) {
      // the new bounds are not smaller, no further recalculate necessary
      break;
    }
    nodeToRecalculate->boundingShape = newBounds;
    nodeToRecalculate = nodeToRecalculate->parent;
  }
}

}  // namespace fr
