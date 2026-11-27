#include "JobManager.h"

#include <chrono>
#include <cstring>
#include <random>

#include "autoroute/PerformanceProfiler.h"
#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "core/facade/CancellationToken.h"
#include "core/facade/FreeroutingHeadlessEngine.h"
#include "core/facade/HeadlessRoutingProgress.h"
#include "core/facade/HeadlessRoutingProgressListener.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "core/RoutingStage.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "core/scoring/BoardStatisticsComponents.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "core/scoring/BoardStatisticsLayers.h"
#include "core/scoring/BoardStatisticsNets.h"
#include "core/scoring/BoardStatisticsTraces.h"
#include "core/scoring/BoardStatisticsVias.h"
#include "drc/ClearanceViolation.h"
#include "drc/DesignRulesChecker.h"
#include "drc/DrcSummaryResponse.h"
#include "io/FileFormat.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettingPaths.h"
#include "settings/RouterSettings.h"

namespace frserver {

const char* jobStateName(JobState s) {
  switch (s) {
    case JobState::INVALID: return "INVALID";
    case JobState::QUEUED: return "QUEUED";
    case JobState::READY_TO_START: return "READY_TO_START";
    case JobState::RUNNING: return "RUNNING";
    case JobState::PAUSED: return "PAUSED";
    case JobState::COMPLETED: return "COMPLETED";
    case JobState::TIMED_OUT: return "TIMED_OUT";
    case JobState::STOPPING: return "STOPPING";
    case JobState::CANCELLED: return "CANCELLED";
    case JobState::TERMINATED: return "TERMINATED";
  }
  return "INVALID";
}

bool isFinished(JobState s) {
  return s == JobState::COMPLETED || s == JobState::TIMED_OUT || s == JobState::CANCELLED ||
         s == JobState::TERMINATED;
}

namespace {

// Java's CLI names the design after the input file without its extension (main.cpp designName).
std::string designName(const std::string& path) {
  auto pos = path.find_last_of("/\\");
  std::string b = pos == std::string::npos ? path : path.substr(pos + 1);
  auto dot = b.find_last_of('.');
  return dot == std::string::npos ? b : b.substr(0, dot);
}

jrt::Array<int8_t>* toBytes(const std::string& data) {
  auto* bytes = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(data.size()));
  if (!data.empty()) std::memcpy(bytes->data(), data.data(), data.size());
  return bytes;
}

BoardFigures figuresOf(const std::string& data, fr::io::FileFormat format) {
  BoardFigures f;
  auto* stats = new (jrt::kOp) fr::BoardStatistics(toBytes(data), format);
  f.host = stats->host;
  f.layerCount = stats->layers->totalCount;
  f.componentCount = stats->components->totalCount;
  f.netclassCount = stats->nets->classCount;
  f.totalNetCount = stats->nets->totalCount;
  f.viaCount = stats->vias->totalCount;
  f.traceCount = stats->traces->totalCount;
  return f;
}

DrcSummary copyDrcSummary(fr::DrcSummaryResponse* s) {
  DrcSummary d;
  d.clearanceViolationsCount = s->clearanceViolationsCount;
  d.unconnectedNetsCount = s->unconnectedNetsCount;
  for (fr::DrcSummaryResponse_DiagnosticViolation* v : *s->violations) {
    DrcViolation c{v->type, v->severity, v->layer, v->explanation, v->expectedClearanceMm, v->actualClearanceMm,
                   v->shortfallMm, {}};
    for (const std::string& item : *v->items) c.items.push_back(item);
    d.violations.push_back(std::move(c));
  }
  for (fr::DrcSummaryResponse_CongestionZone* z : *s->congestionZones) {
    d.congestionZones.push_back(DrcZone{z->centerXmm, z->centerYmm, z->radiusMm, z->violationCount, z->description});
  }
  for (const std::string& h : *s->hints) d.hints.push_back(h);
  return d;
}

}  // namespace

// ------------------------------------------------------------------------------- utilities --

int64_t JobManager::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string JobManager::newUuid() {
  static std::mutex m;
  static std::mt19937_64 rng{std::random_device{}() ^ static_cast<uint64_t>(nowMs())};
  uint64_t a, b;
  {
    std::lock_guard<std::mutex> lock(m);
    a = rng();
    b = rng();
  }
  a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version 4
  b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // RFC 4122 variant
  char buf[37];
  std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
  return buf;
}

uint32_t JobManager::crc32(const std::string& data) {
  static uint32_t table[256];
  static bool init = [] {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    return true;
  }();
  (void)init;
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : data) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// ----------------------------------------------------------------------------- lifecycle ----

JobManager::JobManager() {
  fr::FRLogger::sink = [this](int32_t level, const std::string& text) { appendLog(level, text); };
  thread_ = std::thread([this] { threadMain(); });
}

JobManager::~JobManager() { shutdown(); }

void JobManager::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    if (running_) running_->cancelRequested = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  fr::FRLogger::sink = nullptr;
}

JobManager& JobManager::instance() {
  static JobManager manager;
  return manager;
}

// ------------------------------------------------------------------------------ sessions ----

std::string JobManager::createSession(const std::string& host) {
  std::lock_guard<std::mutex> lock(mutex_);
  Session s{newUuid(), host, nowMs()};
  sessions_[s.id] = s;
  sessionOrder_.push_back(s.id);
  return s.id;
}

bool JobManager::sessionExists(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.count(id) != 0;
}

std::vector<std::string> JobManager::sessions() {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessionOrder_;
}

std::string JobManager::sessionHost(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(id);
  return it == sessions_.end() ? std::string() : it->second.host;
}

// ---------------------------------------------------------------------------------- jobs ----

std::shared_ptr<JobManager::Job> JobManager::find(const std::string& jobId) {
  auto it = jobs_.find(jobId);
  return it == jobs_.end() ? nullptr : it->second;
}

void JobManager::touch(Job& job) {
  ++job.version;
  changed_.notify_all();
}

std::string JobManager::enqueue(const std::string& sessionId, const std::string& name, const std::string& priority) {
  auto job = std::make_shared<Job>();
  job->info.id = newUuid();
  job->info.sessionId = sessionId;
  job->info.name = name;
  job->info.priority = priority.empty() ? "NORMAL" : priority;
  job->info.state = JobState::QUEUED;
  job->info.createdMs = nowMs();
  job->info.settingsJson = "{}";
  std::lock_guard<std::mutex> lock(mutex_);
  jobs_[job->info.id] = job;
  jobOrder_.push_back(job->info.id);
  return job->info.id;
}

std::optional<JobInfo> JobManager::info(const std::string& jobId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto job = find(jobId);
  if (!job) return std::nullopt;
  return job->info;
}

std::vector<JobInfo> JobManager::list(const std::optional<std::string>& sessionId) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<JobInfo> out;
  for (const std::string& id : jobOrder_) {
    const JobInfo& i = jobs_[id]->info;
    if (!sessionId || i.sessionId == *sessionId) out.push_back(i);
  }
  return out;
}

#define FR_JOB_OR_ERROR(jobId)                                     \
  std::lock_guard<std::mutex> lock(mutex_);                        \
  auto job = find(jobId);                                          \
  if (!job) return "Job '" + (jobId) + "' not found.";

#define FR_REQUIRE_QUEUED()                                                                      \
  if (job->info.state != JobState::QUEUED)                                                       \
    return std::string("The job is in state ") + jobStateName(job->info.state) +                 \
           "; it can only be changed while it is QUEUED.";

std::string JobManager::setInput(const std::string& jobId, JobFile file) {
  FR_JOB_OR_ERROR(jobId)
  FR_REQUIRE_QUEUED()
  file.crc32 = crc32(file.data);
  job->info.input = std::move(file);
  job->info.inputFigures.reset();
  touch(*job);
  return "";
}

std::string JobManager::setRules(const std::string& jobId, JobFile file) {
  FR_JOB_OR_ERROR(jobId)
  FR_REQUIRE_QUEUED()
  file.crc32 = crc32(file.data);
  job->info.rules = std::move(file);
  touch(*job);
  return "";
}

std::string JobManager::setKiCadFiles(const std::string& jobId, std::optional<std::string> dru,
                                      std::optional<std::string> pro, std::optional<std::string> pcb) {
  FR_JOB_OR_ERROR(jobId)
  FR_REQUIRE_QUEUED()
  job->info.kicadDesignRules = std::move(dru);
  job->info.kicadProject = std::move(pro);
  job->info.kicadBoard = std::move(pcb);
  touch(*job);
  return "";
}

std::string JobManager::setSettings(const std::string& jobId, JobSettings settings, const std::string& settingsJson) {
  FR_JOB_OR_ERROR(jobId)
  FR_REQUIRE_QUEUED()
  job->info.settings = std::move(settings);
  job->info.settingsJson = settingsJson;
  touch(*job);
  return "";
}

std::string JobManager::setWantDrcSummary(const std::string& jobId, bool want) {
  FR_JOB_OR_ERROR(jobId)
  job->info.wantDrcSummary = want;
  return "";
}

std::string JobManager::start(const std::string& jobId) {
  {
    FR_JOB_OR_ERROR(jobId)
    if (job->info.state != JobState::QUEUED)
      return std::string("The job is in state ") + jobStateName(job->info.state) + "; only a QUEUED job can be started.";
    if (!job->info.input) return "The job has no input: upload a design (DSN) first.";
    job->info.state = JobState::READY_TO_START;
    ready_.push_back(jobId);
    touch(*job);
  }
  cv_.notify_all();
  return "";
}

std::string JobManager::cancel(const std::string& jobId) {
  FR_JOB_OR_ERROR(jobId)
  JobState s = job->info.state;
  if (isFinished(s)) return "";
  job->cancelRequested = true;
  if (s == JobState::QUEUED || s == JobState::READY_TO_START) {
    for (auto it = ready_.begin(); it != ready_.end(); ++it) {
      if (*it == jobId) {
        ready_.erase(it);
        break;
      }
    }
    job->info.state = JobState::CANCELLED;
    job->info.finishedMs = nowMs();
    job->info.error = "Routing was cancelled";
  } else {
    job->info.state = JobState::STOPPING;
  }
  touch(*job);
  return "";
}

std::optional<std::string> JobManager::output(const std::string& jobId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto job = find(jobId);
  if (!job || !job->info.hasOutput) return std::nullopt;
  return job->ses;
}

std::optional<DrcSummary> JobManager::drcSummary(const std::string& jobId) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto job = find(jobId);
  if (!job) return std::nullopt;
  return job->drc;
}

size_t JobManager::logs(const std::string& jobId, size_t from, std::vector<LogLine>& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto job = find(jobId);
  if (!job) return from;
  for (size_t i = from; i < job->logLines.size(); ++i) out.push_back(job->logLines[i]);
  return job->logLines.size();
}

std::vector<LogLine> JobManager::sessionLogs(const std::string& sessionId) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LogLine> out;
  for (const std::string& id : jobOrder_) {
    const auto& job = jobs_[id];
    if (job->info.sessionId == sessionId) out.insert(out.end(), job->logLines.begin(), job->logLines.end());
  }
  return out;
}

std::optional<JobInfo> JobManager::wait(const std::string& jobId, int64_t timeoutMs,
                                        const std::function<void(const JobInfo&)>& onChange,
                                        const std::function<bool()>& stop) {
  int64_t deadline = timeoutMs < 0 ? INT64_MAX : nowMs() + timeoutMs;
  uint64_t seen = 0;
  for (;;) {
    JobInfo snapshot;
    bool changed = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto job = find(jobId);
      if (!job) return std::nullopt;
      changed_.wait_for(lock, std::chrono::milliseconds(250), [&] { return job->version != seen; });
      changed = job->version != seen;
      seen = job->version;
      snapshot = job->info;
    }
    if (isFinished(snapshot.state)) return snapshot;
    if (changed && onChange) onChange(snapshot);
    if (nowMs() >= deadline || (stop && stop())) return snapshot;
  }
}

int32_t JobManager::jobCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int32_t>(jobs_.size());
}

int32_t JobManager::runningCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_ ? 1 : 0;
}

int32_t JobManager::queuedCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int32_t>(ready_.size());
}

void JobManager::appendLog(int32_t level, const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  auto& lines = running_->logLines;
  if (lines.size() >= kMaxLogLines) return;  // a runaway trace log; the console still has it
  lines.push_back(LogLine{nowMs(), level, text});
  touch(*running_);
}

// ------------------------------------------------------------------------- routing thread ----

void JobManager::threadMain() {
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return quit_ || !ready_.empty(); });
      if (quit_) return;
      job = find(ready_.front());
      ready_.pop_front();
      if (!job || job->info.state != JobState::READY_TO_START) continue;
      running_ = job;
      job->info.state = JobState::RUNNING;
      job->info.stage = "ROUTING";
      job->info.startedMs = nowMs();
      job->info.message = "Routing " + job->info.name;
      touch(*job);
    }
    try {
      runJob(job);
    } catch (const jrt::Throwable& e) {
      std::lock_guard<std::mutex> lock(mutex_);
      job->info.state = JobState::TERMINATED;
      job->info.error = "Routing failed: " + e.getMessage();
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> lock(mutex_);
      job->info.state = JobState::TERMINATED;
      job->info.error = std::string("Routing failed: ") + e.what();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isFinished(job->info.state)) job->info.state = JobState::TERMINATED;
    job->info.finishedMs = nowMs();
    job->info.stage = "IDLE";
    running_ = nullptr;
    touch(*job);
  }
}

void JobManager::runJob(const std::shared_ptr<Job>& job) {
  JobInfo in;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    in = job->info;
  }
  // The memory of this job (MEMORY_MODEL.md, E9): geometry and temporaries that no owner holds
  // are freed when the region ends, after the results were copied out.
  jrt::JobRegion region;

  BoardFigures inputFigures = figuresOf(in.input->data, fr::io::FileFormat::DSN);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    job->info.inputFigures = inputFigures;
    touch(*job);
  }

  // The request exactly as the command line builds it (main.cpp runMain), so that the SES is the
  // same byte for byte.
  const JobSettings& o = in.settings;
  auto* builder = fr::HeadlessRoutingRequest::builder()
                      ->dsnBytes(toBytes(in.input->data))
                      ->designName(designName(in.input->filename))
                      ->runDrc(o.drc);
  if (in.rules) {
    builder->rulesBytes(toBytes(in.rules->data))->rulesName(designName(in.rules->filename));
  }
  builder->kicadDesignRulesFile(in.kicadDesignRules)->kicadProjectFile(in.kicadProject)->kicadBoardFile(in.kicadBoard);
  if (!o.empty()) {
    auto* s = new (jrt::kBoard) fr::RouterSettings();
    if (o.maxPasses) s->setMaxPasses(o.maxPasses);
    if (o.maxThreads) s->setMaxThreads(o.maxThreads);
    if (o.timeout) s->setJobTimeoutString(o.timeout);
    if (!o.optimizer) s->setOptimizerEnabled(false);
    for (const auto& [property, value] : o.paths) fr::applyRouterSetting(s, property, value);
    builder->settings(s);
  }
  Job* raw = job.get();
  builder->cancellationToken(fr::CancellationToken::of([raw] { return raw->cancelRequested.load(); }));
  builder->progressListener(fr::HeadlessRoutingProgressListener::of([this, raw](fr::HeadlessRoutingProgress* p) {
    std::optional<int32_t> violations;
    if (p->statistics() != nullptr && p->statistics()->clearanceViolations != nullptr)
      violations = p->statistics()->clearanceViolations->totalCount;
    std::lock_guard<std::mutex> lock(mutex_);
    raw->info.stage = fr::RoutingStage_toString(p->stage());
    if (raw->info.stage == "IDLE") raw->info.stage = "ROUTING";
    if (p->passNumber() > 0) raw->info.currentPass = p->passNumber();
    if (p->unroutedCount() >= 0) raw->info.unrouted = p->unroutedCount();
    if (violations) raw->info.violations = violations;
    raw->info.message = p->message();
    touch(*raw);
  }));

  fr::HeadlessRoutingResult* result = fr::FreeroutingHeadlessEngine::route(builder->build());

  std::string ses = result->sesContent();
  int32_t violations = result->clearanceViolations() ? result->clearanceViolations()->size() : 0;
  JobState state;
  if (result->isCancelled()) {
    state = JobState::CANCELLED;
  } else if (result->errorMessage() && *result->errorMessage() == "Routing timed out") {
    state = JobState::TIMED_OUT;
  } else if (result->errorMessage()) {
    state = JobState::TERMINATED;
  } else {
    state = JobState::COMPLETED;
  }
  // Like the command line, the SES of a failed or timed out run is kept (it may be empty).
  bool hasOutput = !result->isCancelled() || !ses.empty();
  std::optional<BoardFigures> outputFigures;
  if (hasOutput) {
    outputFigures = figuresOf(ses, fr::io::FileFormat::SES);
    outputFigures->incompleteConnections = result->unroutedConnections();
    outputFigures->clearanceViolationCount = violations;
    if (result->statistics() != nullptr) {
      fr::BoardStatistics* st = result->statistics();
      if (st->traces != nullptr && st->traces->totalLengthMm) outputFigures->routedNetLengthMm = *st->traces->totalLengthMm;
      if (st->vias != nullptr && st->vias->totalCount) outputFigures->viaCount = st->vias->totalCount;
      if (st->layers != nullptr && st->layers->totalCount) outputFigures->layerCount = st->layers->totalCount;
      if (st->components != nullptr && st->components->totalCount) outputFigures->componentCount = st->components->totalCount;
      if (st->nets != nullptr) {
        if (st->nets->totalCount) outputFigures->totalNetCount = st->nets->totalCount;
        if (st->nets->classCount) outputFigures->netclassCount = st->nets->classCount;
      }
    }
  }
  // The DRC summary of the routed board, after the SES was written (it cannot change the SES).
  std::optional<DrcSummary> drc;
  if (in.wantDrcSummary && result->board() != nullptr && !result->isCancelled()) {
    try {
      auto* checker = new (jrt::kOp) fr::DesignRulesChecker(result->board(), nullptr);
      drc = copyDrcSummary(checker->generateSummary());
      jrt::Set<int32_t>* incompleteNets = checker->incompleteNetNumbers();
      if (outputFigures && incompleteNets != nullptr) {
        outputFigures->unroutedNetCount = incompleteNets->size();
        if (outputFigures->totalNetCount)
          outputFigures->routedNetCount = *outputFigures->totalNetCount - incompleteNets->size();
      }
    } catch (const jrt::Throwable& e) {
      fr::FRLogger::warn("DRC summary failed: " + e.getMessage());
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    JobInfo& i = job->info;
    i.state = state;
    if (result->errorMessage()) i.error = *result->errorMessage();
    i.unrouted = result->unroutedConnections();
    i.violations = violations;
    i.hasOutput = hasOutput;
    if (hasOutput) {
      job->ses = std::move(ses);
      i.outputSize = static_cast<int64_t>(job->ses.size());
      i.outputCrc32 = crc32(job->ses);
      i.outputVersion++;
      i.outputFigures = outputFigures;
    }
    if (drc) {
      job->drc = std::move(drc);
      i.hasDrcSummary = true;
    }
    i.message = state == JobState::COMPLETED
                    ? "Routing finished: " + std::to_string(result->unroutedConnections()) + " unrouted, " +
                          std::to_string(violations) + " clearance violations"
                    : (result->errorMessage() ? *result->errorMessage() : std::string("Routing stopped"));
    touch(*job);
  }
  // Stage E: the run is over; the routed board is the last board of the run.
  if (result->board() != nullptr) fr::BasicBoard::discardLast(result->board());
  jrt::dispose(result);
  // Process-wide statistics of the profiler refer to this job's memory (kOp): the next job
  // starts without them (the autorouter resets them only when it runs to its end).
  fr::PerformanceProfiler::reset();
}

}  // namespace frserver

namespace frserver {

std::string JobManager::setName(const std::string& jobId, const std::string& name) {
  FR_JOB_OR_ERROR(jobId)
  job->info.name = name;
  touch(*job);
  return "";
}

}  // namespace frserver
