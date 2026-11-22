// Port of app.freerouting.board.actions.ItemSearchTreesInfo (+ nested SearchTreeInfo).
// Fully implemented in this header (tiny type).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

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
  ItemSearchTreesInfo() { this->treeList = new (jrt::kOp) jrt::LinkedList<ItemSearchTreesInfo_SearchTreeInfo*>(); }

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
        currentTreeInfo->entryArr = treeEntries;
        return;
      }
    }
    auto* newTreeInfo = new (jrt::kOp) ItemSearchTreesInfo_SearchTreeInfo(tree);
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
        currentTreeInfo->precalculatedTreeShapes = tileShapes;
        return;
      }
    }
    auto* newTreeInfo = new (jrt::kOp) ItemSearchTreesInfo_SearchTreeInfo(tree);
    newTreeInfo->precalculatedTreeShapes = tileShapes;
    this->treeList->add(newTreeInfo);
  }

  /** Clears the stored information about the precalculated tree shapes for all search trees. */
  void clearPrecalculatedTreeShapes() {
    for (ItemSearchTreesInfo_SearchTreeInfo* currentTreeInfo : *this->treeList) {
      currentTreeInfo->precalculatedTreeShapes = nullptr;
    }
  }
};

}  // namespace fr
