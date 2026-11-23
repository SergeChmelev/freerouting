// Port of app.freerouting.datastructures.UndoableObjects
#include "datastructures/UndoableObjects.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "board/model/items/Item.h"
#include "logger/FRLogger.h"

namespace fr {

using UONode = UndoableObjects_UndoableObjectNode;

// ---------------------------------------------------------------------------------------------
// UndoableObjects_NodeIterator: weakly consistent ConcurrentSkipListMap values() iterator
// ---------------------------------------------------------------------------------------------

UndoableObjects_NodeIterator::UndoableObjects_NodeIterator(
    jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UONode*>* map, int32_t* liveIterators)
    : map_(map), liveIterators_(liveIterators) {
  ++*liveIterators_;
  restart();
}

void UndoableObjects_NodeIterator::restart() {
  lastReturned_ = nullptr;
  nextIt_ = map_->mapBegin();
  nextStamp_ = map_->eraseCount();
  nextEntry_ = nextIt_ != map_->mapEnd() ? nextIt_->second : nullptr;
  nextValue_ = nextEntry_ != nullptr ? nextEntry_->value : nullptr;
}

UONode* UndoableObjects_NodeIterator::next() {
  if (nextEntry_ == nullptr) {
    throw jrt::NoSuchElementException();
  }
  UONode* v = nextValue_;
  lastReturned_ = nextEntry_;
  // advance (Java prefetches the following node and its value right away): the entry after
  // lastReturned_ in key order, i.e. higherEntry(lastReturned_->key).
  if (map_->eraseCount() == nextStamp_) {
    ++nextIt_;  // nextIt_ still points at lastReturned_
  } else {
    nextIt_ = map_->mapUpperBound(lastReturned_->key);
    nextStamp_ = map_->eraseCount();
  }
  nextEntry_ = nextIt_ != map_->mapEnd() ? nextIt_->second : nullptr;
  nextValue_ = nextEntry_ != nullptr ? nextEntry_->value : nullptr;
  return v;
}

void UndoableObjects_NodeIterator::remove() {
  if (lastReturned_ == nullptr) {
    throw jrt::IllegalStateException();
  }
  map_->remove(lastReturned_->key);
  lastReturned_ = nullptr;
}

// ---------------------------------------------------------------------------------------------
// UndoableObjects
// ---------------------------------------------------------------------------------------------

UndoableObjects* UndoableObjects::copyMapped(
    std::function<UndoableObjects_Storable*(UndoableObjects_Storable*)> mapObject) {
  auto* result = new (jrt::kBoard) UndoableObjects();
  result->stackLevel = this->stackLevel;
  result->redoPossible = this->redoPossible;
  result->version = this->version;
  result->contentVersion = this->contentVersion;

  // Node copies, memoized by node identity so that shared nodes (undo/redo chains, delete
  // lists) stay shared in the copy. Links are filled in iteratively (no deep recursion).
  auto* nodeCopies = new (jrt::kOp) jrt::IdentityHashMap<UONode*, UONode*>();
  std::vector<UONode*> pending;
  auto getCopy = [&](UONode* original) -> UONode* {
    if (original == nullptr) {
      return nullptr;
    }
    UONode* existing = nodeCopies->get(original);
    if (existing != nullptr) {
      return existing;
    }
    auto* copy = result->newNode(mapObject(original->object), original->level);
    nodeCopies->put(original, copy);
    pending.push_back(original);
    return copy;
  };

  for (auto* entry : *this->objects) {
    UONode* nodeCopy = getCopy(entry->getValue());
    result->objects->put(mapObject(entry->getKey()), nodeCopy);
  }
  for (auto* deleteList : *this->deletedObjectsStack) {
    jrt::Collection<UONode*>* listCopy = new (jrt::kOp) jrt::LinkedList<UONode*>();
    for (auto it = deleteList->iterator(); it->hasNext();) {
      listCopy->add(getCopy(it->next()));
    }
    result->deletedObjectsStack->add(listCopy);
  }
  while (!pending.empty()) {
    UONode* original = pending.back();
    pending.pop_back();
    UONode* copy = nodeCopies->get(original);
    copy->undoObject = getCopy(original->undoObject);
    copy->redoObject = getCopy(original->redoObject);
  }
  return result;
}

UndoableObjects::UndoableObjects() {
  stackLevel = 0;
  objects = new (jrt::kBoard) jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UONode*>();
  deletedObjectsStack = new (jrt::kBoard) jrt::ArrayList<jrt::Collection<UONode*>*>();
}

UONode* UndoableObjects::newNode(UndoableObjects_Storable* object, int32_t level) {
  auto* node = new (jrt::kBoardData) UONode(object, level);
  if (jrt::is_heap_block(node)) {
    ownedNodes_.push_back(node);  // board-arena nodes are released with the arena
  }
  return node;
}

std::size_t UndoableObjects::sweepMinimum() {
  static const std::size_t v = [] {
    const char* e = std::getenv("FR_UNDO_SWEEP_MIN");
    return e != nullptr ? static_cast<std::size_t>(std::max(1L, std::atol(e))) : std::size_t{4096};
  }();
  return v;
}

void UndoableObjects::sweepNodes() {
  // Mark every node reachable from the map and the delete lists (following undo/redo links).
  uint32_t epoch = ++markEpoch_;
  if (epoch == 0) {
    epoch = markEpoch_ = 1;  // wrapped: stale marks of 0 never equal a live epoch
    for (UONode* n : ownedNodes_) n->mark = 0;
  }
  std::vector<UONode*> stack;
  auto push = [&](UONode* n) {
    if (n != nullptr && n->mark != epoch) {
      n->mark = epoch;
      stack.push_back(n);
    }
  };
  for (auto* entry : *this->objects) push(entry->getValue());
  for (auto* deleteList : *this->deletedObjectsStack) {
    for (UONode* n : *deleteList) push(n);
  }
  while (!stack.empty()) {
    UONode* n = stack.back();
    stack.pop_back();
    push(n->undoObject);
    push(n->redoObject);
  }
  // Free the others: nothing can reach them any more (no iteration is live).
  std::size_t kept = 0;
  for (UONode* n : ownedNodes_) {
    if (n->mark == epoch) {
      ownedNodes_[kept++] = n;
    } else {
      jrt::dispose(n);
    }
  }
  // Null the tail first: a shrunk std::vector keeps the old pointers in its capacity.
  std::fill(ownedNodes_.begin() + static_cast<std::ptrdiff_t>(kept), ownedNodes_.end(), nullptr);
  ownedNodes_.resize(kept);
  sweepAt_ = std::max<std::size_t>(sweepMinimum(), 2 * kept);
}

void UndoableObjects::disposeAll(const std::function<void(UndoableObjects_Storable*)>& disposeObject) {
  // The objects of every node the list can still reach (the map, the delete lists, undo/redo
  // links). Objects only held by unreachable nodes that were not swept yet are not owned any
  // more (Java: garbage) and are left alone; their nodes are freed below like all others.
  uint32_t epoch = ++markEpoch_;
  if (epoch == 0) {
    epoch = markEpoch_ = 1;
    for (UONode* n : ownedNodes_) n->mark = 0;
  }
  std::vector<UONode*> nodes;
  std::vector<UONode*> stack;
  auto push = [&](UONode* n) {
    if (n != nullptr && n->mark != epoch) {
      n->mark = epoch;
      stack.push_back(n);
      nodes.push_back(n);
    }
  };
  for (auto* entry : *this->objects) push(entry->getValue());
  for (auto* deleteList : *this->deletedObjectsStack) {
    for (UONode* n : *deleteList) push(n);
  }
  while (!stack.empty()) {
    UONode* n = stack.back();
    stack.pop_back();
    push(n->undoObject);
    push(n->redoObject);
  }
  std::unordered_set<UndoableObjects_Storable*> seen;
  std::vector<UndoableObjects_Storable*> objectsToDispose;
  for (UONode* n : nodes) {
    if (n->object != nullptr && seen.insert(n->object).second) objectsToDispose.push_back(n->object);
  }
  for (UndoableObjects_Storable* object : objectsToDispose) disposeObject(object);
  jrt::disposeMapAndEntries(this->objects);
  this->objects = nullptr;
  for (auto* deleteList : *this->deletedObjectsStack) jrt::dispose(deleteList);
  this->deletedObjectsStack->clear();
  jrt::dispose(this->deletedObjectsStack);
  this->deletedObjectsStack = nullptr;
  // Board-arena nodes are not registered (released with the arena).
  for (UONode* n : ownedNodes_) jrt::dispose(n);
  std::fill(ownedNodes_.begin(), ownedNodes_.end(), nullptr);
  ownedNodes_.clear();
  ownedNodes_.shrink_to_fit();
}

UndoableObjects_Storable* UndoableObjects::readObject(jrt::Iterator<UONode*>* it) {
  while (it->hasNext()) {
    UONode* currentNode = it->next();
    // skip objects getting alive only by redo
    if (currentNode != nullptr && currentNode->level <= this->stackLevel) {
      return currentNode->object;
    }
  }
  return nullptr;
}

void UndoableObjects::insert(UndoableObjects_Storable* object) {
  disableRedo();
  ++version;
  ++contentVersion;
  maybeSweepNodes();
  auto* currentUndoableObject = newNode(object, stackLevel);
  objects->put(object, currentUndoableObject);
}

bool UndoableObjects::delete_(UndoableObjects_Storable* object) {
  maybeSweepNodes();
  disableRedo();
  jrt::Collection<UONode*>* currentDeleteList;
  if (deletedObjectsStack->isEmpty()) {
    // stackLevel 0
    currentDeleteList = nullptr;
  } else {
    currentDeleteList = deletedObjectsStack->getLast();
  }
  // search object in the list
  UONode* objectNode = objects->get(object);
  if (objectNode == nullptr) {
    return false;
  }

  if (auto* item = jrt::fast_cast<Item>(object)) {
    // PORT NOTE: guarded so that the message is only built when granular tracing is active
    // (FRLogger.trace has no other effect in the port).
    if (FRLogger::isGranularTraceActive()) {
      std::string itemNetNames = item->getAllNetNames();
      FRLogger::trace("UndoableObjects.delete", "delete",
                      "Deleting item with " + itemNetNames + ": " + "itemType=" +
                          jrt::className(item) + ", item=" + item->toString(),
                      itemNetNames, nullptr);
    }
  }
  // object can be cloned from the object pointed by objectNode.object (see Java comment)

  if (currentDeleteList != nullptr) {
    if (objectNode->level < this->stackLevel) {
      // add currentObject to the current delete list to make Undo possible.
      currentDeleteList->add(objectNode);
    } else if (objectNode->undoObject != nullptr) {
      // add currentObject.undoObject to the current delete list to make Undo possible.
      currentDeleteList->add(objectNode->undoObject);
    }
  }
  objects->remove(object);
  ++version;
  ++contentVersion;
  return true;
}

void UndoableObjects::generateSnapshot() {
  maybeSweepNodes();
  disableRedo();
  jrt::Collection<UONode*>* currentDeletedObjectsList = new (jrt::kOp) jrt::LinkedList<UONode*>();
  deletedObjectsStack->add(currentDeletedObjectsList);
  ++stackLevel;
  ++version;
}

bool UndoableObjects::undo(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
                           jrt::Collection<UndoableObjects_Storable*>* restoredObjects) {
  if (stackLevel == 0) {
    return false;  // no more undo possible
  }
  for (auto scope = readScope(); scope.hasNext();) {
    auto* it = &scope;
    UONode* currentNode = it->next();
    if (currentNode->level == stackLevel) {
      if (currentNode->undoObject != nullptr) {
        // replace the current object by its previous state.
        currentNode->undoObject->redoObject = currentNode;
        objects->put(currentNode->object, currentNode->undoObject);
        if (restoredObjects != nullptr) {
          restoredObjects->add(currentNode->undoObject->object);
        }
      }
      if (cancelledObjects != nullptr) {
        cancelledObjects->add(currentNode->object);
      }
    }
  }
  // restore the deleted objects
  jrt::Collection<UONode*>* currentDeleteList = deletedObjectsStack->get(stackLevel - 1);
  for (auto it = currentDeleteList->iterator(); it->hasNext();) {
    UONode* currentDeletedNode = it->next();
    this->objects->put(currentDeletedNode->object, currentDeletedNode);
    if (restoredObjects != nullptr) {
      restoredObjects->add(currentDeletedNode->object);
    }
  }
  --this->stackLevel;
  ++version;
  ++contentVersion;
  redoPossible = true;
  return true;
}

bool UndoableObjects::redo(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
                           jrt::Collection<UndoableObjects_Storable*>* restoredObjects) {
  if (this->stackLevel >= deletedObjectsStack->size()) {
    return false;  // already at the top level
  }
  ++this->stackLevel;
  ++version;
  ++contentVersion;
  for (auto scope = readScope(); scope.hasNext();) {
    auto* it = &scope;
    UONode* currentNode = it->next();
    if (currentNode->redoObject != nullptr && currentNode->redoObject->level == this->stackLevel) {
      // Object was created on a lower level and changed on the current level,
      // replace the lower level object by the object on the current layer.
      objects->put(currentNode->object, currentNode->redoObject);
      if (cancelledObjects != nullptr) {
        cancelledObjects->add(currentNode->object);
      }
      if (restoredObjects != nullptr) {
        restoredObjects->add(currentNode->redoObject->object);
        // else the redoObject was deleted on the redo level
      }
    } else if (currentNode->level == this->stackLevel) {
      // Object was created on the current level, allow it to be restored.
      if (restoredObjects != nullptr) {
        restoredObjects->add(currentNode->object);
      }
    }
  }
  // Delete the objects, which were deleted on the current level, again.
  jrt::Collection<UONode*>* currentDeleteList = deletedObjectsStack->get(stackLevel - 1);
  for (auto it = currentDeleteList->iterator(); it->hasNext();) {
    UONode* currentDeletedNode = it->next();
    while (currentDeletedNode->redoObject != nullptr &&
           currentDeletedNode->redoObject->level <= this->stackLevel) {
      currentDeletedNode = currentDeletedNode->redoObject;
    }
    if (!this->objects->removeOpt(currentDeletedNode->object).has_value()) {
      FRLogger::warn("previous deleted object not found");
    }
    if (restoredObjects == nullptr || !restoredObjects->remove(currentDeletedNode->object)) {
      // the object needs only be cancelled if it is already in the board
      if (cancelledObjects != nullptr) {
        cancelledObjects->add(currentDeletedNode->object);
      }
    }
  }
  return true;
}

bool UndoableObjects::changesSinceSnapshot(jrt::Collection<UndoableObjects_Storable*>* before,
                                           jrt::Collection<UndoableObjects_Storable*>* after) {
  if (stackLevel == 0) {
    return false;
  }
  for (auto scope = readScope(); scope.hasNext();) {
    auto* it = &scope;
    UONode* node = it->next();
    if (node == nullptr || node->level != stackLevel) {
      continue;
    }
    after->add(node->object);
    if (node->undoObject != nullptr && node->undoObject->level < stackLevel) {
      before->add(node->undoObject->object);
    }
  }
  jrt::Collection<UONode*>* deletedList = deletedObjectsStack->get(stackLevel - 1);
  for (auto it = deletedList->iterator(); it->hasNext();) {
    UONode* deleted = it->next();
    before->add(deleted->object);
  }
  return true;
}

bool UndoableObjects::popSnapshot() {
  disableRedo();
  if (stackLevel == 0) {
    return false;
  }
  for (auto scope = readScope(); scope.hasNext();) {
    auto* it = &scope;
    UONode* currentNode = it->next();
    if (currentNode->level == stackLevel - 1) {
      if (currentNode->redoObject != nullptr && currentNode->redoObject->level == stackLevel) {
        currentNode->redoObject->undoObject = currentNode->undoObject;
        if (currentNode->undoObject != nullptr) {
          currentNode->undoObject->redoObject = currentNode->redoObject;
        }
      }
    } else if (currentNode->level >= stackLevel) {
      --currentNode->level;
    }
  }
  int32_t deletedObjectsStackSize = deletedObjectsStack->size();
  if (deletedObjectsStackSize >= 2) {
    // join the top delete list with the delete list of the second top level
    jrt::Collection<UONode*>* fromDeleteList = deletedObjectsStack->get(deletedObjectsStackSize - 1);
    jrt::Collection<UONode*>* toDeleteList = deletedObjectsStack->get(deletedObjectsStackSize - 2);
    for (auto it = fromDeleteList->iterator(); it->hasNext();) {
      UONode* currentDeletedNode = it->next();
      if (currentDeletedNode->level < this->stackLevel - 1) {
        toDeleteList->add(currentDeletedNode);
      } else if (currentDeletedNode->undoObject != nullptr) {
        toDeleteList->add(currentDeletedNode->undoObject);
      }
    }
  }
  deletedObjectsStack->removeAt(deletedObjectsStackSize - 1);
  --stackLevel;
  ++version;
  return true;
}

void UndoableObjects::saveForUndo(UndoableObjects_Storable* object) {
  ++contentVersion;
  disableRedo();
  // search object in the map
  UONode* currentNode = objects->get(object);
  if (currentNode == nullptr) {
    FRLogger::warn("UndoableObjects.save_for_undo: object node not found");
    return;
  }
  if (currentNode->level < this->stackLevel) {
    auto* oldNode = newNode(jrt::fast_cast<UndoableObjects_Storable>(object->clone()), currentNode->level);
    oldNode->undoObject = currentNode->undoObject;
    oldNode->redoObject = currentNode;
    currentNode->undoObject = oldNode;
    currentNode->level = this->stackLevel;
  }
}

void UndoableObjects::disableRedo() {
  if (!redoPossible) {
    return;
  }
  redoPossible = false;
  // shorten the size of the deletedObjectsStack to this.stackLevel
  deletedObjectsStack->removeRange(this->stackLevel, deletedObjectsStack->size());
  auto scope = readScope();
  jrt::Iterator<UONode*>* it = &scope;
  while (it->hasNext()) {
    UONode* currentNode = it->next();
    if (currentNode->level > this->stackLevel) {
      it->remove();
    } else if (currentNode->level == this->stackLevel) {
      currentNode->redoObject = nullptr;
    }
  }
}

}  // namespace fr
