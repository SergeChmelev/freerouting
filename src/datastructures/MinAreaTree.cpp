// Port of app.freerouting.datastructures.MinAreaTree
#include "datastructures/MinAreaTree.h"

#include <algorithm>

#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Shape.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {

// PORT NOTE: area() and unionArea() of octagons given by ShapeTree_TreeNode::oct coordinates.
double octArea(const int32_t* o) {
  return IntOctagon::areaOf(o[0], o[2], o[1], o[3], o[6], o[7], o[4], o[5]);
}

double octUnionArea(const int32_t* a, const int32_t* o) {
  return IntOctagon::areaOf(std::min(a[0], o[0]), std::min(a[2], o[2]), std::max(a[1], o[1]),
                            std::max(a[3], o[3]), std::min(a[6], o[6]), std::max(a[7], o[7]),
                            std::min(a[4], o[4]), std::max(a[5], o[5]));
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// MinAreaTree_ThreadScratchStacks
// ---------------------------------------------------------------------------------------------

MinAreaTree_ThreadScratchStacks::MinAreaTree_ThreadScratchStacks()
    : stacks(new (jrt::Like{this}) jrt::ArrayList<ArrayStack<ShapeTree_TreeNode*>*>(2)) {}

ArrayStack<ShapeTree_TreeNode*>* MinAreaTree_ThreadScratchStacks::acquire() {
  if (activeDepth == stacks->size()) {
    // PORT NOTE (stage E): the scratch stacks are a process-wide cache: on the heap, not in the
    // operation region that first needs one.
    jrt::HeapScope heap;
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
  // Java: ThreadLocal.withInitial(ThreadScratchStacks::new).
  return jrt::perThread<MinAreaTree_ThreadScratchStacks>(
      [] { return new (jrt::kBoard) MinAreaTree_ThreadScratchStacks(); });
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
  // PORT NOTE: octagon nodes are tested on their coordinates (ShapeTree_TreeNode::oct), the
  // children of a node on the copies the node keeps (childOct) before they are pushed: a child
  // that does not intersect is not loaded (it was dropped after the pop before; the order of the
  // others on the stack is the same).
  const ShapeTree_OctagonQuery query(shape);
  try {
    if (this->root->intersects(query, static_cast<Shape*>(shape))) {
      stack->push(this->root);
    }
    ShapeTree_TreeNode* currentNode;
    while ((currentNode = stack->pop()) != nullptr) {
      onNodeVisited();
      if (currentNode->isLeaf) {
        result->add(static_cast<ShapeTree_Leaf*>(currentNode));
      } else {
        auto* inner = static_cast<ShapeTree_InnerNode*>(currentNode);
        if (inner->childIntersects(0, query, static_cast<Shape*>(shape))) {
          prefetchTreeNode(inner->firstChild);
          stack->push(inner->firstChild);
        }
        if (inner->childIntersects(1, query, static_cast<Shape*>(shape))) {
          prefetchTreeNode(inner->secondChild);
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
  // PORT NOTE (stage E): inner-node bounding shapes belong to the tree, not to the operation
  // (a search region) that inserts a leaf.
  jrt::DomainScope domain(this);
  leafInserted(leaf);
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
  newNode->refreshChildBounds();  // PORT NOTE: ShapeTree_InnerNode::childOct
  if (currentParent != nullptr) {
    currentParent->refreshChildBounds();
  }
}

ShapeTree_Leaf* MinAreaTree::positionLocate(ShapeTree_TreeNode* currentNode,
                                            ShapeTree_Leaf* leafToInsert) {
  ShapeTree_TreeNode* node = currentNode;
  // PORT NOTE: octagons are read from the coordinates kept in the nodes (ShapeTree_TreeNode::oct).
  const bool leafIsOctagon = leafToInsert->octValid;
  const int32_t* l = leafToInsert->oct;

  while (!node->isLeaf) {
    auto* currentInnerNode = static_cast<ShapeTree_InnerNode*>(node);
    // PORT NOTE: an octagon bounding shape that already contains the leaf's is kept instead of
    // being replaced by an equal union (bounding shapes are immutable and compared by value).
    // (IntOctagon::unionEquals)
    const int32_t* b = currentInnerNode->oct;
    bool containsLeaf = leafIsOctagon && currentInnerNode->octValid && l[0] >= b[0] &&
                        l[2] >= b[2] && l[1] <= b[1] && l[3] <= b[3] && l[6] >= b[6] &&
                        l[7] <= b[7] && l[4] >= b[4] && l[5] <= b[5];
    if (!containsLeaf) {
      // PORT NOTE (stage C): an inner node owns its bounding shape (always a fresh union, never
      // shared: board copies rebuild their trees by insertion), so the replaced one is freed.
      RegularTileShape* oldBounds = currentInnerNode->boundingShape;
      currentInnerNode->setBoundingShape(leafToInsert->boundingShape->union_(oldBounds));
      jrt::dispose(oldBounds);
    }

    // Choose the child, so that the area increase of that child after taking the union
    // with the shape of leafToInsert is minimal.

    // PORT NOTE: for octagons the area of the union is computed without creating it (same
    // value; the union shapes were only used for their area).
    ShapeTree_TreeNode* firstChild = currentInnerNode->firstChild;
    RegularTileShape* firstChildShape = firstChild->boundingShape;
    double firstAreaIncrease;
    if (leafIsOctagon && firstChild->octValid) {
      firstAreaIncrease = octUnionArea(l, firstChild->oct) - octArea(firstChild->oct);
    } else {
      RegularTileShape* tmpUnion = leafToInsert->boundingShape->union_(firstChildShape);
      firstAreaIncrease = tmpUnion->area() - firstChildShape->area();
      jrt::dispose(tmpUnion);
    }

    ShapeTree_TreeNode* secondChild = currentInnerNode->secondChild;
    RegularTileShape* secondChildShape = secondChild->boundingShape;
    double secondAreaIncrease;
    if (leafIsOctagon && secondChild->octValid) {
      secondAreaIncrease = octUnionArea(l, secondChild->oct) - octArea(secondChild->oct);
    } else {
      RegularTileShape* tmpUnion = leafToInsert->boundingShape->union_(secondChildShape);
      secondAreaIncrease = tmpUnion->area() - secondChildShape->area();
      jrt::dispose(tmpUnion);
    }

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
  jrt::DomainScope domain(this);  // see insertUnlocked
  // remove the leaf node
  ShapeTree_InnerNode* parent = leaf->parent;
  --this->leafCount;
  leafRemoved(leaf);
  // PORT NOTE (stage C): the tree owns its leaves (the bounding shape is owned by the item's
  // precalculated tree shapes, not by the leaf). Experimental: freed when removed.
  jrt::dispose(leaf);
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
    grandParent->refreshChildBounds();  // PORT NOTE: ShapeTree_InnerNode::childOct
  }
  // PORT NOTE (stage C): the removed inner node and its bounding shape are owned by the tree.
  // The node is cleared first: in a board arena (optimizer worker board) dispose() leaves it in
  // place, and a stale pointer to the freed shape would remain in live arena memory.
  RegularTileShape* parentBounds = parent->boundingShape;
  parent->parent = nullptr;
  parent->firstChild = nullptr;
  parent->secondChild = nullptr;
  parent->setBoundingShape(nullptr);
  jrt::dispose(parentBounds);
  jrt::dispose(parent);

  // recalculate the bounding shapes of the ancestors
  // as long as it gets smaller after removing leaf
  ShapeTree_InnerNode* nodeToRecalculate = grandParent;
  while (nodeToRecalculate != nullptr) {
    RegularTileShape* newBounds = nodeToRecalculate->secondChild->boundingShape->union_(
        nodeToRecalculate->firstChild->boundingShape);
    if (newBounds->contains(nodeToRecalculate->boundingShape)) {
      // the new bounds are not smaller, no further recalculate necessary
      jrt::dispose(newBounds);
      break;
    }
    jrt::dispose(nodeToRecalculate->boundingShape);
    nodeToRecalculate->setBoundingShape(newBounds);
    nodeToRecalculate = nodeToRecalculate->parent;
  }
}

}  // namespace fr
