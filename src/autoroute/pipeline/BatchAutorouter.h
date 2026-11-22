#pragma once

#include <functional>

#include "autoroute/pipeline/NamedAlgorithm.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Why BatchAutorouter::runBatchLoop() stopped. */
enum class BatchAutorouter_StopReason : int32_t {
  COMPLETED,
  MAX_PASSES,
  STAGNATION,
  NO_IMPROVEMENT,
  TIMED_OUT,
  CANCELLED,
  REPEATED
};

inline std::string BatchAutorouter_StopReason_name(BatchAutorouter_StopReason e) {
  switch (e) {
    case BatchAutorouter_StopReason::COMPLETED: return "COMPLETED";
    case BatchAutorouter_StopReason::MAX_PASSES: return "MAX_PASSES";
    case BatchAutorouter_StopReason::STAGNATION: return "STAGNATION";
    case BatchAutorouter_StopReason::NO_IMPROVEMENT: return "NO_IMPROVEMENT";
    case BatchAutorouter_StopReason::TIMED_OUT: return "TIMED_OUT";
    case BatchAutorouter_StopReason::CANCELLED: return "CANCELLED";
    case BatchAutorouter_StopReason::REPEATED: return "REPEATED";
  }
  return "";
}
inline std::string BatchAutorouter_StopReason_toString(BatchAutorouter_StopReason e) {
  return BatchAutorouter_StopReason_name(e);
}
inline jrt::Array<BatchAutorouter_StopReason>* BatchAutorouter_StopReason_values() {
  auto* a = new (jrt::kOp) jrt::Array<BatchAutorouter_StopReason>(7);
  for (int32_t i = 0; i < 7; ++i) (*a)[i] = static_cast<BatchAutorouter_StopReason>(i);
  return a;
}
inline BatchAutorouter_StopReason BatchAutorouter_StopReason_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 7; ++i) {
    auto e = static_cast<BatchAutorouter_StopReason>(i);
    if (BatchAutorouter_StopReason_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant BatchAutorouter.StopReason." + s);
}

/**
 * The unfractured planes of a net, those of them that a pin of the net reaches, and one
 * representative pin per pin-bearing island of any fractured planes of the net.
 * Java record NetPlanes(List<ConductionArea> all, List<ConductionArea> withPins, List<Pin>
 * fracturedIslandPins).
 */
class BatchAutorouter_NetPlanes : public virtual jrt::Object {
  friend class BatchAutorouter;

 private:
  jrt::List<ConductionArea*>* all_;
  jrt::List<ConductionArea*>* withPins_;
  jrt::List<Pin*>* fracturedIslandPins_;

 public:
  BatchAutorouter_NetPlanes(
      jrt::List<ConductionArea*>* all, jrt::List<ConductionArea*>* withPins, jrt::List<Pin*>* fracturedIslandPins)
      : all_(all), withPins_(withPins), fracturedIslandPins_(fracturedIslandPins) {}
  jrt::List<ConductionArea*>* all() { return all_; }
  jrt::List<ConductionArea*>* withPins() { return withPins_; }
  jrt::List<Pin*>* fracturedIslandPins() { return fracturedIslandPins_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Handles the sequencing of the auto-router passes.
 *
 * PORT NOTES:
 *  - Java package-private members are public (AutorouteBatchLoop, AutoroutePassRunner,
 *    AutorouteConnectionRouter, RoutingFallbackRunner ... access them directly).
 *  - java.time.Instant sessionStartTime -> int64_t epoch milliseconds
 *    (jrt::System::currentTimeMillis()).
 *  - Boolean.getBoolean(...) statics -> accessor functions (read the system property once).
 */
class BatchAutorouter : public NamedAlgorithm {
 public:
  // PORT NOTE: Java = BoardHistory.MAX_HISTORY_SIZE (30). Literal to avoid including BoardHistory.h;
  // the .cpp must static_assert(BOARD_RANK_LIMIT == BoardHistory::MAX_HISTORY_SIZE).
  static constexpr int32_t BOARD_RANK_LIMIT = 30;
  static constexpr int32_t MAXIMUM_TRIES_ON_THE_SAME_BOARD = 3;
  static constexpr int32_t STOP_AT_PASS_MINIMUM = 8;
  static constexpr int32_t STOP_AT_PASS_MODULO = 4;
  static constexpr int32_t STAGNATION_PASS_LIMIT = 10;
  static constexpr int32_t FANOUT_RECOVERY_STAGNATION_PASSES = 3;
  static constexpr int32_t LAST_MILE_INCOMPLETE_LIMIT = 8;
  static constexpr int32_t LAST_MILE_STAGNATION_PASSES = 3;
  static constexpr int32_t LAST_MILE_MAX_ATTEMPTS = 2;
  // Interval for statistics refreshes during a pass.
  static constexpr int32_t PROGRESS_STATISTICS_ITEM_INTERVAL = 10;
  // Minimum score gain (on the 0-1000 normalized scale) that counts as a meaningful improvement.
  static constexpr float STAGNATION_SCORE_THRESHOLD = 0.5F;

 private:
  /** Java: Boolean.getBoolean("freerouting.benchmark.profile"). */
  static bool BENCHMARK_PROFILE_ENABLED();
  /** Java: Boolean.getBoolean("freerouting.benchmark.retain_autoroute_database"). */
  static bool BENCHMARK_RETAIN_AUTOROUTE_DATABASE();

 public:
  bool removeUnconnectedVias;
  bool preferredDirections;
  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts;
  bool retainAutorouteDatabase;
  int32_t startRipupCosts;
  int32_t tracePullTightAccuracy;

 private:
  jrt::List<Item*>* reusableAutorouteItemList;  // Java init: new ArrayList<>() (set in constructor)
  jrt::List<Item*>* reusablePlaneItemList;  // Java init: new ArrayList<>() (set in constructor)
  jrt::List<Item*>* reusableSignalItemList;  // Java init: new ArrayList<>() (set in constructor)
  jrt::Set<Item*>* reusableHandledItems;  // Java init: new TreeSet<>() (set in constructor)
  AutorouteConnectionRouter* connectionRouter;
  AutoroutePassRunner* passRunner;
  AutorouteBatchLoop* batchLoop;

 public:
  /** Which net ripped which during this stage (Java initialiser: new RipupMemory()). */
  RipupMemory* ripupMemory;

 private:
  /** Nets of the priority net classes, in net order; nullptr until first asked. */
  jrt::TreeSet<int32_t>* routedFirstNets = nullptr;

 public:
  /**
   * Nets whose traces and vias netNo may not rip: those the rip-up memory keeps it away from,
   * and, for a net outside the priority net classes, every net of those classes.
   */
  virtual jrt::Set<int32_t>* netsNotToRipFor(int32_t netNo);

  RoutingJob* job = nullptr;
  int32_t totalItemsRouted = 0;
  bool fanoutTimedOut = false;
  jrt::Random* random;
  /** Used to draw the airline of the current routed incomplete. */
  FloatLine* airLine = nullptr;
  /** Initial number of unrouted nets at the start of the routing session. */
  int32_t initialUnroutedCount = 0;
  /** Time when the routing session started (PORT NOTE: epoch millis instead of Instant). */
  int64_t sessionStartTime = 0;
  int64_t lastBoardUpdateTimestamp = 0;
  bool isOptimizerAutorouter = false;
  /** Nets whose open connections this router does not try to route (Java init: Set.of()). */
  jrt::Set<int32_t>* netsLeftUnrouted = new (jrt::kOp) jrt::HashSet<int32_t>();
  int64_t profileItemSelectionNanos = 0;
  int64_t profileIntermediateStatisticsNanos = 0;
  int64_t profileBoardStatisticsNanos = 0;
  int64_t profileIncompleteDrcNanos = 0;
  int64_t profileAutorouteItemNanos = 0;
  int64_t profileMazeSearchNanos = 0;
  int64_t profileOptChangedAreaNanos = 0;
  int64_t profileTailRemovalNanos = 0;
  int32_t profileRouteItemCount = 0;
  int32_t profilePlaneItemCount = 0;
  BoardStatistics* progressStatistics = nullptr;
  int32_t progressItemsSinceStatistics = 0;
  /**
   * Erik Gavriluk (freerouting.net): nets that routed at neck width earlier in the pass loop (or
   * had necked traces when the loop started).
   */
  jrt::Set<int32_t>* neckedRouteNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
  /** Erik Gavriluk (freerouting.net): whether nets in neckedRouteNets route at neck width directly. */
  bool startNecked = false;
  int32_t startNeckedRouted = 0;
  int32_t startNeckedFailed = 0;

 private:
  BallEscapeOrder* ballEscapeOrder = nullptr;
  bool ballEscapeOrderFound = false;

 public:
  virtual BallEscapeOrder* getBallEscapeOrder();

  /** Why the last pass loop stopped. Empty (Java null) until the loop records a reason. */
  std::optional<BatchAutorouter_StopReason> lastStopReason;
  /** Board hash after fanout, where the pass loop started (Java: nullable String). */
  std::optional<std::string> passLoopStartHash;

 private:
  std::function<bool(const std::string&)> repeatsPassLoop;
  std::optional<std::string> strategyName;

 public:
  virtual void setStrategyName(const std::optional<std::string>& strategyName) { this->strategyName = strategyName; }
  virtual std::optional<std::string> getStrategyName() { return this->strategyName; }

  /**
   * Builds an attempt on a private board and settings. removeUnconnectedVias stays false for a
   * continuation that must keep the incumbent's fanout vias.
   */
  static BatchAutorouter* createAttempt(
      RoutingJob* job, RoutingBoard* board, RouterSettings* settings, bool removeUnconnectedVias);
  /** Fanout-only attempts stop when this returns true for the post-fanout board hash. */
  virtual void setRepeatsPassLoop(std::function<bool(const std::string&)> repeatsPassLoop) {
    this->repeatsPassLoop = std::move(repeatsPassLoop);
  }
  /** Java: hash != null && repeatsPassLoop != null && repeatsPassLoop.test(hash). */
  virtual bool repeatsEarlierPassLoop(const std::optional<std::string>& hash);

  /** Creates a BatchAutorouter for the given routing job. */
  explicit BatchAutorouter(RoutingJob* job);
  /** Creates a new BatchAutorouter instance. */
  BatchAutorouter(
      StoppableThread* thread,
      RoutingBoard* board,
      RouterSettings* settings,
      bool removeUnconnectedVias,
      bool withPreferredDirections,
      int32_t startRipupCosts,
      int32_t pullTightAccuracy);

  static bool isBenchmarkProfileEnabled() { return BENCHMARK_PROFILE_ENABLED(); }
  virtual bool isRemoveUnconnectedVias() { return this->removeUnconnectedVias; }
  virtual jrt::Array<AutorouteControl_ExpansionCostFactor*>* getTraceCosts() { return this->traceCosts; }
  virtual bool isRetainAutorouteDatabase() { return this->retainAutorouteDatabase; }
  virtual int32_t getStartRipupCosts() { return this->startRipupCosts; }
  virtual int32_t getTracePullTightAccuracy() { return this->tracePullTightAccuracy; }
  virtual void addProfileMazeSearchNanos(int64_t elapsedNanos) { this->profileMazeSearchNanos += elapsedNanos; }
  virtual void addProfileOptChangedAreaNanos(int64_t elapsedNanos) {
    this->profileOptChangedAreaNanos += elapsedNanos;
  }
  virtual void setAirLine(FloatLine* airLine) { this->airLine = airLine; }
  virtual void resetPassProfile();
  virtual void logBenchmarkProfile(int32_t passNo);

  /**
   * Auto-routes ripup passes until the board is completed or the auto-router is stopped by the
   * user, or if maxPassCount is exceeded. Used in the optimize via batch pass. The open
   * connections of netsLeftUnrouted are not routed.
   */
  static int32_t autoroutePassesForOptimizingItem(
      RoutingJob* job,
      int32_t maxPassCount,
      int32_t ripupCosts,
      int32_t tracePullTightAccuracy,
      bool withPreferredDirections,
      RoutingBoard* updatedRoutingBoard,
      RouterSettings* routerSettings,
      jrt::Set<int32_t>* netsLeftUnrouted);

  static jrt::Array<Point*>* getImpactedPoints(Item* item);

  /**
   * Strict-DRC enforcement: if any trace/via inserted by the connection that just routed (item id
   * above maxItemIdBefore) carries a clearance violation, rip the whole set of new items and report
   * the connection FAILED.
   */
  static AutorouteAttemptResult* enforceStrictDrc(RoutingBoard* board, int32_t routeNetNo, int32_t maxItemIdBefore);

  virtual bool isFanoutTimedOut();
  virtual bool shouldFireBoardUpdate();
  virtual jrt::List<Item*>* getAutorouteItems(RoutingBoard* board);

  /**
   * Returns true if the connected set reaches an unfractured plane of the net and contains every
   * unfractured plane of that net that a pin reaches, as well as every pin-bearing island of any
   * fractured planes of that net.
   */
  static bool coversAllPlanesOfNet(
      jrt::Set<Item*>* connectedSet,
      jrt::Map<int32_t, BatchAutorouter_NetPlanes*>* planesByNet,
      RoutingBoard* board,
      int32_t netNo);

  /** Returns true if the given item belongs to at least one net with containsPlane = true. */
  virtual bool isPlaneItem(Item* item, RoutingBoard* board);
  /** Auto-routes one ripup pass of all items of the board. Returns false if already complete. */
  virtual bool autoroutePass(int32_t passNo);
  virtual void resetAntiOscillationState();

  std::string getId() override;
  std::string getName() override;
  std::string getVersion() override;
  std::string getDescription() override;
  NamedAlgorithmType getType() override;

  /** Returns the initial number of unrouted nets at the start of the routing session. */
  virtual int32_t getInitialUnroutedCount();
  /** Returns the time when the routing session started (epoch millis, see PORT NOTE). */
  virtual int64_t getSessionStartTime();
  /**
   * Autoroutes ripup passes until the board is completed or the autorouter is stopped by the
   * user. Returns true if the board is completed.
   */
  virtual bool runBatchLoop();
  /** Human-readable summary of all unrouted connections on the current board, grouped by net. */
  virtual std::string buildUnroutedConnectionsReport();
  virtual void removeTails(Item_StopConnectionOption stopConnectionOption);
  virtual AutorouteAttemptResult* autorouteItem(
      Item* item,
      int32_t routeNetNo,
      jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* ripupCosts,
      int32_t ripupPassNo);
  /** Returns the airline of the current autorouted connection, or nullptr. */
  virtual FloatLine* getAirLine();
  virtual int32_t calculateIncompleteCount(RoutingBoard* board);
  /**
   * Counts incomplete connections once. When incompleteNets is non-null, also records every net
   * that still has an incomplete connection from that same scan.
   */
  virtual int32_t calculateIncompleteCount(RoutingBoard* board, jrt::Set<int32_t>* incompleteNets);
  virtual bool isStartNecked();
  virtual void setStartNecked(bool startNecked);
  virtual jrt::Set<int32_t>* getNeckedRouteNets();
  virtual int32_t getStartNeckedRouted();
  virtual int32_t getStartNeckedFailed();
};

}  // namespace fr
