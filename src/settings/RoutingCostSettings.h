#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Costs used by the router's search and rip-up algorithms.
 *
 * <p>This object is deliberately separate from router and optimizer board-score weights. Its values
 * affect route search behavior; only the legacy score's length and via terms read the corresponding
 * legacy values.
 */
// PORT NOTE: Serializable/Cloneable and gson annotations dropped (JSON names in comments).
class RoutingCostSettings : public virtual jrt::Object {
 public:
  /** "preferred_direction_trace_cost" (Java transient double[]): cost of 1 mm per layer. */
  jrt::Array<double>* preferredDirectionTraceCost = nullptr;
  /** "undesired_direction_trace_cost" (Java transient double[]): cost of 1 mm per layer. */
  jrt::Array<double>* undesiredDirectionTraceCost = nullptr;
  /** "default_preferred_direction_trace_cost" */
  std::optional<double> defaultPreferredDirectionTraceCost;
  /** "default_undesired_direction_trace_cost" */
  std::optional<double> defaultUndesiredDirectionTraceCost;
  /** "via_costs": cost of a via on a regular (non-plane) net. */
  std::optional<int32_t> viaCosts;
  /** "plane_via_costs": cost of a via placed on a plane. */
  std::optional<int32_t> planeViaCosts;
  /**
   * "start_ripup_costs": base rip-up cost for the first rip-up-and-reroute pass; multiplied by the
   * pass number inside BatchAutorouter.
   */
  std::optional<int32_t> startRipupCosts;
  /** "default_bend_cost": default bend cost per direction change on a layer. */
  std::optional<double> defaultBendCost;
  /** Legacy board-score weights ("unrouted_net_penalty", ...). */
  std::optional<float> unroutedNetPenalty;
  std::optional<float> clearanceViolationPenalty;
  std::optional<float> bendPenalty;

  /** Creates a deep copy, including layer-specific trace-cost arrays. */
  virtual RoutingCostSettings* clone();

  /**
   * PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). The double[]
   * arrays are assigned by reference (shared with the source), exactly like Java reflection does.
   */
  int32_t copyNewValuesFrom(RoutingCostSettings* source);
};

}  // namespace fr
