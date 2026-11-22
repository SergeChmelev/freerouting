#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/OptimizerScoringVersion.h"
#include "settings/RouterScoringVersion.h"
#include "settings/SettingsSource.h"

namespace fr {

/**
 * Provides hardcoded default values for all router settings. Lowest priority (0); it serves as the
 * base for all other settings. The DEFAULT_* constants are the single source of truth for every
 * scoring weight.
 */
class DefaultSettings : public virtual SettingsSource {
 public:
  // -----------------------------------------------------------------------
  // Scoring weight defaults
  // -----------------------------------------------------------------------
  /** Penalty per unrouted connection (largest penalty: completion dominates). */
  static constexpr float DEFAULT_UNROUTED_NET_PENALTY = 5000000.0F;
  /** Penalty per clearance (DRC) violation. */
  static constexpr float DEFAULT_CLEARANCE_VIOLATION_PENALTY = 1000000.0F;
  /** Penalty per bend (direction-change corner) in any trace. */
  static constexpr float DEFAULT_BEND_PENALTY = 10.0F;
  /** Cost per via placed on a regular (non-plane) net. */
  static constexpr int32_t DEFAULT_VIA_COSTS = 50;
  /** Reduced via cost for vias that connect to a copper pour / power plane. */
  static constexpr int32_t DEFAULT_PLANE_VIA_COSTS = 5;
  /** Base ripup cost at the start of each ripup-and-reroute pass (times the pass number). */
  static constexpr int32_t DEFAULT_START_RIPUP_COSTS = 100;
  /** Cost multiplier per mm of trace in the preferred direction. */
  static constexpr double DEFAULT_PREFERRED_DIRECTION_TRACE_COST = 1.0;
  /** Cost multiplier per mm of trace against the preferred direction. */
  static constexpr double DEFAULT_UNDESIRED_DIRECTION_TRACE_COST = 1.0;
  /** Default copper-to-board-edge clearance in micrometres (0.25 mm). */
  static constexpr double DEFAULT_COPPER_TO_EDGE_CLEARANCE_UM = 250.0;
  /** Default drill-hole-to-copper clearance in micrometres (0 = legacy DSN behaviour). */
  static constexpr double DEFAULT_HOLE_CLEARANCE_UM = 0.0;
  /** Default hole-to-hole minimum in micrometres (KiCad's m_HoleToHoleMin). */
  static constexpr double DEFAULT_HOLE_TO_HOLE_UM = 250.0;
  /** Default clearance violation shortfall tolerance in micrometres. */
  static constexpr double DEFAULT_CLEARANCE_TOLERANCE_UM = 1.0;
  /** Default trace neck width in micrometres (0 = disabled). */
  static constexpr double DEFAULT_NECK_WIDTH_UM = 0.0;
  static constexpr double DEFAULT_MEANDER_MIN_AMPLITUDE_MM = 0.2;
  static constexpr double DEFAULT_MEANDER_MAX_AMPLITUDE_MM = 1.0;
  static constexpr double DEFAULT_MEANDER_SPACING_MM = 0.6;
  static inline const std::string DEFAULT_MEANDER_CORNER_STYLE = "FILLET";
  static constexpr double DEFAULT_MEANDER_RADIUS_PERCENTAGE = 80.0;
  static constexpr bool DEFAULT_MEANDER_SINGLE_SIDED = false;
  /** Current default router score formula. */
  static constexpr RouterScoringVersion DEFAULT_ROUTER_SCORING_VERSION =
      RouterScoringVersion::V2_CONTINUOUS;
  /** Current default optimizer score formula. */
  static constexpr OptimizerScoringVersion DEFAULT_OPTIMIZER_SCORING_VERSION =
      OptimizerScoringVersion::V2_LOWER_BOUND;
  static constexpr float DEFAULT_ROUTER_UNROUTED_CONNECTION_WEIGHT = 1000.0F;
  static inline const std::string DEFAULT_FALLBACK_STRATEGIES =
      "direct-first,fanout-retry,short-escape,open-escape,no-fanout,continue-auto-neck";
  static inline const std::string DEFAULT_EXPLORE_FALLBACK_STRATEGIES = "short-escape,open-escape";
  static constexpr double DEFAULT_SMD_NET_VIA_COST_FACTOR = 0.1;
  /** Split between first-half and second-half unrouted weights. */
  static constexpr float DEFAULT_ROUTER_UNROUTED_FREE_FRACTION = 0.5F;
  /** Penalty for having the first half of connections still open (float division like Java). */
  static constexpr float DEFAULT_ROUTER_UNROUTED_FIRST_HALF_WEIGHT = 1000.0F / 3.0F;
  /** Penalty for having the last half of connections still open. */
  static constexpr float DEFAULT_ROUTER_UNROUTED_SECOND_HALF_WEIGHT = 2000.0F / 3.0F;
  static constexpr float DEFAULT_ROUTER_CLEARANCE_COUNT_WEIGHT = 25.0F;
  static constexpr float DEFAULT_ROUTER_CLEARANCE_DEPTH_WEIGHT = 300.0F;
  static constexpr float DEFAULT_ROUTER_CLEARANCE_DEPTH_SCALE_UM = 1000.0F;
  static constexpr float DEFAULT_OPTIMIZER_EXCESS_LENGTH_WEIGHT = 1000.0F;
  static constexpr float DEFAULT_OPTIMIZER_EXCESS_VIA_WEIGHT = 2000.0F;
  static constexpr float DEFAULT_OPTIMIZER_EXCESS_BEND_WEIGHT = 500.0F;
  static constexpr float DEFAULT_OPTIMIZER_LENGTH_FLOOR = 1.0F;
  static constexpr float DEFAULT_OPTIMIZER_DIFFICULTY_SCALE_FLOOR = 1.0F;
  /** Relative optimizer-pass improvement percentage below which BatchOptimizer stops. */
  static constexpr float DEFAULT_OPTIMIZER_IMPROVEMENT_THRESHOLD = 2.5F;
  /** Consecutive passes without score improvement before the autorouter stops. */
  static constexpr int32_t DEFAULT_STAGNATION_PASS_LIMIT = 10;

  /** A new RouterSettings object with all default values (layer arrays left null). */
  RouterSettings* getSettings() override;
  std::string getSourceName() override { return "Default Settings"; }
  int32_t getPriority() override { return PRIORITY; }

 private:
  static constexpr int32_t PRIORITY = 0;
};

}  // namespace fr
