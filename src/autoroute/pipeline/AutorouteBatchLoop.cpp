#include "autoroute/pipeline/AutorouteBatchLoop.h"

#include "autoroute/BoardHistory.h"
#include "autoroute/PerformanceProfiler.h"
#include "autoroute/events/TaskStateChangedEvent.h"
#include "autoroute/pipeline/AutorouteRuntimeMetrics.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/BatchFanout.h"
#include "autoroute/pipeline/LastMileBlockerRipup.h"
#include "autoroute/pipeline/TaskState.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "core/RouterCounters.h"
#include "core/RouterJobResourceUsage.h"
#include "core/RoutingJob.h"
#include "core/RoutingJobState.h"
#include "core/StoppableThread.h"
#include "core/results/RoutingResultManifest.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "drc/DesignRulesChecker.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Nets.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

static_assert(BatchAutorouter::BOARD_RANK_LIMIT == BoardHistory::MAX_HISTORY_SIZE,
              "BatchAutorouter::BOARD_RANK_LIMIT must equal BoardHistory::MAX_HISTORY_SIZE");

AutorouteBatchLoop::AutorouteBatchLoop(BatchAutorouter* router) : router(router) {}

bool AutorouteBatchLoop::run() {
  RoutingBoard* board = router->board;
  RouterSettings* settings = router->settings;
  if (router->job == nullptr) {
    throw jrt::NullPointerException("routing job");
  }
  RoutingJob* job = router->job;
  StoppableThread* thread = router->thread;
  const bool isOptimizerAutorouter = router->isOptimizerAutorouter;

  bool anyRoutable = false;
  for (int32_t i = 0; i < router->settings->getLayerCount(); i++) {
    if (router->settings->getLayerActive(i) && (*router->board->layerStructure->layers)[i]->isSignal) {
      anyRoutable = true;
      break;
    }
  }
  if (!anyRoutable) {
    FRLogger::warn("Cannot start autorouter: all layers are disabled.");
    router->fireTaskStateChangedEvent(
        new TaskStateChangedEvent(router, TaskState::CANCELLED, 0, router->board->getHash()));
    throw jrt::IllegalArgumentException("Cannot start autorouter: all layers are disabled.");
  }

  router->fireTaskStateChangedEvent(
      new TaskStateChangedEvent(router, TaskState::STARTED, 0, router->board->getHash()));

  int32_t optimizerThreads = settings->getOptimizerMaxThreads();
  job->logInfo(
      "Pipeline thread limits: autorouter pass is single-threaded, optimizer.max_threads=" +
      jrt::str(optimizerThreads) + ".");

  // Capture initial state for session summary
  router->sessionStartTime = jrt::System::currentTimeMillis();
  router->initialUnroutedCount = calculateIncompleteCount(router->board);
  router->resetAntiOscillationState();
  router->lastStopReason = std::nullopt;

  BoardHistory* bh = new BoardHistory(job->routerSettings);

  // Record configuration for profiler
  if (router->settings->getLayerCount() > 0) {
    int32_t layerCount = router->settings->getLayerCount();
    auto* prefCosts = new jrt::Array<double>(layerCount);
    auto* againstCosts = new jrt::Array<double>(layerCount);
    for (int32_t i = 0; i < layerCount; i++) {
      (*prefCosts)[i] = router->settings->getPreferredDirectionTraceCosts(i);
      (*againstCosts)[i] = router->settings->getAgainstPreferredDirectionTraceCosts(i);
    }
    PerformanceProfiler::recordConfiguration(
        router->settings->getViaCosts(), router->settings->getPlaneViaCosts(), prefCosts,
        againstCosts);
  }

  job->logDebug("Checking fanout pre-pass. settings.fanout.enabled=" +
                jrt::str(router->settings->isFanoutEnabled()) +
                ", smdPins=" + jrt::str(router->board->getSmdPins()->size()));
  // PORT NOTE: Instant -> epoch millis (0 == Java null, only read when fanoutBeforeStats != null).
  int64_t fanoutStageStart = 0;
  float fanoutStageCpuStart = -1.0f;
  BoardStatistics* fanoutBeforeStats = nullptr;
  if (router->settings->isFanoutEnabled()) {
    fanoutStageStart = jrt::System::currentTimeMillis();
    fanoutStageCpuStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    fanoutBeforeStats = new BoardStatistics(router->board);
  }
  // Run SMD fanout pre-pass when the board has SMD pins and fanout is enabled
  if (router->settings->isFanoutEnabled()) {
    if (router->board->getSmdPins()->isEmpty()) {
      job->logInfo("Fanout stage is enabled but skipped because the board has no SMD pins.");
    } else {
      const float fanoutCpuSecondsStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
      const float fanoutAllocatedMbStart = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
      float fanoutPeakHeapMbAtStart = AutorouteRuntimeMetrics::currentHeapUsageMb();
      auto* fanoutPeakHeapMbObserved = new jrt::Array<float>(1);
      (*fanoutPeakHeapMbObserved)[0] = fanoutPeakHeapMbAtStart;
      // Count pins that actually need fanout. BatchFanout only processes SMD pins that
      // belong to an active (non-ignored) net, so exclude netless and ignored pins from the
      // total. Among routable pins, count those that are already fully connected (empty
      // unconnected set).
      int32_t netConnectedSmdPins = 0;
      int32_t alreadyConnectedAtStart = 0;
      int32_t ignoredSmdPins = 0;
      for (Pin* pin : *router->board->getSmdPins()) {
        if (pin->netCount() > 0) {
          if (pin->hasIgnoredNets()) {
            ignoredSmdPins++;
          } else {
            netConnectedSmdPins++;
            if (pin->getUnconnectedSet(pin->getNetNumber(0))->isEmpty()) {
              alreadyConnectedAtStart++;
            }
          }
        }
      }
      int32_t pinsToFanout = netConnectedSmdPins - alreadyConnectedAtStart;
      int32_t netlessPins = router->board->getSmdPins()->size() - netConnectedSmdPins - ignoredSmdPins;
      job->logInfo("Fanout stage started on board '" + router->board->getHash() + "' with " +
                   jrt::str(pinsToFanout) + " of " + jrt::str(router->board->getSmdPins()->size()) +
                   " SMD pins needing fanout (" + jrt::str(alreadyConnectedAtStart) +
                   " already connected, " + jrt::str(netlessPins) + " netless, " +
                   jrt::str(ignoredSmdPins) + " ignored).");
      BatchAutorouter* r = router;
      BatchFanout_FanoutRunSummary* fanoutSummary = BatchFanout::fanoutBoard(
          router->board,
          router->settings,
          router->thread,
          BatchFanout_FanoutProgressListener::of(
              [r, job, fanoutPeakHeapMbObserved](BatchFanout_FanoutPassStatus* status) {
                (*fanoutPeakHeapMbObserved)[0] = jrt::Math::max(
                    (*fanoutPeakHeapMbObserved)[0], AutorouteRuntimeMetrics::currentHeapUsageMb());
                RouterCounters* fanoutCounters = new RouterCounters();
                fanoutCounters->phase = std::string("fanout");
                fanoutCounters->passCount = status->passNo();
                fanoutCounters->queuedToBeRoutedCount = status->pinsToGo();
                fanoutCounters->routedCount = status->routedCount();
                fanoutCounters->skippedCount = 0;
                fanoutCounters->rippedCount = 0;
                fanoutCounters->failedToBeRoutedCount =
                    status->notRoutedCount() + status->insertErrorCount();
                fanoutCounters->incompleteCount =
                    status->boardStatistics()->connections->incompleteCount;
                fanoutCounters->fanoutExtraViasCount = status->extraViasThisPass();
                r->fireBoardUpdatedEvent(status->boardStatistics(), fanoutCounters, r->board);

                if (status->passCompleted()) {
                  std::string boardHash = r->board->getHash();
                  std::string fanoutMessage = jrt::format(
                      "Fanout pass #%d on board '%s' completed in %.2f seconds with "
                      "%d SMD pin%s fanouted, %d not routed, %d insert error%s, "
                      "+%d extra via%s (%d SMD pin%s still to check in pass, "
                      "ripup costs=%d).",
                      status->passNo(),
                      boardHash.c_str(),
                      status->passDurationMillis() / 1000.0,
                      status->routedCount(),
                      status->routedCount() == 1 ? "" : "s",
                      status->notRoutedCount(),
                      status->insertErrorCount(),
                      status->insertErrorCount() == 1 ? "" : "s",
                      status->extraViasThisPass(),
                      status->extraViasThisPass() == 1 ? "" : "s",
                      status->pinsToGo(),
                      status->pinsToGo() == 1 ? "" : "s",
                      status->ripupCosts());
                  job->logInfo(fanoutMessage);
                }
              }),
          [job](const std::string& message) { job->logInfo(message); });
      router->fanoutTimedOut = fanoutSummary->isTimedOut();

      float fanoutCpuSecondsEnd = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
      float fanoutAllocatedMbEnd = AutorouteRuntimeMetrics::currentThreadAllocatedMb();

      float fanoutCpuSecondsUsed;
      if (fanoutCpuSecondsStart >= 0.0f && fanoutCpuSecondsEnd >= fanoutCpuSecondsStart) {
        fanoutCpuSecondsUsed = fanoutCpuSecondsEnd - fanoutCpuSecondsStart;
      } else {
        float snapshot = AutorouteRuntimeMetrics::cpuSecondsSnapshot(job);
        if (snapshot > 0.0f) {
          fanoutCpuSecondsUsed = snapshot;
        } else {
          fanoutCpuSecondsUsed =
              (fanoutCpuSecondsStart < 0.0f || fanoutCpuSecondsEnd < 0.0f) ? -1.0f : 0.0f;
        }
      }

      float fanoutAllocatedMb;
      if (fanoutAllocatedMbStart >= 0.0f && fanoutAllocatedMbEnd >= fanoutAllocatedMbStart) {
        fanoutAllocatedMb = fanoutAllocatedMbEnd - fanoutAllocatedMbStart;
      } else {
        float snapshot = AutorouteRuntimeMetrics::allocatedMemoryMbSnapshot(job);
        if (snapshot > 0.0f) {
          fanoutAllocatedMb = snapshot;
        } else {
          fanoutAllocatedMb =
              (fanoutAllocatedMbStart < 0.0f || fanoutAllocatedMbEnd < 0.0f) ? -1.0f : 0.0f;
        }
      }

      float fanoutPeakHeapMb = jrt::Math::max(
          (*fanoutPeakHeapMbObserved)[0], AutorouteRuntimeMetrics::currentHeapUsageMb());
      fanoutPeakHeapMb =
          jrt::Math::max(fanoutPeakHeapMb, AutorouteRuntimeMetrics::peakHeapMbSnapshot(job));
      BatchFanout_EscapeStatistics* finalEscape = fanoutSummary->escapeStatistics();
      std::string fanoutCompletionStatus =
          fanoutSummary->isTimedOut()
              ? "completed with timeout:"
              : (router->thread->isStopAutoRouterRequested() ? "interrupted:" : "completed:");
      std::string cpuText =
          AutorouteRuntimeMetrics::formatOptionalMetric(fanoutCpuSecondsUsed, "%.2f");
      std::string allocText = AutorouteRuntimeMetrics::formatOptionalMetric(
          fanoutAllocatedMb < 0.0f ? -1.0f : fanoutAllocatedMb / 1024.0f, "%.2f");
      std::string fanoutSummaryMessage = jrt::format(
          "Fanout stage %s started with %d total SMD pins, completed in %.2f seconds, "
          "escaped pins: %d/%d (%.1f%%), using %s total CPU seconds, "
          "%s GB total allocated, and %.1f MB peak heap usage.",
          fanoutCompletionStatus.c_str(),
          finalEscape->totalSmdPins(),
          fanoutSummary->totalDurationMillis() / 1000.0,
          finalEscape->escapedCount(),
          finalEscape->totalSmdPins(),
          finalEscape->escapedPercentage(),
          cpuText.c_str(),
          allocText.c_str(),
          static_cast<double>(fanoutPeakHeapMb));
      job->logInfo(fanoutSummaryMessage);
    }
  }

  if (fanoutBeforeStats != nullptr) {
    BoardStatistics* fanoutAfterStats = new BoardStatistics(router->board);
    RoutingResultManifest_PhaseDetail* phase = job->resultPhaseMetrics->fanout;
    phase->before = new RoutingResultManifest_PhaseSnapshot();
    phase->before->boardStatistics = fanoutBeforeStats;
    phase->before->scoreSource = std::string("not_applicable");
    phase->after = new RoutingResultManifest_PhaseSnapshot();
    phase->after->boardStatistics = fanoutAfterStats;
    phase->after->scoreSource = std::string("not_applicable");
    phase->durationSeconds =
        static_cast<float>((jrt::System::currentTimeMillis() - fanoutStageStart) / 1000.0);
    float fanoutStageCpuEnd = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    if (fanoutStageCpuStart >= 0.0f && fanoutStageCpuEnd >= fanoutStageCpuStart) {
      phase->cpuSeconds = fanoutStageCpuEnd - fanoutStageCpuStart;
    }
  }

  int32_t currentUnrouted = calculateIncompleteCount(router->board);
  bool isRouterEnabled =
      router->settings->getRunRouter() &&
      (!router->settings->autorouter->maxPasses.has_value() ||
       *router->settings->autorouter->maxPasses >= 0);
  // PORT NOTE: Instant -> epoch millis (only read when autorouterBeforeStats != null).
  int64_t autorouterStageStart = 0;
  float autorouterCpuStart = -1.0f;
  float autorouterAllocatedStart = -1.0f;
  BoardStatistics* autorouterBeforeStats = nullptr;
  if (isRouterEnabled) {
    autorouterStageStart = jrt::System::currentTimeMillis();
    autorouterCpuStart = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    autorouterAllocatedStart = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
    autorouterBeforeStats = new BoardStatistics(router->board);
    float routerScoreBefore =
        (new BoardStatistics(router->board))->getRouterScore(job->routerSettings);
    job->logInfo(jrt::format(
        "Auto-routing stage started on board '%s' with baseline score %.2f for %d "
        "unrouted item%s.",
        router->board->getHash().c_str(),
        static_cast<double>(routerScoreBefore),
        currentUnrouted,
        currentUnrouted == 1 ? "" : "s"));
  }
  bool continueAutorouting = isRouterEnabled;
  if (isRouterEnabled) {
    router->passLoopStartHash = router->board->getHash();
    if (router->repeatsEarlierPassLoop(router->passLoopStartHash)) {
      router->lastStopReason = BatchAutorouter_StopReason::REPEATED;
      continueAutorouting = false;
      job->logInfo(
          "Pass loop skipped: fanout left the board an earlier pass loop with the same settings "
          "started on.");
    }
  }

  int32_t currentPass = 1;
  int32_t consecutiveNoImprovementPasses = 0;
  bool fanoutRecoveryApplied = false;
  int32_t lastMileAttempts = 0;
  float lastBestScore = jrt::Float::NEGATIVE_INFINITY;  // score at last board-restore or improvement
  float globalBestScore = jrt::Float::NEGATIVE_INFINITY;  // best score seen across all passes
  int32_t passOfBestScore = 0;  // pass where globalBestScore was achieved
  int32_t incompleteCountAtBestScore = 0;  // incomplete count when globalBestScore was recorded
  const int32_t stagnationPassLimit =
      (router->settings->autorouter != nullptr &&
       router->settings->autorouter->stagnationPassLimit.has_value())
          ? *router->settings->autorouter->stagnationPassLimit
          : BatchAutorouter::STAGNATION_PASS_LIMIT;
  // Track board hashes that have already been routed (same-hash stop currently disabled).
  jrt::Set<std::string>* alreadyRoutedBoardHashes = new jrt::HashSet<std::string>();
  while (continueAutorouting && !router->thread->isStopAutoRouterRequested()) {
    if (job->state == RoutingJobState::TIMED_OUT) {
      stopFor(BatchAutorouter_StopReason::TIMED_OUT);
    }

    std::string currentBoardHash = router->board->getHash();

    // Same-hash stop disabled because ripup budgets and random seeds change per-pass, making
    // progress possible in later passes.

    if (router->settings->autorouter->maxPasses.has_value() &&
        *router->settings->autorouter->maxPasses > 0 &&
        currentPass > *router->settings->autorouter->maxPasses) {
      stopFor(BatchAutorouter_StopReason::MAX_PASSES);
      break;
    }

    job->setCurrentPass(currentPass);

    router->fireTaskStateChangedEvent(
        new TaskStateChangedEvent(router, TaskState::RUNNING, currentPass, currentBoardHash));

    bh->add(router->board);

    const std::string passStartBoardHash = currentBoardHash;
    FRLogger::traceEntry("BatchAutorouter.autoroute_pass #" + jrt::str(currentPass) +
                         " on board '" + passStartBoardHash + "'");

    continueAutorouting = autoroutePass(currentPass);

    BoardStatistics* boardStatisticsAfter = new BoardStatistics(router->board);
    float boardScoreAfter = boardStatisticsAfter->getRouterScore(job->routerSettings);

    if ((bh->size() >= BatchAutorouter::STOP_AT_PASS_MINIMUM) ||
        (router->thread->isStopAutoRouterRequested())) {
      if (((currentPass % BatchAutorouter::STOP_AT_PASS_MODULO == 0) &&
           (currentPass >= BatchAutorouter::STOP_AT_PASS_MINIMUM)) ||
          (router->thread->isStopAutoRouterRequested())) {
        // Check if the score improved compared to the previous passes, restore a
        // previous board if not. Use strict ">" so that equally-scored boards do NOT
        // trigger a restore.
        if (bh->getMaxScore() > boardScoreAfter) {
          RoutingBoard* boardToRestore =
              bh->restoreBoard(BatchAutorouter::MAXIMUM_TRIES_ON_THE_SAME_BOARD);
          if (boardToRestore == nullptr) {
            job->logInfo("The router was not able to improve the board, stopping the auto-router.");
            stopFor(BatchAutorouter_StopReason::NO_IMPROVEMENT);
            break;
          }

          int32_t boardToRestoreRank = bh->getRank(boardToRestore);

          if (stagnationPassLimit > 0 && boardToRestoreRank > BatchAutorouter::BOARD_RANK_LIMIT) {
            stopFor(BatchAutorouter_StopReason::STAGNATION);
            break;
          }

          router->board = boardToRestore;
          board = router->board;
          router->resetAntiOscillationState();
          BoardStatistics* boardStatistics = router->board->getStatistics();
          // Reset pass-local stagnation counter when restoring a previous board state
          consecutiveNoImprovementPasses = 0;
          boardStatisticsAfter = boardStatistics;
          boardScoreAfter = boardStatisticsAfter->getRouterScore(job->routerSettings);
          lastBestScore = boardScoreAfter;
          currentBoardHash = router->board->getHash();
          // Reset the same-hash set after a board restore.
          alreadyRoutedBoardHashes->clear();
          job->logDebug("Restoring an earlier board that has the score of " +
                        FRLogger::formatScore(
                            boardScoreAfter,
                            boardStatisticsAfter->connections->incompleteCount.value(),
                            boardStatisticsAfter->clearanceViolations->totalCount.value()) +
                        ".");
        }
      }
    }
    double autorouterPassDuration = FRLogger::traceExit(
        "BatchAutorouter.autoroute_pass #" + jrt::str(currentPass) + " on board '" +
        passStartBoardHash + "'");

    std::string strategyTag =
        router->getStrategyName().has_value() ? " [" + *router->getStrategyName() + "]" : "";
    std::string scoreText =
        FRLogger::formatScore(boardScoreAfter,
                              boardStatisticsAfter->connections->incompleteCount.value(),
                              boardStatisticsAfter->clearanceViolations->totalCount.value());
    std::string passCompletedMessage = jrt::format(
        "Auto-routing pass #%d%s on board '%s' was completed in %.2f seconds with score %s",
        currentPass,
        strategyTag.c_str(),
        currentBoardHash.c_str(),
        autorouterPassDuration,
        scoreText.c_str());
    if (job->resourceUsage->cpuTimeUsed > 0) {
      passCompletedMessage += jrt::format(
          ", using %.2f CPU seconds and the job allocated %.2f GB of memory so far.",
          static_cast<double>(job->resourceUsage->cpuTimeUsed),
          static_cast<double>(job->resourceUsage->maxMemoryUsed / 1024.0f));
    } else {
      passCompletedMessage += ".";
    }
    if (!isOptimizerAutorouter) {
      job->logInfo(passCompletedMessage);
    }

    DesignRulesChecker* tempDrc = new DesignRulesChecker(router->board, nullptr);
    tempDrc->calculateAllIncompletes();
    std::string perNetBreakdown;
    for (int32_t netNumber = 1; netNumber <= router->board->rules->nets->maxNetNumber();
         netNumber++) {
      int32_t netIncomplete = tempDrc->getIncompleteCount(netNumber);
      if (netIncomplete > 0) {
        FRLogger::trace("BatchAutorouter.autoroute_pass",
                        "compare_unrouted_net",
                        "pass=" + jrt::str(currentPass) + ", net=" + jrt::str(netNumber) +
                            ", incomplete=" + jrt::str(netIncomplete),
                        "Net #" + jrt::str(netNumber),
                        new jrt::Array<Point*>(0));
        if (!perNetBreakdown.empty()) {
          perNetBreakdown += ',';
        }
        perNetBreakdown += jrt::str(netNumber);
        perNetBreakdown += '=';
        perNetBreakdown += jrt::str(netIncomplete);
      }
    }
    FRLogger::trace("BatchAutorouter.autoroute_pass",
                    "compare_unrouted_breakdown",
                    "pass=" + jrt::str(currentPass) + ", total=" +
                        jrt::str(tempDrc->getIncompleteCount()) + ", breakdown=" + perNetBreakdown,
                    "",
                    new jrt::Array<Point*>(0));

    if (router->settings->autorouter->saveIntermediateStages.has_value() &&
        *router->settings->autorouter->saveIntermediateStages) {
      fireBoardSnapshotEvent(router->board);
    }

    // Stagnation detection: abort when the normalized score hasn't improved by
    // at least STAGNATION_SCORE_THRESHOLD over STAGNATION_PASS_LIMIT consecutive
    // passes, whenever the router is still actively running after the mandatory minimum passes.
    if (currentPass >= BatchAutorouter::STOP_AT_PASS_MINIMUM && continueAutorouting) {
      // --- Pass-local counter (resets after board restores) ---
      if (boardScoreAfter > lastBestScore + BatchAutorouter::STAGNATION_SCORE_THRESHOLD) {
        consecutiveNoImprovementPasses = 0;
        lastBestScore = boardScoreAfter;
      } else {
        consecutiveNoImprovementPasses++;

        // One-time recovery for fanout-enabled jobs: aggressively remove tails, including
        // fanout vias, when score plateaus with remaining incompletes.
        if (router->settings->isFanoutEnabled() && !fanoutRecoveryApplied &&
            boardStatisticsAfter->connections->incompleteCount.value() > 0 &&
            consecutiveNoImprovementPasses >= BatchAutorouter::FANOUT_RECOVERY_STAGNATION_PASSES) {
          const int32_t incompletesBeforeRecovery =
              boardStatisticsAfter->connections->incompleteCount.value();
          removeTails(Item_StopConnectionOption::NONE);
          boardStatisticsAfter = new BoardStatistics(router->board);
          boardScoreAfter = boardStatisticsAfter->getRouterScore(job->routerSettings);
          lastBestScore = boardScoreAfter;
          consecutiveNoImprovementPasses = 0;
          passOfBestScore = currentPass;
          fanoutRecoveryApplied = true;
          alreadyRoutedBoardHashes->clear();
          job->logDebug(
              "Applied one-time fanout recovery cleanup (removed fanout tails/vias). "
              "Incompletes: " +
              jrt::str(incompletesBeforeRecovery) + " -> " +
              jrt::str(boardStatisticsAfter->connections->incompleteCount) + ".");
        }

        int32_t incompleteNow = boardStatisticsAfter->connections->incompleteCount.value();
        if (incompleteNow > 0 && incompleteNow <= BatchAutorouter::LAST_MILE_INCOMPLETE_LIMIT &&
            lastMileAttempts < BatchAutorouter::LAST_MILE_MAX_ATTEMPTS &&
            consecutiveNoImprovementPasses >= BatchAutorouter::LAST_MILE_STAGNATION_PASSES) {
          int32_t removed = LastMileBlockerRipup::ripBlockers(router->board, router->ripupMemory);
          lastMileAttempts++;
          if (removed > 0) {
            boardStatisticsAfter = new BoardStatistics(router->board);
            boardScoreAfter = boardStatisticsAfter->getRouterScore(job->routerSettings);
            lastBestScore = boardScoreAfter;
            consecutiveNoImprovementPasses = 0;
            alreadyRoutedBoardHashes->clear();
            job->logInfo("Last-mile rip-up removed " + jrt::str(removed) +
                         " blocking trace(s) or via(s) around " + jrt::str(incompleteNow) +
                         " remaining connection(s).");
          }
        }

        if (stagnationPassLimit > 0 && consecutiveNoImprovementPasses >= stagnationPassLimit) {
          std::string report = buildUnroutedConnectionsReport();
          int32_t incompleteCount = boardStatisticsAfter->connections->incompleteCount.value();
          job->logInfo(
              "The router's score (" + FRLogger::defaultFloatFormat()->format(boardScoreAfter) +
              ") has not improved by more than " +
              jrt::str(BatchAutorouter::STAGNATION_SCORE_THRESHOLD) + " points in the last " +
              jrt::str(stagnationPassLimit) + " passes (" + jrt::str(incompleteCount) + " item" +
              (incompleteCount == 1 ? "" : "s") +
              " still unconnected). Stopping the auto-router.\n"
              "The following connections could not be routed -- please review your design "
              "(e.g. check pad clearances, trace width rules, and available routing "
              "space):\n" +
              report);
          stopFor(BatchAutorouter_StopReason::STAGNATION);
          break;
        }
      }

      // --- Global best tracker (not reset by board restores) ---
      if (boardScoreAfter > globalBestScore + BatchAutorouter::STAGNATION_SCORE_THRESHOLD) {
        globalBestScore = boardScoreAfter;
        passOfBestScore = currentPass;
        incompleteCountAtBestScore = boardStatisticsAfter->connections->incompleteCount.value();
      } else if (stagnationPassLimit > 0 && (currentPass - passOfBestScore) >= stagnationPassLimit) {
        std::string report = buildUnroutedConnectionsReport();
        job->logInfo(
            "The router's best score (" + FRLogger::defaultFloatFormat()->format(globalBestScore) +
            ") has not improved by more than " +
            jrt::str(BatchAutorouter::STAGNATION_SCORE_THRESHOLD) + " points since pass #" +
            jrt::str(passOfBestScore) + ". Stopping the auto-router after " +
            jrt::str(currentPass) + " passes (" + jrt::str(incompleteCountAtBestScore) + " item" +
            (incompleteCountAtBestScore == 1 ? "" : "s") +
            " still unconnected).\n"
            "The following connections could not be routed -- please review your design "
            "(e.g. check pad clearances, trace width rules, and available routing space):\n" +
            report);
        stopFor(BatchAutorouter_StopReason::STAGNATION);
        break;
      }

    } else if (boardStatisticsAfter->connections->incompleteCount.value() == 0 &&
               boardScoreAfter > BatchAutorouter::STAGNATION_SCORE_THRESHOLD) {
      // Board is fully routed AND has a positive score (genuine success).
      consecutiveNoImprovementPasses = 0;
      lastBestScore = boardScoreAfter;
    }

    // check if there are still unrouted items
    if (continueAutorouting && !router->thread->isStopAutoRouterRequested()) {
      currentPass++;
    }
  }
  (void)board;

  // Ensure we finish with the best board ever seen during this routing session.
  float currentFinalScore =
      (new BoardStatistics(router->board))->getRouterScore(job->routerSettings);
  float bestHistoryScore = bh->getMaxScore();
  if (bestHistoryScore > currentFinalScore) {
    RoutingBoard* bestBoard = bh->restoreBestBoard();
    if (bestBoard != nullptr) {
      BoardStatistics* currentStats = new BoardStatistics(router->board);
      router->board = bestBoard;
      BoardStatistics* bestStats = new BoardStatistics(router->board);
      job->logDebug(
          "The final board state (score " +
          FRLogger::formatScore(currentFinalScore,
                                currentStats->connections->incompleteCount.value(),
                                currentStats->clearanceViolations->totalCount.value()) +
          ") is worse than the best board seen during routing (score " +
          FRLogger::formatScore(bestStats->getRouterScore(job->routerSettings),
                                bestStats->connections->incompleteCount.value(),
                                bestStats->clearanceViolations->totalCount.value()) +
          "). Restoring the best board as the final result.");
    }
  }

  job->board = router->board;

  if (!router->lastStopReason.has_value()) {
    if (job->state == RoutingJobState::TIMED_OUT) {
      router->lastStopReason = BatchAutorouter_StopReason::TIMED_OUT;
    } else if (thread->isStopRequested()) {
      router->lastStopReason = BatchAutorouter_StopReason::CANCELLED;
    } else if (thread->isStopAutoRouterRequested()) {
      router->lastStopReason = BatchAutorouter_StopReason::STAGNATION;
    } else {
      router->lastStopReason = BatchAutorouter_StopReason::COMPLETED;
    }
  }

  bool wasRouterRun = router->settings->getRunRouter() &&
                      (!router->settings->autorouter->maxPasses.has_value() ||
                       *router->settings->autorouter->maxPasses >= 0);
  if (wasRouterRun && !router->thread->isStopAutoRouterRequested() &&
      router->lastStopReason != BatchAutorouter_StopReason::REPEATED) {
    // clean up dangling tails and unused orphan fanout vias when autorouting finishes normally.
    removeTails(Item_StopConnectionOption::NONE);
  }

  if (isRouterEnabled && autorouterBeforeStats != nullptr) {
    BoardStatistics* autorouterAfterStats = new BoardStatistics(router->board);
    RoutingResultManifest_PhaseDetail* phase = job->resultPhaseMetrics->autorouter;
    phase->before = RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
        autorouterBeforeStats, job->routerSettings, std::string("current"));
    phase->before->score = phase->before->routerScore;
    phase->after = RoutingResultManifest_PhaseSnapshot::fromBoardStatistics(
        autorouterAfterStats, job->routerSettings, std::string("current"));
    phase->after->score = phase->after->routerScore;
    phase->passesCompleted = jrt::Math::max(0, currentPass);
    phase->durationSeconds =
        static_cast<float>((jrt::System::currentTimeMillis() - autorouterStageStart) / 1000.0);
    float autorouterCpuEnd = AutorouteRuntimeMetrics::currentThreadCpuSeconds();
    if (autorouterCpuStart >= 0.0f && autorouterCpuEnd >= autorouterCpuStart) {
      phase->cpuSeconds = autorouterCpuEnd - autorouterCpuStart;
    }
    float autorouterAllocatedEnd = AutorouteRuntimeMetrics::currentThreadAllocatedMb();
    if (autorouterAllocatedStart >= 0.0f && autorouterAllocatedEnd >= autorouterAllocatedStart) {
      phase->totalAllocatedGb = (autorouterAllocatedEnd - autorouterAllocatedStart) / 1024.0f;
    }
    phase->peakHeapMb = AutorouteRuntimeMetrics::peakHeapMbSnapshot(job);
  }

  bh->clear();

  if (router->startNeckedRouted + router->startNeckedFailed > 0) {
    job->logInfo("Start necked: " + jrt::str(router->startNeckedRouted) +
                 " connection(s) of nets that needed the neck before routed at the neck width "
                 "without a class-width search, " +
                 jrt::str(router->startNeckedFailed) + " did not route.");
  }

  // Print all profiling results at the end of session
  PerformanceProfiler::printResults();
  PerformanceProfiler::reset();

  if (!router->thread->isStopAutoRouterRequested()) {
    router->fireTaskStateChangedEvent(new TaskStateChangedEvent(
        router, TaskState::FINISHED, currentPass, router->board->getHash()));
  } else {
    // Distinguish between a user-requested cancellation and a job timeout so that
    // API consumers can tell the two apart via TaskStateChangedEvent.
    bool isTimedOut = job->state == RoutingJobState::TIMED_OUT;
    router->fireTaskStateChangedEvent(new TaskStateChangedEvent(
        router, isTimedOut ? TaskState::TIMED_OUT : TaskState::CANCELLED, currentPass,
        router->board->getHash()));
  }

  return !router->thread->isStopAutoRouterRequested();
}

int32_t AutorouteBatchLoop::calculateIncompleteCount(RoutingBoard* board) {
  return router->calculateIncompleteCount(board);
}

void AutorouteBatchLoop::removeTails(Item_StopConnectionOption option) {
  router->removeTails(option);
}

bool AutorouteBatchLoop::autoroutePass(int32_t passNo) { return router->autoroutePass(passNo); }

std::string AutorouteBatchLoop::buildUnroutedConnectionsReport() {
  return router->buildUnroutedConnectionsReport();
}

void AutorouteBatchLoop::fireBoardSnapshotEvent(RoutingBoard* board) {
  router->fireBoardSnapshotEvent(board);
}

void AutorouteBatchLoop::stopFor(BatchAutorouter_StopReason reason) {
  if (!router->lastStopReason.has_value()) {
    router->lastStopReason = reason;
  }
  router->thread->requestStopAutoRouter();
}

}  // namespace fr
