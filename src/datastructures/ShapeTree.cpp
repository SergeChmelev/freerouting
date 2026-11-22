// Port of app.freerouting.datastructures.ShapeTree
#include "datastructures/ShapeTree.h"

#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

int32_t ShapeTree_Leaf::distanceToRoot() {
  int32_t result = 1;
  ShapeTree_InnerNode* currentParent = this->parent;
  while (currentParent->parent != nullptr) {
    currentParent = currentParent->parent;
    ++result;
  }
  return result;
}

void ShapeTree::insert(ShapeTree_Storable* obj) {
  int32_t shapeCount = obj->treeShapeCount(this);
  if (shapeCount <= 0) {
    return;
  }
  auto* leafArr = new jrt::Array<ShapeTree_Leaf*>(shapeCount);
  for (int32_t i = 0; i < shapeCount; i++) {
    (*leafArr)[i] = insert(obj, i);
  }
  obj->setSearchTreeEntries(leafArr, this);
}

ShapeTree_Leaf* ShapeTree::insert(ShapeTree_Storable* object, int32_t index) {
  Shape* objectShape = object->getTreeShape(this, index);
  if (objectShape == nullptr) {
    return nullptr;
  }

  RegularTileShape* boundingShape = objectShape->boundingShape(boundingDirections);
  if (boundingShape == nullptr) {
    FRLogger::warn("ShapeTree.insert: bounding shape of TreeObject is null");
    return nullptr;
  }
  // Construct a new KdLeaf and set it up
  auto* newLeaf = new ShapeTree_Leaf(object, index, nullptr, boundingShape);
  this->insert(newLeaf);
  return newLeaf;
}

jrt::Array<ShapeTree_Leaf*>* ShapeTree::toArray() { return toArrayUnlocked(); }

jrt::Array<ShapeTree_Leaf*>* ShapeTree::toArrayUnlocked() {
  auto* result = new jrt::Array<ShapeTree_Leaf*>(this->leafCount);
  if (result->length == 0) {
    return result;
  }
  ShapeTree_TreeNode* currentNode = this->root;
  int32_t currentIndex = 0;
  for (;;) {
    // go down from currentNode to the left most leaf
    while (auto* inner = jrt::fast_cast<ShapeTree_InnerNode>(currentNode)) {
      currentNode = inner->firstChild;
    }
    (*result)[currentIndex] = jrt::fast_cast<ShapeTree_Leaf>(currentNode);

    ++currentIndex;
    // go up until parent.secondChild != currentNode, which means we came from firstChild
    ShapeTree_InnerNode* currentParent = currentNode->parent;
    while (currentParent != nullptr && currentParent->secondChild == currentNode) {
      currentNode = currentParent;
      currentParent = currentNode->parent;
    }
    if (currentParent == nullptr) {
      break;
    }
    currentNode = currentParent->secondChild;
  }
  return result;
}

void ShapeTree::remove(jrt::Array<ShapeTree_Leaf*>* entries) {
  if (entries == nullptr) {
    return;
  }
  for (int32_t i = 0; i < entries->length; i++) {
    removeLeaf((*entries)[i]);
  }
}

int32_t ShapeTree::size() { return leafCount; }

void ShapeTree::statistics(const std::string& message) {
  jrt::Array<ShapeTree_Leaf*>* leafArr = this->toArrayUnlocked();
  double cumulativeDepth = 0;
  int32_t maximumDepth = 0;
  for (int32_t i = 0; i < leafArr->length; i++) {
    if ((*leafArr)[i] != nullptr) {
      int32_t distanceToRoot = (*leafArr)[i]->distanceToRoot();
      cumulativeDepth += distanceToRoot;
      maximumDepth = jrt::Math::max(maximumDepth, distanceToRoot);
    }
  }
  double averageDepth = cumulativeDepth / leafArr->length;
  FRLogger::info("MinAreaTree: Entry count: " + jrt::str(leafArr->length) +
                 " log: " + jrt::str(jrt::Math::round(jrt::Math::log(leafArr->length))) +
                 " Average depth: " + jrt::str(jrt::Math::round(averageDepth)) + " " +
                 " Maximum depth: " + jrt::str(maximumDepth) + " " + message);
}

}  // namespace fr
