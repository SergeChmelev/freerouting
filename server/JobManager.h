// Routing jobs of the API and MCP servers (PORT EXTENSION; Java: core.RoutingJob +
// RoutingJobScheduler as used by api.v1.JobControllerV1).
//
// Threading model
//   * One routing thread runs the jobs, one at a time, in the order they were started (the core
//     keeps process-wide state, like the Lua engine host in bindings/lua). Each job runs in its own
//     jrt::JobRegion, so the memory of a finished job is given back before the next one starts.
//   * Everything a client can read (state, progress, logs, the SES, the DRC summary) is plain data
//     copied out of the core under the manager's mutex: request threads never touch core objects.
//   * The SES of a job is the one the command line writes for the same input and settings: the
//     request is built the same way (main.cpp), so the output is byte-identical.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "jrt/job_thread.h"

namespace frserver {

/** Java core.RoutingJobState. */
enum class JobState { INVALID, QUEUED, READY_TO_START, RUNNING, PAUSED, COMPLETED, TIMED_OUT, STOPPING, CANCELLED, TERMINATED };
const char* jobStateName(JobState s);
bool isFinished(JobState s);

/** Router settings of a job: the command line's options, then "--router.<path>=<value>" pairs. */
struct JobSettings {
  std::optional<int32_t> maxPasses;       // -mp
  std::optional<int32_t> maxThreads;      // -mt
  std::optional<std::string> timeout;     // --timeout (engine syntax "HH:MM:SS")
  bool optimizer = true;                  // --no-optimizer
  bool drc = true;                        // --no-drc
  std::vector<std::pair<std::string, std::string>> paths;  // --router.<path>=<value>, in order

  bool empty() const { return !maxPasses && !maxThreads && !timeout && optimizer && paths.empty(); }
};

/** A file given to a job: its content and its file name. */
struct JobFile {
  std::string filename;
  std::string data;
  std::optional<int64_t> crc32;  // computed when stored
};

/** Board figures shown with the input and the output of a job (Java BoardFileDetails.statistics). */
struct BoardFigures {
  std::optional<std::string> host;
  std::optional<int32_t> layerCount, componentCount, netclassCount, totalNetCount, unroutedNetCount, routedNetCount, traceCount,
      clearanceViolationCount, viaCount, incompleteConnections;
  std::optional<double> routedNetLengthMm;
};

/** One DRC summary violation (Java drc.DrcSummaryResponse.DiagnosticViolation). */
struct DrcViolation {
  std::string type, severity, layer, explanation;
  std::optional<double> expectedClearanceMm, actualClearanceMm, shortfallMm;
  std::vector<std::string> items;
};
struct DrcZone {
  double centerXmm = 0, centerYmm = 0, radiusMm = 0;
  int32_t violationCount = 0;
  std::string description;
};
/** Java drc.DrcSummaryResponse, copied out of the core. */
struct DrcSummary {
  int32_t clearanceViolationsCount = 0;
  int32_t unconnectedNetsCount = 0;
  std::vector<DrcViolation> violations;
  std::vector<DrcZone> congestionZones;
  std::vector<std::string> hints;
};

struct LogLine {
  int64_t timeMs;
  int32_t level;  // FRLogger::LEVEL_*
  std::string text;
};

/** Everything a client can know about a job (a copy). */
struct JobInfo {
  std::string id, sessionId, name, priority = "NORMAL";
  JobState state = JobState::INVALID;
  std::string stage = "IDLE";
  int64_t createdMs = 0, startedMs = 0, finishedMs = 0;
  int32_t currentPass = 0;
  std::optional<int32_t> unrouted;    // unrouted connections (latest)
  std::optional<int32_t> violations;  // clearance violations (latest)
  std::string message;                // latest progress message
  std::optional<std::string> error;   // failure / cancel / timeout message
  JobSettings settings;
  std::string settingsJson;  // the settings as the client posted them (merged), "{}" if none
  std::optional<JobFile> input, rules;
  std::optional<std::string> kicadDesignRules, kicadProject, kicadBoard;  // paths
  std::optional<BoardFigures> inputFigures, outputFigures;
  bool hasOutput = false;
  int64_t outputSize = 0, outputCrc32 = 0;
  int32_t outputVersion = 0;
  bool hasDrcSummary = false;
  bool wantDrcSummary = true;
};

class JobManager {
 public:
  JobManager();
  ~JobManager();  // cancels the running job and joins the routing thread
  JobManager(const JobManager&) = delete;
  JobManager& operator=(const JobManager&) = delete;

  /** The manager of the process (created on first use). */
  static JobManager& instance();

  /**
   * Cancels the running and the waiting jobs and joins the routing thread (before the process
   * exits: the core's static objects must outlive the job). Idempotent.
   */
  void shutdown();

  // --- sessions (Java SessionManager): a session groups jobs and their logs.
  std::string createSession(const std::string& host);
  bool sessionExists(const std::string& id);
  std::vector<std::string> sessions();
  std::string sessionHost(const std::string& id);

  // --- jobs
  /** Creates a QUEUED job (no input yet). */
  std::string enqueue(const std::string& sessionId, const std::string& name, const std::string& priority = "NORMAL");
  std::optional<JobInfo> info(const std::string& jobId);
  std::vector<JobInfo> list(const std::optional<std::string>& sessionId);
  /** The functions below return "" on success, else an error message (unknown job, wrong state). */
  std::string setInput(const std::string& jobId, JobFile file);
  std::string setRules(const std::string& jobId, JobFile file);
  std::string setKiCadFiles(const std::string& jobId, std::optional<std::string> dru, std::optional<std::string> pro,
                            std::optional<std::string> pcb);
  std::string setSettings(const std::string& jobId, JobSettings settings, const std::string& settingsJson);
  std::string setWantDrcSummary(const std::string& jobId, bool want);
  std::string setName(const std::string& jobId, const std::string& name);
  /** QUEUED -> READY_TO_START: the routing thread takes it when the jobs before it are done. */
  std::string start(const std::string& jobId);
  /** Cancels a queued or running job (a finished job is left as it is). */
  std::string cancel(const std::string& jobId);

  /** The SES of a finished job (or nullopt). */
  std::optional<std::string> output(const std::string& jobId);
  std::optional<DrcSummary> drcSummary(const std::string& jobId);
  /** Log lines of a job from index `from` on; returns the index after the last one. */
  size_t logs(const std::string& jobId, size_t from, std::vector<LogLine>& out);
  /** Log lines of all jobs of a session. */
  std::vector<LogLine> sessionLogs(const std::string& sessionId);

  /**
   * Waits until the job is finished, `timeoutMs` passed (-1: no limit) or `stop` returns true;
   * `onChange` is called (without the lock) whenever the progress of the job changed.
   * Returns the job state at the end.
   */
  std::optional<JobInfo> wait(const std::string& jobId, int64_t timeoutMs,
                              const std::function<void(const JobInfo&)>& onChange = nullptr,
                              const std::function<bool()>& stop = nullptr);

  /** Counts for the status endpoint. */
  int32_t jobCount();
  int32_t runningCount();
  int32_t queuedCount();

  static int64_t nowMs();
  static std::string newUuid();
  static uint32_t crc32(const std::string& data);

 private:
  struct Job {
    JobInfo info;
    std::atomic<bool> cancelRequested{false};
    std::string ses;
    std::optional<DrcSummary> drc;
    std::vector<LogLine> logLines;
    uint64_t version = 0;  // incremented on each change (waiters)
  };
  struct Session {
    std::string id, host;
    int64_t createdMs = 0;
  };

  void threadMain();
  void runJob(const std::shared_ptr<Job>& job);
  void touch(Job& job);  // under mutex_: a change of the job
  std::shared_ptr<Job> find(const std::string& jobId);  // under mutex_
  void appendLog(int32_t level, const std::string& text);

  std::mutex mutex_;
  std::condition_variable cv_;       // the routing thread waits for started jobs
  std::condition_variable changed_;  // waiters of job changes
  std::map<std::string, std::shared_ptr<Job>> jobs_;
  std::vector<std::string> jobOrder_;
  std::map<std::string, Session> sessions_;
  std::vector<std::string> sessionOrder_;
  std::deque<std::string> ready_;
  std::shared_ptr<Job> running_;
  bool quit_ = false;
  jrt::JobThread thread_;
  static constexpr size_t kMaxLogLines = 20000;
};

}  // namespace frserver
