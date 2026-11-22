#include "core/results/RoutingResultManifest.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "board/facade/RoutingBoard.h"
#include "core/RouterJobResourceUsage.h"
#include "core/RoutingJob.h"
#include "core/RoutingJobState.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsBounds.h"
#include "settings/RouterSettings.h"

namespace fr {

namespace {

// PORT NOTE: Constants.FREEROUTING_VERSION (build-generated in Java) -> the ported version.
const char* const FREEROUTING_VERSION = "2.5.1-SNAPSHOT";

// Instant.now().toString(): ISO-8601 UTC with millisecond precision (Java omits a zero fraction).
std::string instantNowString() {
  int64_t ms = jrt::System::currentTimeMillis();
  std::time_t secs = static_cast<std::time_t>(ms / 1000);
  int32_t millis = static_cast<int32_t>(ms % 1000);
  std::tm tm{};
  gmtime_r(&secs, &tm);
  char buf[64];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  std::string s(buf);
  if (millis != 0) {
    s += jrt::format(".%03d", millis);
  }
  return s + "Z";
}

// Minimal SHA-256 (FIPS 180-4) for MessageDigest.getInstance("SHA-256").
class Sha256 {
 public:
  Sha256() {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (int i = 0; i < 8; i++) h_[i] = init[i];
  }
  void update(const uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; i++) {
      buf_[bufLen_++] = data[i];
      if (bufLen_ == 64) {
        block(buf_);
        bufLen_ = 0;
      }
    }
    total_ += len;
  }
  std::string hexDigest() {
    uint64_t bitLen = total_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (bufLen_ != 56) update(&zero, 1);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; i++) lenBytes[i] = static_cast<uint8_t>(bitLen >> (56 - 8 * i));
    update(lenBytes, 8);
    std::string out;
    for (int i = 0; i < 8; i++) out += jrt::format("%08x", h_[i]);
    return out;
  }

 private:
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void block(const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(p[4 * i + 2]) << 8) | static_cast<uint32_t>(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6],
             h = h_[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = h + S1 + ch + k[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
  }
  uint32_t h_[8];
  uint8_t buf_[64];
  std::size_t bufLen_ = 0;
  uint64_t total_ = 0;
};

bool isBlank(const std::optional<std::string>& s) {
  return !s.has_value() || jrt::strip(*s).empty();
}

// ---- simplified JSON writer (PORT NOTE: replaces GsonProvider.GSON.toJson) ----
std::string jsonString(const std::string& s) {
  std::string r = "\"";
  for (char c : s) {
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          r += jrt::format("\\u%04x", static_cast<unsigned>(c));
        } else {
          r += c;
        }
    }
  }
  return r + "\"";
}

std::string jsonOptString(const std::optional<std::string>& s) {
  return s.has_value() ? jsonString(*s) : "null";
}

template <class T>
std::string jsonOpt(const std::optional<T>& v) {
  return v.has_value() ? jrt::str(*v) : "null";
}

// Object toString() embedded verbatim when it already is JSON, otherwise as a JSON string.
std::string jsonObject(jrt::Object* o) {
  if (o == nullptr) return "null";
  std::string s = o->toString();
  std::string t = jrt::trim(s);
  if (!t.empty() && t[0] == '{') return s;
  return jsonString(s);
}

class JsonObjectWriter {
 public:
  explicit JsonObjectWriter(int indent) : indent_(indent) {}
  void field(const std::string& name, const std::string& rawValue) {
    if (!first_) out_ += ",";
    out_ += "\n" + std::string(indent_ + 2, ' ') + jsonString(name) + ": " + rawValue;
    first_ = false;
  }
  std::string finish() {
    if (first_) return "{}";
    return "{" + out_ + "\n" + std::string(indent_, ' ') + "}";
  }

 private:
  int indent_;
  bool first_ = true;
  std::string out_;
};

std::string snapshotJson(RoutingResultManifest_PhaseSnapshot* s, int indent) {
  if (s == nullptr) return "null";
  JsonObjectWriter w(indent);
  w.field("board_statistics", jsonObject(s->boardStatistics));
  w.field("score", jsonOpt(s->score));
  w.field("router_score", jsonOpt(s->routerScore));
  w.field("optimizer_score", jsonOpt(s->optimizerScore));
  w.field("score_source", jsonOptString(s->scoreSource));
  w.field("current_router_score", jsonOpt(s->currentRouterScore));
  w.field("current_optimizer_score", jsonOpt(s->currentOptimizerScore));
  w.field("current_score_source", jsonOptString(s->currentScoreSource));
  return w.finish();
}

std::string detailJson(RoutingResultManifest_PhaseDetail* d, int indent) {
  if (d == nullptr) return "null";
  JsonObjectWriter w(indent);
  w.field("before", snapshotJson(d->before, indent + 2));
  w.field("after", snapshotJson(d->after, indent + 2));
  w.field("duration_seconds", jsonOpt(d->durationSeconds));
  w.field("cpu_seconds", jsonOpt(d->cpuSeconds));
  w.field("passes_completed", jsonOpt(d->passesCompleted));
  w.field("total_allocated_gb", jsonOpt(d->totalAllocatedGb));
  w.field("peak_heap_mb", jsonOpt(d->peakHeapMb));
  return w.finish();
}

std::string fallbackJson(RoutingResultManifest_FallbackDetail* f, int indent) {
  if (f == nullptr) return "null";
  JsonObjectWriter w(indent);
  w.field("chosen", jsonOptString(f->chosen));
  std::string attempts = "[";
  bool first = true;
  if (f->attempts != nullptr) {
    for (RoutingResultManifest_FallbackAttempt* a : *f->attempts) {
      if (!first) attempts += ",";
      first = false;
      JsonObjectWriter aw(indent + 4);
      aw.field("strategy", jsonOptString(a->strategy));
      aw.field("stop_reason", jsonOptString(a->stopReason));
      aw.field("unrouted", jrt::str(a->unrouted));
      aw.field("new_or_changed_violations", jrt::str(a->newOrChangedViolations));
      aw.field("clearance_violations", jrt::str(a->clearanceViolations));
      aw.field("router_score", jrt::str(a->routerScore));
      aw.field("duration_seconds", jrt::str(a->durationSeconds));
      attempts += "\n" + std::string(indent + 4, ' ') + aw.finish();
    }
  }
  attempts += first ? "]" : "\n" + std::string(indent + 2, ' ') + "]";
  w.field("attempts", attempts);
  return w.finish();
}

std::string phasesJson(RoutingResultManifest_PhaseMetrics* p, int indent) {
  if (p == nullptr) return "null";
  JsonObjectWriter w(indent);
  w.field("fanout", detailJson(p->fanout, indent + 2));
  w.field("autorouter", detailJson(p->autorouter, indent + 2));
  w.field("optimizer", detailJson(p->optimizer, indent + 2));
  if (p->fallback != nullptr) {
    w.field("fallback", fallbackJson(p->fallback, indent + 2));
  }
  return w.finish();
}

std::string settingsJson(RouterSettings* s, int indent) {
  if (s == nullptr) return "null";
  // PORT NOTE: abbreviated settings snapshot (top-level scalar fields only).
  JsonObjectWriter w(indent);
  w.field("copper_to_edge_clearance_um", jsonOpt(s->copperToEdgeClearanceUm));
  w.field("hole_clearance_um", jsonOpt(s->holeClearanceUm));
  w.field("hole_to_hole_um", jsonOpt(s->holeToHoleUm));
  w.field("clearance_tolerance_um", jsonOpt(s->clearanceToleranceUm));
  w.field("drc_epsilon_um", jsonOpt(s->drcEpsilonUm));
  w.field("plane_as_obstacle", jsonOpt(s->planeAsObstacle));
  w.field("neck_width_um", jsonOpt(s->neckWidthUm));
  w.field("exact_copper", jsonOpt(s->exactCopper));
  w.field("strict_drc", jsonOpt(s->strictDrc));
  w.field("job_timeout", jsonOptString(s->jobTimeoutString));
  w.field("trace_pull_tight_accuracy", jsonOpt(s->tracePullTightAccuracy));
  w.field("allowed_via_types", jsonOpt(s->viasAllowed));
  w.field("automatic_neckdown", jsonOpt(s->automaticNeckdown));
  w.field("max_threads", jsonOpt(s->maxThreads));
  w.field("result_json", jsonOptString(s->resultJsonPath));
  return w.finish();
}

std::string resourceUsageJson(RouterJobResourceUsage* r, int indent) {
  if (r == nullptr) return "null";
  JsonObjectWriter w(indent);
  w.field("cpu_time", jrt::str(r->cpuTimeUsed));
  w.field("max_memory", jrt::str(r->maxMemoryUsed));
  w.field("peak_memory", jrt::str(r->peakMemoryUsed));
  w.field("io_read", jrt::str(r->ioRead));
  w.field("io_written", jrt::str(r->ioWrite));
  return w.finish();
}

}  // namespace

RoutingResultManifest_PhaseSnapshot* RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
    BoardStatistics* statistics, RouterSettings* settings,
    const std::optional<std::string>& scoreSource) {
  auto* snapshot = new (jrt::kBoard) RoutingResultManifest_PhaseSnapshot();
  snapshot->boardStatistics = statistics;
  snapshot->scoreSource = scoreSource;
  if (statistics != nullptr && settings != nullptr) {
    snapshot->routerScore = statistics->getRouterScore(settings);
    snapshot->optimizerScore = statistics->getOptimizerScore(settings);
  }
  return snapshot;
}

RoutingResultManifest* RoutingResultManifest::fromJob(
    RoutingJob* job, const std::optional<std::string>& inputFilePath, bool outputWritten,
    int32_t exitCode) {
  return fromJob(job, inputFilePath, outputWritten, exitCode, std::nullopt);
}

RoutingResultManifest* RoutingResultManifest::fromJob(
    RoutingJob* job, const std::optional<std::string>& inputFilePath, bool outputWritten,
    int32_t exitCode, std::optional<int32_t> cpuScore) {
  auto* manifest = new (jrt::kBoard) RoutingResultManifest();
  manifest->generatedAt = instantNowString();
  manifest->appVersion = std::string(FREEROUTING_VERSION);
  manifest->gitSha = resolveGitSha();
  manifest->fixture = new (jrt::kBoard) RoutingResultManifest_FixtureInfo();
  if (inputFilePath.has_value()) {
    manifest->fixture->filename = std::filesystem::path(*inputFilePath).filename().string();
    manifest->fixture->sha256 = sha256Hex(*inputFilePath);
  }
  manifest->settingsSnapshot = job->routerSettings;
  if (job->resultPhaseMetrics != nullptr) {
    manifest->phases = job->resultPhaseMetrics;
  }
  manifest->finalState = RoutingJobState_name(job->state);
  manifest->exitCode = exitCode;
  manifest->outputWritten = outputWritten;
  manifest->resourceUsage = job->resourceUsage;
  manifest->cpuScore = cpuScore;

  if (job->board != nullptr) {
    manifest->boardStatistics = new (jrt::kOp) BoardStatistics(static_cast<BasicBoard*>(job->board));
    manifest->bounds = manifest->boardStatistics->bounds;
    if (job->routerSettings != nullptr && job->routerSettings->scoring != nullptr) {
      manifest->normalizedScore = manifest->boardStatistics->getRouterScore(job->routerSettings);
      if (manifest->phases->optimizer->before != nullptr ||
          manifest->phases->optimizer->after != nullptr) {
        manifest->optimizerScore =
            manifest->boardStatistics->getOptimizerScore(job->routerSettings);
      }
    }
  }

  if (job->getCurrentPass() > 0 && !manifest->phases->autorouter->passesCompleted.has_value()) {
    manifest->phases->autorouter->passesCompleted = job->getCurrentPass();
  }

  if (!manifest->phases->autorouter->durationSeconds.has_value() && job->startedAt.has_value() &&
      job->finishedAt.has_value()) {
    float totalSeconds = static_cast<float>((*job->finishedAt - *job->startedAt) / 1000.0);
    manifest->phases->autorouter->durationSeconds = totalSeconds;
  }

  return manifest;
}

void RoutingResultManifest::write(const std::string& targetPath,
                                  RoutingResultManifest* manifest) {
  std::filesystem::path target(targetPath);
  if (target.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
      throw jrt::IOException(target.parent_path().string() + ": " + ec.message());
    }
  }
  JsonObjectWriter w(0);
  w.field("schema_version", jrt::str(manifest->schemaVersion));
  w.field("generated_at", jsonOptString(manifest->generatedAt));
  w.field("app_version", jsonOptString(manifest->appVersion));
  w.field("git_sha", jsonOptString(manifest->gitSha));
  if (manifest->fixture != nullptr) {
    JsonObjectWriter fw(2);
    fw.field("filename", jsonOptString(manifest->fixture->filename));
    fw.field("sha256", jsonOptString(manifest->fixture->sha256));
    w.field("fixture", fw.finish());
  }
  w.field("settings_snapshot", settingsJson(manifest->settingsSnapshot, 2));
  w.field("phases", phasesJson(manifest->phases, 2));
  w.field("board_statistics", jsonObject(manifest->boardStatistics));
  w.field("bounds", jsonObject(manifest->bounds));
  w.field("normalized_score", jsonOpt(manifest->normalizedScore));
  w.field("optimizer_score", jsonOpt(manifest->optimizerScore));
  w.field("resource_usage", resourceUsageJson(manifest->resourceUsage, 2));
  w.field("final_state", jsonOptString(manifest->finalState));
  w.field("exit_code", jrt::str(manifest->exitCode));
  w.field("output_written", jrt::str(manifest->outputWritten));
  w.field("cpu_score", jsonOpt(manifest->cpuScore));
  std::string json = w.finish();

  std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw jrt::IOException(targetPath);
  }
  out << json;
  if (!out) {
    throw jrt::IOException(targetPath);
  }
}

std::string RoutingResultManifest::resolveGitSha() {
  std::optional<std::string> env = jrt::System::getenv("FREEROUTING_GIT_SHA");
  if (!isBlank(env)) {
    return jrt::trim(*env);
  }
  std::optional<std::string> prop = jrt::System::getProperty("freerouting.git.sha");
  if (!isBlank(prop)) {
    return jrt::trim(*prop);
  }
  std::optional<std::string> legacyProp = jrt::System::getProperty("FREEROUTING_GIT_SHA");
  if (!isBlank(legacyProp)) {
    return jrt::trim(*legacyProp);
  }
  return "unknown";
}

std::optional<std::string> RoutingResultManifest::sha256Hex(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    return std::nullopt;
  }
  Sha256 digest;
  digest.update(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  return digest.hexDigest();
}

}  // namespace fr
