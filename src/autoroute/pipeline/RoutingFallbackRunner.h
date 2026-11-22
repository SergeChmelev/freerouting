#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Java private record Attempt(RoutingFallback.Outcome outcome, RoutingBoard board,
 * BatchAutorouter router, ClearanceBaseline baseline).
 */
class RoutingFallbackRunner_Attempt : public virtual jrt::Object {
  friend class RoutingFallbackRunner;

 private:
  RoutingFallback_Outcome* outcome_;
  RoutingBoard* board_;
  BatchAutorouter* router_;
  ClearanceBaseline* baseline_;

 public:
  RoutingFallbackRunner_Attempt(
      RoutingFallback_Outcome* outcome, RoutingBoard* board, BatchAutorouter* router, ClearanceBaseline* baseline)
      : outcome_(outcome), board_(board), router_(router), baseline_(baseline) {}
  RoutingFallback_Outcome* outcome() { return outcome_; }
  RoutingBoard* board() { return board_; }
  BatchAutorouter* router() { return router_; }
  ClearanceBaseline* baseline() { return baseline_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Re-routes an unfinished board under the configured fallback strategies and keeps the best
 * result. A residual board (a few open connections) is continued first. A fanout-only strategy
 * whose fanout leaves a board an earlier pass loop already started on stops after that fanout.
 * Ported from freerouting.net (Erik Gavriluk, Experimental Television Center).
 *
 * PORT NOTE: `byte[] input` (BasicBoard.serialize) -> BasicBoard* frozen snapshot (see
 * BasicBoard::serialize / deserialize and tools/phase1-decisions.md).
 */
class RoutingFallbackRunner : public virtual jrt::Object {
 private:
  /** Java init: new LinkedHashMap<>() (set in constructor). */
  jrt::Map<std::string, RoutingFallback_Outcome*>* passLoopStarts;
  BatchAutorouter* listenerSource;

 public:
  explicit RoutingFallbackRunner(BatchAutorouter* listenerSource);

  virtual BatchAutorouter* run(
      RoutingJob* job,
      BatchAutorouter* defaultRouter,
      jrt::List<RoutingFallback_Strategy*>* strategies,
      BasicBoard* input);
  static void warnIfAutoNeckFallbackUsed(RoutingJob* job, const std::string& strategy);
  /**
   * Exploration attempts after fallback. Each attempt is a full route of the best board with a
   * lighter fallback list. Later tries of a kind go further. Stops after explore_patience attempts
   * without an improvement.
   */
  virtual BatchAutorouter* explore(RoutingJob* job, BatchAutorouter* incumbent);

 private:
  RoutingFallbackRunner_Attempt* runContinuations(
      RoutingJob* job, RoutingFallbackRunner_Attempt* bestAttempt, jrt::List<RoutingFallback_Strategy*>* continuations);
  RoutingFallbackRunner_Attempt* runAttempt(
      RoutingJob* job,
      BasicBoard* input,
      RoutingFallback_Strategy* strategy,
      bool keepFanoutVias,
      ClearanceBaseline* inputBaseline);
  RoutingFallbackRunner_Attempt* runPrepared(
      RoutingJob* job,
      RoutingBoard* board,
      RouterSettings* settings,
      bool keepFanoutVias,
      const std::string& strategyName);
  static bool replaces(RoutingFallbackRunner_Attempt* candidate, RoutingFallbackRunner_Attempt* incumbent);
  static bool stopAlternatives(RoutingFallback_Outcome* best, std::optional<BatchAutorouter_StopReason> reason);
  static bool stopped(RoutingJob* job);

 public:
  static jrt::List<RoutingFallback_Strategy*>* automaticContinuations(RoutingJob* job);
};

}  // namespace fr
