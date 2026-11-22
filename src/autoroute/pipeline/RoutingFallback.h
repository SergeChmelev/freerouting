#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A named settings change applied to a private copy of the input or incumbent board.
 * PORT NOTE: the field `apply` clashes with the method apply() -> field renamed `apply_`.
 */
class RoutingFallback_Strategy : public virtual jrt::Object {
 public:
  std::string name;
  bool needsFanout;
  bool continueBest;
  bool fanoutOnly;

 private:
  std::function<void(RouterSettings*)> apply_;

 public:
  RoutingFallback_Strategy(
      const std::string& name,
      bool needsFanout,
      std::function<void(RouterSettings*)> apply,
      bool continueBest,
      bool fanoutOnly)
      : name(name), needsFanout(needsFanout), continueBest(continueBest), fanoutOnly(fanoutOnly), apply_(std::move(apply)) {}

  virtual void apply(RouterSettings* settings) { this->apply_(settings); }
};

/** One attempt, ranked by fewer opens, then fewer new violations, then the higher score. */
class RoutingFallback_Outcome : public virtual jrt::Object {
 public:
  std::string strategy;
  int32_t unrouted;
  int32_t newOrChanged;
  int32_t violations;
  float score;
  int32_t designBlocked;

  RoutingFallback_Outcome(
      const std::string& strategy,
      int32_t unrouted,
      int32_t newOrChanged,
      int32_t violations,
      float score,
      int32_t designBlocked)
      : strategy(strategy),
        unrouted(unrouted),
        newOrChanged(newOrChanged),
        violations(violations),
        score(score),
        designBlocked(designBlocked) {}

  virtual bool isCompleteAndClean() { return this->unrouted - this->designBlocked == 0 && this->newOrChanged == 0; }
  /** The attempt baseline, or the input-board baseline when the attempt has none. */
  static ClearanceBaseline* exemptionBaseline(ClearanceBaseline* incumbent, ClearanceBaseline* boardBaseline) {
    return incumbent != nullptr ? incumbent : boardBaseline;
  }
  /** Violations that are not exact unchanged exemptions. A null baseline counts them all. */
  static int32_t newOrChangedCount(
      int32_t violationCount, ClearanceBaseline* baseline, jrt::Collection<ClearanceViolation*>* violations);
  static RoutingFallback_Outcome* of(const std::string& strategy, RoutingBoard* board, RouterSettings* scoring);
  /**
   * Measures the board. An explicit incumbent wins; otherwise the input-board clearance baseline
   * is used. A null baseline counts every violation as new.
   */
  static RoutingFallback_Outcome* of(
      const std::string& strategy, RoutingBoard* board, RouterSettings* scoring, ClearanceBaseline* incumbent);
  /** Whether a better candidate preserves the incumbent's clearance legality. */
  static bool canReplace(RoutingFallback_Outcome* candidate, RoutingFallback_Outcome* incumbent, int32_t changedSinceIncumbent);
  /** Negative when a is better. */
  static int32_t compare(RoutingFallback_Outcome* a, RoutingFallback_Outcome* b);
  std::string toString() override;
};

/**
 * When the pass loop stops with open connections, the pipeline re-routes the input board under
 * named setting changes and keeps the best result.
 * Ported from freerouting.net (Erik Gavriluk, Experimental Television Center).
 */
class RoutingFallback : public virtual jrt::Object {
  // Java: the nested Outcome class uses the private violationsOf().
  friend class RoutingFallback_Outcome;

 private:
  RoutingFallback() = default;

 public:
  /** The strategies a settings value may name, in catalog order (Java: List.of(...)). */
  static jrt::List<RoutingFallback_Strategy*>* KNOWN();

  /**
   * Strategies that can change this job. Fanout strategies need fanout and at least one SMD pin.
   * Neck strategies apply only while neck-down is on, and the fine neck only while the neck is
   * wider than 100 um.
   */
  static jrt::List<RoutingFallback_Strategy*>* applicable(
      jrt::List<RoutingFallback_Strategy*>* strategies, RouterSettings* settings, bool fanoutRuns);
  /**
   * The strategies the settings name, in order and without repeats. Unknown names are reported
   * (to `unknown`, which may be empty = Java null) and skipped. `none` disables the fallback.
   */
  static jrt::List<RoutingFallback_Strategy*>* parse(
      RouterSettings* settings, std::function<void(const std::string&)> unknown);
  /** Returns nullptr when no strategy has this name. */
  static RoutingFallback_Strategy* find(const std::string& name);
  /** Whether a pass loop that stopped for this reason may be followed by another attempt. */
  static bool isRetryable(std::optional<BatchAutorouter_StopReason> reason);
  /** Human-readable stop reason for logs ("an unknown stop" for empty). */
  static std::string describe(std::optional<BatchAutorouter_StopReason> reason);
  static ClearanceBaseline* captureClearance(RoutingBoard* board, int32_t violations);
  static int32_t newOrChangedClearance(RoutingBoard* candidate, int32_t violations, ClearanceBaseline* incumbent);

 private:
  static jrt::Collection<ClearanceViolation*>* violationsOf(RoutingBoard* board);
};

}  // namespace fr
