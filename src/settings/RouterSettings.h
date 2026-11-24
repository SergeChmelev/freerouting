#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
// The nested settings objects are included (not just forward-declared) because practically every
// user of RouterSettings reads their public fields (routerSettings->autorouter->maxPasses ...).
#include "settings/AutorouterSettings.h"
#include "settings/FanoutSettings.h"
#include "settings/LayerSettings.h"
#include "settings/OptimizerScoreSettings.h"
#include "settings/OptimizerSettings.h"
#include "settings/RouterScoreSettings.h"
#include "settings/RoutingCostSettings.h"

namespace fr {

/**
 * Mutable router configuration assembled from the configured settings sources.
 *
 * <p>Invariant (from SettingsMerger): every field is nullable and has no default value here; the
 * defaults live exclusively in DefaultSettings::getSettings(). The no-arg constructor only creates
 * the (empty) nested settings objects.
 */
// PORT NOTE: Serializable/Cloneable, gson @SerializedName and swagger @Schema dropped. JSON names
// are kept in the field comments. Boxed nullable fields -> std::optional, String -> optional
// string, String[] / double[] -> jrt::Array pointers (nullptr = null).
class RouterSettings : public virtual jrt::Object {
 public:
  // Current algorithm identifier.
  static inline const std::string ALGORITHM_CURRENT = "freerouting-router";
  static constexpr double MIN_BEND_COST = 0.0;
  static constexpr double MAX_BEND_COST = 9.9;

  /** "autorouter" */
  AutorouterSettings* autorouter = nullptr;
  /** "fanout": configuration for the SMD-pin fanout pre-pass. */
  FanoutSettings* fanout = nullptr;
  /** "copper_to_edge_clearance_um" */
  std::optional<double> copperToEdgeClearanceUm;
  /** "hole_clearance_um" */
  std::optional<double> holeClearanceUm;
  /**
   * "hole_to_hole_um": KiCad's hole-to-hole minimum in micrometres (edge-to-edge distance a via's
   * drill keeps from every other drilled hole of any net). 0 turns it off.
   */
  std::optional<double> holeToHoleUm;
  /**
   * "clearance_tolerance_um": shortfalls less than or equal to this are treated as rounding noise
   * rather than clearance violations.
   */
  std::optional<double> clearanceToleranceUm;
  /** "drc_epsilon_um": the board checker's tolerance in micrometers. */
  std::optional<double> drcEpsilonUm;
  /** "plane_nets": explicit list of power-plane net names (plane-routing mode). */
  jrt::Array<std::string>* planeNets = nullptr;
  /**
   * "plane_as_obstacle" (alternates "conduction_is_obstacle", "planeAsObstacle",
   * "conductionIsObstacle"): when true conduction areas block foreign traces.
   */
  std::optional<bool> planeAsObstacle;
  /** "meander_min_amplitude_mm" */
  std::optional<double> meanderMinAmplitudeMm;
  /** "meander_max_amplitude_mm" */
  std::optional<double> meanderMaxAmplitudeMm;
  /** "meander_spacing_mm" */
  std::optional<double> meanderSpacingMm;
  /** "meander_corner_style": FILLET or CHAMFER. */
  std::optional<std::string> meanderCornerStyle;
  /** "meander_radius_percentage": 0 to 100. */
  std::optional<double> meanderRadiusPercentage;
  /** "meander_single_sided" */
  std::optional<bool> meanderSingleSided;
  /**
   * "neck_width_um": opt-in width necking; a connection failing at its net-class width is retried
   * once with all trace half-widths clamped to this width. 0/absent = off.
   */
  std::optional<double> neckWidthUm;
  /** "exact_copper": measure pin/via copper exactly where search-tree octagons overlap. */
  std::optional<bool> exactCopper;
  /** "strict_drc": rip up connections whose new items carry clearance violations. */
  std::optional<bool> strictDrc;
  /** "fix_kicad_footprints": give overlapping netless pads their footprint's net. */
  std::optional<bool> fixKicadFootprints;
  /** "job_timeout" (format "HH:MM:SS") */
  std::optional<std::string> jobTimeoutString;
  /** "layers" (Java transient): per-layer configuration. */
  jrt::Array<LayerSettings*>* layers = nullptr;
  /** "validation_warnings": populated during validation. */
  jrt::List<std::string>* validationWarnings = nullptr;
  /** "trace_pull_tight_accuracy" (alternate "tracePullTightAccuracy") */
  std::optional<int32_t> tracePullTightAccuracy;
  /** "allowed_via_types" */
  std::optional<bool> viasAllowed;
  /** "automatic_neckdown": lower trace width at smaller static pins automatically. */
  std::optional<bool> automaticNeckdown;
  /** "optimizer" */
  OptimizerSettings* optimizer = nullptr;
  /** "scoring" */
  RoutingCostSettings* scoring = nullptr;
  /** "router_scoring" */
  RouterScoreSettings* routerScoring = nullptr;
  /** "optimizer_scoring" */
  OptimizerScoreSettings* optimizerScoring = nullptr;
  /** "max_threads" */
  std::optional<int32_t> maxThreads;
  /** "result_json": optional path of the machine-readable result manifest. */
  std::optional<std::string> resultJsonPath;
  /**
   * (Java transient) true when per-layer trace costs were initialized from board geometry (or set
   * explicitly) and must not be overwritten by applyBoardSpecificOptimizations.
   */
  std::optional<bool> boardSpecificTraceCostsApplied;

  /** Initializes the nested settings objects (all fields stay null). */
  RouterSettings();
  /** Creates router settings sized and tuned for the supplied board. */
  explicit RouterSettings(RoutingBoard* board);

 private:
  // PORT NOTE: Runtime.availableProcessors() -> std::thread::hardware_concurrency() (>= 1).
  static int32_t defaultMaxThreads();
  static int32_t normalizeMaxThreads(std::optional<int32_t> value);

 public:
  /** Registers a listener for changes made through the settings API. */
  void addPropertyChangeListener(PropertyChangeListener* listener);
  /** Removes a previously registered property-change listener. */
  void removePropertyChangeListener(PropertyChangeListener* listener);

 private:
  // PORT NOTE: Java's private `autorouter()` lazy accessor collides with the public field
  // `autorouter`; the field keeps its name (read everywhere), the accessor is renamed.
  AutorouterSettings* autorouterOrCreate();

 public:
  /** Sets the maximum number of autorouter passes and notifies listeners. */
  void setMaxPasses(std::optional<int32_t> value);
  /** Sets and normalizes the maximum number of worker threads. */
  void setMaxThreads(std::optional<int32_t> value);
  /** Effective autorouter thread limit (autorouter.maxThreads bounded by maxThreads). */
  int32_t getAutorouterMaxThreads();
  /** Effective optimizer thread limit (optimizer.maxThreads bounded by maxThreads). */
  int32_t getOptimizerMaxThreads();
  /** Sets the maximum duration allowed for a routing job. */
  void setJobTimeoutString(const std::optional<std::string>& value);
  /** Enables or disables the autorouter. */
  void setEnabled(std::optional<bool> value);
  /** Sets whether the autorouter may place vias (fires a property change). */
  void setViasAllowed(std::optional<bool> value);
  /** Sets whether the autorouter may place vias using a primitive boolean (no event). */
  void setViasAllowed(bool value);
  /** Selects the routing algorithm implementation. */
  void setAlgorithm(const std::optional<std::string>& value);
  /** Enables or disables the post-routing optimizer. */
  void setOptimizerEnabled(std::optional<bool> value);
  /** Applies board-specific optimizations only when layer count changed or not yet applied. */
  void applyBoardSpecificOptimizationsIfNeeded(RoutingBoard* board);
  /** Returns whether board-specific trace costs have been initialized. */
  bool areBoardSpecificTraceCostsApplied();
  /** Calculates layer routing costs from board geometry and signal-layer structure. */
  void applyBoardSpecificOptimizations(RoutingBoard* board);
  /** Applies configured net-class autorouter exclusions onto the board's net classes. */
  void applyNetClassExclusions(RoutingBoard* board);
  /** The via costs of a net of a priority net class. */
  int32_t getPriorityViaCosts();

 private:
  /**
   * The board net classes named in a list such as ignore_net_classes; comma-split KiCad composite
   * names are re-joined. Names matching no class are added to {@code unknown} (may be null).
   */
  static jrt::List<NetClass*>* resolveNetClasses(BoardRules* rules,
                                                 jrt::Array<std::string>* listed,
                                                 jrt::List<std::string>* unknown);
  /** The board net class a user-given name refers to, ignoring case (KiCad default aliases). */
  static NetClass* findNetClass(BoardRules* rules, const std::string& name);

 public:
  /** Populates effective layer costs from scoring onto the layers array. */
  void populateEffectiveLayerCosts();
  /** Number of layers configured in the router settings (0 if layers is null). */
  int32_t getLayerCount();
  /** Sets the number of board layers and initializes layer-specific cost arrays. */
  void setLayerCount(int32_t layerCount);
  /** Creates a deep copy of this RouterSettings object (nested objects and arrays cloned). */
  virtual RouterSettings* clone();
  /** Returns the configured necking width in micrometres, or zero when disabled. */
  double getNeckWidthUm();
  /** Sets the configured necking width in micrometres. */
  void setNeckWidthUm(double width);
  /** Returns whether direct pin routing strategy is configured for fanout. */
  bool usesDirectPinRouting();
  /** Returns whether connections with newly introduced clearance violations are rejected. */
  bool isStrictDrc();
  /** Returns the minimum ripup cost used by the router. */
  int32_t getStartRipupCosts();
  /** Sets the minimum ripup cost used by the router. */
  void setStartRipupCosts(int32_t value);
  /** Returns whether the autorouter should run. */
  bool getRunRouter();
  /** Sets whether the autorouter should run. */
  void setRunRouter(bool value);
  /** Returns whether the post-routing optimizer should run. */
  bool getRunOptimizer();
  /** Sets whether the post-routing optimizer should run. */
  void setRunOptimizer(bool value);
  /** Returns whether the fanout pre-pass should run (default true). */
  bool getRunFanout();
  /** Returns whether the fanout pre-pass should run (default false). */
  bool isFanoutEnabled();
  /** Enables or disables the fanout pre-pass. */
  void setFanoutEnabled(std::optional<bool> value);
  /** Returns whether the autorouter may place vias. */
  bool getViasAllowed();
  /** Returns the cost assigned to regular vias. */
  int32_t getViaCosts();
  /** Sets the cost assigned to regular vias. */
  void setViaCosts(int32_t value);
  /** Returns the cost assigned to vias connecting to a plane. */
  int32_t getPlaneViaCosts();
  /** Sets the cost assigned to vias connecting to a plane. */
  void setPlaneViaCosts(int32_t value);
  /** Returns (a copy of) the explicit list of power-plane net names, or an empty array. */
  jrt::Array<std::string>* getPlaneNets();
  /** Sets the explicit list of power-plane net names (cloned). */
  void setPlaneNets(jrt::Array<std::string>* value);
  /** Returns whether conduction areas act as obstacles, or null if unconfigured. */
  std::optional<bool> getPlaneAsObstacle();
  /** Returns whether conduction areas act as obstacles (false by default). */
  bool isPlaneAsObstacle();
  /** Sets whether conduction areas act as obstacles. */
  void setPlaneAsObstacle(std::optional<bool> value);
  /** Enables or disables routing on one layer. */
  void setLayerActive(int32_t layer, bool value);
  /** Returns whether routing is enabled on one layer. */
  bool getLayerActive(int32_t layer);
  /** Sets the bend cost for one layer (clamped to [MIN_BEND_COST, MAX_BEND_COST]). */
  void setBendCost(int32_t layer, double value);
  /** Returns the effective bend cost for one layer. */
  double getBendCost(int32_t layer);
  /** Sets the preferred routing direction for one layer. */
  void setPreferredDirectionIsHorizontal(int32_t layer, bool value);
  /** Returns the preferred routing direction for one layer. */
  bool getPreferredDirectionIsHorizontal(int32_t layer);
  /** Sets the preferred-direction trace cost for one layer. */
  void setPreferredDirectionTraceCosts(int32_t layer, double value);
  /** Returns the preferred-direction trace cost for one layer. */
  double getPreferredDirectionTraceCosts(int32_t layer);
  /** Returns the against-preferred-direction trace cost for one layer. */
  double getAgainstPreferredDirectionTraceCosts(int32_t layer);
  /** Returns the trace cost for horizontal movement on one layer. */
  double getHorizontalTraceCosts(int32_t layer);
  /** Sets the against-preferred-direction trace cost for one layer. */
  void setAgainstPreferredDirectionTraceCosts(int32_t layer, double value);
  /** Returns the trace cost for vertical movement on one layer. */
  double getVerticalTraceCosts(int32_t layer);
  /** Returns per-layer horizontal and vertical trace-cost factors. */
  jrt::Array<AutorouteControl_ExpansionCostFactor*>* getTraceCosts();
  /** Returns whether automatic trace neckdown is enabled. */
  bool getAutomaticNeckdown();
  /** Sets whether automatic trace neckdown is enabled. */
  void setAutomaticNeckdown(bool value);

  /**
   * Applies explicitly populated values from another settings object; returns the number of
   * fields changed. Java: ReflectionUtil.copyFields(settings, this) then property-change events.
   * Port: copyNewValuesFrom(settings).
   */
  int32_t applyNewValuesFrom(RouterSettings* settings);

  /**
   * PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this) for RouterSettings.
   * Visits the public fields in Java declaration order (autorouter, fanout,
   * copperToEdgeClearanceUm, holeClearanceUm, holeToHoleUm, clearanceToleranceUm, drcEpsilonUm,
   * planeNets, planeAsObstacle, meander*, neckWidthUm, exactCopper, strictDrc,
   * fixKicadFootprints, jobTimeoutString, layers, validationWarnings, tracePullTightAccuracy,
   * viasAllowed, automaticNeckdown, optimizer, scoring, routerScoring, optimizerScoring,
   * maxThreads, resultJsonPath, boardSpecificTraceCostsApplied; the private pcs is skipped) and
   * uses the settings_copy helpers of settings/SettingsCopy.h: nested() for the settings
   * objects, value() for optionals, array() for planeNets, objectArray() for layers, list() for
   * validationWarnings. Transient fields ARE copied (Java reflection ignores `transient`).
   */
  int32_t copyNewValuesFrom(RouterSettings* source);

  /**
   * Validates and normalizes values that affect routing execution, verifying net class lists
   * against the board if one is provided (null = static validation only).
   */
  void validateAgainstBoard(RoutingBoard* board);
  /** Validates and normalizes values that affect routing execution. */
  void validate();

 private:
  // PropertyChangeSupport for bidirectional binding with GUI (created in the constructor).
  PropertyChangeSupport* pcs = nullptr;
  /** Stage E: frees the layer settings / the nested settings objects this object owns. */
  void disposeLayers();
  void disposeParts();

 public:
  /** Stage E: frees a private settings object (a clone nobody else refers to) with its parts. */
  static void disposeTree(RouterSettings* s);

 private:
};

}  // namespace fr
