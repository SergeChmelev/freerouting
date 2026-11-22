#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

// PORT NOTE (whole file): gson @SerializedName dropped (JSON names in comments). JSON output is
// produced by a simplified hand-written writer in write(); java.nio Path -> std::string,
// Instant.now().toString() -> ISO-8601 UTC text.

/** Board metrics and calculated scores at a phase boundary. */
class RoutingResultManifest_PhaseSnapshot : public virtual jrt::Object {
 public:
  // "board_statistics"
  BoardStatistics* boardStatistics = nullptr;
  // "score"
  std::optional<float> score;
  // "router_score"
  std::optional<float> routerScore;
  // "optimizer_score"
  std::optional<float> optimizerScore;
  // "score_source"
  std::optional<std::string> scoreSource;
  // "current_router_score"
  std::optional<float> currentRouterScore;
  // "current_optimizer_score"
  std::optional<float> currentOptimizerScore;
  // "current_score_source"
  std::optional<std::string> currentScoreSource;

  /** Snapshot of the statistics; router/optimizer scores computed when both args non-null. */
  static RoutingResultManifest_PhaseSnapshot* fromBoardStatistics(
      BoardStatistics* statistics, RouterSettings* settings,
      const std::optional<std::string>& scoreSource);
};

/** Duration and pass count for one routing stage. */
class RoutingResultManifest_PhaseDetail : public virtual jrt::Object {
 public:
  // "before"
  RoutingResultManifest_PhaseSnapshot* before = nullptr;
  // "after"
  RoutingResultManifest_PhaseSnapshot* after = nullptr;
  // "duration_seconds"
  std::optional<float> durationSeconds;
  // "cpu_seconds"
  std::optional<float> cpuSeconds;
  // "passes_completed"
  std::optional<int32_t> passesCompleted;
  // "total_allocated_gb"
  std::optional<float> totalAllocatedGb;
  // "peak_heap_mb"
  std::optional<float> peakHeapMb;
};

/** One routing attempt of the fallback. */
class RoutingResultManifest_FallbackAttempt : public virtual jrt::Object {
 public:
  // "strategy"
  std::optional<std::string> strategy;
  // "stop_reason"
  std::optional<std::string> stopReason;
  // "unrouted"
  int32_t unrouted = 0;
  // "new_or_changed_violations"
  int32_t newOrChangedViolations = 0;
  // "clearance_violations"
  int32_t clearanceViolations = 0;
  // "router_score"
  float routerScore = 0.0f;
  // "duration_seconds"
  float durationSeconds = 0.0f;
};

/** Routing fallback attempts (default first) and final choice. */
class RoutingResultManifest_FallbackDetail : public virtual jrt::Object {
 public:
  // "chosen"
  std::optional<std::string> chosen;
  // "attempts"
  jrt::List<RoutingResultManifest_FallbackAttempt*>* attempts =
      new (jrt::kBoard) jrt::ArrayList<RoutingResultManifest_FallbackAttempt*>();
};

/** Per-stage duration and pass counts. */
class RoutingResultManifest_PhaseMetrics : public virtual jrt::Object {
 public:
  // "fanout"
  RoutingResultManifest_PhaseDetail* fanout = new (jrt::kBoard) RoutingResultManifest_PhaseDetail();
  // "autorouter"
  RoutingResultManifest_PhaseDetail* autorouter = new (jrt::kBoard) RoutingResultManifest_PhaseDetail();
  // "optimizer"
  RoutingResultManifest_PhaseDetail* optimizer = new (jrt::kBoard) RoutingResultManifest_PhaseDetail();
  // "fallback"
  RoutingResultManifest_FallbackDetail* fallback = nullptr;
};

/** Input fixture identity for the run. */
class RoutingResultManifest_FixtureInfo : public virtual jrt::Object {
 public:
  // "filename"
  std::optional<std::string> filename;
  // "sha256"
  std::optional<std::string> sha256;
};

/** Machine-readable summary of a headless CLI routing run for benchmark and autopilot harnesses. */
class RoutingResultManifest final : public virtual jrt::Object {
 public:
  static constexpr int32_t SCHEMA_VERSION = 1;

  /** Creates an empty manifest. */
  RoutingResultManifest() = default;

  // "schema_version"
  int32_t schemaVersion = SCHEMA_VERSION;
  // "generated_at"
  std::optional<std::string> generatedAt;
  // "app_version"
  std::optional<std::string> appVersion;
  // "git_sha"
  std::optional<std::string> gitSha;
  // "fixture"
  RoutingResultManifest_FixtureInfo* fixture = nullptr;
  // "settings_snapshot"
  RouterSettings* settingsSnapshot = nullptr;
  // "phases"
  RoutingResultManifest_PhaseMetrics* phases = new (jrt::kBoard) RoutingResultManifest_PhaseMetrics();
  // "board_statistics"
  BoardStatistics* boardStatistics = nullptr;
  // "bounds"
  BoardStatisticsBounds* bounds = nullptr;
  // "normalized_score"
  std::optional<float> normalizedScore;
  // "optimizer_score"
  std::optional<float> optimizerScore;
  // "resource_usage"
  RouterJobResourceUsage* resourceUsage = nullptr;
  // "final_state"
  std::optional<std::string> finalState;
  // "exit_code"
  int32_t exitCode = 0;
  // "output_written"
  bool outputWritten = false;
  // "cpu_score"
  std::optional<int32_t> cpuScore;

  /** Builds a manifest from a completed routing job without a CPU score. */
  static RoutingResultManifest* fromJob(RoutingJob* job,
                                        const std::optional<std::string>& inputFilePath,
                                        bool outputWritten, int32_t exitCode);

  /**
   * Builds a manifest from a completed routing job.
   *
   * @param cpuScore single-thread CPU score of this process, or null if not measured
   */
  static RoutingResultManifest* fromJob(RoutingJob* job,
                                        const std::optional<std::string>& inputFilePath,
                                        bool outputWritten, int32_t exitCode,
                                        std::optional<int32_t> cpuScore);

  /** Writes the manifest as UTF-8 JSON to {@code targetPath} (creating parent directories). */
  // PORT NOTE: gson serialisation replaced by a simplified JSON writer (same top-level keys;
  // nested board statistics / settings may be abbreviated).
  static void write(const std::string& targetPath, RoutingResultManifest* manifest);

  /** Resolves the git SHA from FREEROUTING_GIT_SHA env / system properties, or "unknown". */
  static std::string resolveGitSha();

 private:
  /** SHA-256 of the file as lower-case hex, or null on error. */
  static std::optional<std::string> sha256Hex(const std::string& path);
};

}  // namespace fr
