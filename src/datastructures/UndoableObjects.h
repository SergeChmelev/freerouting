// Port of app.freerouting.datastructures.UndoableObjects
#pragma once

#include <vector>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Condition for an Object to be stored in an UndoableObjects database. An object of class
 * UndoableObjects.Storable must not contain any references. (Java: extends Comparable<Object>.)
 */
class UndoableObjects_Storable : public virtual jrt::Object {
 public:
  /** Comparable<Object>.compareTo: defines the iteration order of the database. */
  virtual int32_t compareTo(jrt::Object* other) = 0;

  /**
   * Creates an exact copy of this object. Public overwriting of the protected clone method in
   * java.lang.Object. Implementations may use a covariant return type (e.g. Item* clone()).
   */
  virtual jrt::Object* clone() = 0;
};

/**
 * Stores information for correct restoring or cancelling an object in an undo or redo operation.
 * level is the level in the Undo stack, where this object was inserted.
 */
class UndoableObjects_UndoableObjectNode : public virtual jrt::Object {
 public:
  /** Creates a new instance of UndoableObjectNode. */
  UndoableObjects_UndoableObjectNode(UndoableObjects_Storable* object, int32_t level)
      : object(object), level(level), undoObject(nullptr), redoObject(nullptr) {}

  UndoableObjects_Storable* object;  // the object in the node (final in Java)
  int32_t level;  // the level in the Undo stack, where this node was inserted
  UndoableObjects_UndoableObjectNode* undoObject;  // the object to restore in an undo or null.
  UndoableObjects_UndoableObjectNode* redoObject;  // the object to restore in a redo or null.
  uint32_t mark = 0;  // PORT NOTE (stage C): reachability epoch of UndoableObjects::sweepNodes
};

/**
 * PORT NOTE: not a Java type. Reproduces the weakly consistent values() iterator of Java's
 * ConcurrentSkipListMap over UndoableObjects::objects: it always holds the next node (key and
 * value read when the previous element was returned, like the Java iterator's prefetch) and finds
 * the following one as the next live key greater than the last returned key. So objects deleted or
 * inserted behind the current position during an iteration are seen exactly as in Java (a jrt
 * values() snapshot would not). remove() deletes the last returned key from the map.
 */
class UndoableObjects_NodeIterator
    : public virtual jrt::Iterator<UndoableObjects_UndoableObjectNode*> {
 public:
  // PORT NOTE (stage C): created only as a scoped value (UndoableObjects::readScope); it counts
  // itself in `liveIterators` while it exists, because it may hold a node that has been removed
  // from the map meanwhile (the prefetched next node), so no node is freed during an iteration.
  UndoableObjects_NodeIterator(
      jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>*
          map,
      int32_t* liveIterators);
  ~UndoableObjects_NodeIterator() override { --*liveIterators_; }
  UndoableObjects_NodeIterator(const UndoableObjects_NodeIterator&) = delete;
  UndoableObjects_NodeIterator& operator=(const UndoableObjects_NodeIterator&) = delete;

  bool hasNext() override { return nextEntry_ != nullptr; }
  UndoableObjects_UndoableObjectNode* next() override;
  void remove() override;
  /** Starts again at the first object (Java: a new startReadObject() into the same variable). */
  void restart();

 private:
  jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>* map_;
  jrt::MapEntry<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>* nextEntry_ =
      nullptr;
  UndoableObjects_UndoableObjectNode* nextValue_ = nullptr;
  jrt::MapEntry<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>* lastReturned_ =
      nullptr;
  // PORT NOTE: position of nextEntry_ in the map, valid while the map erase count equals
  // nextStamp_; lets next() step in O(1) instead of a higherEntry() search (same result).
  typename jrt::ConcurrentSkipListMap<UndoableObjects_Storable*,
                                      UndoableObjects_UndoableObjectNode*>::MapIt nextIt_{};
  uint64_t nextStamp_ = 0;
  int32_t* liveIterators_;
};

/**
 * Database of objects, for which Undo and Redo operations are made possible. The algorithm works
 * only for objects containing no references.
 */
class UndoableObjects : public virtual jrt::Object {
 public:
  // PORT NOTE (board deepCopy hook): fresh copy with identical map order, node levels, stack level
  // and undo/redo stacks; every stored object (also those only in undo/redo history) is replaced by
  // mapObject(original). mapObject must be memoizing (same original -> same copy).
  UndoableObjects* copyMapped(std::function<UndoableObjects_Storable*(UndoableObjects_Storable*)> mapObject);
  /** Returns the current change stamp; equal stamps mean the readable objects are unchanged. */
  int64_t getVersion() { return version; }

  /** Returns the content change stamp. */
  int64_t getContentVersion() { return contentVersion; }

  /** PORT NOTE: not in Java. The current undo level (snapshots taken and not undone or popped). */
  int32_t getStackLevel() { return stackLevel; }

  /** Records an in-place change made without saveForUndo. */
  void noteContentChange() { ++contentVersion; }

  /** Creates a new instance of UndoableObjectsList. */
  UndoableObjects();

  /**
   * Returns an iterator for sequential reading of the object list (an UndoableObjects_NodeIterator
   * with Java ConcurrentSkipListMap semantics).
   * PORT NOTE (stage C): Java startReadObject(); returned as a scoped value instead of a heap
   * object (`auto scope = list->readScope(); auto* it = &scope;`), so that the list knows when
   * no iteration is live and removed nodes can be freed.
   */
  UndoableObjects_NodeIterator readScope() { return UndoableObjects_NodeIterator(objects, &liveIterators_); }

  /**
   * Reads the next object in this list. Returns null, if the list is exhausted. it must be created
   * by readScope.
   */
  UndoableObjects_Storable* readObject(jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it);

  /**
   * PORT NOTE: the object readObject would return for the entry whose key compares equal to
   * `key` (O(log n); e.g. an ItemIdProbe for an item id), or nullptr if there is none or it is
   * not readable at this level.
   */
  UndoableObjects_Storable* findVisible(UndoableObjects_Storable* key);

  /** Adds object to the UndoableObjectsList. */
  void insert(UndoableObjects_Storable* object);

  /**
   * Removes object from the top level of the UndoableObjectsList. Returns false, if object was not
   * found in the list.
   */
  bool delete_(UndoableObjects_Storable* object);

  /** Makes the current state of the list restorable by Undo. */
  void generateSnapshot();

  /**
   * Restores the situation before the last snapshot. Outputs the cancelled and the restored objects
   * (if != null). Returns false, if no more undo is possible.
   */
  bool undo(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
            jrt::Collection<UndoableObjects_Storable*>* restoredObjects);

  /**
   * Restores the situation before the last undo. Outputs the cancelled and the restored objects (if
   * != null). Returns false, if no more redo is possible.
   */
  bool redo(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
            jrt::Collection<UndoableObjects_Storable*>* restoredObjects);

  /**
   * Returns the changes since the last snapshot. before receives the pre-snapshot state of every
   * object that existed before it and was changed or deleted since; after receives the current
   * state of every object inserted or changed since. Returns false when there is no snapshot.
   */
  bool changesSinceSnapshot(jrt::Collection<UndoableObjects_Storable*>* before,
                            jrt::Collection<UndoableObjects_Storable*>* after);

  /**
   * Removes the top snapshot from the undo stack, so that its situation cannot be restored anymore.
   * Returns false, if no more snapshot could be popped.
   */
  bool popSnapshot();

  /**
   * PORT NOTE (stage E, board teardown): ends the life of the list. Calls `disposeObject` once
   * for every distinct object of the list (current, deleted on an undo level, or only reachable
   * through undo/redo links: all of them belong to the list's owner), then frees the nodes, the
   * map with its entries and the delete lists. The list must not be used afterwards.
   */
  void disposeAll(const std::function<void(UndoableObjects_Storable*)>& disposeObject);

  /**
   * Must be called before object will be modified after a snapshot for the first time, if it may
   * have existed before that snapshot.
   */
  void saveForUndo(UndoableObjects_Storable* object);

 private:
  /** Must be called, if objects are changed for the first time after undo. */
  void disableRedo();

  /**
   * The entries of this map are of type UndoableObjectNode, the keys of type Storable.
   * PORT NOTE: Java declares ConcurrentMap and creates a ConcurrentSkipListMap (ordered by
   * Storable.compareTo); jrt::ConcurrentSkipListMap is jrt::TreeMap.
   */
  jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>*
      objects;

  /**
   * Lists of deleted objects on each undo level, which were already existing before the previous
   * snapshot. (Java: Vector<Collection<UndoableObjectNode>>.)
   */
  jrt::ArrayList<jrt::Collection<UndoableObjects_UndoableObjectNode*>*>* deletedObjectsStack;

  /** The current undo level. */
  int32_t stackLevel = 0;

  bool redoPossible = false;

  /** Changes whenever the set of objects a reader sees can change. */
  int64_t version = 0;

  /** Changes whenever an object's content may change. */
  int64_t contentVersion = 0;

  // PORT NOTE (stage C): node ownership. Every node allocated on the heap (not in a board arena)
  // is registered; when no iteration is live, sweepNodes() frees the nodes that can no longer be
  // reached from the map, the delete lists or the undo/redo links of reachable nodes.
  UndoableObjects_UndoableObjectNode* newNode(UndoableObjects_Storable* object, int32_t level);
  void maybeSweepNodes() {
    if (liveIterators_ != 0) return;
    objects->freeRetired();
    if (ownedNodes_.size() >= sweepAt_) sweepNodes();
  }
  void sweepNodes();

 public:
  /** PORT NOTE (stage E): called by sweepNodes for every object that only swept nodes held
   * (no live node, no map key): removed for good, no undo can bring it back. The owner (the
   * board) frees it later, when nothing outside can still use it. */
  void setDeadObjectListener(std::function<void(UndoableObjects_Storable*)> listener) {
    deadObjectListener_ = std::move(listener);
  }

 private:
  // PORT NOTE (performance): undo, disableRedo, popSnapshot and changesSinceSnapshot only act on
  // entries whose node has a level >= 1 or a redo link, but Java walks the whole map for them
  // (once or twice per trial move of the optimizer and StepSlider). The keys of such entries are
  // indexed here: levelKeys_[l] holds the keys whose node got level l (l >= 1), redoKeys_ the keys
  // whose node had a redo link, each noted when the node was put into the map or its level
  // changed. Entries may be stale (dropped when read); the index is rebuilt from the map by every
  // sweepNodes, before removed objects can be freed. The indexed keys are visited in map order,
  // so every walk does exactly what the full walk did.
  std::vector<std::vector<UndoableObjects_Storable*>> levelKeys_;
  std::vector<UndoableObjects_Storable*> redoKeys_;
  /** Indexes `key` after its map entry got `node` or node's level changed. */
  void noteEntry(UndoableObjects_Storable* key, UndoableObjects_UndoableObjectNode* node);
  void rebuildIndex();
  /**
   * The keys (in map order, each once) of the entries whose node level is in
   * [fromLevel, toLevel] (fromLevel >= 1), and with withRedo also those whose node has a redo link.
   */
  std::vector<UndoableObjects_Storable*> indexedKeys(int32_t fromLevel, int32_t toLevel, bool withRedo);

  std::function<void(UndoableObjects_Storable*)> deadObjectListener_;
  std::vector<UndoableObjects_UndoableObjectNode*> ownedNodes_;
  static std::size_t sweepMinimum();  // 4096, or env FR_UNDO_SWEEP_MIN (stress tests)
  std::size_t sweepAt_ = sweepMinimum();
  uint32_t markEpoch_ = 0;
  int32_t liveIterators_ = 0;
};

}  // namespace fr
