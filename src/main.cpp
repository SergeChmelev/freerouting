// freerouting-cpp: command-line front end of the C++ port (headless DSN -> SES routing).
//
// usage: freerouting-cpp -de <input.dsn> -do <output.ses> [options]
//   -de <files>        the design: board.dsn, or several files joined with '+' (or given one
//                      after the other), the extension deciding: .dsn, .rules, .kicad_dru,
//                      .kicad_pro, .kicad_pcb (Java GlobalSettings)
//   -dr <file.rules>   apply a rules file
//   -mp <n>            maximum autorouter passes
//   -mt <n>            maximum threads of the optimizer (Java: router.max_threads; 1 = sequential)
//   --timeout <dur>    job timeout (Java Duration syntax, e.g. PT10M)
//   --no-optimizer     skip the optimization stage
//   --no-drc           skip the final design rules check
//   --router.<path>=<value>  one router setting by its JSON name, like Java (e.g.
//                      --router.scoring.via_costs=200, --router.automatic_neckdown=false)
//   -q / -v / -vv      log level warn / debug / trace (default info)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/facade/FreeroutingHeadlessEngine.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "core/scoring/BoardStatistics.h"
#include "jrt/jrt.h"
#include "platform/platform.h"
#include "logger/FRLogger.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace {

void usage() {
  std::fprintf(stderr,
               "usage: freerouting-cpp -de <input.dsn> -do <output.ses> [-dr <rules>] [-mp <passes>]\n"
               "                       [-mt <threads>] [--timeout <PTxxM>] [--no-optimizer] [--no-drc]\n"
               "                       [--router.<setting>=<value> ...] [-q|-v|-vv]\n"
               "  e.g. --router.scoring.via_costs=200  --router.automatic_neckdown=false\n"
               "       --router.optimizer.max_passes=10  --router.max_threads=4\n");
}

jrt::Array<int8_t>* readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return nullptr;
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto* bytes = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(data.size()));
  std::memcpy(bytes->data(), data.data(), data.size());
  return bytes;
}

std::string baseName(const std::string& path) {
  auto pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Java's CLI names the design after the input file without its extension.
std::string designName(const std::string& path) {
  std::string b = baseName(path);
  auto dot = b.find_last_of('.');
  return dot == std::string::npos ? b : b.substr(0, dot);
}

// Java CliSettings.applyRouterSetting: "--router.<path>=<value>" (and "--optimizer.<path>=") sets
// one field of the router settings by its JSON name (or an alternate name). The port has no
// reflection: the settings a CLI user can set are listed here (all numeric / boolean / text
// fields of RouterSettings, AutorouterSettings, OptimizerSettings and RoutingCostSettings).
using Setter = std::function<void(fr::RouterSettings*, const std::string&)>;

int32_t parseInt(const std::string& v) { return static_cast<int32_t>(std::stol(v, nullptr, 0)); }
bool parseBool(const std::string& v) { return v == "true" || v == "TRUE" || v == "True"; }  // Boolean.parseBoolean
double parseDouble(const std::string& v) { return std::stod(v); }

const std::map<std::string, Setter>& routerSetters() {
  using S = fr::RouterSettings*;
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

/** Applies "--router.<path>=<value>" / "--optimizer.<path>=<value>"; false if unknown or bad. */
bool applyRouterSetting(fr::RouterSettings* s, const std::string& property, const std::string& value) {
  std::string path = property.rfind("router.", 0) == 0 ? property.substr(7) : property;
  auto it = routerSetters().find(path);
  if (it == routerSetters().end()) {
    fr::FRLogger::warn("Failed to apply CLI router setting: " + property + ": unknown or unsupported setting");
    return false;
  }
  try {
    it->second(s, value);
  } catch (const std::exception& e) {
    fr::FRLogger::warn("Failed to apply CLI router setting: " + property + ": bad value '" + value + "'");
    return false;
  }
  fr::FRLogger::debug("Applied CLI router setting: " + property + " = " + value);
  return true;
}

// Java GlobalSettings.DE_VALID_EXTENSIONS
const std::vector<std::string>& deValidExtensions() {
  static const std::vector<std::string> extensions = {".dsn",       ".json",      ".ses",      ".rules",
                                                      ".kicad_dru", ".kicad_pro", ".kicad_pcb"};
  return extensions;
}

// Java GlobalSettings.isMultiFileConcatenation: whether an argument is a '+' concatenated list of
// file paths ("file1.dsn+file2.rules"), rather than one path with a '+' in its name.
bool isMultiFileConcatenation(const std::string& rawArg, const std::vector<std::string>* validExtensions) {
  if (rawArg.find('+') == std::string::npos) {
    return false;
  }
  std::vector<std::string> parts = jrt::split(rawArg, '+');  // Java split("\+", -1); a trailing empty part fails either way
  if (parts.size() < 2) {
    return false;
  }
  for (const std::string& part : parts) {
    std::string trimmed = jrt::trim(part);
    if (trimmed.empty()) {
      return false;
    }
    // Each file in a '+' concatenation must have a file extension
    int32_t lastSlash = std::max(jrt::lastIndexOf(trimmed, "/"), jrt::lastIndexOf(trimmed, "\\"));
    int32_t lastDot = jrt::lastIndexOf(trimmed, ".");
    if (lastDot <= lastSlash || lastDot == static_cast<int32_t>(trimmed.size()) - 1) {
      return false;
    }
    std::string ext = jrt::toLowerCase(trimmed.substr(lastDot));
    if (validExtensions != nullptr &&
        std::find(validExtensions->begin(), validExtensions->end(), ext) == validExtensions->end()) {
      return false;
    }
  }
  return true;
}

bool fileExists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

int runMain(int argc, char** argv) {
  std::string input, output, rules;
  std::optional<std::string> kicadDesignRules, kicadProject, kicadBoard;
  std::optional<int32_t> maxPasses, maxThreads;
  std::optional<std::string> timeout;
  bool optimizer = true, drc = true;
  std::vector<std::pair<std::string, std::string>> routerArgs;  // --router.<path>=<value>, in order
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-de") {
      // Java GlobalSettings: the design file(s), each identified by its extension
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        std::vector<std::string> files;
        int j = i + 1;
        while (j < argc && argv[j][0] != '-') {
          std::string rawArg = jrt::trim(argv[j]);
          if (fileExists(rawArg)) {
            files.push_back(rawArg);
          } else if (isMultiFileConcatenation(rawArg, &deValidExtensions())) {
            // Split each argument by '+' to support legacy concatenation (e.g. file1.dsn+file2.rules)
            for (const std::string& part : jrt::split(rawArg, '+')) {
              if (!jrt::trim(part).empty()) {
                files.push_back(jrt::trim(part));
              }
            }
          } else {
            files.push_back(rawArg);
          }
          j++;
        }
        bool hasDsn = false, hasSes = false, hasRules = false;
        bool hasKiCadRules = false, hasKiCadProject = false, hasKiCadBoard = false;
        for (std::string file : files) {
          file = jrt::trim(file);
          if (file.empty()) {
            continue;
          }
          std::string lowerFile = jrt::toLowerCase(file);
          if (jrt::endsWith(lowerFile, ".dsn")) {
            if (hasDsn) {
              fr::FRLogger::warn("Multiple DSN files provided in -de argument. Only the last one will be used.");
            }
            input = file;
            hasDsn = true;
          } else if (jrt::endsWith(lowerFile, ".json")) {
            if (!hasDsn) {
              input = file;
              hasDsn = true;
            } else {
              if (hasSes) {
                fr::FRLogger::warn(
                    "Multiple session files (SES/JSON) provided in -de argument. Only the last one will be used.");
              }
              // PORT NOTE: initial session files (designSessionFilename) are not supported by the port.
              fr::FRLogger::warn("Ignored session file (not supported by freerouting-cpp): " + file);
              hasSes = true;
            }
          } else if (jrt::endsWith(lowerFile, ".ses")) {
            if (hasSes) {
              fr::FRLogger::warn("Multiple SES files provided in -de argument. Only the last one will be used.");
            }
            fr::FRLogger::warn("Ignored session file (not supported by freerouting-cpp): " + file);
            hasSes = true;
          } else if (jrt::endsWith(lowerFile, ".rules")) {
            if (hasRules) {
              fr::FRLogger::warn("Multiple RULES files provided in -de argument. Only the last one will be used.");
            }
            rules = file;
            hasRules = true;
          } else if (jrt::endsWith(lowerFile, ".kicad_dru")) {
            if (hasKiCadRules) {
              fr::FRLogger::warn(
                  "Multiple KiCad design rules files provided in -de argument. Only the last one will be used.");
            }
            kicadDesignRules = file;
            hasKiCadRules = true;
          } else if (jrt::endsWith(lowerFile, ".kicad_pro")) {
            if (hasKiCadProject) {
              fr::FRLogger::warn(
                  "Multiple KiCad project files provided in -de argument. Only the last one will be used.");
            }
            kicadProject = file;
            hasKiCadProject = true;
          } else if (jrt::endsWith(lowerFile, ".kicad_pcb")) {
            if (hasKiCadBoard) {
              fr::FRLogger::warn(
                  "Multiple KiCad board files provided in -de argument. Only the last one will be used.");
            }
            kicadBoard = file;
            hasKiCadBoard = true;
          } else {
            fr::FRLogger::warn("Unknown file type in -de argument: " + file +
                               ". Expected .dsn, .json, .ses, .rules, .kicad_dru, .kicad_pro or .kicad_pcb");
          }
        }
        // Skip the processed arguments
        i = j - 1;
      }
    }
    else if (a == "-do") output = next();
    else if (a == "-dr") rules = next();
    else if (a == "-mp") maxPasses = std::stoi(next());
    else if (a == "-mt") maxThreads = std::stoi(next());
    else if (a == "--timeout") timeout = next();
    else if (a == "--no-optimizer") optimizer = false;
    else if (a == "--no-drc") drc = false;
    else if (a == "-q") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_WARN;
    else if (a == "-v") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_DEBUG;
    else if (a == "-vv") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_TRACE;
    else if (a.rfind("--", 0) == 0 && a.find('=') != std::string::npos) {
      std::string property = a.substr(2, a.find('=') - 2);
      std::string value = a.substr(a.find('=') + 1);
      if (property.rfind("router.", 0) == 0 || property.rfind("optimizer.", 0) == 0) {
        routerArgs.emplace_back(property, value);
      } else {
        fr::FRLogger::warn("Ignored setting (not a router setting): " + property);
      }
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
      usage();
      return 2;
    }
  }
  if (input.empty() || output.empty()) {
    usage();
    return 2;
  }

  auto* dsn = readFile(input);
  if (!dsn) {
    std::fprintf(stderr, "cannot read input file '%s'\n", input.c_str());
    return 1;
  }
  auto* builder = fr::HeadlessRoutingRequest::builder()->dsnBytes(dsn)->designName(designName(input))->runDrc(drc);
  if (!rules.empty()) {
    auto* rulesBytes = readFile(rules);
    if (!rulesBytes) {
      std::fprintf(stderr, "cannot read rules file '%s'\n", rules.c_str());
      return 1;
    }
    // The rules header names the design (the .rules file of board.dsn is "(rules PCB board ...)"):
    // the name to check is the file name without its extension.
    builder->rulesBytes(rulesBytes)->rulesName(designName(rules));
  }
  builder->kicadDesignRulesFile(kicadDesignRules)->kicadProjectFile(kicadProject)->kicadBoardFile(kicadBoard);
  if (maxPasses || maxThreads || timeout || !optimizer || !routerArgs.empty()) {
    // Same role as Java's CLI/API settings source: only the given values override.
    auto* s = new (jrt::kBoard) fr::RouterSettings();
    if (maxPasses) s->setMaxPasses(maxPasses);
    if (maxThreads) s->setMaxThreads(maxThreads);
    if (timeout) s->setJobTimeoutString(timeout);
    if (!optimizer) s->setOptimizerEnabled(false);
    for (const auto& [property, value] : routerArgs) {
      applyRouterSetting(s, property, value);
    }
    builder->settings(s);
  }

  int64_t start = jrt::System::currentTimeMillis();
  fr::HeadlessRoutingResult* result = fr::FreeroutingHeadlessEngine::route(builder->build());
  double seconds = (jrt::System::currentTimeMillis() - start) / 1000.0;

  if (result->errorMessage()) {
    // Like the Java reference harness (tools/parity/HeadlessRoute.java), the SES of a failed
    // run is still written: it is empty, so the output matches Java's byte for byte.
    std::ofstream out(output, std::ios::binary);
    out << result->sesContent();
    std::fprintf(stderr, "routing failed: %s\n", result->errorMessage()->c_str());
    return 1;
  }
  std::string ses = result->sesContent();
  {
    std::ofstream out(output, std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "cannot write output file '%s'\n", output.c_str());
      return 1;
    }
    out << ses;
  }
  int32_t violations = result->clearanceViolations() ? result->clearanceViolations()->size() : 0;
  std::printf("%s: %d unrouted, %d clearance violations, %.1f s%s -> %s (%zu bytes)\n", baseName(input).c_str(),
              result->unroutedConnections(), violations, seconds, result->isCancelled() ? " (cancelled)" : "",
              output.c_str(), ses.size());
  if (std::getenv("FR_MEM_STATS") != nullptr) {
    jrt::drain_arena_pool();  // cached free chunks are not a leak
    std::fprintf(stderr, "memory: arena peak %.1f MB, heap %.1f MB, disposed %zu objects / %.1f MB\n",
                 jrt::Arena::peakBytes() / 1048576.0, jrt::heap_live_bytes() / 1048576.0, jrt::disposed_count(),
                 jrt::disposed_bytes() / 1048576.0);
    std::fprintf(stderr, "memory: %s\n", fr::BasicBoard::graveyardStats().c_str());
    std::fprintf(stderr, "memory: board copies made %lld, freed %lld\n",
                 static_cast<long long>(fr::BasicBoard::copiesMade()),
                 static_cast<long long>(fr::BasicBoard::copiesDiscarded()));
  }
  int rc = result->isSuccess() ? 0 : 1;
  // Stage E: the run is over. The routed board is the last board of the run.
  fr::BasicBoard::discardLast(result->board());
  jrt::dispose(result);
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  // The optimizer's worker threads would each get a malloc arena of their own, which keeps the
  // memory freed in it: on large boards that doubles the peak memory with many threads, while two
  // arenas cost no measurable time (MALLOC_ARENA_MAX in the environment still wins).
  jrt::platform::limit_malloc_arenas(2);
  int rc;
  {
    // Stage E: geometry and temporaries that no operation region takes and no owner holds
    // belong to the run and are dropped here (jrt::JobRegion).
    jrt::JobRegion job;
    rc = runMain(argc, argv);
    if (std::getenv("FR_MEM_STATS") != nullptr) {
      std::fprintf(stderr, "memory: job heap %.1f MB left at the end of the run (peak %.1f MB)\n",
                   jrt::job_heap_live_bytes() / 1048576.0, jrt::job_heap_peak_bytes() / 1048576.0);
    }
  }
  if (std::getenv("FR_MEM_STATS") != nullptr) {
    jrt::drain_arena_pool();
    std::fprintf(stderr, "memory: still allocated after the run %.1f MB\n", jrt::heap_live_bytes() / 1048576.0);
  }
  return rc;
}
