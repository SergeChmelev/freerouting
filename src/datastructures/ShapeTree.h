// Port of app.freerouting.datastructures.ShapeTree
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Interface, which must be implemented by objects to be stored in a ShapeTree. */
class ShapeTree_Storable : public virtual jrt::Object {
 public:
  /** Comparable<Object>.compareTo (used to order tree query results). */
  virtual int32_t compareTo(jrt::Object* other) = 0;

  /** Number of shapes of an object to store in shapeTree. */
  virtual int32_t treeShapeCount(ShapeTree* shapeTree) = 0;

  /** Get the Shape of this object with index stored in the ShapeTree tree. */
  virtual TileShape* getTreeShape(ShapeTree* tree, int32_t index) = 0;

  /**
   * Stores the entries in the ShapeTrees of this object for better performance while for example
   * deleting tree entries. Called only by insert methods of class ShapeTree.
   */
  virtual void setSearchTreeEntries(jrt::Array<ShapeTree_Leaf*>* entries, ShapeTree* tree) = 0;
};

/** Information of a single object stored in a tree. */
class ShapeTree_TreeEntry : public virtual jrt::Object {
 public:
  /** Creates a tree entry for object and shapeIndexInObject. */
  ShapeTree_TreeEntry(ShapeTree_Storable* object, int32_t shapeIndexInObject)
      : object(object), shapeIndexInObject(shapeIndexInObject) {}

  ShapeTree_Storable* object;  // final in Java
  int32_t shapeIndexInObject;  // final in Java
};

/** Common functionality of inner nodes and leaf nodes. */
class ShapeTree_TreeNode : public virtual jrt::Object {
 public:
  RegularTileShape* boundingShape = nullptr;
  ShapeTree_InnerNode* parent = nullptr;
};

/** Description of an inner node of the tree, which implements a fork to its two children. */
class ShapeTree_InnerNode : public ShapeTree_TreeNode {
 public:
  /** Creates an inner node with boundingShape and parent. */
  ShapeTree_InnerNode(RegularTileShape* boundingShape, ShapeTree_InnerNode* parent) {
    this->boundingShape = boundingShape;
    this->parent = parent;
  }

  ShapeTree_TreeNode* firstChild = nullptr;
  ShapeTree_TreeNode* secondChild = nullptr;
};

/** Description of a leaf of the Tree, where the geometric information is stored. */
class ShapeTree_Leaf : public ShapeTree_TreeNode {
 public:
  /** Creates a leaf node for object at index with parent and boundingShape. */
  ShapeTree_Leaf(ShapeTree_Storable* object, int32_t index, ShapeTree_InnerNode* parent,
                 RegularTileShape* boundingShape)
      : object(object), shapeIndexInObject(index) {
    this->boundingShape = boundingShape;
    this->parent = parent;
  }

  /** Comparable<Leaf>: by object, then by shape index. */
  virtual int32_t compareTo(ShapeTree_Leaf* other) {
    int32_t result = this->object->compareTo(other->object);
    if (result == 0) {
      result = shapeIndexInObject - other->shapeIndexInObject;
    }
    return result;
  }

  /** Returns the number of nodes between this leaf and the root of the tree. */
  virtual int32_t distanceToRoot();

  /** Actual object stored. */
  ShapeTree_Storable* object;

  /** Index of the shape in the object. */
  int32_t shapeIndexInObject;
};

/**
 * Abstract binary search tree for shapes in the plane. The shapes are stored in the leafs of the
 * tree. Objects to be stored in the tree must implement the interface ShapeTree_Storable.
 *
 * PORT NOTE: the ReentrantReadWriteLock and readLock()/writeLock() are dropped (single-threaded
 * port); the *Unlocked helper methods are kept so the code mirrors Java.
 */
class ShapeTree : public virtual jrt::Object {
 public:
  /** Inserts all shapes of obj into the tree. */
  virtual void insert(ShapeTree_Storable* obj);

  /** Inserts a leaf into the tree (package-private abstract in Java). */
  virtual void insert(ShapeTree_Leaf* leaf) = 0;

  virtual void removeLeaf(ShapeTree_Leaf* leaf) = 0;

  /** PORT NOTE (stage E): hooks for the owner of the leaves' bounding shapes (a leaf's bounding
   * shape can be an item's tree shape itself: it must stay alive as long as the leaf). */
  virtual void leafInserted(ShapeTree_Leaf* leaf) {}
  virtual void leafRemoved(ShapeTree_Leaf* leaf) {}

  /** Inserts the leaves of this tree into an array. */
  virtual jrt::Array<ShapeTree_Leaf*>* toArray();

  /** Removes all entries of obj in the tree. */
  virtual void remove(jrt::Array<ShapeTree_Leaf*>* entries);

  /** Returns the number of entries stored in the tree. */
  virtual int32_t size();

  /** Outputs some statistic information about the tree. */
  virtual void statistics(const std::string& message);

  /** PORT NOTE (stage E, board teardown): frees every node of the tree (inner nodes with their
   * bounding shapes, leaves) and leaves it empty. The stored objects and their tree shapes are
   * not touched; the caller drops the objects' leaf arrays (Item::clearSearchTreeEntries). */
  void disposeNodes();

 protected:
  /** Creates a new instance of ShapeTree. */
  explicit ShapeTree(ShapeBoundingDirections* directions)
      : boundingDirections(directions), root(nullptr), leafCount(0) {}

  /** Insert a shape - creates a new node with a bounding shape. */
  virtual ShapeTree_Leaf* insert(ShapeTree_Storable* object, int32_t index);

  /** Fixed directions for calculating bounding regular tile shapes stored in this tree. */
  ShapeBoundingDirections* boundingDirections;  // final in Java

  /** Root node - initially null. */
  ShapeTree_TreeNode* root;

  /** The number of entries stored in the tree. */
  int32_t leafCount;

 private:
  /** Inserts the leaves of this tree into an array while the read lock is already held. */
  jrt::Array<ShapeTree_Leaf*>* toArrayUnlocked();
};

}  // namespace fr
