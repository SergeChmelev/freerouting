#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Owns item-list lookups, filtered item views and insertion/removal bookkeeping of a BasicBoard.
 * (Java package-private methods are public in C++.)
 */
class BoardItemRepository final : public virtual jrt::Object {
 private:
  BasicBoard* board;
  BoardOutline* outline = nullptr;
  int64_t outlineStamp = -1;
  jrt::Map<int32_t, Item*>* idMap = nullptr;  // a HashMap
  int64_t idMapStamp = -1;

 public:
  explicit BoardItemRepository(BasicBoard* board) : board(board) {}

  /** Returns the outline item, if one is present. */
  BoardOutline* getOutline();

 private:
  BoardOutline* findOutline();

 public:
  /** Returns the item with the requested ID, if one is present. */
  Item* getItem(int32_t id);

  /** Counts the vias on the board without collecting them. */
  int32_t getViaCount();

  /** Counts the pins on the board without collecting them. */
  int32_t getPinCount();

  /** Returns a snapshot collection of all items currently in the item list. */
  jrt::Collection<Item*>* getItems();

  jrt::Collection<ConductionArea*>* getConductionAreas();

  jrt::Collection<Pin*>* getPins();

  jrt::Collection<Pin*>* getSmdPins();

  jrt::Collection<Via*>* getVias();

  jrt::Collection<Trace*>* getTraces();

  double cumulativeTraceLength();

  /** Inserts an item and performs the existing tree, observer, and revision updates. */
  void insertItem(Item* item);

  /** Removes an item and performs the existing tree, observer, and revision updates. */
  void removeItem(Item* item);

  /** A move detaches component items without granting ordinary deletion permission. */
  void detachItemForMove(Item* item);

 private:
  void removeItem(Item* item, bool forMove);

 public:
  /** Removes all removable items and reports whether every requested item was removed. */
  bool removeItems(jrt::Collection<Item*>* items);

  /** Deletes all traces and vias from the undoable item list. */
  void deleteAllTracksAndVias();

 private:
  static bool isItemActivityDebugCandidate(Item* item);

  static std::string firstNetOrNone(Item* item);

  static std::string describeBounds(IntBox* bounds);

  static void logTraceRemoval(Trace* trace);
};

}  // namespace fr
