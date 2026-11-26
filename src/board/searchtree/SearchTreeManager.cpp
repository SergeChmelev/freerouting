// Port of app.freerouting.board.searchtree.SearchTreeManager
#include "board/searchtree/SearchTreeManager.h"

#include <typeinfo>

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/searchtree/ShapeSearchTree45Degree.h"
#include "board/searchtree/ShapeSearchTree90Degree.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/ShapeTree.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/FortyfiveDegreeBoundingDirections.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Polyline.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"

namespace fr {

SearchTreeManager::SearchTreeManager(BasicBoard* board) {
  this->board = board;
  compensatedSearchTrees = new (jrt::kBoard) jrt::LinkedList<ShapeSearchTree*>();
  defaultTree = new (jrt::kBoard) ShapeSearchTree(FortyfiveDegreeBoundingDirections::INSTANCE(), board, 0);
  compensatedSearchTrees->add(defaultTree);
  this->clearanceCompensationUsed = false;
}

void SearchTreeManager::insert(Item* item) {
  board->noteChange(item);
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    currentTree->insert(static_cast<ShapeTree_Storable*>(item));
  }
  item->setOnTheBoard(true);
}

void SearchTreeManager::remove(Item* item) {
  if (!item->isOnTheBoard()) {
    return;
  }
  board->noteChange(item);
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    jrt::Array<ShapeTree_Leaf*>* currentTreeEntries = item->getSearchTreeEntries(currentTree);
    {
      if (currentTreeEntries != nullptr) {
        currentTree->remove(currentTreeEntries);
      }
    }
  }
  item->clearSearchTreeEntries();
  item->setOnTheBoard(false);
}

ShapeSearchTree* SearchTreeManager::getUncompensatedTree() {
  for (ShapeSearchTree* tree : *compensatedSearchTrees) {
    if (tree->compensatedClearanceClassNo == 0 && typeid(*tree) == typeid(ShapeSearchTree)) {
      return tree;
    }
  }
  ShapeSearchTree* tree =
      new (jrt::kBoard) ShapeSearchTree(FortyfiveDegreeBoundingDirections::INSTANCE(), board, 0);
  auto itScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (Item* item = jrt::fast_cast<Item>(board->itemList->readObject(it)); item != nullptr;
       item = jrt::fast_cast<Item>(board->itemList->readObject(it))) {
    tree->insert(static_cast<ShapeTree_Storable*>(item));
  }
  compensatedSearchTrees->add(tree);
  return tree;
}

bool SearchTreeManager::validateEntries(Item* item) {
  bool result = true;
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    if (!currentTree->validateEntries(item)) {
      result = false;
    }
  }
  return result;
}

void SearchTreeManager::setClearanceCompensationUsed(bool value) {
  if (this->clearanceCompensationUsed == value) {
    return;
  }

  this->clearanceCompensationUsed = value;
  removeAllBoardItems();
  this->compensatedSearchTrees->clear();
  int32_t compensatedClearanceClassNo;
  if (value) {
    compensatedClearanceClassNo = 1;
  } else {
    compensatedClearanceClassNo = 0;
  }
  defaultTree = new (jrt::kBoard) ShapeSearchTree(FortyfiveDegreeBoundingDirections::INSTANCE(), this->board,
                                    compensatedClearanceClassNo);
  this->compensatedSearchTrees->add(defaultTree);
  insertAllBoardItems();
}

void SearchTreeManager::clearanceValueChanged() {
  // delete all trees except the default tree
  ShapeSearchTree* defTree = defaultTree;
  this->compensatedSearchTrees->removeIf([defTree](ShapeSearchTree* t) {
    return t->compensatedClearanceClassNo != defTree->compensatedClearanceClassNo;
  });
  if (this->clearanceCompensationUsed) {
    removeAllBoardItems();
    insertAllBoardItems();
  }
}

void SearchTreeManager::clearanceClassRemoved(int32_t no) {
  jrt::Iterator<ShapeSearchTree*>* it = this->compensatedSearchTrees->iterator();
  if (no == defaultTree->compensatedClearanceClassNo) {
    FRLogger::warn("SearchtreeManager.clearance_class_removed: unable to remove default tree");
    return;
  }
  while (it->hasNext()) {
    ShapeSearchTree* currentTree = it->next();
    if (currentTree->compensatedClearanceClassNo == no) {
      it->remove();
    }
  }
}

ShapeSearchTree* SearchTreeManager::findTree(int32_t clearanceClassIndex,
                                             const std::type_info& type) {
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    if (currentTree->compensatedClearanceClassNo == clearanceClassIndex &&
        typeid(*currentTree) == type) {
      return currentTree;
    }
  }
  for (ShapeSearchTree* currentTree : lendingTrees_) {
    if (currentTree->compensatedClearanceClassNo == clearanceClassIndex &&
        typeid(*currentTree) == type) {
      return currentTree;
    }
  }
  return nullptr;
}

ShapeSearchTree* SearchTreeManager::newAutorouteTreeObject(int32_t clearanceClassIndex) {
  if (this->board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
    // fast algorithm with 90 degree restriction
    return new (jrt::kBoard) ShapeSearchTree90Degree(this->board, clearanceClassIndex);
  }
  if (this->board->rules->getTraceAngleRestriction() == AngleRestriction::FORTYFIVE_DEGREE) {
    // fast algorithm with 45 degree restriction
    return new (jrt::kBoard) ShapeSearchTree45Degree(this->board, clearanceClassIndex);
  }
  // slow algorithm or no angle restriction
  return new (jrt::kBoard) ShapeSearchTree(FortyfiveDegreeBoundingDirections::INSTANCE(),
                                           this->board, clearanceClassIndex);
}

std::vector<int32_t> SearchTreeManager::autorouteTreeClasses() {
  std::vector<int32_t> result;
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    if (currentTree != defaultTree) {
      result.push_back(currentTree->compensatedClearanceClassNo);
    }
  }
  return result;
}

void SearchTreeManager::prepareLendingTree(int32_t clearanceClassIndex) {
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    if (currentTree->compensatedClearanceClassNo == clearanceClassIndex) {
      return;  // the tree itself lends (getAutorouteTree would return it)
    }
  }
  for (ShapeSearchTree* currentTree : lendingTrees_) {
    if (currentTree->compensatedClearanceClassNo == clearanceClassIndex) {
      return;
    }
  }
  ShapeSearchTree* tree = newAutorouteTreeObject(clearanceClassIndex);
  lendingTrees_.push_back(tree);
  auto itScope = this->board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(this->board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (currentItem->isOnTheBoard()) {
      currentItem->treeShapeCount(tree);  // computes and keeps the item's shapes for the tree
    }
  }
}

ShapeSearchTree* SearchTreeManager::getAutorouteTree(int32_t clearanceClassIndex) {
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    if (currentTree->compensatedClearanceClassNo == clearanceClassIndex) {
      return currentTree;
    }
  }

  // Create a new ShapeSearchTree object based on the board's settings
  ShapeSearchTree* currentAutorouteTree = newAutorouteTreeObject(clearanceClassIndex);
  this->compensatedSearchTrees->add(currentAutorouteTree);
  // C++ only: a candidate copy takes the shapes of its unchanged items from the baseline's tree.
  this->board->borrowTreeShapesFor(currentAutorouteTree);

  auto itScope = this->board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(this->board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    currentAutorouteTree->insert(static_cast<ShapeTree_Storable*>(currentItem));
  }
  return currentAutorouteTree;
}

void SearchTreeManager::disposeTrees() {
  for (ShapeSearchTree* tree : *compensatedSearchTrees) {
    tree->disposeNodes();
    jrt::dispose(tree);
  }
  for (ShapeSearchTree* tree : lendingTrees_) {
    jrt::dispose(tree);  // no nodes
  }
  lendingTrees_.clear();
  compensatedSearchTrees->clear();
  jrt::dispose(compensatedSearchTrees);
  compensatedSearchTrees = nullptr;
  defaultTree = nullptr;
}

void SearchTreeManager::resetCompensatedTrees() {
  ShapeSearchTree* defTree = defaultTree;
  this->compensatedSearchTrees->removeIf([defTree](ShapeSearchTree* t) { return t != defTree; });
}

void SearchTreeManager::reinsertTreeItems() {
  board->noteUnknownChange();
  removeAllBoardItems();
  // Removing clears the tree entries but NOT the precalculated tree shapes cached on each
  // item; without dropping those, re-insertion silently reuses the stale shapes and rule
  // changes (e.g. the drill-hole clearance override) never reach the trees.
  auto itScope = this->board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(this->board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    currentItem->clearDerivedData();
  }
  insertAllBoardItems();
}

void SearchTreeManager::removeAllBoardItems() {
  if (this->board == nullptr) {
    FRLogger::warn("SearchtreeManager.remove_all_board_items: app.freerouting.board is null");
    return;
  }
  auto itScope = this->board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(this->board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    this->remove(currentItem);
  }
}

void SearchTreeManager::insertAllBoardItems() {
  if (this->board == nullptr) {
    FRLogger::warn("SearchtreeManager.insert_all_board_items: app.freerouting.board is null");
    return;
  }
  auto itScope = this->board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(this->board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    currentItem->clearDerivedData();
    this->insert(currentItem);
  }
}

void SearchTreeManager::mergeEntriesInFront(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                            Polyline* joinedPolyline, int32_t fromEntryNo,
                                            int32_t toEntryNo) {
  board->noteChange(fromTrace);
  board->noteChange(toTrace);
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    currentTree->mergeEntriesInFront(fromTrace, toTrace, joinedPolyline, fromEntryNo, toEntryNo);
  }
}

void SearchTreeManager::mergeEntriesAtEnd(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                          Polyline* joinedPolyline, int32_t fromEntryNo,
                                          int32_t toEntryNo) {
  board->noteChange(fromTrace);
  board->noteChange(toTrace);
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    currentTree->mergeEntriesAtEnd(fromTrace, toTrace, joinedPolyline, fromEntryNo, toEntryNo);
  }
}

void SearchTreeManager::changeEntries(PolylineTrace* obj, Polyline* newPolyline,
                                      int32_t keepAtStartCount, int32_t keepAtEndCount) {
  // the trace as it is, and as it will be
  board->noteChange(obj);
  board->noteChange(newPolyline->boundingBox()->offset(obj->getHalfWidth() + 1), obj->firstLayer(),
                    obj->lastLayer());
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    currentTree->changeEntries(obj, newPolyline, keepAtStartCount, keepAtEndCount);
  }
}

void SearchTreeManager::reuseEntriesAfterCutout(PolylineTrace* fromTrace,
                                                PolylineTrace* startPiece,
                                                PolylineTrace* endPiece) {
  board->noteChange(fromTrace);
  for (ShapeSearchTree* currentTree : *compensatedSearchTrees) {
    currentTree->reuseEntriesAfterCutout(fromTrace, startPiece, endPiece);
  }
}

}  // namespace fr
