// freerouting-cpp: command-line front end of the C++ port (headless DSN -> SES routing).
//
// usage: freerouting-cpp -de <input.dsn> -do <output.ses> [options]
//   -dr <file.rules>   apply a rules file
//   -mp <n>            maximum autorouter passes
//   -mt <n>            maximum threads (accepted for compatibility; the port is single-threaded)
//   --timeout <dur>    job timeout (Java Duration syntax, e.g. PT10M)
//   --no-optimizer     skip the optimization stage
//   --no-drc           skip the final design rules check
//   -q / -v / -vv      log level warn / debug / trace (default info)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "core/facade/FreeroutingHeadlessEngine.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "core/scoring/BoardStatistics.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace {

void usage() {
  std::fprintf(stderr,
               "usage: freerouting-cpp -de <input.dsn> -do <output.ses> [-dr <rules>] [-mp <passes>]\n"
               "                       [-mt <threads>] [--timeout <PTxxM>] [--no-optimizer] [--no-drc]\n"
               "                       [-q|-v|-vv]\n");
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

}  // namespace

int main(int argc, char** argv) {
  std::string input, output, rules;
  std::optional<int32_t> maxPasses, maxThreads;
  std::optional<std::string> timeout;
  bool optimizer = true, drc = true;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-de") input = next();
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
    else if (a == "-h" || a == "--help") {
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
    builder->rulesBytes(rulesBytes)->rulesName(baseName(rules));
  }
  if (maxPasses || maxThreads || timeout || !optimizer) {
    // Same role as Java's CLI/API settings source: only the given values override.
    auto* s = new (jrt::kBoard) fr::RouterSettings();
    if (maxPasses) s->setMaxPasses(maxPasses);
    if (maxThreads) s->setMaxThreads(maxThreads);
    if (timeout) s->setJobTimeoutString(timeout);
    if (!optimizer) s->setOptimizerEnabled(false);
    builder->settings(s);
  }

  int64_t start = jrt::System::currentTimeMillis();
  fr::HeadlessRoutingResult* result = fr::FreeroutingHeadlessEngine::route(builder->build());
  double seconds = (jrt::System::currentTimeMillis() - start) / 1000.0;

  if (result->errorMessage()) {
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
    std::fprintf(stderr, "memory: arena peak %.1f MB, GC heap %.1f MB, disposed %zu objects / %.1f MB\n",
                 jrt::Arena::peakBytes() / 1048576.0, jrt::gc_heap_size() / 1048576.0, jrt::disposed_count(),
                 jrt::disposed_bytes() / 1048576.0);
  }
  return result->isSuccess() ? 0 : 1;
}
