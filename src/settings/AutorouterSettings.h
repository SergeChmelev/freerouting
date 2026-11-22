#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Execution knobs for the batch autorouter stage (fanout and optimizer stay in their own objects).
 * All fields are nullable so {@code SettingsMerger} can tell "this source sets the field" from
 * "this source has no opinion". Hardcoded defaults live only in DefaultSettings::getSettings().
 */
// PORT NOTE: Serializable/Cloneable, gson @SerializedName and swagger @Schema dropped. The JSON
// names are kept in the field comments for reference.
class AutorouterSettings : public virtual jrt::Object {
 public:
  /** "enabled": whether the autorouter stage runs after fanout. */
  std::optional<bool> enabled;
  /** "algorithm": algorithm identifier (e.g. RouterSettings::ALGORITHM_CURRENT). */
  std::optional<std::string> algorithm;
  /** "max_passes": maximum autorouter passes; 0 means no limit. */
  std::optional<int32_t> maxPasses;
  /**
   * "stagnation_pass_limit": consecutive passes without score improvement before the autorouter
   * stops; 0 or negative disables stagnation detection.
   */
  std::optional<int32_t> stagnationPassLimit;
  /** "max_items": maximum items attempted in the autorouter stage. */
  std::optional<int32_t> maxItems;
  /** "max_threads": kept for the CLI; the autorouter pass does not read it. */
  std::optional<int32_t> maxThreads;
  /** "save_intermediate_stages": save board snapshots between passes. */
  std::optional<bool> saveIntermediateStages;
  /** "ignore_net_classes": net class names the autorouter should skip (null = unset). */
  jrt::Array<std::string>* ignoreNetClasses = nullptr;
  /** "priority_net_classes": net classes routed first in every pass (null = unset). */
  jrt::Array<std::string>* priorityNetClasses = nullptr;
  /** "priority_via_costs": via costs of the nets of priority_net_classes; unset: via costs. */
  std::optional<int32_t> priorityViaCosts;
  /** "start_necked": nets that required necking route at the neck width directly later on. */
  std::optional<bool> startNecked;
  /** "fallback_strategies": comma-separated routing fallback strategies, tried in order. */
  std::optional<std::string> fallbackStrategies;
  /** "explore_fallback_strategies": fallback alternatives an exploration round runs. */
  std::optional<std::string> exploreFallbackStrategies;
  /** "smd_net_via_cost_factor": factor applied to the via cost of nets whose pins are all SMD. */
  std::optional<double> smdNetViaCostFactor;
  /** "residual_repair_max_open": open connections at which the best board is continued. */
  std::optional<int32_t> residualRepairMaxOpen;
  /** "explore_patience": exploration attempts after the opening fallback; 0 disables them. */
  std::optional<int32_t> explorePatience;
  /** "item_order_seed": extra seed mixed into the autorouter item order; null keeps the order. */
  std::optional<int32_t> itemOrderSeed;
  /** "plane_as_priority": plane pads skip the fanout; each drops into its plane by its own via. */
  std::optional<bool> planeAsPriority;
  /** "plane_share_gap_um": plane pads closer than this may share one via. Unset: 500 um. */
  std::optional<double> planeShareGapUm;
  /** "via_in_pad": allows vias directly in SMD component pads of their net. */
  std::optional<bool> viaInPad;
  /** "via_touching_pad": allows vias touching SMD pads of their net, drill hole outside. */
  std::optional<bool> viaTouchingPad;
  /** "via_centre_of_gravity": centre-of-gravity via candidate for pieces with SMD pads. */
  std::optional<bool> viaCentreOfGravity;
  /** "fanout_via_pad_gap": extra cost for fanout vias inside own-net pad clearance. */
  std::optional<double> fanoutViaPadGap;
  /** "ball_escape_order": "auto", "off", "outer_first" or "inner_first". */
  std::optional<std::string> ballEscapeOrder;
  /** "exit_stubs": straight exit stubs for fine-pitch IC pins with exact clearance. */
  std::optional<bool> exitStubs;

  /** Java clone(): shallow copy, but the two String[] arrays are cloned. */
  virtual AutorouterSettings* clone();

  /**
   * PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this) for this type.
   * Copies every field of {@code source} that is set (non-null) into this object, in Java
   * declaration order, and returns the number of fields counted as changed (scalars/strings: when
   * the value differs; String[] arrays: always, the array reference is shared like Java does).
   */
  int32_t copyNewValuesFrom(AutorouterSettings* source);
};

}  // namespace fr
