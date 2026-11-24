#include "autoroute/pipeline/RoutingFallbackRunner.h"

#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/RetryNeighbourhood.h"
#include "autoroute/pipeline/RoutingFallback.h"
#include "autoroute/pipeline/RoutingSupervisor.h"
#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "core/RoutingJob.h"
#include "core/RoutingJobState.h"
#include "core/StoppableThread.h"
#include "drc/ClearanceBaseline.h"
#include "io/kicad/KiCadProject.h"
#include "settings/RouterSettings.h"

namespace fr {

namespace {
/** Java passLoopStarts.get(hash) with a nullable hash (a null key is never stored). */
RoutingFallback_Outcome* getByHash(jrt::Map<std::string, RoutingFallback_Outcome*>* map,
                                   const std::optional<std::string>& hash) {
  if (!hash.has_value()) {
    return nullptr;
  }
  return map->get(*hash);
}
}  // namespace

bool RoutingFallbackRunner_Attempt::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<RoutingFallbackRunner_Attempt>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(outcome_, other->outcome_) && jrt::equalsOf(board_, other->board_) &&
         jrt::equalsOf(router_, other->router_) && jrt::equalsOf(baseline_, other->baseline_);
}

int32_t RoutingFallbackRunner_Attempt::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(outcome_));
  h = jrt::hashCombine(h, jrt::hashOf(board_));
  h = jrt::hashCombine(h, jrt::hashOf(router_));
  h = jrt::hashCombine(h, jrt::hashOf(baseline_));
  return h;
}

std::string RoutingFallbackRunner_Attempt::toString() {
  return "Attempt[outcome=" + jrt::str(outcome_) + ", board=" + jrt::str(board_) + ", router=" + jrt::str(router_) +
         ", baseline=" + jrt::str(baseline_) + "]";
}

RoutingFallbackRunner::RoutingFallbackRunner(BatchAutorouter* listenerSource)
    : passLoopStarts(new (jrt::kOp) jrt::LinkedHashMap<std::string, RoutingFallback_Outcome*>()),
      listenerSource(listenerSource) {}

BatchAutorouter* RoutingFallbackRunner::run(RoutingJob* job, BatchAutorouter* defaultRouter,
                                            jrt::List<RoutingFallback_Strategy*>* strategies, BasicBoard* input) {
  ClearanceBaseline* inputBaseline = job->board->clearanceBaseline;
  RoutingFallback_Outcome* best = RoutingFallback_Outcome::of("default", job->board, job->routerSettings);
  if (best->isCompleteAndClean()) {
    return defaultRouter;
  }
  std::optional<BatchAutorouter_StopReason> reason = defaultRouter->lastStopReason;
  if (!RoutingFallback::isRetryable(reason)) {
    job->logInfo("Routing fallback skipped: the pass loop stopped at its " + RoutingFallback::describe(reason) +
                 " with " + jrt::str(best) + ".");
    return defaultRouter;
  }
  bool fanoutRuns = job->routerSettings->isFanoutEnabled() && !job->board->getSmdPins()->isEmpty();
  jrt::List<RoutingFallback_Strategy*>* applicable =
      RoutingFallback::applicable(strategies, job->routerSettings, fanoutRuns);
  jrt::List<RoutingFallback_Strategy*>* continuations = new (jrt::kOp) jrt::ArrayList<RoutingFallback_Strategy*>();
  jrt::List<RoutingFallback_Strategy*>* alternatives = new (jrt::kOp) jrt::ArrayList<RoutingFallback_Strategy*>();
  for (RoutingFallback_Strategy* strategy : *applicable) {
    if (strategy->continueBest) {
      continuations->add(strategy);
    } else {
      alternatives->add(strategy);
    }
  }
  bool residual = RoutingSupervisor::residual(job->routerSettings, best->unrouted);
  if (alternatives->isEmpty() && continuations->isEmpty() && !residual) {
    return defaultRouter;
  }
  job->logInfo("Routing fallback: the default configuration stopped at " + RoutingFallback::describe(reason) +
               " with " + jrt::str(best) + "; trying " + jrt::str(alternatives->size()) + " alternative " +
               (alternatives->size() == 1 ? "strategy" : "strategies") +
               (residual ? ", then continuing the best board" : "") + ".");
  if (defaultRouter->passLoopStartHash.has_value()) {
    this->passLoopStarts->put(*defaultRouter->passLoopStartHash, best);
  }
  ClearanceBaseline* defaultClearance = RoutingFallback::captureClearance(job->board, best->violations);
  RoutingFallbackRunner_Attempt* bestAttempt =
      new (jrt::kOp) RoutingFallbackRunner_Attempt(best, job->board, defaultRouter, defaultClearance);
  RoutingBoard* continuedFrom = nullptr;
  std::vector<RoutingFallbackRunner_Attempt*> repeatedAttempts;
  if (residual && !alternatives->isEmpty()) {
    bestAttempt = runContinuations(job, bestAttempt, automaticContinuations(job));
    continuedFrom = bestAttempt->board_;
  }
  for (RoutingFallback_Strategy* strategy : *alternatives) {
    if (stopped(job) || bestAttempt->outcome_->isCompleteAndClean()) {
      break;
    }
    RoutingFallbackRunner_Attempt* attempt = runAttempt(job, input, strategy, false, inputBaseline);
    if (attempt == nullptr) {
      continue;
    }
    if (attempt->router_->lastStopReason == BatchAutorouter_StopReason::REPEATED) {
      RoutingFallback_Outcome* repeated = getByHash(this->passLoopStarts, attempt->router_->passLoopStartHash);
      job->logInfo("Routing fallback '" + strategy->name +
                   "': its fanout left the board an earlier pass loop started on; stopped after the fanout (" +
                   jrt::str(repeated != nullptr ? repeated : attempt->outcome_) + ").");
      repeatedAttempts.push_back(attempt);  // freed at the end: job->board still refers to it
      if (stopAlternatives(bestAttempt->outcome_, attempt->router_->lastStopReason)) {
        break;
      }
      continue;
    }
    if (strategy->fanoutOnly && attempt->router_->passLoopStartHash.has_value() &&
        attempt->router_->lastStopReason != BatchAutorouter_StopReason::TIMED_OUT) {
      this->passLoopStarts->putIfAbsent(*attempt->router_->passLoopStartHash, attempt->outcome_);
    }
    job->logInfo("Routing fallback '" + strategy->name + "': " + jrt::str(attempt->outcome_) + " after " +
                 RoutingFallback::describe(attempt->router_->lastStopReason) + ".");
    if (replaces(attempt, bestAttempt)) {
      RoutingFallbackRunner_Attempt* replaced = bestAttempt;
      bestAttempt = attempt;
      if (replaced->board_ != continuedFrom) dropAttempt(job, replaced, bestAttempt);
    } else {
      job->board = bestAttempt->board_;
      dropAttempt(job, attempt, bestAttempt);
    }
    if (stopAlternatives(bestAttempt->outcome_, attempt->router_->lastStopReason)) {
      break;
    }
  }
  jrt::List<RoutingFallback_Strategy*>* after = new (jrt::kOp) jrt::ArrayList<RoutingFallback_Strategy*>(continuations);
  if (continuedFrom != nullptr && bestAttempt->board_ == continuedFrom) {
    if (!bestAttempt->outcome_->isCompleteAndClean()) {
      job->logInfo(
          "Routing fallback: no alternative replaced the board the continuations ran on; not continuing it again.");
    }
  } else {
    after->addAll(automaticContinuations(job));
  }
  if (!after->isEmpty()) {
    bestAttempt = runContinuations(job, bestAttempt, after);
  }
  job->board = bestAttempt->board_;
  for (RoutingFallbackRunner_Attempt* a : repeatedAttempts) dropAttempt(job, a, bestAttempt);
  job->logInfo("Routing fallback chose '" + bestAttempt->outcome_->strategy + "': " + jrt::str(bestAttempt->outcome_) +
               ".");
  warnIfAutoNeckFallbackUsed(job, bestAttempt->outcome_->strategy);
  return bestAttempt->router_;
}

void RoutingFallbackRunner::warnIfAutoNeckFallbackUsed(RoutingJob* job, const std::string& strategy) {
  if ("continue-auto-neck" == strategy && job->routerSettings->getNeckWidthUm() <= 0) {
    job->logWarning(
        "Fallback auto-neckdown (neck_width_um=150.0) was used because routing remained incomplete with no explicit "
        "neck_width_um configured.");
  }
}

BatchAutorouter* RoutingFallbackRunner::explore(RoutingJob* job, BatchAutorouter* incumbent) {
  if (!RoutingSupervisor::explores(job->routerSettings) || stopped(job)) {
    return incumbent;
  }
  RoutingSupervisor* supervisor = new (jrt::kOp) RoutingSupervisor();
  RoutingFallback_Outcome* best = RoutingFallback_Outcome::of("incumbent", job->board, job->routerSettings);
  RoutingFallbackRunner_Attempt* bestAttempt = new (jrt::kOp) RoutingFallbackRunner_Attempt(best, job->board, incumbent, nullptr);
  RouterSettings* jobSettings = job->routerSettings;
  int32_t round = 0;
  while (!bestAttempt->outcome_->isCompleteAndClean() && !supervisor->explorationExhausted(jobSettings) &&
         !stopped(job)) {
    round++;
    RoutingSupervisor_Attempt* planned = supervisor->nextAttempt(round, jobSettings, bestAttempt->board_);
    RouterSettings* settings = jobSettings->clone();
    RoutingSupervisor::apply(planned, jobSettings, settings);
    // PORT NOTE: byte[] snapshot -> BasicBoard* frozen snapshot.
    BasicBoard* input;
    try {
      input = bestAttempt->board_->serialize(false);
    } catch (const jrt::RuntimeException& e) {
      job->logWarning("Exploration stopped; the best board could not be saved: " + e.getMessage());
      break;
    }
    RoutingBoard* copy = static_cast<RoutingBoard*>(BasicBoard::deserialize(input));
    if (copy->clearanceBaseline == nullptr) {
      copy->clearanceBaseline = bestAttempt->board_->clearanceBaseline;
    }
    BasicBoard::discard(input, "fallback");  // stage E: the snapshot was only needed for the copy
    if ("neighbourhood" == planned->kind()) {
      int32_t ripped = RetryNeighbourhood::ripNearOpenPins(copy, planned->tryNumber());
      job->logInfo("Exploration '" + planned->name() + "' ripped " + jrt::str(ripped) + " item(s) within " +
                   jrt::str(RetryNeighbourhood_Reach::of(planned->tryNumber())->radiusMm()) + " mm.");
    }
    job->logInfo("Exploration attempt '" + planned->name() + "' from " + jrt::str(bestAttempt->outcome_) + ".");
    int32_t openBefore = bestAttempt->outcome_->unrouted;
    RoutingFallbackRunner_Attempt* attempt = runPrepared(job, copy, settings, false, "explore-" + planned->kind());
    bool kept = false;
    if (attempt != nullptr && replaces(attempt, bestAttempt)) {
      bestAttempt = attempt;
      kept = true;
    }
    int32_t openAfter = bestAttempt->outcome_->unrouted;
    supervisor->attemptDone(planned, openBefore, openAfter, kept);
  }
  job->board = bestAttempt->board_;
  job->routerSettings = jobSettings;
  return bestAttempt->router_;
}

RoutingFallbackRunner_Attempt* RoutingFallbackRunner::runContinuations(
    RoutingJob* job, RoutingFallbackRunner_Attempt* bestAttempt, jrt::List<RoutingFallback_Strategy*>* continuations) {
  for (RoutingFallback_Strategy* strategy : *continuations) {
    if (bestAttempt->outcome_->isCompleteAndClean() || stopped(job)) {
      break;
    }
    if (!strategy->continueBest && !RoutingSupervisor::residual(job->routerSettings, bestAttempt->outcome_->unrouted)) {
      break;
    }
    RoutingBoard* copy = bestAttempt->board_->deepCopy();
    RouterSettings* settings = job->routerSettings->clone();
    strategy->apply(settings);
    settings->setFanoutEnabled(false);
    RoutingFallbackRunner_Attempt* attempt = runPrepared(job, copy, settings, true, strategy->name);
    if (attempt == nullptr) {
      break;
    }
    job->logInfo("Routing fallback continuation '" + strategy->name + "': " + jrt::str(attempt->outcome_) +
                 " after " + RoutingFallback::describe(attempt->router_->lastStopReason) + ".");
    if (replaces(attempt, bestAttempt)) {
      RoutingFallbackRunner_Attempt* replaced = bestAttempt;
      bestAttempt = attempt;
      dropAttempt(job, replaced, bestAttempt);
    } else if (!strategy->continueBest) {
      job->board = bestAttempt->board_;
      dropAttempt(job, attempt, bestAttempt);
      job->logInfo("Routing fallback: stopping residual continuations after '" + strategy->name +
                   "' did not improve the incumbent.");
      break;
    } else {
      job->board = bestAttempt->board_;
      dropAttempt(job, attempt, bestAttempt);
    }
  }
  return bestAttempt;
}

RoutingFallbackRunner_Attempt* RoutingFallbackRunner::runAttempt(RoutingJob* job, BasicBoard* input,
                                                                 RoutingFallback_Strategy* strategy,
                                                                 bool keepFanoutVias,
                                                                 ClearanceBaseline* inputBaseline) {
  RoutingBoard* board = static_cast<RoutingBoard*>(BasicBoard::deserialize(input));
  if (board->clearanceBaseline == nullptr) {
    board->clearanceBaseline = inputBaseline;
  }
  RouterSettings* settings = job->routerSettings->clone();
  strategy->apply(settings);
  RoutingFallbackRunner_Attempt* attempt = runPrepared(job, board, settings, keepFanoutVias, strategy->name);
  if (attempt != nullptr && strategy->fanoutOnly &&
      attempt->router_->lastStopReason == BatchAutorouter_StopReason::REPEATED) {
    RoutingFallback_Outcome* repeated = getByHash(this->passLoopStarts, attempt->router_->passLoopStartHash);
    if (repeated != nullptr) {
      auto* repeatedAttempt = new (jrt::kOp) RoutingFallbackRunner_Attempt(
          new (jrt::kOp) RoutingFallback_Outcome(strategy->name, repeated->unrouted, repeated->newOrChanged, repeated->violations,
                                      repeated->score, repeated->designBlocked),
          job->board, attempt->router_, attempt->baseline_);
      repeatedAttempt->startBoard_ = attempt->startBoard_;
      repeatedAttempt->owned_ = attempt->owned_;
      return repeatedAttempt;
    }
  }
  return attempt;
}

void RoutingFallbackRunner::dropAttempt(RoutingJob* job, RoutingFallbackRunner_Attempt* attempt,
                                        RoutingFallbackRunner_Attempt* keep) {
  if (attempt == nullptr || !attempt->owned_ || attempt == keep) return;
  RoutingBoard* boards[2] = {attempt->board_, attempt->startBoard_};
  for (int i = 0; i < 2; ++i) {
    RoutingBoard* b = boards[i];
    if (b == nullptr || (i == 1 && b == boards[0]) || b == job->board) continue;
    if (keep != nullptr && (b == keep->board_ || b == keep->startBoard_)) continue;
    BasicBoard::discard(b, "fallback");
  }
  attempt->owned_ = false;
}

RoutingFallbackRunner_Attempt* RoutingFallbackRunner::runPrepared(RoutingJob* job, RoutingBoard* board,
                                                                  RouterSettings* settings, bool keepFanoutVias,
                                                                  const std::string& strategyName) {
  RouterSettings* savedSettings = job->routerSettings;
  RoutingBoard* savedBoard = job->board;
  job->thread->resetAutoRouterStopRequest();
  job->board = board;
  job->routerSettings = settings;
  RoutingFallbackRunner_Attempt* result;
  try {
    bool removeUnconnectedVias = !keepFanoutVias && !settings->isFanoutEnabled();
    BatchAutorouter* router = BatchAutorouter::createAttempt(job, board, settings, removeUnconnectedVias);
    router->setStrategyName(strategyName);
    router->copyListenersFrom(this->listenerSource);
    RoutingFallback_Strategy* strategy = RoutingFallback::find(strategyName);
    if (strategy != nullptr && strategy->fanoutOnly) {
      auto* starts = this->passLoopStarts;
      // Java: hash -> hash != null && passLoopStarts.containsKey(hash); C++ never passes a null hash.
      router->setRepeatsPassLoop([starts](const std::string& hash) { return starts->containsKey(hash); });
    }
    router->runBatchLoop();
    RoutingFallback_Outcome* outcome = RoutingFallback_Outcome::of(strategyName, job->board, savedSettings);
    ClearanceBaseline* baseline = RoutingFallback::captureClearance(job->board, outcome->violations);
    result = new (jrt::kOp) RoutingFallbackRunner_Attempt(outcome, job->board, router, baseline);
    result->startBoard_ = board;
    result->owned_ = true;
  } catch (const jrt::RuntimeException& e) {
    job->board = savedBoard;
    job->logError("Routing fallback attempt '" + strategyName + "' failed and was discarded.", &e);
    result = nullptr;
  } catch (...) {
    // Java finally
    job->routerSettings = savedSettings;
    throw;
  }
  // Java finally
  job->routerSettings = savedSettings;
  return result;
}

bool RoutingFallbackRunner::replaces(RoutingFallbackRunner_Attempt* candidate, RoutingFallbackRunner_Attempt* incumbent) {
  int32_t changed =
      RoutingFallback::newOrChangedClearance(candidate->board_, candidate->outcome_->violations, incumbent->baseline_);
  return RoutingFallback_Outcome::canReplace(candidate->outcome_, incumbent->outcome_, changed);
}

bool RoutingFallbackRunner::stopAlternatives(RoutingFallback_Outcome* best,
                                             std::optional<BatchAutorouter_StopReason> reason) {
  return best->isCompleteAndClean() || reason == BatchAutorouter_StopReason::TIMED_OUT ||
         reason == BatchAutorouter_StopReason::CANCELLED;
}

bool RoutingFallbackRunner::stopped(RoutingJob* job) {
  return job->thread->isStopRequested() || job->state == RoutingJobState::TIMED_OUT;
}

jrt::List<RoutingFallback_Strategy*>* RoutingFallbackRunner::automaticContinuations(RoutingJob* job) {
  jrt::List<RoutingFallback_Strategy*>* result = new (jrt::kOp) jrt::ArrayList<RoutingFallback_Strategy*>();
  RoutingFallback_Strategy* continueStrategy = RoutingFallback::find("continue");
  if (continueStrategy != nullptr) {
    result->add(continueStrategy);
  }
  bool hasProjectMinTrackWidth = job != nullptr && job->kicadProject != nullptr &&
                                 job->kicadProject->narrowestTraceWidthUm().has_value();
  if (job != nullptr && job->routerSettings != nullptr && job->routerSettings->getNeckWidthUm() > 0 &&
      !hasProjectMinTrackWidth) {
    RoutingFallback_Strategy* noNeck = RoutingFallback::find("continue-no-neck");
    if (noNeck != nullptr) {
      result->add(noNeck);
    }
  }
  return result;
}

}  // namespace fr
