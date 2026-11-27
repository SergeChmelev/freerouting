// freerouting-cpp: command-line front end of the C++ port (headless DSN -> SES routing).
//
// usage: freerouting-cpp -de <input.dsn> -do <output.ses> [options]
//   -de <files>        the design: board.dsn, or several files joined with '+' (or given one
//                      after the other), the extension deciding: .dsn, .rules, .kicad_dru,
//                      .kicad_pro, .kicad_pcb (Java GlobalSettings)
//   -dr <file.rules>   apply a rules file
//   -mp <n>            maximum autorouter passes
//   -mt <n>            maximum threads of the optimizer (Java: router.max_threads; 1 = sequential)
//   --timeout <dur>    job timeout: an ISO-8601 duration (Java Duration syntax, e.g. PT10M) or HH:MM:SS
//   --no-optimizer     skip the optimization stage
//   --no-drc           skip the final design rules check
//   --router.<path>=<value>  one router setting by its JSON name, like Java (e.g.
//                      --router.scoring.via_costs=200, --router.automatic_neckdown=false)
//   -q / -v / -vv      log level warn / debug / trace (default info)
//
// Servers (server/): freerouting-cpp --mcp            MCP server over stdin / stdout
//                    freerouting-cpp --api [--port N] [--host H]   REST API v1 (and MCP at
//                                       /v1/mcp) on 127.0.0.1:37864

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
#include "settings/RouterSettingPaths.h"
#include "settings/RouterSettings.h"
#include "JobManager.h"
#include "McpServer.h"
#include "RestServer.h"

namespace {

void usage() {
  std::fprintf(stderr,
               "usage: freerouting-cpp -de <input.dsn> -do <output.ses> [-dr <rules>] [-mp <passes>]\n"
               "                       [-mt <threads>] [--timeout <PTxxM>] [--no-optimizer] [--no-drc]\n"
               "                       [--router.<setting>=<value> ...] [-q|-v|-vv]\n"
               "  e.g. --router.scoring.via_costs=200  --router.automatic_neckdown=false\n"
               "       --router.optimizer.max_passes=10  --router.max_threads=4\n"
               "       freerouting-cpp --mcp                         MCP server on stdin/stdout\n"
               "       freerouting-cpp --api [--port 37864] [--host 127.0.0.1]   local REST API v1\n");
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

// --timeout: the engine reads the job timeout as "HH:MM:SS" (RouterSettings.job_timeout, like
// Java). The CLI also takes an ISO-8601 duration as Java's Duration.parse reads it (PT10M,
// PT1H30M, P1DT2H, PT90S, PT2.5S; case-insensitive; fractions of a second are dropped) and
// converts it. Text with a ':' is passed on as it is. Returns nullopt for anything else.
std::optional<std::string> cliTimeoutToHms(const std::string& text) {
  if (text.find(':') != std::string::npos) return text;
  const size_t n = text.size();
  auto is = [&](size_t k, char upper) { return k < n && (text[k] == upper || text[k] == upper + 32); };
  auto digit = [&](size_t k) { return k < n && text[k] >= '0' && text[k] <= '9'; };
  if (!is(0, 'P')) return std::nullopt;
  size_t i = 1;
  int64_t totalSeconds = 0;
  bool inTime = false, anyDate = false, anyTime = false;
  while (i < n) {
    if (is(i, 'T')) {
      if (inTime) return std::nullopt;
      inTime = true;
      ++i;
      continue;
    }
    size_t j = i;
    while (digit(j)) ++j;
    if (j == i || j - i > 12) return std::nullopt;
    int64_t value = std::stoll(text.substr(i, j - i));
    bool fraction = false;
    if (j < n && (text[j] == '.' || text[j] == ',')) {
      fraction = true;
      ++j;
      while (digit(j)) ++j;
    }
    int64_t unit;
    if (!inTime && is(j, 'D')) unit = 86400;
    else if (inTime && is(j, 'H')) unit = 3600;
    else if (inTime && is(j, 'M')) unit = 60;
    else if (inTime && is(j, 'S')) unit = 1;
    else return std::nullopt;
    if (fraction && unit != 1) return std::nullopt;
    totalSeconds += value * unit;
    (inTime ? anyTime : anyDate) = true;
    i = j + 1;
  }
  if (inTime ? !anyTime : !anyDate) return std::nullopt;  // "P", "PT", "P1DT"
  char hms[48];
  std::snprintf(hms, sizeof hms, "%lld:%02lld:%02lld", static_cast<long long>(totalSeconds / 3600),
                static_cast<long long>(totalSeconds / 60 % 60), static_cast<long long>(totalSeconds % 60));
  return std::string(hms);
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
    else if (a == "--timeout") {
      std::string text = next();
      timeout = cliTimeoutToHms(text);
      if (!timeout) {
        std::fprintf(stderr, "invalid --timeout '%s': use an ISO-8601 duration (PT10M) or HH:MM:SS\n",
                     text.c_str());
        return 2;
      }
    }
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
      fr::applyRouterSetting(s, property, value);
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
  std::fflush(stdout);  // the summary reaches a pipe reader (the KiCad plugin) before the exit
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
  // Server modes: the routing jobs run on the job manager's thread, each in a job region of its own.
  bool mcp = false, api = false;
  frserver::RestOptions rest;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--mcp") mcp = true;
    else if (a == "--api") api = true;
    else if (a == "--port" && i + 1 < argc) rest.port = std::atoi(argv[++i]);
    else if (a == "--host" && i + 1 < argc) rest.host = argv[++i];
    else if (a == "-q") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_WARN;
    else if (a == "-v") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_DEBUG;
    else if (a == "-vv") fr::FRLogger::logLevel = fr::FRLogger::LEVEL_TRACE;
  }
  if (mcp && api) {
    std::fprintf(stderr, "--mcp and --api are separate modes (the REST server also serves MCP at /v1/mcp)\n");
    return 2;
  }
  if (mcp || api) {
    int code = mcp ? frserver::McpServer().runStdio()  // the log goes to stderr
                   : frserver::runRestServer(rest);
    frserver::JobManager::instance().shutdown();
    return code;
  }
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
