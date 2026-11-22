#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Settings for the route optimizer which runs after auto-routing to reduce vias and trace length.
 */
// PORT NOTE: Serializable/Cloneable and gson annotations dropped (JSON names in comments).
class OptimizerSettings : public virtual jrt::Object {
 public:
  /** "enabled": whether the route optimizer is enabled. */
  std::optional<bool> enabled;
  /** "algorithm": optimization algorithm identifier (e.g. "freerouting-optimizer"). */
  std::optional<std::string> algorithm;
  /** "max_passes": maximum number of full optimization passes. */
  std::optional<int32_t> maxPasses;
  /** "max_items": maximum number of item optimization attempts. */
  std::optional<int32_t> maxItems;
  /** "max_threads": maximum number of threads for parallel route optimization. */
  std::optional<int32_t> maxThreads;
  /**
   * "improvement_threshold": relative pass-improvement threshold as a percentage (2.5 = 2.5%).
   * BatchOptimizer stops when the relative improvement is below this value.
   */
  std::optional<float> optimizationImprovementThreshold;
  /** "enable_preflight_guards": pre-flight guards that skip un-improvable boards. */
  std::optional<bool> enablePreflightGuards;
  /** "max_consecutive_failures": consecutive item failures before aborting the current pass. */
  std::optional<int32_t> maxConsecutiveFailures;
  /** "max_consecutive_failures_pass1": canary limit for pass 1. */
  std::optional<int32_t> maxConsecutiveFailuresPass1;
  /** "additional_ripup_cost_factor_at_start": multiplier on the base ripup cost at the start. */
  std::optional<int32_t> additionalRipupCostFactorAtStart;
  /** "keep_increased_ripup_costs": keep the start ripup costs after a non-improving pass. */
  std::optional<bool> keepIncreasedRipupCosts;
  /** "via_nudge_enabled": final pass that moves vias with shoving. */
  std::optional<bool> viaNudgeEnabled;
  /** "step_slide_enabled": final pass that slides trace steps with shoving. */
  std::optional<bool> stepSlideEnabled;
  /** "trace_ripup_cost_factor": discount factor when ripping up traces (vs vias). */
  std::optional<float> traceRipupCostFactor;
  /** "max_autoroute_passes": autoroute passes when rerouting a single item. */
  std::optional<int32_t> maxAutoroutePasses;

  // -------------------------------
  /** "board_update_strategy" (Java transient). */
  std::optional<BoardUpdateStrategy> boardUpdateStrategy;
  /** "item_selection_strategy" (Java transient). */
  std::optional<ItemSelectionStrategy> itemSelectionStrategy;
  /** "timeout": timeout for the optimizer stage (e.g. "5m", "300s"); null = no timeout. */
  std::optional<std::string> timeoutString;

  /** Creates optimizer settings without source-specific values. */
  OptimizerSettings() = default;

  /** Java clone(): shallow copy including the (transient) strategy fields. */
  virtual OptimizerSettings* clone();

  /**
   * PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). Java reflection
   * also copies the transient strategy fields (they are public), so they are merged here too.
   */
  int32_t copyNewValuesFrom(OptimizerSettings* source);
};

}  // namespace fr
