// Router settings by their JSON path (PORT EXTENSION, shared by the CLI and the API/MCP servers).
#include "settings/RouterSettingPaths.h"

#include <functional>
#include <map>
#include <string>

#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"
#include "settings/RoutingCostSettings.h"

namespace fr {

namespace {

// Java CliSettings.applyRouterSetting: "--router.<path>=<value>" (and "--optimizer.<path>=") sets
// one field of the router settings by its JSON name (or an alternate name). The port has no
// reflection: the settings a CLI user can set are listed here (all numeric / boolean / text
// fields of RouterSettings, AutorouterSettings, OptimizerSettings and RoutingCostSettings).
using Setter = std::function<void(RouterSettings*, const std::string&)>;

int32_t parseInt(const std::string& v) { return static_cast<int32_t>(std::stol(v, nullptr, 0)); }
bool parseBool(const std::string& v) { return v == "true" || v == "TRUE" || v == "True"; }  // Boolean.parseBoolean
double parseDouble(const std::string& v) { return std::stod(v); }

const std::map<std::string, Setter>& routerSetters() {
  using S = RouterSettings*;
  using V = const std::string&;
  static const std::map<std::string, Setter> setters = [] {
    std::map<std::string, Setter> m;
    auto add = [&m](std::initializer_list<const char*> names, Setter f) {
      for (const char* n : names) m[n] = f;
    };
    // RouterSettings
    add({"max_threads"}, [](S s, V v) { s->maxThreads = parseInt(v); });
    add({"job_timeout"}, [](S s, V v) { s->jobTimeoutString = v; });
    add({"trace_pull_tight_accuracy", "tracePullTightAccuracy"}, [](S s, V v) { s->tracePullTightAccuracy = parseInt(v); });
    add({"allowed_via_types"}, [](S s, V v) { s->viasAllowed = parseBool(v); });
    add({"automatic_neckdown", "automaticNeckdown"}, [](S s, V v) { s->automaticNeckdown = parseBool(v); });
    add({"neck_width_um"}, [](S s, V v) { s->neckWidthUm = parseDouble(v); });
    add({"exact_copper"}, [](S s, V v) { s->exactCopper = parseBool(v); });
    add({"strict_drc"}, [](S s, V v) { s->strictDrc = parseBool(v); });
    add({"fix_kicad_footprints"}, [](S s, V v) { s->fixKicadFootprints = parseBool(v); });
    add({"plane_as_obstacle", "conduction_is_obstacle", "planeAsObstacle", "conductionIsObstacle"},
        [](S s, V v) { s->planeAsObstacle = parseBool(v); });
    add({"copper_to_edge_clearance_um"}, [](S s, V v) { s->copperToEdgeClearanceUm = parseDouble(v); });
    add({"hole_clearance_um"}, [](S s, V v) { s->holeClearanceUm = parseDouble(v); });
    add({"hole_to_hole_um"}, [](S s, V v) { s->holeToHoleUm = parseDouble(v); });
    add({"clearance_tolerance_um"}, [](S s, V v) { s->clearanceToleranceUm = parseDouble(v); });
    add({"drc_epsilon_um"}, [](S s, V v) { s->drcEpsilonUm = parseDouble(v); });
    add({"meander_min_amplitude_mm", "meanderMinAmplitudeMm"}, [](S s, V v) { s->meanderMinAmplitudeMm = parseDouble(v); });
    add({"meander_max_amplitude_mm", "meanderMaxAmplitudeMm"}, [](S s, V v) { s->meanderMaxAmplitudeMm = parseDouble(v); });
    add({"meander_spacing_mm", "meanderSpacingMm"}, [](S s, V v) { s->meanderSpacingMm = parseDouble(v); });
    add({"meander_corner_style", "meanderCornerStyle"}, [](S s, V v) { s->meanderCornerStyle = v; });
    add({"meander_radius_percentage", "meanderRadiusPercentage"}, [](S s, V v) { s->meanderRadiusPercentage = parseDouble(v); });
    add({"meander_single_sided", "meanderSingleSided"}, [](S s, V v) { s->meanderSingleSided = parseBool(v); });
    // scoring (RoutingCostSettings)
    add({"scoring.via_costs", "scoring.viaCosts"}, [](S s, V v) { s->scoring->viaCosts = parseInt(v); });
    add({"scoring.plane_via_costs"}, [](S s, V v) { s->scoring->planeViaCosts = parseInt(v); });
    add({"scoring.start_ripup_costs", "scoring.startRipupCosts"}, [](S s, V v) { s->scoring->startRipupCosts = parseInt(v); });
    add({"scoring.default_bend_cost"}, [](S s, V v) { s->scoring->defaultBendCost = parseDouble(v); });
    add({"scoring.default_preferred_direction_trace_cost"}, [](S s, V v) { s->scoring->defaultPreferredDirectionTraceCost = parseDouble(v); });
    add({"scoring.default_undesired_direction_trace_cost"}, [](S s, V v) { s->scoring->defaultUndesiredDirectionTraceCost = parseDouble(v); });
    add({"scoring.unrouted_net_penalty"}, [](S s, V v) { s->scoring->unroutedNetPenalty = static_cast<float>(parseDouble(v)); });
    add({"scoring.clearance_violation_penalty"}, [](S s, V v) { s->scoring->clearanceViolationPenalty = static_cast<float>(parseDouble(v)); });
    add({"scoring.bend_penalty"}, [](S s, V v) { s->scoring->bendPenalty = static_cast<float>(parseDouble(v)); });
    // autorouter
    add({"autorouter.enabled"}, [](S s, V v) { s->autorouter->enabled = parseBool(v); });
    add({"autorouter.algorithm"}, [](S s, V v) { s->autorouter->algorithm = v; });
    add({"autorouter.max_passes"}, [](S s, V v) { s->autorouter->maxPasses = parseInt(v); });
    add({"autorouter.stagnation_pass_limit"}, [](S s, V v) { s->autorouter->stagnationPassLimit = parseInt(v); });
    add({"autorouter.max_items"}, [](S s, V v) { s->autorouter->maxItems = parseInt(v); });
    add({"autorouter.max_threads"}, [](S s, V v) { s->autorouter->maxThreads = parseInt(v); });
    add({"autorouter.save_intermediate_stages"}, [](S s, V v) { s->autorouter->saveIntermediateStages = parseBool(v); });
    add({"autorouter.priority_via_costs"}, [](S s, V v) { s->autorouter->priorityViaCosts = parseInt(v); });
    add({"autorouter.start_necked"}, [](S s, V v) { s->autorouter->startNecked = parseBool(v); });
    add({"autorouter.fallback_strategies"}, [](S s, V v) { s->autorouter->fallbackStrategies = v; });
    add({"autorouter.explore_fallback_strategies"}, [](S s, V v) { s->autorouter->exploreFallbackStrategies = v; });
    add({"autorouter.smd_net_via_cost_factor"}, [](S s, V v) { s->autorouter->smdNetViaCostFactor = parseDouble(v); });
    add({"autorouter.residual_repair_max_open"}, [](S s, V v) { s->autorouter->residualRepairMaxOpen = parseInt(v); });
    add({"autorouter.explore_patience"}, [](S s, V v) { s->autorouter->explorePatience = parseInt(v); });
    add({"autorouter.item_order_seed"}, [](S s, V v) { s->autorouter->itemOrderSeed = parseInt(v); });
    add({"autorouter.plane_as_priority"}, [](S s, V v) { s->autorouter->planeAsPriority = parseBool(v); });
    add({"autorouter.plane_share_gap_um"}, [](S s, V v) { s->autorouter->planeShareGapUm = parseDouble(v); });
    add({"autorouter.via_in_pad"}, [](S s, V v) { s->autorouter->viaInPad = parseBool(v); });
    add({"autorouter.via_touching_pad"}, [](S s, V v) { s->autorouter->viaTouchingPad = parseBool(v); });
    add({"autorouter.via_centre_of_gravity"}, [](S s, V v) { s->autorouter->viaCentreOfGravity = parseBool(v); });
    add({"autorouter.fanout_via_pad_gap"}, [](S s, V v) { s->autorouter->fanoutViaPadGap = parseDouble(v); });
    add({"autorouter.ball_escape_order"}, [](S s, V v) { s->autorouter->ballEscapeOrder = v; });
    add({"autorouter.exit_stubs"}, [](S s, V v) { s->autorouter->exitStubs = parseBool(v); });
    // fanout (FanoutSettings)
    add({"fanout.enabled"}, [](S s, V v) { s->fanout->enabled = parseBool(v); });
    add({"fanout.max_passes"}, [](S s, V v) { s->fanout->maxPasses = parseInt(v); });
    add({"fanout.max_items"}, [](S s, V v) { s->fanout->maxItems = parseInt(v); });
    add({"fanout.max_milliseconds_per_pin"}, [](S s, V v) { s->fanout->maxMillisecondsPerPin = std::stoll(v, nullptr, 0); });
    add({"fanout.ripup_allowed", "fanout.ripupAllowed"}, [](S s, V v) { s->fanout->ripupAllowed = parseBool(v); });
    add({"fanout.min_escape_length_mm"}, [](S s, V v) { s->fanout->minEscapeLengthMm = parseDouble(v); });
    add({"fanout.max_escape_length_mm"}, [](S s, V v) { s->fanout->maxEscapeLengthMm = parseDouble(v); });
    add({"fanout.start_via_diameter_mm"}, [](S s, V v) { s->fanout->startViaDiameterMm = parseDouble(v); });
    add({"fanout.end_via_diameter_mm"}, [](S s, V v) { s->fanout->endViaDiameterMm = parseDouble(v); });
    add({"fanout.pin_sorting_order"}, [](S s, V v) { s->fanout->pinSortingOrder = v; });
    add({"fanout.fallback_to_board_vias"}, [](S s, V v) { s->fanout->fallbackToBoardVias = parseBool(v); });
    add({"fanout.retry_without_escape_window"}, [](S s, V v) { s->fanout->retryWithoutEscapeWindow = parseBool(v); });
    add({"fanout.strategy"}, [](S s, V v) { s->fanout->strategy = v; });
    add({"fanout.two_layer_bga"}, [](S s, V v) { s->fanout->twoLayerBga = v; });
    add({"fanout.timeout"}, [](S s, V v) { s->fanout->timeoutString = v; });
    add({"fanout.bga_escape_enabled"}, [](S s, V v) { s->fanout->bgaEscapeEnabled = parseBool(v); });
    add({"fanout.bga_escape_min_pins"}, [](S s, V v) { s->fanout->bgaEscapeMinPins = parseInt(v); });
    // optimizer
    add({"optimizer.enabled"}, [](S s, V v) { s->optimizer->enabled = parseBool(v); });
    add({"optimizer.algorithm"}, [](S s, V v) { s->optimizer->algorithm = v; });
    add({"optimizer.max_passes"}, [](S s, V v) { s->optimizer->maxPasses = parseInt(v); });
    add({"optimizer.max_items"}, [](S s, V v) { s->optimizer->maxItems = parseInt(v); });
    add({"optimizer.max_threads"}, [](S s, V v) { s->optimizer->maxThreads = parseInt(v); });
    add({"optimizer.improvement_threshold"}, [](S s, V v) {
      // Java: a value in (0, 1) is a fraction, converted to percent.
      float f = static_cast<float>(parseDouble(v));
      s->optimizer->optimizationImprovementThreshold = (f > 0.0f && f < 1.0f) ? f * 100.0f : f;
    });
    add({"optimizer.enable_preflight_guards"}, [](S s, V v) { s->optimizer->enablePreflightGuards = parseBool(v); });
    add({"optimizer.max_consecutive_failures"}, [](S s, V v) { s->optimizer->maxConsecutiveFailures = parseInt(v); });
    add({"optimizer.max_consecutive_failures_pass1"}, [](S s, V v) { s->optimizer->maxConsecutiveFailuresPass1 = parseInt(v); });
    add({"optimizer.additional_ripup_cost_factor_at_start"}, [](S s, V v) { s->optimizer->additionalRipupCostFactorAtStart = parseInt(v); });
    add({"optimizer.keep_increased_ripup_costs"}, [](S s, V v) { s->optimizer->keepIncreasedRipupCosts = parseBool(v); });
    add({"optimizer.via_nudge_enabled"}, [](S s, V v) { s->optimizer->viaNudgeEnabled = parseBool(v); });
    add({"optimizer.step_slide_enabled"}, [](S s, V v) { s->optimizer->stepSlideEnabled = parseBool(v); });
    add({"optimizer.trace_ripup_cost_factor"}, [](S s, V v) { s->optimizer->traceRipupCostFactor = static_cast<float>(parseDouble(v)); });
    add({"optimizer.max_autoroute_passes"}, [](S s, V v) { s->optimizer->maxAutoroutePasses = parseInt(v); });
    add({"optimizer.timeout"}, [](S s, V v) { s->optimizer->timeoutString = v; });
    // Java LegacyRouterSettingsBridge: deprecated flat autorouter keys.
    for (const char* flat : {"enabled", "max_passes", "algorithm", "max_items", "save_intermediate_stages",
                             "stagnation_pass_limit"}) {
      m[flat] = m[std::string("autorouter.") + flat];
    }
    return m;
  }();
  return setters;
}

}  // namespace

/** Applies "--router.<path>=<value>" / "--optimizer.<path>=<value>"; false if unknown or bad. */
bool applyRouterSetting(RouterSettings* s, const std::string& property, const std::string& value) {
  std::string path = property.rfind("router.", 0) == 0 ? property.substr(7) : property;
  auto it = routerSetters().find(path);
  if (it == routerSetters().end()) {
    FRLogger::warn("Failed to apply CLI router setting: " + property + ": unknown or unsupported setting");
    return false;
  }
  try {
    it->second(s, value);
  } catch (const std::exception& e) {
    FRLogger::warn("Failed to apply CLI router setting: " + property + ": bad value '" + value + "'");
    return false;
  }
  FRLogger::debug("Applied CLI router setting: " + property + " = " + value);
  return true;
}

bool hasRouterSetting(const std::string& path) {
  std::string p = path.rfind("router.", 0) == 0 ? path.substr(7) : path;
  return routerSetters().count(p) != 0;
}

}  // namespace fr
