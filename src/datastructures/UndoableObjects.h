// Port of app.freerouting.datastructures.UndoableObjects
#pragma once

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
  explicit UndoableObjects_NodeIterator(
      jrt::ConcurrentSkipListMap<UndoableObjects_Storable*, UndoableObjects_UndoableObjectNode*>*
          map);

  bool hasNext() override { return nextEntry_ != nullptr; }
  UndoableObjects_UndoableObjectNode* next() override;
  void remove() override;

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

  /** Records an in-place change made without saveForUndo. */
  void noteContentChange() { ++contentVersion; }

  /** Creates a new instance of UndoableObjectsList. */
  UndoableObjects();

  /**
   * Returns an iterator for sequential reading of the object list (an UndoableObjects_NodeIterator
   * with Java ConcurrentSkipListMap semantics).
   */
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* startReadObject();

  /**
   * Reads the next object in this list. Returns null, if the list is exhausted. it must be created
   * by startReadObject.
   */
  UndoableObjects_Storable* readObject(jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it);

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
};

}  // namespace fr
