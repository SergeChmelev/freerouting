#pragma once

#include <memory>

#include "autoroute/pipeline/NamedAlgorithm.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/*
 * SEQUENTIALISATION (PORT NOTE, read before implementing optRoutePass in phase 2)
 * ------------------------------------------------------------------------------
 * Java runs the candidates of a pass on a fixed thread pool (ExecutorService + FutureTask +
 * Semaphore), with at most `maxRunning` candidates running and at most `maxUnread` results
 * queued, but it READS the results strictly in candidate (submission) order and every
 * decision (resultMap, currentPosition, winner selection via improvedOver, consecutive-failure
 * early stop, deadline / stop checks at the top of the loop) is taken while reading. Each
 * candidate works on a private worker board (a deep copy of the unchanged baseline board, kept
 * per thread in a ThreadLocal<WorkerBoardState>), so the result of a candidate does not depend
 * on scheduling.
 *
 * THREADS: with optimizer.max_threads > 1 the port runs the candidates on a thread pool like
 * Java (BatchOptimizer.cpp, CandidatePool): a candidate starts as soon as a thread is free (at
 * most maxRunning running, maxUnread unread), each worker thread has its own WorkerBoardState,
 * and the results are read in candidate order with the same decisions as below, so the result
 * does not depend on the number of threads. With one thread the sequential path below is used.
 *
 * The sequential path (max_threads = 1):
 *  - No pool, futures, semaphore or ThreadLocal. BatchOptimizer owns ONE
 *    BatchOptimizer_WorkerBoardState (field `workerBoardStates`), which plays the role of the
 *    single worker thread's ThreadLocal value; OptimizeCandidateTask receives that pointer and
 *    uses it where Java calls workerBoardStates.get().
 *  - The read loop stays as in Java, but instead of submitting and polling a future, the
 *    candidate at the head of the queue is computed on the calling thread with
 *    `new BatchOptimizer_OptimizeCandidateTask(...)->call()` at the moment Java would read its
 *    future (candidate order == submission order). Every `isDone()` check is therefore true and
 *    the "wait for completions" branch disappears. Do not run candidates ahead of the reader:
 *    candidates Java would have started but never read (early stop) have no observable effect.
 *  - Keep the stop/deadline checks at the top of each loop iteration and inside call().
 *  - ExecutionException handling becomes catch (const jrt::Exception&) around call(), logging
 *    "Error in candidate optimization task" via job->logError and continuing.
 *  - AtomicLong workerCpuNanos / workerAllocBytes -> plain int64_t; volatile currentPosition ->
 *    plain pointer. replayWinner() is unchanged (it already calls call() directly).
 *  - threadPoolSize (settings.getOptimizerMaxThreads()) is still computed for the log line and
 *    for maxRunning/maxUnread, which only bound the queue and no longer influence results.
 */

/** Java private static final class CandidateResult. */
class BatchOptimizer_CandidateResult : public virtual jrt::Object {
 public:
  ItemRouteResult* result;
  RoutingBoard* board;
  FloatPoint* itemPosition;
  int64_t cpuUsedNanos;
  int64_t allocUsedBytes;

  BatchOptimizer_CandidateResult(
      ItemRouteResult* result, RoutingBoard* board, FloatPoint* itemPosition, int64_t cpuUsedNanos,
      int64_t allocUsedBytes)
      : result(result),
        board(board),
        itemPosition(itemPosition),
        cpuUsedNanos(cpuUsedNanos),
        allocUsedBytes(allocUsedBytes) {}
};

/**
 * Reuses one candidate board on each executor thread while the baseline board remains unchanged.
 * A candidate that is not selected is discarded; an improved candidate transfers ownership of the
 * worker board to the result and removes the snapshot without undoing its changes.
 * (Single-threaded port: one instance per BatchOptimizer, see the note above.)
 */
class BatchOptimizer_WorkerBoardState : public virtual jrt::Object {
 private:
  RoutingBoard* baselineBoard = nullptr;
  RoutingBoard* workerBoard = nullptr;

 public:
  BatchOptimizer_WorkerBoardState() = default;
  virtual RoutingBoard* acquire(RoutingBoard* baselineBoard);
  virtual void finish(bool transferOwnership);
  /** True while a worker board copy exists (between acquire and finish). */
  bool hasWorkerBoard() { return workerBoard != nullptr; }
};

/**
 * Java private static final class OptimizeCandidateTask implements Callable<CandidateResult>.
 * PORT NOTE: ThreadLocal<WorkerBoardState> -> BatchOptimizer_WorkerBoardState* (the
 * optimizer's single instance); Long deadlineMs -> std::optional<int64_t>.
 */
class BatchOptimizer_OptimizeCandidateTask : public virtual jrt::Object {
 private:
  RoutingJob* job;
  RoutingBoard* baselineBoard;
  int32_t itemId;
  double baselineTraceLength;
  bool withPreferredDirections;
  bool useIncreasedRipupCosts;
  StoppableThread* thread;
  std::optional<int64_t> deadlineMs;
  BatchOptimizer_WorkerBoardState* workerBoardStates;
  BoardStatistics_OptimizerMetrics* baselineMetrics;
  bool keepBoard;

 public:
  BatchOptimizer_OptimizeCandidateTask(
      RoutingJob* job,
      RoutingBoard* baselineBoard,
      int32_t itemId,
      double baselineTraceLength,
      bool withPreferredDirections,
      bool useIncreasedRipupCosts,
      StoppableThread* thread,
      std::optional<int64_t> deadlineMs,
      BatchOptimizer_WorkerBoardState* workerBoardStates,
      BoardStatistics_OptimizerMetrics* baselineMetrics,
      bool keepBoard)
      : job(job),
        baselineBoard(baselineBoard),
        itemId(itemId),
        baselineTraceLength(baselineTraceLength),
        withPreferredDirections(withPreferredDirections),
        useIncreasedRipupCosts(useIncreasedRipupCosts),
        thread(thread),
        deadlineMs(deadlineMs),
        workerBoardStates(workerBoardStates),
        baselineMetrics(baselineMetrics),
        keepBoard(keepBoard) {}

  /** Java: this(..., workerBoardStates, null, true). */
  BatchOptimizer_OptimizeCandidateTask(
      RoutingJob* job,
      RoutingBoard* baselineBoard,
      int32_t itemId,
      double baselineTraceLength,
      bool withPreferredDirections,
      bool useIncreasedRipupCosts,
      StoppableThread* thread,
      std::optional<int64_t> deadlineMs,
      BatchOptimizer_WorkerBoardState* workerBoardStates)
      : BatchOptimizer_OptimizeCandidateTask(
            job, baselineBoard, itemId, baselineTraceLength, withPreferredDirections,
            useIncreasedRipupCosts, thread, deadlineMs, workerBoardStates, nullptr, true) {}

  virtual BatchOptimizer_CandidateResult* call();

  /** The worker board state of the thread that runs the task (Java: workerBoardStates.get()). */
  void useWorkerBoardState(BatchOptimizer_WorkerBoardState* state) { workerBoardStates = state; }
};

/**
 * The scores and the gain in percent read by the pass loop's stop rule.
 * Java record StopRuleView(float before, float after, double percent) - fully implemented.
 */
class BatchOptimizer_StopRuleView : public virtual jrt::Object {
 private:
  float before_;
  float after_;
  double percent_;

 public:
  BatchOptimizer_StopRuleView(float before, float after, double percent)
      : before_(before), after_(after), percent_(percent) {}
  float before() { return before_; }
  float after() { return after_; }
  double percent() { return percent_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<BatchOptimizer_StopRuleView>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf(before_, other->before_) && jrt::equalsOf(after_, other->after_) &&
           jrt::equalsOf(percent_, other->percent_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(before_));
    h = jrt::hashCombine(h, jrt::hashOf(after_));
    h = jrt::hashCombine(h, jrt::hashOf(percent_));
    return h;
  }
  std::string toString() override {
    return "StopRuleView[before=" + jrt::str(before_) + ", after=" + jrt::str(after_) +
           ", percent=" + jrt::str(percent_) + "]";
  }
};

/**
 * Reads the vias and traces on the board in ascending x order (Java: protected inner class
 * ReadSortedRouteItems, reads the outer `board`).
 */
class BatchOptimizer_ReadSortedRouteItems : public virtual jrt::Object {
 public:
  BatchOptimizer* outer;
  FloatPoint* minItemCoor;
  int32_t minItemLayer;

  explicit BatchOptimizer_ReadSortedRouteItems(BatchOptimizer* outer);
  virtual Item* next();
  virtual FloatPoint* getCurrentPosition();
};

/**
 * Optimizes the routed connections of a board after auto-routing, using a deterministic thread
 * pool pipeline (sequential in the port, see the SEQUENTIALISATION note at the top of this file).
 *
 * PORT NOTES: Java `protected` members are public (package access). Field `isTimedOut` clashes
 * with method isTimedOut() -> field renamed `isTimedOut_`.
 */
class BatchOptimizer : public NamedAlgorithm {
 public:
  /** Java init: new ProgressThrottler(1000) (set in constructor). */
  ProgressThrottler* progressThrottler;
  BatchOptimizer_ReadSortedRouteItems* sortedRouteItems = nullptr;
  bool useIncreasedRipupCosts = false;
  double minCumulativeTraceLength = 0.0;
  RoutingJob* job = nullptr;
  int32_t totalItemsOptimized = 0;
  std::optional<int64_t> deadlineMs;
  bool isTimedOut_ = false;
  RoutingBoard* bestBoard = nullptr;
  // PORT NOTE (stage E): the board this->board had when runBatchLoop started (owned by the job's
  // caller, never freed here). Every other board this->board / bestBoard takes is a copy made by
  // the optimizer and is freed when the optimizer drops it (dropBoard).
  RoutingBoard* inputBoard_ = nullptr;
  float bestScore = 0.0f;
  int32_t bestIncompleteCount = 0;
  int32_t bestClearanceViolationCount = 0;
  jrt::Map<int32_t, ItemRouteResult*>* resultMap;  // Java init: new HashMap<>() (set in constructor)
  /** Java: AtomicLong. */
  int64_t workerCpuNanos = 0;
  /** Java: AtomicLong. */
  int64_t workerAllocBytes = 0;
  /** Java: volatile. */
  FloatPoint* currentPosition = nullptr;

  /** Java nested class: reads the private getItemPosition. */
  friend class BatchOptimizer_OptimizeCandidateTask;

 private:
  /** Java: ThreadLocal.withInitial(WorkerBoardState::new); one instance (set in constructor). */
  BatchOptimizer_WorkerBoardState* workerBoardStates;
  /**
   * PORT NOTE (C++ only): the clearance classes whose autoroute-tree shapes the candidates could
   * not borrow from their baseline (BasicBoard::TreeShapeRequests); optRoutePass prepares lending
   * trees for them on the baseline of the next pass. A std::shared_ptr<BasicBoard::
   * TreeShapeRequests> (the type is not complete here).
   */
  std::shared_ptr<void> treeShapeRequests_;

 public:
  /** Creates a new instance of BatchOptimizer, which is used to optimize the board. */
  explicit BatchOptimizer(RoutingJob* job);

  /** Creates the canonical optimizer for a routing job. */
  static BatchOptimizer* create(RoutingJob* job);
  /** Creates the optimizer used by headless jobs. */
  static BatchOptimizer* createForHeadless(RoutingJob* job);
  /** Creates the optimizer used by GUI jobs. */
  static BatchOptimizer* createForGui(RoutingJob* job);

 private:
  static void normalizeAlgorithm(RoutingJob* job, BatchOptimizer* optimizer);

 public:
  /** Returns true if timed out. */
  virtual bool isTimedOut() { return isTimedOut_; }
  static bool containsOnlyUnfixedTraces(jrt::Collection<Item*>* itemList);
  /**
   * Evaluates whether optimization can be safely bypassed before starting the batch loop.
   * Returns the reason, or empty (Java null) if the guards pass.
   */
  virtual std::optional<std::string> evaluatePreFlightGuards(BoardStatistics* stats);
  /**
   * Checks if all vias on the board are mandatory layer transitions between SMD pins on different
   * layers, with no alternative routing possible. Fixed vias are skipped.
   */
  static bool areAllViasMandatoryLayerTransitions(RoutingBoard* routingBoard);
  /** Optimize the route on the board. */
  virtual void runBatchLoop();
  /** Whether the optimizer ends with the via nudge pass: optimizer.via_nudge_enabled. */
  static bool viaNudgeEnabled(OptimizerSettings* optimizerSettings);

 private:
  /** After the last pass, moves vias where that shortens or straightens the traces. */
  void nudgeVias();

 public:
  /** Whether the optimizer ends with the step slide pass: optimizer.step_slide_enabled. */
  static bool stepSlideEnabled(OptimizerSettings* optimizerSettings);

 private:
  /** Slides the steps of the traces, with shoving, where that improves the board (issue #1161). */
  bool slideSteps();
  /**
   * Restores a working copy of the incumbent snapshot (assigning board = bestBoard would alias
   * and destroy the snapshot).
   */
  void restoreIncumbentBoard();
  /** Frees `old`, a board the optimizer no longer refers to, unless it is the input board or
   * still the job's / incumbent board. */
  void dropBoard(RoutingBoard* old);

 public:
  /**
   * Returns the explicit reason a candidate must be rejected, or empty (Java null) when it is
   * accepted. Completeness and DRC count are vetoes; optimizer score ranks candidates.
   */
  static std::optional<std::string> optimizerCandidateRejectionReason(
      int32_t incumbentIncompleteCount,
      int32_t incumbentClearanceViolationCount,
      float incumbentOptimizerScore,
      BoardStatistics* candidate,
      float candidateOptimizerScore);
  /** What a pass gained, in percent, for the stop rule's improvement threshold. */
  static double scoreImprovementPercent(float before, float after);
  /** Calculates what the stop rule reads of a pass's scores. */
  static BatchOptimizer_StopRuleView* stopRuleScores(float before, float after, bool connectionsOpen);
  /**
   * Checks whether the final board regressed compared to the best accepted incumbent board
   * (FR-077).
   */
  static bool finalBoardRegressed(
      float bestScore,
      int32_t bestIncomplete,
      int32_t bestViolations,
      float finalScore,
      int32_t finalIncomplete,
      int32_t finalViolations);

 private:
  jrt::List<int32_t>* prepareCandidateItems();

 public:
  /**
   * Tries to reduce the number of vias and the trace length of the routed connections. Returns
   * the improvement in [0, 1], or -1 if the routing must go on no matter how much it improves.
   */
  virtual float optRoutePass(int32_t passNo, bool withPreferredDirections);
  /** Try to improve the route by re-routing the connections containing item. */
  virtual ItemRouteResult* optRouteItem(Item* item, bool withPreferredDirections);
  static ItemRouteResult* optRouteItemOnBoard(
      RoutingJob* job,
      RoutingBoard* routingBoard,
      Item* item,
      double baselineTraceLength,
      bool withPreferredDirections,
      bool useIncreasedRipupCosts,
      StoppableThread* thread,
      std::optional<int64_t> deadlineMs);
  static ItemRouteResult* optRouteItemOnBoard(
      RoutingJob* job,
      RoutingBoard* routingBoard,
      Item* item,
      double baselineTraceLength,
      bool withPreferredDirections,
      bool useIncreasedRipupCosts,
      StoppableThread* thread,
      std::optional<int64_t> deadlineMs,
      BoardStatistics_OptimizerMetrics* baselineMetrics);

 private:
  /** Returns the nets that are unrouted before item is ripped, except the nets of item. */
  static jrt::Set<int32_t>* netsLeftUnrouted(RoutingBoard* routingBoard, Item* item);
  static FloatPoint* getItemPosition(Item* item);
  /**
   * Reroutes the winning candidate again, on a fresh copy of the baseline board, and keeps that
   * board. Returns nullptr if the replay does not reach the same result.
   */
  BatchOptimizer_CandidateResult* replayWinner(
      RoutingJob* job,
      BatchOptimizer_CandidateResult* winner,
      bool withPreferredDirections,
      BoardStatistics_OptimizerMetrics* baselineMetrics);

 public:
  /** Returns the current position of the item which will be rerouted, or nullptr if inactive. */
  virtual FloatPoint* getCurrentPosition();

  std::string getId() override;
  std::string getName() override;
  std::string getVersion() override;
  std::string getDescription() override;
  NamedAlgorithmType getType() override;

 private:
  static int32_t calculateIncompleteCount(RoutingBoard* board);

 public:
  /**
   * Whether the increased ripup costs of the start are still used after a pass that did not
   * improve the board (optimizer.keep_increased_ripup_costs...).
   */
  static bool increasedRipupCostsAfterPassWithoutGain(OptimizerSettings* optimizerSettings);
  /**
   * Whether the optimizer goes on, whatever the stop rule says, after a pass with the increased
   * ripup costs that did not improve the board.
   */
  static bool continueAfterPassWithoutGain(OptimizerSettings* optimizerSettings, int32_t passesWithoutGainInARow);
  /**
   * A pass has gain only if the candidate was not vetoed (rejectionReason empty = Java null) and
   * its optimizer score improved over the score before the pass.
   */
  static bool hasPassGain(const std::optional<std::string>& rejectionReason, float scoreBeforePass, float scoreAfterPass);
  /** Decides whether the optimizer should stop after consecutive passes without gain. */
  static bool shouldStopAfterConsecutivePassesWithoutGain(
      OptimizerSettings* optimizerSettings, int32_t passesWithoutGainInARow);
};

}  // namespace fr
