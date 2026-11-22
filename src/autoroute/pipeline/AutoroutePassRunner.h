#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Per-net timing/outcome counters of one pass (Java: private static final nested class). */
class AutoroutePassRunner_NetPassProfile : public virtual jrt::Object {
 public:
  int32_t netNo;
  std::string netName;
  int64_t totalNanos = 0;
  int32_t attempts = 0;
  int32_t routed = 0;
  int32_t failed = 0;
  int32_t skipped = 0;
  int32_t ripped = 0;

  AutoroutePassRunner_NetPassProfile(int32_t netNo, const std::string& netName)
      : netNo(netNo), netName(netName) {}
};

/** Timing/outcome of one slow item in a pass (Java: private static final nested class). */
class AutoroutePassRunner_ItemPassProfile : public virtual jrt::Object {
 public:
  int64_t elapsedNanos;
  int32_t netNo;
  std::string netName;
  std::string itemType;
  int32_t itemId;
  AutorouteAttemptState state;
  int32_t ripped;

  AutoroutePassRunner_ItemPassProfile(
      int64_t elapsedNanos,
      int32_t netNo,
      const std::string& netName,
      const std::string& itemType,
      int32_t itemId,
      AutorouteAttemptState state,
      int32_t ripped)
      : elapsedNanos(elapsedNanos),
        netNo(netNo),
        netName(netName),
        itemType(itemType),
        itemId(itemId),
        state(state),
        ripped(ripped) {}
};

/**
 * Java record FailedConnectionPair(int netNo, Set<Integer> sideA, Set<Integer> sideB). Used as a
 * HashMap key: equals/hashCode follow the Java record contract (Set equality / Set hashCode of the
 * components), implemented in the .cpp.
 */
class AutoroutePassRunner_FailedConnectionPair : public virtual jrt::Object {
  friend class AutoroutePassRunner;

 private:
  int32_t netNo_;
  jrt::Set<int32_t>* sideA_;
  jrt::Set<int32_t>* sideB_;

 public:
  AutoroutePassRunner_FailedConnectionPair(int32_t netNo, jrt::Set<int32_t>* sideA, jrt::Set<int32_t>* sideB)
      : netNo_(netNo), sideA_(sideA), sideB_(sideB) {}
  int32_t netNo() { return netNo_; }
  jrt::Set<int32_t>* sideA() { return sideA_; }
  jrt::Set<int32_t>* sideB() { return sideB_; }

  /** Returns nullptr when the item has no connected or no unconnected side on the net. */
  static AutoroutePassRunner_FailedConnectionPair* of(Item* item, int32_t netNo);

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Executes one autoroute pass on the calling thread. */
class AutoroutePassRunner : public virtual jrt::Object {
 private:
  BatchAutorouter* router;
  /** Java initialiser: Collections.emptySet() (set in the constructor). */
  jrt::Set<int32_t>* previousIncompleteNets;
  int32_t previousIncompleteCount = -1;
  int32_t stagnationCount = 0;

 public:
  explicit AutoroutePassRunner(BatchAutorouter* router);

  virtual bool runSingleThread(int32_t passNo);

  static jrt::Map<int32_t, IntBox*>* buildPaddedNetBounds(jrt::List<Item*>* autorouteItemList);
  static void invalidateZeroRipupPairs(
      jrt::Map<AutoroutePassRunner_FailedConnectionPair*, IntBox*>* zeroRipupFailedPairs,
      jrt::Collection<Item*>* rippedItems);
  static int32_t componentPinCount(RoutingBoard* board, Item* item);
  static jrt::Comparator<Item*> deterministicItemComparator(RoutingBoard* board);

 private:
  void reorderSingleThreadItems(jrt::List<Item*>* autorouteItemList, int32_t passNo);
  /**
   * Moves the items of the priority net classes ahead of the other signal items, after the plane
   * items, keeping the order within each group.
   */
  void priorityItemsFirst(jrt::List<Item*>* autorouteItemList);
  bool isRoutedFirst(Item* item);
  /**
   * Moves the items of nets that the rip-up memory keeps away from other nets to the end of the
   * pass, keeping the order otherwise.
   */
  void keptAwayNetsLast(jrt::List<Item*>* signalItems);
  /** Feeds the rip-up memory: which nets this attempt moved aside. */
  void rememberRipups(int32_t netNo, jrt::TreeSet<Item*>* rippedItemList, int32_t passNo);

 public:
  /**
   * Evaluates the outcome for nets that were kept away from other nets during this pass. Only
   * after all connections for the net have succeeded in the pass is the relation resolved.
   */
  virtual void resolveKeptAwayOutcomes(
      jrt::Set<int32_t>* keptAwayNetsAttempted,
      jrt::Set<int32_t>* incompleteNets,
      RouterCounters* routerCounters,
      int32_t passNo);

 private:
  /**
   * Removes the unfixed traces and vias of netNo that lie across the open connections of
   * failedNetNo. Returns the number of items removed.
   */
  int32_t ripInCorridors(int32_t netNo, int32_t failedNetNo);
  void logDebug(int32_t passNo, const std::string& message);
  std::string netName(int32_t netNo);
  std::string netNames(jrt::Set<int32_t>* netNumbers);

 public:
  virtual void resetAntiOscillationState();

 private:
  void logIncompleteDetails(int32_t passNo, int32_t itemsToGoCount, RouterCounters* counters);
  void logRippedItems(
      Item* currentItem,
      int32_t netIndex,
      jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* rippedItemCosts);
  void logTraceRouteComparison(
      Item* currentItem,
      int32_t netIndex,
      AutorouteAttemptResult* result,
      jrt::TreeSet<Item*>* rippedItemList,
      int32_t netItemsBefore);
  void logNet94Items();
  void updateProgress(
      RouterCounters* counters,
      int32_t itemsToGoCount,
      int32_t rippedItemCount,
      int32_t notRouted,
      int32_t routed,
      int32_t skipped);
  void dropDesignBlockedItems(jrt::List<Item*>* items);

 public:
  static jrt::Set<int32_t>* ruleBlockedNets(RoutingBoard* board);
  static bool designBlocked(RoutingBoard* board, Item* item, int32_t netNumber);
  static jrt::List<Item*>* failedNetRipItems(RoutingBoard* board, Item* failedItem, int32_t netNumber);

 private:
  void logTailRemoval(int32_t passNo, int32_t incompleteCount, bool before);
  void logPassBreakdownProfile(
      int32_t passNo,
      jrt::Map<int32_t, AutoroutePassRunner_NetPassProfile*>* netProfiles,
      jrt::List<AutoroutePassRunner_ItemPassProfile*>* slowItemProfiles);
};

}  // namespace fr
