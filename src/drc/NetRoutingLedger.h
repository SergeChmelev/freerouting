#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The counts of a built ledger, for the ledger of a copy of its board. (Java record) */
class NetRoutingLedger_Counts : public virtual jrt::Object {
 public:
  NetRoutingLedger_Counts(jrt::Array<int32_t>* incompleteByNet, int32_t incompleteTotal,
                          int32_t maximumConnections)
      : incompleteByNet_(incompleteByNet),
        incompleteTotal_(incompleteTotal),
        maximumConnections_(maximumConnections) {}

  jrt::Array<int32_t>* incompleteByNet() { return incompleteByNet_; }
  int32_t incompleteTotal() { return incompleteTotal_; }
  int32_t maximumConnections() { return maximumConnections_; }

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<NetRoutingLedger_Counts>(o);
    if (other == nullptr) return false;
    // Java record equality on an array component is reference equality.
    return incompleteByNet_ == other->incompleteByNet_ &&
           incompleteTotal_ == other->incompleteTotal_ &&
           maximumConnections_ == other->maximumConnections_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(incompleteByNet_));
    h = jrt::hashCombine(h, incompleteTotal_);
    h = jrt::hashCombine(h, maximumConnections_);
    return h;
  }
  std::string toString() override {
    return "Counts[incompleteByNet=" + jrt::str(incompleteByNet_) +
           ", incompleteTotal=" + jrt::str(incompleteTotal_) +
           ", maximumConnections=" + jrt::str(maximumConnections_) + "]";
  }

 private:
  jrt::Array<int32_t>* incompleteByNet_;
  int32_t incompleteTotal_;
  int32_t maximumConnections_;
};

/**
 * Incremental incomplete-connection counts. A full DesignRulesChecker::calculateAllIncompletes()
 * scan builds the ledger once. Later inserts and removals recompute only the touched nets, using
 * the same NetIncompletes count the full scan uses.
 * PORT NOTE: `synchronized` dropped (single-threaded port).
 */
class NetRoutingLedger final : public virtual jrt::Object {
 private:
  BasicBoard* board;
  // conductionAreas and itemNets are initialised in the constructor (.cpp): the concrete jrt
  // collections need ConductionArea / Item complete.
  jrt::List<ConductionArea*>* conductionAreas;
  bool built = false;
  jrt::Array<jrt::List<Item*>*>* itemsByNet = nullptr;
  jrt::Array<int32_t>* incompleteByNet = new (jrt::kOp) jrt::Array<int32_t>(0);
  int32_t incompleteTotal = 0;
  /** PORT NOTE: field renamed (trailing _) - clashes with method maximumConnections(). */
  int32_t maximumConnections_ = 0;
  jrt::Set<int32_t>* dirtyNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
  jrt::Map<Item*, jrt::Array<int32_t>*>* itemNets;
  void clearItemNets();

  /** PORT NOTE: not in Java. The counts kept at snapshots (noteSnapshot), lowest level first. */
  static constexpr int32_t kSavedSnapshots = 4;
  struct SavedCounts {
    int32_t level = 0;
    int64_t netAssignmentChanges = 0;
    jrt::Array<int32_t>* incompleteByNet = nullptr;  // owned, reused
    int32_t incompleteTotal = 0;
    int32_t maximumConnections = 0;
  };
  SavedCounts saved_[kSavedSnapshots];
  int32_t savedCount_ = 0;

 public:
  explicit NetRoutingLedger(BasicBoard* board);

  /** Stage E: frees the ledger's collections (its board is dropped); the ledger is unusable. */
  void disposeOwned();

  /** Drops the cache. The next read rebuilds it from the current item list. */
  void invalidate();

  /** Records an insertion. Ignored until the ledger has been built. */
  void noteInserted(Item* item);

  /** Records a removal. Ignored until the ledger has been built. */
  void noteRemoved(Item* item);

  /** Returns the total incomplete-connection count, recomputing dirty nets first. */
  int32_t incompleteCount();

  /** Returns the stable maximum connection count from pins and conduction areas. */
  int32_t maximumConnections();

  /** Returns the incomplete-connection count of one net, recomputing dirty nets first. */
  int32_t incompleteCount(int32_t netNumber);

  /** Returns the current counts, or nullptr if the ledger is not built. */
  NetRoutingLedger_Counts* counts();

  /**
   * Builds this ledger for a copy of the board whose ledger gave counts, taking them instead of
   * counting every net again. Does nothing if counts is null or the net count differs.
   */
  void seed(NetRoutingLedger_Counts* counts);

  /** Returns every net that still has at least one incomplete connection. */
  jrt::Set<int32_t>* incompleteNetNumbers();

  /**
   * PORT NOTE: not in Java. An undo puts the items of the board back as they were when the
   * snapshot it undoes was taken, so the counts of that moment are the counts after the undo:
   * the ledger keeps them (for the last few snapshots taken with every count up to date) and
   * takes them back instead of counting the touched nets again. Counts depend only on the items
   * of each net (the number of airlines joins the connected sets, whatever the item order); an
   * in-place net change (BasicBoard::noteNetAssignmentChanged), which no undo reverts, voids them.
   * noteSnapshot: after a snapshot, with the new undo level; noteUndone: after an undo and its
   * side effects, with the undo level it left; dropSnapshotsAbove: after a popSnapshot or a redo.
   */
  void noteSnapshot(int32_t level);
  void noteUndone(int32_t level);
  void dropSnapshotsAbove(int32_t level);

 private:
  void ensureBuilt();

  /** Fills the item lists from the board, as the first step of a build. Returns the net count. */
  int32_t buildItemLists();

  void flushDirty();

  int32_t countNet(int32_t netNumber);

  int32_t countMaximumConnections();

  void addItem(Item* item);

  void removeItem(Item* item);

  void addItemToLists(Item* item);

  void markNets(Item* item);

  static jrt::Array<jrt::List<Item*>*>* emptyLists() {
    return new (jrt::kOp) jrt::Array<jrt::List<Item*>*>(0);
  }
};

}  // namespace fr
