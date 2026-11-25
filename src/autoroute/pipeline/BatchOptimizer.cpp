// Port of app.freerouting.autoroute.pipeline.BatchOptimizer
#include "autoroute/pipeline/BatchOptimizer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "autoroute/ItemRouteResult.h"
#include "autoroute/ItemSelectionStrategy.h"
#include "autoroute/events/TaskStateChangedEvent.h"
#include "autoroute/pipeline/AutorouteRuntimeMetrics.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/NamedAlgorithmType.h"
#include "autoroute/pipeline/TaskState.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Unit.h"
#include "board/optimize/StepSlider.h"
#include "board/optimize/ViaNudger.h"
#include "board/state/Communication.h"
#include "core/ProgressThrottler.h"
#include "core/RouterCounters.h"
#include "core/RoutingJob.h"
#include "core/StoppableThread.h"
#include "core/results/RoutingResultManifest.h"
#include "core/scoring/BoardStatistics.h"
#include "datastructures/UndoableObjects.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "settings/OptimizerSettings.h"
#include "settings/RouterSettings.h"
#include "settings/sources/DefaultSettings.h"
#include "util/TextManager.h"

namespace fr {

namespace {

/**
 * Java "%(,.2f".formatted(Float): two decimals, grouping separator, negative values in
 * parentheses. A null Float prints "null". (Only used for a trace/log id string.)
 */
std::string formatParenGrouped2(std::optional<float> value) {
  if (!value.has_value()) {
    return "null";
  }
  double d = static_cast<double>(*value);
  if (std::isnan(d)) {
    return "NaN";
  }
  bool negative = std::signbit(d);
  double a = std::fabs(d);
  std::string body;
  if (std::isinf(a)) {
    body = "Infinity";
  } else {
    std::string s = jrt::format("%.2f", a);
    size_t dot = s.find('.');
    std::string intPart = dot == std::string::npos ? s : s.substr(0, dot);
    std::string frac = dot == std::string::npos ? std::string() : s.substr(dot);
    std::string grouped;
    int32_t n = static_cast<int32_t>(intPart.size());
    for (int32_t i = 0; i < n; i++) {
      grouped += intPart[i];
      int32_t remaining = n - 1 - i;
      if (remaining > 0 && remaining % 3 == 0) {
        grouped += ',';
      }
    }
    body = grouped + frac;
  }
  return negative ? "(" + body + ")" : body;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// BatchOptimizer
// ---------------------------------------------------------------------------------------------

BatchOptimizer::BatchOptimizer(RoutingJob* job)
    : NamedAlgorithm(job->thread, job->board, job->routerSettings),
      progressThrottler(new (jrt::kBoard) ProgressThrottler(1000)),
      resultMap(new (jrt::kOp) jrt::HashMap<int32_t, ItemRouteResult*>()),
      workerBoardStates(new (jrt::kOp) BatchOptimizer_WorkerBoardState()) {
  this->job = job;
}

BatchOptimizer* BatchOptimizer::create(RoutingJob* job) {
  BatchOptimizer* optimizer = new (jrt::kOp) BatchOptimizer(job);
  normalizeAlgorithm(job, optimizer);
  return optimizer;
}

BatchOptimizer* BatchOptimizer::createForHeadless(RoutingJob* job) { return create(job); }

BatchOptimizer* BatchOptimizer::createForGui(RoutingJob* job) { return create(job); }

void BatchOptimizer::normalizeAlgorithm(RoutingJob* job, BatchOptimizer* optimizer) {
  const std::optional<std::string>& algorithm = job->routerSettings->optimizer->algorithm;
  if (!(algorithm.has_value() && optimizer->getId() == algorithm.value())) {
    job->logWarning(
        "The algorithm '"
        + jrt::str(job->routerSettings->optimizer->algorithm)
        + "' is not supported by the batch autorouter. The default algorithm '"
        + optimizer->getId()
        + "' will be used instead.");
    job->routerSettings->optimizer->algorithm = optimizer->getId();
  }
}

bool BatchOptimizer::containsOnlyUnfixedTraces(jrt::Collection<Item*>* itemList) {
  for (Item* currentItem : *itemList) {
    if (currentItem->isUserFixed() || jrt::fast_cast<Trace>(currentItem) == nullptr) {
      return false;
    }
  }
  return true;
}

std::optional<std::string> BatchOptimizer::evaluatePreFlightGuards(BoardStatistics* stats) {
  if (this->settings->optimizer != nullptr
      && this->settings->optimizer->enablePreflightGuards.has_value()
      && this->settings->optimizer->enablePreflightGuards.value() == false) {
    return std::nullopt;
  }

  float initialScore = stats->getOptimizerScore(this->job->routerSettings);

  // Guard 2: Zero vias and optimal trace length
  if (stats->vias->totalCount.value() == 0) {
    if (initialScore >= 950.0f) {
      return jrt::format(
          "the board has no vias to eliminate and initial optimizer score (%.2f) is already >= 950.00",
          static_cast<double>(initialScore));
    }
    if (stats->bounds != nullptr
        && stats->bounds->minTraceLengthMm.has_value()
        && stats->bounds->minTraceLengthMm.value() > 0
        && stats->traces->totalLengthMm.has_value()
        && stats->traces->totalLengthMm.value() <= stats->bounds->minTraceLengthMm.value() * 1.05f) {
      return jrt::format(
          "the board has no vias to eliminate and trace length (%.2f mm) is within 5%% of theoretical minimum (%.2f mm)",
          static_cast<double>(stats->traces->totalLengthMm.value()),
          static_cast<double>(stats->bounds->minTraceLengthMm.value()));
    }
  }

  // Guard 3: Score ceiling / theoretical optimum
  if (initialScore >= 995.0f) {
    return jrt::format(
        "the initial optimizer score (%.2f) is already at or near theoretical maximum (995.00)",
        static_cast<double>(initialScore));
  }
  if (stats->bounds != nullptr
      && stats->bounds->minTraceLengthMm.has_value()
      && stats->bounds->minTraceLengthMm.value() > 0
      && stats->traces->totalLengthMm.has_value()
      && stats->traces->totalLengthMm.value() <= stats->bounds->minTraceLengthMm.value() * 1.02f
      && (!stats->bounds->minViaCount.has_value()
          || stats->vias->totalCount.value() <= stats->bounds->minViaCount.value())) {
    return jrt::format(
        "total trace length (%.2f mm) is already within 2%% of the theoretical minimum (%.2f mm)",
        static_cast<double>(stats->traces->totalLengthMm.value()),
        static_cast<double>(stats->bounds->minTraceLengthMm.value()));
  }

  // Guard 4: All vias mandatory layer transitions, and nothing left to shorten. Vias that cannot
  // be removed say nothing about the traces: the optimizer also removes detours and loops.
  if (this->board != nullptr
      && stats->bounds != nullptr
      && stats->bounds->minTraceLengthMm.has_value()
      && stats->bounds->minTraceLengthMm.value() > 0
      && stats->traces->totalLengthMm.has_value()
      && stats->traces->totalLengthMm.value() <= stats->bounds->minTraceLengthMm.value() * 1.05f
      && areAllViasMandatoryLayerTransitions(this->board)) {
    return jrt::format(
        "all vias on the board are mandatory layer transitions between SMD pins that cannot be eliminated and trace length (%.2f mm) is within 5%% of theoretical minimum (%.2f mm)",
        static_cast<double>(stats->traces->totalLengthMm.value()),
        static_cast<double>(stats->bounds->minTraceLengthMm.value()));
  }

  return std::nullopt;
}

bool BatchOptimizer::areAllViasMandatoryLayerTransitions(RoutingBoard* routingBoard) {
  jrt::DisposeOnExit<jrt::Collection<Via*>> viasOwner(routingBoard->getVias());
  jrt::Collection<Via*>* vias = viasOwner.get();
  if (vias->isEmpty()) {
    return false;
  }
  int32_t examinedViaCount = 0;
  for (Via* via : *vias) {
    if (via->isUserFixed()) {
      continue;
    }
    examinedViaCount++;
    if (via->netCount() == 0) {
      return false;
    }
    int32_t netNo = via->getNetNumber(0);
    jrt::DisposeOnExit<jrt::Set<Item*>> connectedOwner(via->getConnectedSet(netNo, true));
    jrt::Set<Item*>* connected = connectedOwner.get();
    jrt::List<Pin*>* smdPins = new (jrt::kOp) jrt::ArrayList<Pin*>();
    bool onlySmdPinsAndTraces = true;
    for (Item* item : *connected) {
      if (auto* pin = jrt::fast_cast<Pin>(item)) {
        if (pin->firstLayer() == pin->lastLayer()) {
          smdPins->add(pin);
        } else {
          onlySmdPinsAndTraces = false;
          break;
        }
      } else if (auto* v = jrt::fast_cast<Via>(item)) {
        if (v != via) {
          onlySmdPinsAndTraces = false;
          break;
        }
      } else if (jrt::fast_cast<Trace>(item) == nullptr) {
        onlySmdPinsAndTraces = false;
        break;
      }
    }
    if (!onlySmdPinsAndTraces || smdPins->size() < 2) {
      return false;
    }
    int32_t firstLayer = smdPins->get(0)->firstLayer();
    bool crossesLayers = false;
    for (Pin* pin : *smdPins) {
      if (pin->firstLayer() != firstLayer) {
        crossesLayers = true;
        break;
      }
    }
    if (!crossesLayers) {
      return false;
    }
  }
  return examinedViaCount > 0;
}

void BatchOptimizer::runBatchLoop() {
  if (this->job != nullptr && this->job->board != nullptr) {
    this->board = this->job->board;
  }
  this->inputBoard_ = this->board;
  if (this->thread != nullptr) {
    this->thread->resetStopAutoRouterRequest();
  }

  job->logDebug(
      "Before optimization: Via count: "
      + jrt::str(board->getViaCount())
      + ", trace length: "
      + jrt::str(jrt::Math::round(board->cumulativeTraceLength())));

  useIncreasedRipupCosts = true;

  // Capture initial board state for baseline and session summary
  BoardStatistics* initialStats = board->getStatistics();
  float initialRouterScore = initialStats->getRouterScore(job->routerSettings);
  float initialOptimizerScore = initialStats->getOptimizerRank(job->routerSettings);
  RoutingResultManifest_PhaseDetail* phase = job->resultPhaseMetrics->optimizer;
  phase->before =
      RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
          initialStats, job->routerSettings, std::string("current"));
  phase->before->score = phase->before->optimizerScore;
  int32_t initialIncomplete = initialStats->connections->incompleteCount.value();
  int32_t initialViolations = initialStats->clearanceViolations->totalCount.value();

  std::optional<std::string> bypassReason = evaluatePreFlightGuards(initialStats);
  if (bypassReason.has_value()) {
    job->logInfo("Skipping optimization stage: " + bypassReason.value() + ".");
    int64_t bypassStartNanos = jrt::System::nanoTime();
    float bypassCpuStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    float bypassAllocStart = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
    // The guards look at vias and trace length; the steps of a bundle cost neither.
    if (slideSteps()) {
      initialStats = board->getStatistics();
    }
    float bypassCpuEnd = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    float bypassAllocEnd = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
    phase->durationSeconds =
        static_cast<float>((jrt::System::nanoTime() - bypassStartNanos) / 1e9);
    phase->cpuSeconds =
        (bypassCpuStart >= 0.0f && bypassCpuEnd >= bypassCpuStart)
            ? bypassCpuEnd - bypassCpuStart
            : 0.0f;
    phase->totalAllocatedGb =
        (bypassAllocStart >= 0.0f && bypassAllocEnd >= bypassAllocStart)
            ? (bypassAllocEnd - bypassAllocStart) / 1024.0f
            : 0.0f;
    phase->peakHeapMb = AutorouteRuntimeMetrics::currentHeapUsageMb();
    phase->after =
        RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
            initialStats, job->routerSettings, std::string("current"));
    phase->after->score = phase->after->optimizerScore;
    phase->passesCompleted = 0;
    this->fireTaskStateChangedEvent(
        new (jrt::kOp) TaskStateChangedEvent(this, TaskState::FINISHED, 0, this->board->getHash()));
    return;
  }

  BasicBoard::discard(this->bestBoard, "optimizer");  // (none: a new run)
  this->bestBoard = this->board->deepCopy();
  this->bestScore = initialOptimizerScore;
  this->bestIncompleteCount = initialIncomplete;
  this->bestClearanceViolationCount = initialViolations;

  job->logInfo(
      jrt::format(
          "Optimization stage started on board '%s'. Baseline router score: %.2f, "
          "optimizer score: %.2f, incomplete connections: %d, "
          "clearance violations: %d.",
          this->board->getHash().c_str(),
          static_cast<double>(initialRouterScore),
          static_cast<double>(initialOptimizerScore),
          initialIncomplete,
          initialViolations));

  // Capture start-of-session resource usage baselines
  int64_t sessionStartMs = jrt::System::currentTimeMillis();
  const float cpuSecondsStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
  const float allocMbStart = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
  float peakHeapMb = AutorouteRuntimeMetrics::currentHeapUsageMb();
  workerCpuNanos = 0;
  workerAllocBytes = 0;

  if (this->settings->optimizer != nullptr && this->settings->optimizer->timeoutString.has_value()) {
    std::optional<int64_t> timeoutSeconds =
        TextManager::parseTimespanString(this->settings->optimizer->timeoutString);
    if (timeoutSeconds.has_value()) {
      this->deadlineMs = sessionStartMs + timeoutSeconds.value() * 1000;
    }
  }

  if (this->settings->optimizer != nullptr
      && this->settings->optimizer->optimizationImprovementThreshold.has_value()) {
    float threshold = this->settings->optimizer->optimizationImprovementThreshold.value();
    if (jrt::Float::isNaN(threshold) || jrt::Float::isInfinite(threshold) || threshold < 0.0f) {
      job->logWarning(
          jrt::format(
              "Invalid optimizer improvement threshold: %.4f. Resetting to default %.2f%%.",
              static_cast<double>(threshold),
              static_cast<double>(DefaultSettings::DEFAULT_OPTIMIZER_IMPROVEMENT_THRESHOLD)));
      this->settings->optimizer->optimizationImprovementThreshold =
          DefaultSettings::DEFAULT_OPTIMIZER_IMPROVEMENT_THRESHOLD;
    } else if (threshold > 0.0f && threshold < 0.1f) {
      float scaled = threshold * 100.0f;
      job->logInfo(
          jrt::format(
              "Optimizer improvement threshold appears to be specified as a fraction (%.4f). "
              "Auto-scaling to percentage (%.2f%%).",
              static_cast<double>(threshold),
              static_cast<double>(scaled)));
      this->settings->optimizer->optimizationImprovementThreshold = scaled;
    }
  }

  this->fireTaskStateChangedEvent(
      new (jrt::kOp) TaskStateChangedEvent(this, TaskState::STARTED, 0, this->board->getHash()));

  double scoreImprovement = -1;
  int32_t currentPass = 0;
  int32_t passesWithoutGainInARow = 0;
  while ((!this->settings->optimizer->maxPasses.has_value()
          || currentPass < this->settings->optimizer->maxPasses.value())
      && (!this->settings->optimizer->maxItems.has_value()
          || this->totalItemsOptimized < this->settings->optimizer->maxItems.value())
      && (!this->thread->isStopRequested())) {
    if (this->deadlineMs.has_value() && jrt::System::currentTimeMillis() >= this->deadlineMs.value()) {
      this->isTimedOut_ = true;
      job->logInfo("Optimizer stage timed out before starting pass #" + jrt::str(currentPass + 1));
      break;
    }
    ++currentPass;

    float scoreBeforePass = board->getStatistics()->getOptimizerRank(job->routerSettings);

    std::string currentBoardHash = this->board->getHash();
    this->fireTaskStateChangedEvent(
        new (jrt::kOp) TaskStateChangedEvent(this, TaskState::RUNNING, currentPass, currentBoardHash));

    bool withPreferredDirections = currentPass % 2 != 0;  // to create more variations
    optRoutePass(currentPass, withPreferredDirections);
    this->board->releaseGraveyard();  // PORT NOTE (stage E): pass boundary
    peakHeapMb = jrt::Math::max(peakHeapMb, AutorouteRuntimeMetrics::currentHeapUsageMb());

    if (this->isTimedOut_) {
      break;
    }

    BoardStatistics* passStats = board->getStatistics();
    float scoreAfterPass = passStats->getOptimizerRank(job->routerSettings);
    std::optional<std::string> rejectionReason =
        optimizerCandidateRejectionReason(
            this->bestIncompleteCount,
            this->bestClearanceViolationCount,
            this->bestScore,
            passStats,
            scoreAfterPass);
    if (!rejectionReason.has_value()) {
      this->bestScore = scoreAfterPass;
      this->bestIncompleteCount = passStats->connections->incompleteCount.value();
      this->bestClearanceViolationCount = passStats->clearanceViolations->totalCount.value();
      RoutingBoard* oldBest = this->bestBoard;
      this->bestBoard = this->board->deepCopy();
      BasicBoard::discard(oldBest, "optimizer");  // the incumbent snapshot is private to the optimizer
    } else {
      job->logInfo(
          jrt::format(
              "Optimizer pass #%d candidate rejected: %s. Restoring incumbent "
              "(optimizer score %.2f, incomplete connections: %d, clearance violations: %d).",
              currentPass,
              rejectionReason.value().c_str(),
              static_cast<double>(this->bestScore),
              this->bestIncompleteCount,
              this->bestClearanceViolationCount));
      restoreIncumbentBoard();
    }

    float effectiveScoreAfterPass = !rejectionReason.has_value() ? scoreAfterPass : this->bestScore;
    bool effectiveConnectionsOpen =
        !rejectionReason.has_value()
            ? passStats->connections->incompleteCount.value() > 0
            : this->bestIncompleteCount > 0;
    BatchOptimizer_StopRuleView* stopRuleView =
        stopRuleScores(scoreBeforePass, effectiveScoreAfterPass, effectiveConnectionsOpen);
    double passImprovementPercent = scoreImprovementPercent(scoreBeforePass, scoreAfterPass);
    std::string passOutcome =
        scoreAfterPass > scoreBeforePass
            ? "IMPROVED"
            : (scoreAfterPass < scoreBeforePass ? "REGRESSED" : "UNCHANGED");
    std::string passImprovementPercentStr =
        jrt::format("%.4f%%", passImprovementPercent)
        + (scoreBeforePass > 0 ? "" : " of the excess");
    job->logInfo(
        jrt::format(
            "Optimizer pass #%d: optimizer score %.2f -> %.2f (%s, %s), router score: %.2f, "
            "incomplete connections: %d, clearance violations: %d.",
            currentPass,
            static_cast<double>(scoreBeforePass),
            static_cast<double>(scoreAfterPass),
            passOutcome.c_str(),
            passImprovementPercentStr.c_str(),
            static_cast<double>(passStats->getRouterScore(job->routerSettings)),
            passStats->connections->incompleteCount.value(),
            passStats->clearanceViolations->totalCount.value()));

    bool passHadGain = hasPassGain(rejectionReason, scoreBeforePass, scoreAfterPass);

    if (this->useIncreasedRipupCosts && !passHadGain) {
      ++passesWithoutGainInARow;
      this->useIncreasedRipupCosts =
          increasedRipupCostsAfterPassWithoutGain(job->routerSettings->optimizer);
      // Keep the optimizer going: with normal ripup costs, or with the increased ones and the
      // other preferred directions of the next pass. With the increased costs kept, a second
      // pass in a row without gain has tried both directions from the same incumbent: the next
      // passes would only repeat them, so the stop rule applies.
      if (shouldStopAfterConsecutivePassesWithoutGain(
              job->routerSettings->optimizer, passesWithoutGainInARow)) {
        job->logInfo(
            jrt::format(
                "Stopping optimizer after %d consecutive passes without gain with increased ripup costs.",
                passesWithoutGainInARow));
        break;
      }
      scoreImprovement = -1;
    } else {
      passesWithoutGainInARow = 0;
      scoreImprovement = stopRuleView->percent();
    }

    if (scoreImprovement != -1
        && scoreImprovement < this->settings->optimizer->optimizationImprovementThreshold.value()) {
      job->logInfo(
          jrt::format(
              "Stopping optimizer because the improvement in this pass (%.4f%%) is below "
              "the threshold (%.2f%%).",
              scoreImprovement,
              static_cast<double>(this->settings->optimizer->optimizationImprovementThreshold.value())));
      break;
    }
  }

  this->currentPosition = nullptr;

  // Restore best board achieved if final state regressed below best score or added
  // violations/opens
  BoardStatistics* endStats = this->board->getStatistics();
  float finalBoardScore = endStats->getOptimizerRank(job->routerSettings);
  int32_t finalIncomplete = endStats->connections->incompleteCount.value();
  int32_t finalViolations = endStats->clearanceViolations->totalCount.value();

  bool regressed =
      finalBoardRegressed(
          this->bestScore,
          this->bestIncompleteCount,
          this->bestClearanceViolationCount,
          finalBoardScore,
          finalIncomplete,
          finalViolations);

  if (regressed && this->bestBoard != nullptr) {
    job->logInfo(
        jrt::format(
            "Restoring best board achieved (score %.2f vs final %.2f, incomplete connections %d vs %d, clearance violations %d vs %d).",
            static_cast<double>(this->bestScore),
            static_cast<double>(finalBoardScore),
            this->bestIncompleteCount,
            finalIncomplete,
            this->bestClearanceViolationCount,
            finalViolations));
    restoreIncumbentBoard();
  }
  BasicBoard::discard(this->bestBoard, "optimizer");
  this->bestBoard = nullptr;

  nudgeVias();
  slideSteps();

  this->fireTaskStateChangedEvent(
      new (jrt::kOp) TaskStateChangedEvent(this, TaskState::FINISHED, currentPass, this->board->getHash()));

  // Session summary
  double sessionDurationSeconds = (jrt::System::currentTimeMillis() - sessionStartMs) / 1000.0;
  float cpuSecondsEnd = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
  float allocMbEnd = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
  bool cpuSupported = cpuSecondsStart >= 0.0f && cpuSecondsEnd >= cpuSecondsStart;
  bool allocSupported = allocMbStart >= 0.0f && allocMbEnd >= allocMbStart;
  float cpuSecondsMain = cpuSupported ? cpuSecondsEnd - cpuSecondsStart : 0.0f;
  float allocMbMain = allocSupported ? allocMbEnd - allocMbStart : 0.0f;
  int64_t workerCpu = workerCpuNanos;
  int64_t workerAlloc = workerAllocBytes;
  float cpuSecondsUsed = cpuSecondsMain + (workerCpu / 1000000000.0f);
  float allocMbUsed = allocMbMain + (workerAlloc / (1024.0f * 1024.0f));
  peakHeapMb = jrt::Math::max(peakHeapMb, AutorouteRuntimeMetrics::currentHeapUsageMb());
  phase->durationSeconds = static_cast<float>(sessionDurationSeconds);
  phase->cpuSeconds = cpuSecondsUsed;
  phase->totalAllocatedGb = allocMbUsed / 1024.0f;
  phase->peakHeapMb = peakHeapMb;

  BoardStatistics* finalStats = new (jrt::kOp) BoardStatistics(this->board);
  float finalRouterScore = finalStats->getRouterScore(job->routerSettings);
  float finalOptimizerScore = finalStats->getOptimizerScore(job->routerSettings);
  phase->after =
      RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
          finalStats, job->routerSettings, std::string("current"));
  phase->after->score = phase->after->optimizerScore;
  phase->passesCompleted = currentPass;
  std::string completionStatus =
      this->isTimedOut_
          ? "completed with timeout:"
          : (this->thread->isStopRequested() ? "interrupted:" : "completed:");
  std::string cpuSummary =
      AutorouteRuntimeMetrics::formatOptionalMetric(
          (!cpuSupported && workerCpu == 0LL) ? -1.0f : cpuSecondsUsed, "%.2f");
  std::string allocSummary =
      AutorouteRuntimeMetrics::formatOptionalMetric(
          (!allocSupported && workerAlloc == 0LL) ? -1.0f : allocMbUsed / 1024.0f, "%.2f");
  job->logInfo(
      jrt::format(
          "Optimization stage %s. Baseline router score: %.2f, baseline optimizer score: %.2f, "
          "final router score: %.2f, final optimizer score: %.2f, completed in %.2f "
          "seconds, using %s total CPU seconds, %s GB total allocated, and %.1f MB "
          "peak heap usage.",
          completionStatus.c_str(),
          static_cast<double>(initialRouterScore),
          static_cast<double>(initialOptimizerScore),
          static_cast<double>(finalRouterScore),
          static_cast<double>(finalOptimizerScore),
          sessionDurationSeconds,
          cpuSummary.c_str(),
          allocSummary.c_str(),
          static_cast<double>(peakHeapMb)));
}

bool BatchOptimizer::viaNudgeEnabled(OptimizerSettings* optimizerSettings) {
  return optimizerSettings != nullptr
      && optimizerSettings->viaNudgeEnabled.has_value()
      && optimizerSettings->viaNudgeEnabled.value() == true;
}

void BatchOptimizer::nudgeVias() {
  if (!viaNudgeEnabled(job->routerSettings->optimizer)
      || this->isTimedOut_
      || this->thread->isStopRequested()) {
    return;
  }
  int64_t startNanos = jrt::System::nanoTime();
  float cpuStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
  ViaNudger_Result* result =
      ViaNudger::run(this->board, job->routerSettings->tracePullTightAccuracy.value(), this->thread);
  double toMm = this->board->communication->getResolution(Unit::MM);
  job->logInfo(
      jrt::format(
          "Via nudge: %d of %d tried moves kept in %d passes, trace length %.2f -> %.2f mm, "
          "bends %d -> %d, vias %d -> %d, incomplete connections %d -> %d, "
          "clearance violations %d -> %d, in %.2f seconds using %.2f CPU seconds.",
          result->acceptedMoves(),
          result->triedMoves(),
          result->passes(),
          result->lengthBefore() / toMm,
          result->lengthAfter() / toMm,
          result->bendsBefore(),
          result->bendsAfter(),
          result->viasBefore(),
          result->viasAfter(),
          result->incompleteBefore(),
          result->incompleteAfter(),
          result->violationsBefore(),
          result->violationsAfter(),
          (jrt::System::nanoTime() - startNanos) / 1e9,
          static_cast<double>(AutorouteRuntimeMetrics::currentThreadCpuSeconds() - cpuStart)));
  if (result->acceptedMoves() > 0) {
    this->fireBoardUpdatedEvent(new (jrt::kOp) BoardStatistics(this->board), nullptr, this->board);
  }
}

bool BatchOptimizer::stepSlideEnabled(OptimizerSettings* optimizerSettings) {
  return optimizerSettings == nullptr
      || !(optimizerSettings->stepSlideEnabled.has_value()
           && optimizerSettings->stepSlideEnabled.value() == false);
}

bool BatchOptimizer::slideSteps() {
  if (!stepSlideEnabled(job->routerSettings->optimizer)
      || this->isTimedOut_
      || this->thread->isStopRequested()) {
    return false;
  }
  int64_t startNanos = jrt::System::nanoTime();
  float cpuStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
  StepSlider_Result* result =
      StepSlider::run(this->board, job->routerSettings->tracePullTightAccuracy.value(), this->thread);
  double toMm = this->board->communication->getResolution(Unit::MM);
  job->logInfo(
      jrt::format(
          "Step slide: %d of %d tried moves kept in %d passes, trace length %.2f -> %.2f mm, "
          "bends %d -> %d, vias %d -> %d, incomplete connections %d -> %d, "
          "clearance violations %d -> %d, in %.2f seconds using %.2f CPU seconds.",
          result->acceptedMoves(),
          result->triedMoves(),
          result->passes(),
          result->lengthBefore() / toMm,
          result->lengthAfter() / toMm,
          result->bendsBefore(),
          result->bendsAfter(),
          result->viasBefore(),
          result->viasAfter(),
          result->incompleteBefore(),
          result->incompleteAfter(),
          result->violationsBefore(),
          result->violationsAfter(),
          (jrt::System::nanoTime() - startNanos) / 1e9,
          static_cast<double>(AutorouteRuntimeMetrics::currentThreadCpuSeconds() - cpuStart)));
  if (result->acceptedMoves() == 0) {
    return false;
  }
  this->fireBoardUpdatedEvent(new (jrt::kOp) BoardStatistics(this->board), nullptr, this->board);
  return true;
}

void BatchOptimizer::restoreIncumbentBoard() {
  RoutingBoard* old = this->board;
  this->board = this->bestBoard->deepCopy();
  this->job->board = this->board;
  dropBoard(old);
  this->fireBoardUpdatedEvent(new (jrt::kOp) BoardStatistics(this->board), nullptr, this->board);
}

void BatchOptimizer::dropBoard(RoutingBoard* old) {
  if (old == nullptr || old == this->inputBoard_ || old == this->board || old == this->bestBoard
      || (this->job != nullptr && old == this->job->board)) {
    return;
  }
  BasicBoard::discard(old, "optimizer");
}

std::optional<std::string> BatchOptimizer::optimizerCandidateRejectionReason(
    int32_t incumbentIncompleteCount,
    int32_t incumbentClearanceViolationCount,
    float incumbentOptimizerScore,
    BoardStatistics* candidate,
    float candidateOptimizerScore) {
  if (candidate->connections->incompleteCount.value() > incumbentIncompleteCount) {
    return std::string("CONNECTIVITY_REGRESSION");
  }
  if (candidate->clearanceViolations->totalCount.value() > incumbentClearanceViolationCount) {
    return std::string("DRC_COUNT_REGRESSION");
  }
  if (!(candidateOptimizerScore > incumbentOptimizerScore)) {
    return std::string("OPTIMIZER_SCORE_NOT_IMPROVED");
  }
  return std::nullopt;
}

double BatchOptimizer::scoreImprovementPercent(float before, float after) {
  double fraction =
      before > 0
          ? static_cast<double>(after - before) / before
          : static_cast<double>(after - before) / (1000.0 - before);
  return fraction * 100.0;
}

BatchOptimizer_StopRuleView* BatchOptimizer::stopRuleScores(
    float before, float after, bool connectionsOpen) {
  return connectionsOpen && before <= 0
      ? new (jrt::kOp) BatchOptimizer_StopRuleView(0.0f, jrt::Math::max(0.0f, after), 0.0)
      : new (jrt::kOp) BatchOptimizer_StopRuleView(before, after, scoreImprovementPercent(before, after));
}

bool BatchOptimizer::finalBoardRegressed(
    float bestScore,
    int32_t bestIncomplete,
    int32_t bestViolations,
    float finalScore,
    int32_t finalIncomplete,
    int32_t finalViolations) {
  return finalScore < bestScore
      || finalIncomplete > bestIncomplete
      || finalViolations > bestViolations;
}

jrt::List<int32_t>* BatchOptimizer::prepareCandidateItems() {
  jrt::List<int32_t>* itemIds = new (jrt::kOp) jrt::ArrayList<int32_t>();
  this->sortedRouteItems = new (jrt::kOp) BatchOptimizer_ReadSortedRouteItems(this);

  std::optional<ItemSelectionStrategy> strategy =
      this->settings->optimizer != nullptr ? this->settings->optimizer->itemSelectionStrategy
                                           : std::nullopt;
  if (!strategy.has_value()) {
    strategy = ItemSelectionStrategy::SEQUENTIAL;
  }

  if (strategy.value() == ItemSelectionStrategy::PRIORITIZED && !resultMap->isEmpty()) {
    jrt::List<int32_t>* nonPrioritized = new (jrt::kOp) jrt::ArrayList<int32_t>();
    jrt::PriorityQueue<ItemRouteResult*>* pq = new (jrt::kOp) jrt::PriorityQueue<ItemRouteResult*>();

    for (Item* item = sortedRouteItems->next(); item != nullptr; item = sortedRouteItems->next()) {
      ItemRouteResult* r = resultMap->get(item->getId());
      if (r != nullptr) {
        pq->add(r);
      } else {
        nonPrioritized->add(item->getId());
      }
    }

    while (!pq->isEmpty()) {
      itemIds->add(pq->poll()->itemId());
    }
    itemIds->addAll(nonPrioritized);
  } else {
    for (Item* item = sortedRouteItems->next(); item != nullptr; item = sortedRouteItems->next()) {
      itemIds->add(item->getId());
    }
  }

  this->sortedRouteItems = nullptr;
  resultMap->clear();
  return itemIds;
}

namespace {

/**
 * The optimizer's thread pool (Java: Executors.newFixedThreadPool + FutureTask + the completion
 * Semaphore of optRoutePass). Tasks run in submission order on the first free thread; each
 * thread has its own WorkerBoardState (Java: ThreadLocal<WorkerBoardState>).
 */
/**
 * The stop seen by the candidates of one pass: the job's stop, or the end of the pass. When the
 * pass stops early, the candidates still running are not read (their results are dropped); they
 * stop at their next stop check instead of running to the end while the pass waits for them
 * (Java: pool.shutdownNow() leaves them running). Requests from a candidate go to the job's thread.
 */
class PassStop final : public StoppableThread {
 public:
  explicit PassStop(StoppableThread* job) : job_(job) {}
  void cancel() { cancelled_.store(true, std::memory_order_relaxed); }

  void requestStop() override {
    if (job_ != nullptr) job_->requestStop();
    else StoppableThread::requestStop();
  }
  bool isStopRequested() override {
    return cancelled_.load(std::memory_order_relaxed)
        || (job_ != nullptr ? job_->isStopRequested() : StoppableThread::isStopRequested());
  }
  void requestStopAutoRouter() override {
    if (job_ != nullptr) job_->requestStopAutoRouter();
    else StoppableThread::requestStopAutoRouter();
  }
  bool isStopAutoRouterRequested() override {
    return cancelled_.load(std::memory_order_relaxed)
        || (job_ != nullptr ? job_->isStopAutoRouterRequested() : StoppableThread::isStopAutoRouterRequested());
  }
  void resetAutoRouterStopRequest() override {
    if (job_ != nullptr) job_->resetAutoRouterStopRequest();
    else StoppableThread::resetAutoRouterStopRequest();
  }

 protected:
  void threadAction() override {}

 private:
  StoppableThread* job_;
  std::atomic<bool> cancelled_{false};
};

class CandidatePool {
 public:
  struct Future {
    BatchOptimizer_OptimizeCandidateTask* task;
    BatchOptimizer_CandidateResult* result = nullptr;
    std::exception_ptr error;
    bool done = false;  // guarded by the pool's mutex

    /** Java Future.get() of a finished task: the result, or what the task threw. */
    BatchOptimizer_CandidateResult* get() {
      if (error) std::rethrow_exception(error);
      return result;
    }
  };

  explicit CandidatePool(int32_t threads) {
    workers_.reserve(static_cast<std::size_t>(threads));
    for (int32_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this] { run(); });
    }
  }
  ~CandidatePool() { shutdown(); }
  CandidatePool(const CandidatePool&) = delete;
  CandidatePool& operator=(const CandidatePool&) = delete;

  Future* submit(BatchOptimizer_OptimizeCandidateTask* task) {
    auto owned = std::make_unique<Future>();
    owned->task = task;
    Future* f = owned.get();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      futures_.push_back(std::move(owned));
      queue_.push_back(f);
    }
    workAvailable_.notify_one();
    return f;
  }

  bool isDone(Future* f) {
    std::lock_guard<std::mutex> lock(mutex_);
    return f->done;
  }

  /** Waits until a task finished since the last call (Java: completions.acquire + drainPermits). */
  void awaitCompletion() {
    std::unique_lock<std::mutex> lock(mutex_);
    completed_.wait(lock, [this] { return completions_ != seenCompletions_; });
    seenCompletions_ = completions_;
  }

  /** Drops the tasks that have not started and waits for the running ones. */
  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      stopping_ = true;
      queue_.clear();
    }
    workAvailable_.notify_all();
    for (std::thread& t : workers_) {
      t.join();
    }
  }

 private:
  void run() {
    BatchOptimizer_WorkerBoardState workerBoardState;  // this thread's (Java ThreadLocal)
    for (;;) {
      Future* f;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) return;  // stopping
        f = queue_.front();
        queue_.pop_front();
      }
      try {
        f->task->useWorkerBoardState(&workerBoardState);
        f->result = f->task->call();
      } catch (...) {
        f->error = std::current_exception();
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        f->done = true;
        ++completions_;
      }
      completed_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable workAvailable_;
  std::condition_variable completed_;
  std::deque<Future*> queue_;                   // submitted, not started
  std::vector<std::unique_ptr<Future>> futures_;  // every submitted task (owned)
  std::vector<std::thread> workers_;
  int64_t completions_ = 0;
  int64_t seenCompletions_ = 0;
  bool stopping_ = false;
};

}  // namespace

float BatchOptimizer::optRoutePass(int32_t passNo, bool withPreferredDirections) {
  BoardStatistics* boardStatisticsBefore = board->getStatistics();
  RouterCounters* routerCounters = new (jrt::kBoard) RouterCounters();
  routerCounters->passCount = passNo;
  progressThrottler->reset();
  this->fireBoardUpdatedEvent(boardStatisticsBefore, routerCounters, this->board);

  this->minCumulativeTraceLength = boardStatisticsBefore->traces->totalWeightedLength.value();
  jrt::List<int32_t>* candidateItemIds = prepareCandidateItems();

  if (this->settings->optimizer != nullptr
      && this->settings->optimizer->maxItems.has_value()
      && this->settings->optimizer->maxItems.value() > 0) {
    int32_t remaining = this->settings->optimizer->maxItems.value() - this->totalItemsOptimized;
    if (remaining <= 0) {
      job->logInfo(
          "Max items limit reached ("
          + jrt::str(this->settings->optimizer->maxItems.value())
          + "). Stopping optimizer.");
      return 0.0f;
    }
    if (candidateItemIds->size() > remaining) {
      // Java: new ArrayList<>(candidateItemIds.subList(0, remaining))
      jrt::List<int32_t>* truncated = new (jrt::kOp) jrt::ArrayList<int32_t>();
      for (int32_t i = 0; i < remaining; i++) {
        truncated->add(candidateItemIds->get(i));
      }
      candidateItemIds = truncated;
    }
  }

  if (candidateItemIds->isEmpty()) {
    return 0.0f;
  }

  int32_t threadPoolSize = jrt::Math::max(1, this->settings->getOptimizerMaxThreads());

  std::string optimizationPassId =
      "BatchOptRoute.opt_route_pass #"
      + jrt::str(passNo)
      + " with "
      + jrt::str(candidateItemIds->size())
      + " items, "
      + jrt::str(boardStatisticsBefore->items->viaCount)
      + " vias and "
      + formatParenGrouped2(boardStatisticsBefore->traces->totalLength)
      + " trace length running on "
      + jrt::str(threadPoolSize)
      + " thread(s).";

  FRLogger::traceEntry(optimizationPassId);

  // PORT NOTE: with one thread there is no pool (see the notes in BatchOptimizer.h): the
  // candidates are computed lazily, in candidate order, on the calling thread at the moment Java
  // reads their future. With more threads CandidatePool runs them like Java's pool. The merge,
  // winner selection and early stop logic are the same in both.

  BatchOptimizer_CandidateResult* winningCandidate = nullptr;
  // A new candidate starts as soon as a thread is free, even while an older and slower
  // candidate is still running, so one slow candidate does not idle the other threads. The
  // results are still read in candidate order, so the winner and the early stop are the same.
  // A result keeps no board, so the results waiting to be read are small: the winner is
  // replayed on a copy of the baseline board at the end of the pass.
  int32_t maxRunning = threadPoolSize * 2;
  int32_t maxUnread = jrt::Math::max(threadPoolSize * 64, 128);
  bool stoppedOrTimedOut = false;
  bool earlyStopDueToFailures = false;
  int32_t consecutiveFailures = 0;
  int32_t maxConsecutiveFailures =
      passNo == 1
          ? (this->settings->optimizer != nullptr
                     && this->settings->optimizer->maxConsecutiveFailuresPass1.has_value()
                 ? this->settings->optimizer->maxConsecutiveFailuresPass1.value()
                 : 12)
          : (this->settings->optimizer != nullptr
                     && this->settings->optimizer->maxConsecutiveFailures.has_value()
                 ? this->settings->optimizer->maxConsecutiveFailures.value()
                 : 50);

  // The baseline board does not change during the pass: the winner replaces it at the end.
  BoardStatistics_OptimizerMetrics* baselineMetrics =
      BoardStatistics_OptimizerMetrics::of(this->board);
  // The stop / deadline checks at the top of each iteration of the read loop.
  auto stopOrDeadline = [&]() {
    if (this->deadlineMs.has_value() && jrt::System::currentTimeMillis() >= this->deadlineMs.value()) {
      job->logInfo("Optimizer stage timed out.");
      this->isTimedOut_ = true;
      stoppedOrTimedOut = true;
      return true;
    }
    if (this->thread != nullptr && this->thread->isStopRequested()) {
      stoppedOrTimedOut = true;
      return true;
    }
    return false;
  };
  auto newTask = [&](int32_t itemId, StoppableThread* stop) {
    return new (jrt::kOp) BatchOptimizer_OptimizeCandidateTask(
        job,
        this->board,
        itemId,
        this->minCumulativeTraceLength,
        withPreferredDirections,
        this->useIncreasedRipupCosts,
        stop,
        this->deadlineMs,
        this->workerBoardStates,
        baselineMetrics,
        false);
  };
  // Reads the next result in candidate order (Java: the body after future.get()); returns true
  // if the pass stops early. `get` returns the result or throws what the candidate threw.
  auto readResult = [&](auto get) {
    try {
      BatchOptimizer_CandidateResult* res = get();
      this->totalItemsOptimized++;
      if (res != nullptr) {
        this->workerCpuNanos += res->cpuUsedNanos;
        this->workerAllocBytes += res->allocUsedBytes;
        this->resultMap->put(res->result->itemId(), res->result);

        if (res->itemPosition != nullptr) {
          this->currentPosition = res->itemPosition;
        }

        if (res->result->improved()) {
          consecutiveFailures = 0;
          if (winningCandidate == nullptr || res->result->improvedOver(winningCandidate->result)) {
            winningCandidate = res;
          }
        } else {
          consecutiveFailures++;
          if (consecutiveFailures >= maxConsecutiveFailures) {
            job->logInfo(
                jrt::format(
                    "Stopping optimization pass #%d early after %d consecutive items could "
                    "not be improved.",
                    passNo,
                    consecutiveFailures));
            earlyStopDueToFailures = true;
            return true;
          }
        }
      }
    } catch (const jrt::Throwable& e) {
      // Java: ExecutionException from future.get(); the cause is logged and the pass goes on.
      // (VirtualMachineError is rethrown in Java; std::bad_alloc is not a jrt::Throwable and
      // propagates here as well.)
      job->logError("Error in candidate optimization task", &e);
    }
    return false;
  };

  if (threadPoolSize <= 1) {
    int32_t nextCandidate = 0;
    while (nextCandidate < candidateItemIds->size()) {
      if (stopOrDeadline()) {
        break;
      }
      BatchOptimizer_OptimizeCandidateTask* task = newTask(candidateItemIds->get(nextCandidate++), this->thread);
      if (readResult([task] { return task->call(); })) {
        break;
      }
    }
  } else {
    // Java: the ExecutorService loop. Candidates that were started but are not read when the
    // pass stops early are waited for (Java leaves them running) and ignored.
    jrt::JobHeapSharedScope sharedJobHeap;
    PassStop passStop(this->thread);
    CandidatePool pool(threadPoolSize);
    std::deque<CandidatePool::Future*> futures;
    int32_t nextCandidate = 0;
    while (nextCandidate < candidateItemIds->size() || !futures.empty()) {
      if (stopOrDeadline()) {
        break;
      }
      int32_t running = 0;
      for (CandidatePool::Future* f : futures) {
        if (!pool.isDone(f)) {
          running++;
        }
      }
      while (running < maxRunning
             && static_cast<int32_t>(futures.size()) < maxUnread
             && nextCandidate < candidateItemIds->size()) {
        futures.push_back(pool.submit(newTask(candidateItemIds->get(nextCandidate++), &passStop)));
        running++;
      }
      if (!pool.isDone(futures.front())) {
        // Wait for any candidate to finish, then feed the free threads again.
        pool.awaitCompletion();
        continue;
      }
      CandidatePool::Future* future = futures.front();
      futures.pop_front();
      if (readResult([future] { return future->get(); })) {
        break;
      }
    }
    passStop.cancel();  // the unread candidates still running stop at their next check
    pool.shutdown();    // Java: pool.shutdownNow() (here: waits for the running candidates)
  }

  float routeImproved = 0.0f;

  if (!stoppedOrTimedOut && winningCandidate != nullptr && winningCandidate->result->improved()) {
    winningCandidate =
        replayWinner(job, winningCandidate, withPreferredDirections, baselineMetrics);
  }
  if (!stoppedOrTimedOut
      && winningCandidate != nullptr
      && winningCandidate->result->improved()
      && winningCandidate->board != nullptr) {
    RoutingBoard* old = this->board;
    this->board = winningCandidate->board;
    this->job->board = this->board;
    dropBoard(old);  // the baseline of the pass, replaced by the winner's board
    this->minCumulativeTraceLength = this->board->getStatistics()->traces->totalWeightedLength.value();

    BoardStatistics* boardStatisticsAfter = this->board->getStatistics();
    this->fireBoardUpdatedEvent(boardStatisticsAfter, routerCounters, this->board);
    routeImproved = winningCandidate->result->improvementPercentage();
  }

  double routeoptimizerPassDuration = FRLogger::traceExit(optimizationPassId);
  BoardStatistics* boardStatisticsAfter = new (jrt::kOp) BoardStatistics(this->board);
  this->fireBoardUpdatedEvent(boardStatisticsAfter, routerCounters, this->board);
  job->logInfo(
      jrt::format(
          "Optimizer pass #%d on board '%s' was completed in %.2f seconds with the score of %s.",
          passNo,
          this->board->getHash().c_str(),
          routeoptimizerPassDuration,
          FRLogger::formatScore(
              boardStatisticsAfter->getOptimizerRank(job->routerSettings),
              boardStatisticsAfter->connections->incompleteCount.value(),
              boardStatisticsAfter->clearanceViolations->totalCount.value())
              .c_str()));
  return routeImproved;
}

ItemRouteResult* BatchOptimizer::optRouteItem(Item* item, bool withPreferredDirections) {
  auto* routingBoard = jrt::fast_cast<RoutingBoard>(item->board);
  if (routingBoard == nullptr) {
    job->logWarning("The item to be optimized is not on a RoutingBoard.");
    return new (jrt::kOp) ItemRouteResult(item->getId());
  }

  double baseline =
      this->minCumulativeTraceLength > 0
          ? this->minCumulativeTraceLength
          : static_cast<double>(routingBoard->getStatistics()->traces->totalWeightedLength.value());

  return optRouteItemOnBoard(
      job,
      routingBoard,
      item,
      baseline,
      withPreferredDirections,
      this->useIncreasedRipupCosts,
      this->thread,
      this->deadlineMs);
}

ItemRouteResult* BatchOptimizer::optRouteItemOnBoard(
    RoutingJob* job,
    RoutingBoard* routingBoard,
    Item* item,
    double baselineTraceLength,
    bool withPreferredDirections,
    bool useIncreasedRipupCosts,
    StoppableThread* thread,
    std::optional<int64_t> deadlineMs) {
  return optRouteItemOnBoard(
      job,
      routingBoard,
      item,
      baselineTraceLength,
      withPreferredDirections,
      useIncreasedRipupCosts,
      thread,
      deadlineMs,
      nullptr);
}

ItemRouteResult* BatchOptimizer::optRouteItemOnBoard(
    RoutingJob* job,
    RoutingBoard* routingBoard,
    Item* item,
    double baselineTraceLength,
    bool withPreferredDirections,
    bool useIncreasedRipupCosts,
    StoppableThread* thread,
    std::optional<int64_t> deadlineMs,
    BoardStatistics_OptimizerMetrics* baselineMetrics) {
  BoardStatistics_OptimizerMetrics* metricsBefore =
      baselineMetrics != nullptr
          ? baselineMetrics
          : BoardStatistics_OptimizerMetrics::of(routingBoard);
  double baseline =
      baselineTraceLength > 0 ? baselineTraceLength
                              : static_cast<double>(metricsBefore->totalWeightedLength());
  RouterCounters* routerCountersBefore = new (jrt::kOp) RouterCounters();
  if (item->hasIgnoredNets()) {
    return new (jrt::kOp) ItemRouteResult(item->getId());
  }
  routerCountersBefore->incompleteCount = calculateIncompleteCount(routingBoard);
  jrt::Set<int32_t>* netsLeftUnroutedSet = netsLeftUnrouted(routingBoard, item);

  jrt::Set<Item*>* rippedItems = new (jrt::kOp) jrt::TreeSet<Item*>();
  rippedItems->add(item);

  if (auto* currentTrace = jrt::fast_cast<Trace>(item)) {
    jrt::DisposeOnExit<jrt::Set<Item*>> currentContactList(currentTrace->getStartContacts());
    for (int32_t i = 0; i < 2; i++) {
      if (containsOnlyUnfixedTraces(currentContactList.get())) {
        rippedItems->addAll(currentContactList.get());
      }
      currentContactList.reset(currentTrace->getEndContacts());
    }

  }

  jrt::Set<Item*>* rippedConnections = new (jrt::kOp) jrt::TreeSet<Item*>();
  for (Item* currentItem : *rippedItems) {
    rippedConnections->addAll(jrt::DisposeOnExit(currentItem->getConnectionItems(Item_StopConnectionOption::NONE)).get());
  }

  for (Item* currentItem : *rippedConnections) {
    if (currentItem->isUserFixed() || currentItem->hasIgnoredNets()) {
      return new (jrt::kOp) ItemRouteResult(item->getId());
    }
  }

  routingBoard->removeItems(rippedConnections);
  for (int32_t i = 0; i < item->netCount(); i++) {
    routingBoard->combineTraces(item->getNetNumber(i));
  }

  int32_t ripupCosts = job->routerSettings->getStartRipupCosts();
  if (useIncreasedRipupCosts && job->routerSettings->optimizer != nullptr) {
    ripupCosts *= job->routerSettings->optimizer->additionalRipupCostFactorAtStart.value();
  }

  if (jrt::fast_cast<Trace>(item) != nullptr && job->routerSettings->optimizer != nullptr) {
    ripupCosts = static_cast<int32_t>(jrt::Math::round(
        job->routerSettings->optimizer->traceRipupCostFactor.value() * static_cast<double>(ripupCosts)));
  }

  int32_t maxAutoroutePasses =
      job->routerSettings->optimizer != nullptr
          ? job->routerSettings->optimizer->maxAutoroutePasses.value()
          : 1;

  BatchAutorouter::autoroutePassesForOptimizingItem(
      job,
      maxAutoroutePasses,
      ripupCosts,
      job->routerSettings->tracePullTightAccuracy.value(),
      withPreferredDirections,
      routingBoard,
      job->routerSettings,
      netsLeftUnroutedSet);

  BoardStatistics_OptimizerMetrics* metricsAfter = BoardStatistics_OptimizerMetrics::of(routingBoard);
  RouterCounters* routerCountersAfter = new (jrt::kOp) RouterCounters();
  routerCountersAfter->incompleteCount = calculateIncompleteCount(routingBoard);

  ItemRouteResult* result =
      new (jrt::kOp) ItemRouteResult(
          item->getId(),
          metricsBefore->viaCount(),
          metricsAfter->viaCount(),
          baseline,
          static_cast<double>(metricsAfter->totalWeightedLength()),
          routerCountersBefore->incompleteCount.value(),
          routerCountersAfter->incompleteCount.value());
  bool routeImproved =
      (thread == nullptr || !thread->isStopRequested())
      && (!deadlineMs.has_value() || jrt::System::currentTimeMillis() < deadlineMs.value())
      && result->improved();
  result->updateImproved(routeImproved);
  return result;
}

jrt::Set<int32_t>* BatchOptimizer::netsLeftUnrouted(RoutingBoard* routingBoard, Item* item) {
  jrt::Set<int32_t>* result = routingBoard->routingLedger()->incompleteNetNumbers();
  for (int32_t i = 0; i < item->netCount(); i++) {
    result->remove(item->getNetNumber(i));
  }
  return result;
}

FloatPoint* BatchOptimizer::getItemPosition(Item* item) {
  if (auto* via = jrt::fast_cast<Via>(item)) {
    return via->getCenter()->toFloat();
  } else if (auto* trace = jrt::fast_cast<Trace>(item)) {
    return trace->firstCorner()->toFloat();
  }
  return nullptr;
}

BatchOptimizer_CandidateResult* BatchOptimizer::replayWinner(
    RoutingJob* job,
    BatchOptimizer_CandidateResult* winner,
    bool withPreferredDirections,
    BoardStatistics_OptimizerMetrics* baselineMetrics) {
  BatchOptimizer_CandidateResult* replay =
      (new (jrt::kOp) BatchOptimizer_OptimizeCandidateTask(
           job,
           this->board,
           winner->result->itemId(),
           this->minCumulativeTraceLength,
           withPreferredDirections,
           this->useIncreasedRipupCosts,
           this->thread,
           std::nullopt,
           this->workerBoardStates,
           baselineMetrics,
           true))
          ->call();
  if (replay->board == nullptr
      || replay->result->compareTo(winner->result) != 0
      || replay->result->improvementPercentage() != winner->result->improvementPercentage()) {
    job->logWarning(
        "Optimizer: replaying the winning candidate (item "
        + jrt::str(winner->result->itemId())
        + ") did not give the same result; the pass keeps the current board.");
    BasicBoard::discard(replay->board, "replay");  // a kept board nobody takes
    return nullptr;
  }
  return replay;
}

FloatPoint* BatchOptimizer::getCurrentPosition() { return this->currentPosition; }

std::string BatchOptimizer::getId() { return "freerouting-optimizer"; }

std::string BatchOptimizer::getName() { return "Freerouting Optimizer"; }

std::string BatchOptimizer::getVersion() { return "1.0"; }

std::string BatchOptimizer::getDescription() { return "Freerouting Optimizer v1.0"; }

NamedAlgorithmType BatchOptimizer::getType() { return NamedAlgorithmType::OPTIMIZER; }

int32_t BatchOptimizer::calculateIncompleteCount(RoutingBoard* board) {
  return board->routingLedger()->incompleteCount();
}

bool BatchOptimizer::increasedRipupCostsAfterPassWithoutGain(OptimizerSettings* optimizerSettings) {
  return optimizerSettings != nullptr
      && optimizerSettings->keepIncreasedRipupCosts.has_value()
      && optimizerSettings->keepIncreasedRipupCosts.value() == true;
}

bool BatchOptimizer::continueAfterPassWithoutGain(
    OptimizerSettings* optimizerSettings, int32_t passesWithoutGainInARow) {
  return !increasedRipupCostsAfterPassWithoutGain(optimizerSettings)
      || passesWithoutGainInARow < 2;
}

bool BatchOptimizer::hasPassGain(
    const std::optional<std::string>& rejectionReason, float scoreBeforePass, float scoreAfterPass) {
  return !rejectionReason.has_value() && scoreAfterPass > scoreBeforePass;
}

bool BatchOptimizer::shouldStopAfterConsecutivePassesWithoutGain(
    OptimizerSettings* optimizerSettings, int32_t passesWithoutGainInARow) {
  return !continueAfterPassWithoutGain(optimizerSettings, passesWithoutGainInARow);
}

// ---------------------------------------------------------------------------------------------
// BatchOptimizer_WorkerBoardState
// ---------------------------------------------------------------------------------------------

RoutingBoard* BatchOptimizer_WorkerBoardState::acquire(RoutingBoard* baselineBoard) {
  if (this->workerBoard == nullptr || this->baselineBoard != baselineBoard) {
    this->baselineBoard = baselineBoard;
    this->workerBoard = baselineBoard->deepCopy();
  }
  this->workerBoard->generateSnapshot();
  return this->workerBoard;
}

void BatchOptimizer_WorkerBoardState::finish(bool transferOwnership) {
  if (this->workerBoard == nullptr) {
    return;
  }
  if (!transferOwnership) {
    // Discard the private candidate wholesale. Undoing its items cannot make it a
    // pristine baseline (leaves incremented item ids and failure history) and is
    // wasted work when the candidate will not be reused.
    this->workerBoard = nullptr;
    this->baselineBoard = nullptr;
    return;
  }
  bool restored = this->workerBoard->popSnapshot();
  this->workerBoard->clearTransientAutorouteState();
  if (!restored) {
    FRLogger::warn("BatchOptimizer: failed to restore the worker-board snapshot");
  }
  this->workerBoard = nullptr;
  this->baselineBoard = nullptr;
}

// ---------------------------------------------------------------------------------------------
// BatchOptimizer_OptimizeCandidateTask
// ---------------------------------------------------------------------------------------------

BatchOptimizer_CandidateResult* BatchOptimizer_OptimizeCandidateTask::call() {
  if ((thread != nullptr && thread->isStopRequested())
      || (deadlineMs.has_value() && jrt::System::currentTimeMillis() >= deadlineMs.value())) {
    return new (jrt::kOp) BatchOptimizer_CandidateResult(new (jrt::kOp) ItemRouteResult(itemId), nullptr, nullptr, 0, 0);
  }

  int64_t cpuStart = AutorouteRuntimeMetrics::currentThreadCpuNanos();
  int64_t allocStart = AutorouteRuntimeMetrics::currentThreadAllocatedBytes();

  // PORT NOTE: Java workerBoardStates.get() (ThreadLocal) -> the optimizer's single instance.
  BatchOptimizer_WorkerBoardState* workerBoardState = workerBoardStates;

  // Optimizer-candidate arena (MEMORY_MODEL.md): a candidate whose board is not kept works on a
  // private deep copy of the baseline board, dropped in finish(false) (Java leaves it to the GC).
  // The structural data of that copy (kBoardData: items, undo nodes, search-tree nodes) lives in
  // this arena, the connection searches on it in their engines' arenas; both are freed when the
  // candidate ends. Only kBoardData goes here, so shared objects (library, geometry caches,
  // statics) and the returned result never receive arena memory.
  // Stage E: the candidate's geometry lives in `region` (its companion is the arena, so worker
  // items keep region geometry without copies); only the item position leaves the candidate.
  struct CandidateArena {
    jrt::Arena arena;
    jrt::GeomRegion region;
    RoutingBoard* board = nullptr;  // the worker board living in the arena
    CandidateArena() {
      region.setCompanion(&arena);
      region.setTakesOps(true);  // stage E3: the candidate's temporaries die with it too
    }
    ~CandidateArena() {
      // Stage E: the worker board is dropped as a whole: its arena data goes with the arena,
      // its heap parts (board object, trees, item list map, search-tree info, engine) here,
      // before the arena is released (the teardown still reads arena nodes).
      if (board != nullptr && !BasicBoard::discardEnabled("candidate")) {
        board->discardAutorouteEngineWithBoard();  // diagnostics: the pre-E7 teardown
      }
      BasicBoard::discard(board, "candidate");
      static const bool stats = std::getenv("FR_MEM_STATS") != nullptr;
      static std::atomic<int64_t> count{0};
      if (stats && board != nullptr && ++count % 20 == 1) {
        std::fprintf(stderr,
                     "candidate %lld: arena %.1f MB, arenas live %.1f MB, pool %.1f MB, heap %.1f MB, "
                     "job heap %.1f MB\n",
                     static_cast<long long>(count), arena.bytesAllocated() / 1048576.0,
                     jrt::Arena::liveBytes() / 1048576.0, jrt::Arena::poolBytes() / 1048576.0,
                     jrt::heap_live_bytes() / 1048576.0, jrt::job_heap_live_bytes() / 1048576.0);
      }
      arena.release();
      region.release();  // after the arena: arena objects refer into the region
    }
  };
  static const bool boardArenaOff = std::getenv("FR_NO_BOARD_ARENA") != nullptr;  // diagnostics switch
  bool useCandidateArena = !boardArenaOff && !keepBoard && !workerBoardState->hasWorkerBoard();
  CandidateArena candidateArena;
  std::optional<jrt::BoardArenaScope> boardArenaScope;
  std::optional<jrt::GeomRegionScope> regionScope;
  // A kept board (the winner replay) gets a geometry-only region: its items copy out what they
  // store (geomcopy::keep), the rest of the replay's geometry is dropped.
  std::optional<jrt::LocalGeomRegion> replayRegion;
  if (useCandidateArena) {
    boardArenaScope.emplace(&candidateArena.arena);
    regionScope.emplace(&candidateArena.region);
  } else {
    replayRegion.emplace();
  }
  RoutingBoard* workerBoard = workerBoardState->acquire(baselineBoard);
  if (useCandidateArena) {
    candidateArena.board = workerBoard;
  }
  // A heap worker board (the winner replay) that is not handed to the caller is freed when the
  // call ends, before its replay region is released.
  struct DropUnreturnedBoard {
    RoutingBoard* board = nullptr;
    ~DropUnreturnedBoard() { BasicBoard::discard(board, "replay"); }
  } dropUnreturned;
  if (!useCandidateArena) {
    dropUnreturned.board = workerBoard;
  }
  bool snapshotFinished = false;
  try {
    Item* item = workerBoard->getItem(itemId);
    if (item == nullptr) {
      workerBoardState->finish(false);
      snapshotFinished = true;
      return new (jrt::kOp) BatchOptimizer_CandidateResult(new (jrt::kOp) ItemRouteResult(itemId), nullptr, nullptr, 0, 0);
    }

    FloatPoint* position;
    {
      jrt::HeapScope heap;  // returned to the optimizer: outlives the candidate region
      position = BatchOptimizer::getItemPosition(item);
    }

    RoutingJob* candidateJob = new (jrt::kBoard) RoutingJob();
    // Stage E: the candidate's job and its settings copy are private: freed when it ends.
    struct DropCandidateJob {
      RoutingJob* job;
      ~DropCandidateJob() { RoutingJob::disposePrivate(job); }
    } dropCandidateJob{candidateJob};
    candidateJob->name = job->name;
    candidateJob->shortName = job->shortName;
    RouterSettings::disposeTree(candidateJob->routerSettings);  // the constructor's defaults
    candidateJob->routerSettings = job->routerSettings->clone();
    candidateJob->thread = thread;
    candidateJob->board = workerBoard;
    candidateJob->timeoutAt = job->timeoutAt;

    ItemRouteResult* result =
        BatchOptimizer::optRouteItemOnBoard(
            candidateJob,
            workerBoard,
            item,
            baselineTraceLength,
            withPreferredDirections,
            useIncreasedRipupCosts,
            thread,
            deadlineMs,
            baselineMetrics);

    int64_t cpuUsed = 0;
    if (cpuStart >= 0) {
      int64_t cpuEnd = AutorouteRuntimeMetrics::currentThreadCpuNanos();
      if (cpuEnd >= cpuStart) {
        cpuUsed = cpuEnd - cpuStart;
      }
    }
    int64_t allocUsed = 0;
    if (allocStart >= 0) {
      int64_t allocEnd = AutorouteRuntimeMetrics::currentThreadAllocatedBytes();
      if (allocEnd >= allocStart) {
        allocUsed = allocEnd - allocStart;
      }
    }

    bool improved = result->improved() && keepBoard;
    workerBoardState->finish(improved);
    snapshotFinished = true;
    jrt::HeapScope heap;  // the result outlives the candidate region
    result = new (jrt::kOp) ItemRouteResult(*result);
    if (improved) {
      dropUnreturned.board = nullptr;  // the caller owns it now
      return new (jrt::kOp) BatchOptimizer_CandidateResult(result, workerBoard, position, cpuUsed, allocUsed);
    }
    return new (jrt::kOp) BatchOptimizer_CandidateResult(result, nullptr, position, cpuUsed, allocUsed);
  } catch (...) {
    // Java finally (exceptional path)
    if (!snapshotFinished) {
      workerBoardState->finish(false);
    }
    throw;
  }
}

// ---------------------------------------------------------------------------------------------
// BatchOptimizer_ReadSortedRouteItems
// ---------------------------------------------------------------------------------------------

BatchOptimizer_ReadSortedRouteItems::BatchOptimizer_ReadSortedRouteItems(BatchOptimizer* outer)
    : outer(outer) {
  minItemCoor = new (jrt::kGeom) FloatPoint(jrt::Integer::MIN_VALUE, jrt::Integer::MIN_VALUE);
  minItemLayer = -1;
}

Item* BatchOptimizer_ReadSortedRouteItems::next() {
  RoutingBoard* board = outer->board;
  Item* result = nullptr;
  FloatPoint* currentMinCoor = new (jrt::kGeom) FloatPoint(jrt::Integer::MAX_VALUE, jrt::Integer::MAX_VALUE);
  int32_t currentMinLayer = jrt::Integer::MAX_VALUE;
  auto itScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentVia = jrt::fast_cast<Via>(currentItem)) {
      if (!currentVia->isUserFixed() && !currentVia->hasIgnoredNets()) {
        FloatPoint* currentViaCenter = currentVia->getCenter()->toFloat();
        int32_t currentViaMinLayer = currentVia->firstLayer();
        if (currentViaCenter->x > minItemCoor->x
            || (currentViaCenter->x == minItemCoor->x
                && (currentViaCenter->y > minItemCoor->y
                    || (currentViaCenter->y == minItemCoor->y
                        && currentViaMinLayer > minItemLayer)))) {
          if (currentViaCenter->x < currentMinCoor->x
              || (currentViaCenter->x == currentMinCoor->x
                  && (currentViaCenter->y < currentMinCoor->y
                      || (currentViaCenter->y == currentMinCoor->y
                          && currentViaMinLayer < currentMinLayer)))) {
            currentMinCoor = currentViaCenter;
            currentMinLayer = currentViaMinLayer;
            result = currentVia;
          }
        }
      }
    }
  }
  // Read traces last to prefer vias to traces at the same location
  itScope.restart();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
      if (!currentTrace->isShoveFixed() && !currentTrace->hasIgnoredNets()) {
        FloatPoint* firstCorner = currentTrace->firstCorner()->toFloat();
        FloatPoint* lastCorner = currentTrace->lastCorner()->toFloat();
        FloatPoint* compareCorner;
        if (firstCorner->x < lastCorner->x
            || (firstCorner->x == lastCorner->x && firstCorner->y < lastCorner->y)) {
          compareCorner = lastCorner;
        } else {
          compareCorner = firstCorner;
        }
        int32_t currentTraceLayer = currentTrace->getLayer();
        if (compareCorner->x > minItemCoor->x
            || (compareCorner->x == minItemCoor->x
                && (compareCorner->y > minItemCoor->y
                    || (compareCorner->y == minItemCoor->y && currentTraceLayer > minItemLayer)))) {
          if (compareCorner->x < currentMinCoor->x
              || (compareCorner->x == currentMinCoor->x
                  && (compareCorner->y < currentMinCoor->y
                      || (compareCorner->y == currentMinCoor->y
                          && currentTraceLayer < currentMinLayer)))) {
            bool isConnectedToVia = false;
            jrt::DisposeOnExit<jrt::Set<Item*>> traceContactsOwner(currentTrace->getNormalContacts());
            jrt::Set<Item*>* traceContacts = traceContactsOwner.get();
            for (Item* currentContact : *traceContacts) {
              if (jrt::fast_cast<Via>(currentContact) != nullptr && !currentContact->isUserFixed()) {
                isConnectedToVia = true;
                break;
              }
            }
            if (!isConnectedToVia) {
              currentMinCoor = compareCorner;
              currentMinLayer = currentTraceLayer;
              result = currentTrace;
            }
          }
        }
      }
    }
  }
  minItemCoor = currentMinCoor;
  minItemLayer = currentMinLayer;
  return result;
}

FloatPoint* BatchOptimizer_ReadSortedRouteItems::getCurrentPosition() { return minItemCoor; }

}  // namespace fr
