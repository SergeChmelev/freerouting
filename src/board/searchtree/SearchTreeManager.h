#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

#include <typeinfo>

namespace fr {

/**
 * The SearchTreeManager manages the search trees used in the auto-router. It is responsible for the
 * creation of the search trees and the insertion and removal of items from the trees. It also
 * provides functions to merge and change tree entries for performance reasons.
 */
class SearchTreeManager : public virtual jrt::Object {
 private:
  jrt::Collection<ShapeSearchTree*>* compensatedSearchTrees;  // a LinkedList
  BasicBoard* board;
  ShapeSearchTree* defaultTree;
  bool clearanceCompensationUsed;

 public:
  /** Creates a new instance of SearchTreeManager. */
  explicit SearchTreeManager(BasicBoard* board);

  /** Inserts the tree shapes of item into all active search trees. */
  virtual void insert(Item* item);

  /** Removes all entries of an item from the search trees. */
  virtual void remove(Item* item);

  /** Returns the default tree used in interactive routing. */
  virtual ShapeSearchTree* getDefaultTree() { return defaultTree; }

  /**
   * Returns a managed general tree without clearance compensation (created and filled on first
   * use). PORT NOTE: Java `synchronized` dropped.
   */
  virtual ShapeSearchTree* getUncompensatedTree();

  virtual bool validateEntries(Item* item);

  /**
   * Returns, if clearance compensation is used for the default tree. This is normally the case, if
   * there exist only the clearance classes null and default in the clearance matrix.
   */
  virtual bool isClearanceCompensationUsed() { return this->clearanceCompensationUsed; }

  /** Sets the usage of clearance compensation to true or false. */
  virtual void setClearanceCompensationUsed(bool value);

  /** Actions to be done, when a value in the clearance matrix is changed interactively. */
  virtual void clearanceValueChanged();

  /** Actions to be done, when a new clearance class is removed interactively. */
  virtual void clearanceClassRemoved(int32_t no);

  /**
   * Returns the tree compensated for the clearance class with number clearanceClassIndex.
   * Initializes the tree, if it is not yet allocated.
   */
  virtual ShapeSearchTree* getAutorouteTree(int32_t clearanceClassIndex);

  /**
   * PORT NOTE (C++ only): the existing tree of this clearance class and dynamic type, or nullptr;
   * creates nothing (safe while other threads read the board).
   */
  ShapeSearchTree* findTree(int32_t clearanceClassIndex, const std::type_info& type);

  // The following functions are used internally for performance improvement.

  /** Clears all compensated trees used in the autoroute algorithm apart from the default tree. */
  virtual void resetCompensatedTrees();

  /**
   * Reinsert all items into the search trees (also dropping each item's precalculated tree
   * shapes), so that rule changes reach the trees.
   */
  virtual void reinsertTreeItems();

  /**
   * PORT NOTE (stage E, board teardown): frees every search tree of the board (its nodes and
   * the tree objects). The items' leaf arrays are dropped by the caller.
   */
  void disposeTrees();

 private:
  void removeAllBoardItems();

  void insertAllBoardItems();

 public:
  /** Merges the tree entries from fromTrace in front of toTrace (combine trace). */
  virtual void mergeEntriesInFront(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                   Polyline* joinedPolyline, int32_t fromEntryNo,
                                   int32_t toEntryNo);

  /** Merges the tree entries from fromTrace to the end of toTrace (combine trace). */
  virtual void mergeEntriesAtEnd(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                 Polyline* joinedPolyline, int32_t fromEntryNo,
                                 int32_t toEntryNo);

  /**
   * Changes the tree entries from keepAtStartCount + 1 to newShapeCount - 1 - keepAtEndCount to
   * changedEntries. Special implementation for change_trace for performance reasons.
   */
  virtual void changeEntries(PolylineTrace* obj, Polyline* newPolyline, int32_t keepAtStartCount,
                             int32_t keepAtEndCount);

  /**
   * Transfers tree entries from fromTrace to start and endPiece after a middle piece was cut out.
   * (Java package-private.)
   */
  virtual void reuseEntriesAfterCutout(PolylineTrace* fromTrace, PolylineTrace* startPiece,
                                       PolylineTrace* endPiece);
};

}  // namespace fr
