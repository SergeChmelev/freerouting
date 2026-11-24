// Port of app.freerouting.board.actions.ItemSearchTreesInfo (+ nested SearchTreeInfo).
// Fully implemented in this header (tiny type).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

#include <vector>

namespace fr {

/**
 * PORT NOTE (stage E): counts the references of the arrays of precalculated tree shapes to
 * their shapes (BasicBoard); a shape no array refers to any more is freed later, at a point
 * where nothing outside the board can still hold it (BasicBoard::releaseGraveyard).
 */
class TreeShapeKeeper {
 public:
  virtual void retainTreeShapes(jrt::Array<TileShape*>* shapes) = 0;
  virtual void releaseTreeShapes(jrt::Array<TileShape*>* shapes) = 0;

 protected:
  ~TreeShapeKeeper() = default;
};

/** (private static nested class in Java) */
class ItemSearchTreesInfo_SearchTreeInfo : public virtual jrt::Object {
 public:
  ShapeTree* tree;  // final
  jrt::Array<ShapeTree_Leaf*>* entryArr;
  jrt::Array<TileShape*>* precalculatedTreeShapes;

  explicit ItemSearchTreesInfo_SearchTreeInfo(ShapeTree* tree) : tree(tree) {
    entryArr = nullptr;
    precalculatedTreeShapes = nullptr;
  }
};

/**
 * Stores information about the search trees of the board items, which is precalculated for
 * performance reasons.
 */
class ItemSearchTreesInfo : public virtual jrt::Object {
 private:
  jrt::Collection<ItemSearchTreesInfo_SearchTreeInfo*>* treeList;  // final

 public:
  /** Creates a new instance of ItemSearchTreeEntries. */
  ItemSearchTreesInfo() { this->treeList = new (jrt::Like{this}) jrt::LinkedList<ItemSearchTreesInfo_SearchTreeInfo*>(); }
  /** PORT NOTE (stage E): with a keeper the precalculated tree shapes are owned (traces, whose
   * tree shapes are computed for the item alone). */
  explicit ItemSearchTreesInfo(TreeShapeKeeper* keeper) : ItemSearchTreesInfo() { this->keeper = keeper; }

  // PORT NOTE (stage C): owns its tree list, the per-tree records and the leaf arrays (the
  // leaves belong to the trees, the precalculated shapes are not owned). Destroyed through
  // jrt::dispose by Item::clearSearchTreeEntries once the item has left the trees.
  ~ItemSearchTreesInfo() override {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      jrt::dispose(currentTreeInfo->entryArr);
      release(currentTreeInfo->precalculatedTreeShapes);
      jrt::dispose(currentTreeInfo);
    }
    jrt::dispose(this->treeList);
  }

  /**
   * Returns the tree entries for the tree with identification number treeNo, or null, if for this
   * tree no entries of this item are inserted.
   */
  jrt::Array<ShapeTree_Leaf*>* getTreeEntries(ShapeTree* tree) {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      if (currentTreeInfo->tree == tree) {
        return currentTreeInfo->entryArr;
      }
    }
    return nullptr;
  }

  /** Sets the item tree entries for the tree with identification number treeNo. */
  void setTreeEntries(jrt::Array<ShapeTree_Leaf*>* treeEntries, ShapeTree* tree) {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      if (currentTreeInfo->tree == tree) {
        if (currentTreeInfo->entryArr != treeEntries) {
          jrt::dispose(currentTreeInfo->entryArr);  // replaced: owned (stage C)
        }
        currentTreeInfo->entryArr = treeEntries;
        return;
      }
    }
    auto* newTreeInfo = new (jrt::Like{this}) ItemSearchTreesInfo_SearchTreeInfo(tree);
    newTreeInfo->entryArr = treeEntries;
    this->treeList->add(newTreeInfo);
  }

  /**
   * Returns the precalculated tiles shapes for the tree with identification number treeNo, or
   * null, if the tile shapes of this tree are not yet precalculated.
   */
  jrt::Array<TileShape*>* getPrecalculatedTreeShapes(ShapeTree* tree) {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      if (currentTreeInfo->tree == tree) {
        return currentTreeInfo->precalculatedTreeShapes;
      }
    }
    return nullptr;
  }

  /** Sets the item tree entries for the tree with identification number treeNo. */
  void setPrecalculatedTreeShapes(jrt::Array<TileShape*>* tileShapes, ShapeTree* tree) {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      if (currentTreeInfo->tree == tree) {
        if (currentTreeInfo->precalculatedTreeShapes == tileShapes) return;
        retain(tileShapes);  // first: shapes reused from the old array stay counted
        release(currentTreeInfo->precalculatedTreeShapes);
        currentTreeInfo->precalculatedTreeShapes = tileShapes;
        return;
      }
    }
    auto* newTreeInfo = new (jrt::Like{this}) ItemSearchTreesInfo_SearchTreeInfo(tree);
    retain(tileShapes);
    newTreeInfo->precalculatedTreeShapes = tileShapes;
    this->treeList->add(newTreeInfo);
  }

  /** Clears the stored information about the precalculated tree shapes for all search trees. */
  void clearPrecalculatedTreeShapes() {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      // PORT NOTE (stage E): the item's leaves may still use some of these shapes (a leaf's
      // bounding shape can be the tree shape itself): the board counts those leaves too.
      release(currentTreeInfo->precalculatedTreeShapes);
      currentTreeInfo->precalculatedTreeShapes = nullptr;
    }
  }

 private:
  TreeShapeKeeper* keeper = nullptr;
  void retain(jrt::Array<TileShape*>* shapes) {
    if (keeper != nullptr && shapes != nullptr) keeper->retainTreeShapes(shapes);
  }
  void release(jrt::Array<TileShape*>* shapes) {
    if (keeper != nullptr && shapes != nullptr) keeper->releaseTreeShapes(shapes);
  }
};

}  // namespace fr
