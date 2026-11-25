// Port of app.freerouting.datastructures.ShapeTree
#pragma once

#include <cstdint>

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

/**
 * PORT NOTE: not in Java. The coordinates of a query octagon in ShapeTree_TreeNode::oct order
 * (valid when the query shape is an IntOctagon), for ShapeTree_TreeNode::intersects.
 */
struct ShapeTree_OctagonQuery {
  int32_t q[8] = {};
  bool valid = false;
  explicit ShapeTree_OctagonQuery(RegularTileShape* shape) { set(shape); }
  void set(RegularTileShape* shape);
};

/** Common functionality of inner nodes and leaf nodes. */
class ShapeTree_TreeNode : public virtual jrt::Object {
 public:
  /** Written only through setBoundingShape. */
  RegularTileShape* boundingShape = nullptr;
  ShapeTree_InnerNode* parent = nullptr;

  /**
   * PORT NOTE: not in Java. The coordinates of boundingShape while it is an IntOctagon (octValid),
   * in the order IntOctagon::intersects(IntOctagon*) compares them: leftX, rightX, bottomY, topY,
   * lowerLeftDiagonalX, upperRightDiagonalX, upperLeftDiagonalX, lowerRightDiagonalX. A tree
   * search tests a node against an octagon without loading the node's shape.
   */
  int32_t oct[8] = {};
  bool octValid = false;
  /** PORT NOTE: not in Java. Whether the node is a ShapeTree_Leaf. */
  bool isLeaf = false;

  void setBoundingShape(RegularTileShape* shape);

  /**
   * PORT NOTE: not in Java. boundingShape->intersects(shape), where query holds shape's
   * coordinates if it is an octagon: an octagon against an octagon node compares the coordinates
   * as IntOctagon::intersects(IntOctagon*) does, without loading the node's shape.
   */
  bool intersects(const ShapeTree_OctagonQuery& query, Shape* shape);

 private:
  bool intersectsShape(Shape* shape);  // boundingShape->intersects(shape)
};

/** Description of an inner node of the tree, which implements a fork to its two children. */
class ShapeTree_InnerNode : public ShapeTree_TreeNode {
 public:
  /** Creates an inner node with boundingShape and parent. */
  ShapeTree_InnerNode(RegularTileShape* boundingShape, ShapeTree_InnerNode* parent) {
    this->setBoundingShape(boundingShape);
    this->parent = parent;
  }

  ShapeTree_TreeNode* firstChild = nullptr;
  ShapeTree_TreeNode* secondChild = nullptr;

  /**
   * PORT NOTE: not in Java. Copies of the children's oct / octValid (0: first, 1: second), kept by
   * refreshChildBounds (called by setBoundingShape of a child and where children are linked), so
   * that a search tests a child without loading it.
   */
  int32_t childOct[2][8] = {};
  bool childOctValid[2] = {false, false};
  void refreshChildBounds();

  /** child(i)->intersects(query, shape), from the copy when it can. child(i) is not null. */
  bool childIntersects(int32_t i, const ShapeTree_OctagonQuery& query, Shape* shape);
};

/** Description of a leaf of the Tree, where the geometric information is stored. */
class ShapeTree_Leaf : public ShapeTree_TreeNode {
 public:
  /** Creates a leaf node for object at index with parent and boundingShape. */
  ShapeTree_Leaf(ShapeTree_Storable* object, int32_t index, ShapeTree_InnerNode* parent,
                 RegularTileShape* boundingShape)
      : object(object), shapeIndexInObject(index) {
    this->setBoundingShape(boundingShape);
    this->parent = parent;
    this->isLeaf = true;
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

  /** PORT NOTE: not in Java. No layer known for the leaf (kNoLayer in layer). */
  static constexpr int32_t kNoLayer = INT32_MIN;
  /**
   * PORT NOTE: not in Java. The object's shapeLayer(shapeIndexInObject), kept by ShapeSearchTree
   * (leafInserted, and where a leaf is handed to another trace) so that a search can pass over a
   * leaf of another layer without loading its object; kNoLayer when unknown.
   */
  int32_t layer = kNoLayer;
};

/**
 * PORT NOTE: not in Java. Starts loading a node that a search has just pushed (its first two
 * cache lines: the leaf flag, the children and their coordinates), to overlap the loads.
 */
inline void prefetchTreeNode(const ShapeTree_TreeNode* node) {
#if defined(__GNUC__)
  __builtin_prefetch(node);
  __builtin_prefetch(reinterpret_cast<const char*>(node) + 64);
#else
  (void)node;
#endif
}

/** IntOctagon::intersects(IntOctagon*) on coordinates in ShapeTree_TreeNode::oct order. */
inline bool octagonCoordinatesIntersect(const int32_t* b, const int32_t* q) {
  return (b[0] > q[0] ? b[0] : q[0]) <= (b[1] < q[1] ? b[1] : q[1]) &&
         (b[2] > q[2] ? b[2] : q[2]) <= (b[3] < q[3] ? b[3] : q[3]) &&
         (b[4] > q[4] ? b[4] : q[4]) <= (b[5] < q[5] ? b[5] : q[5]) &&
         (b[6] > q[6] ? b[6] : q[6]) <= (b[7] < q[7] ? b[7] : q[7]);
}

inline bool ShapeTree_TreeNode::intersects(const ShapeTree_OctagonQuery& query, Shape* shape) {
  if (query.valid && octValid) {
    return octagonCoordinatesIntersect(oct, query.q);
  }
  return intersectsShape(shape);
}

inline bool ShapeTree_InnerNode::childIntersects(int32_t i, const ShapeTree_OctagonQuery& query,
                                                 Shape* shape) {
  if (query.valid && childOctValid[i]) {
    return octagonCoordinatesIntersect(childOct[i], query.q);
  }
  return (i == 0 ? firstChild : secondChild)->intersects(query, shape);
}

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
