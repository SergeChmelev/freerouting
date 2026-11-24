#include "autoroute/pipeline/RoutingPipeline.h"

#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/BatchOptimizer.h"
#include "autoroute/pipeline/DanglingCopper.h"
#include "autoroute/pipeline/ExitStubs.h"
#include "autoroute/pipeline/RoutingFallback.h"
#include "autoroute/pipeline/RoutingFallbackRunner.h"
#include "autoroute/pipeline/RoutingSupervisor.h"
#include "autoroute/pipeline/TwoLayerBga.h"
#include "board/facade/RoutingBoard.h"
#include "core/RoutingJob.h"
#include "core/RoutingStage.h"
#include "core/StoppableThread.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

RoutingPipeline::RoutingPipeline(RoutingJob* job, std::function<BatchOptimizer*(RoutingJob*)> optimizerFactory)
    : job(job), stageListeners(new (jrt::kOp) jrt::ArrayList<RoutingPipeline_StageListener*>()) {
  normalizeRouterAlgorithm(job);
  this->autorouter = new (jrt::kOp) BatchAutorouter(job);
  this->optimizer = job->routerSettings->getRunOptimizer() ? optimizerFactory(job) : nullptr;
}

RoutingPipeline* RoutingPipeline::create(RoutingJob* job) {
  return new (jrt::kOp) RoutingPipeline(job, [](RoutingJob* j) { return BatchOptimizer::create(j); });
}

RoutingPipeline* RoutingPipeline::createForGui(RoutingJob* job) { return create(job); }

RoutingPipeline* RoutingPipeline::createForHeadless(RoutingJob* job) { return create(job); }

void RoutingPipeline::addStageListener(RoutingPipeline_StageListener* listener) {
  this->stageListeners->add(listener);
}

void RoutingPipeline::addBoardUpdatedEventListener(BoardUpdatedEventListener* listener) {
  this->autorouter->addBoardUpdatedEventListener(listener);
  if (this->optimizer != nullptr) {
    this->optimizer->addBoardUpdatedEventListener(listener);
  }
}

void RoutingPipeline::addTaskStateChangedEventListener(TaskStateChangedEventListener* listener) {
  this->autorouter->addTaskStateChangedEventListener(listener);
  if (this->optimizer != nullptr) {
    this->optimizer->addTaskStateChangedEventListener(listener);
  }
}

void RoutingPipeline::run() {
  if (this->job != nullptr && this->job->board != nullptr) {
    this->job->board->awaitPostLoad();
    std::optional<std::string> chosen = TwoLayerBga::apply(this->job->routerSettings, this->job->board);
    if (chosen.has_value()) {
      this->job->logInfo(*chosen);
    }
    std::optional<std::string> stubs = ExitStubs::apply(this->job->routerSettings, this->job->board, this->exitStubItems);
    if (stubs.has_value()) {
      this->job->logInfo(*stubs);
    }
  }
  runRoutingStage();
  this->job->board->releaseGraveyard();  // PORT NOTE (stage E): stage boundary
  runOptimizationStage();
  this->job->board->releaseGraveyard();
  releaseExitStubs();
  removeDanglingCopper();
  this->job->stage = RoutingStage::IDLE;
}

void RoutingPipeline::removeDanglingCopper() {
  bool routerRan = this->job != nullptr && this->job->routerSettings != nullptr &&
                   this->job->routerSettings->getRunRouter() &&
                   (!this->job->routerSettings->autorouter->maxPasses.has_value() ||
                    *this->job->routerSettings->autorouter->maxPasses > 0);
  if (routerRan && this->job->board != nullptr) {
    std::optional<std::string> line = DanglingCopper::remove(this->job->board, "the last stage");
    if (line.has_value()) {
      this->job->logInfo(*line);
    }
  }
}

void RoutingPipeline::releaseExitStubs() {
  if (!this->exitStubItems->isEmpty() && this->job->board != nullptr) {
    std::optional<std::string> stubLine = ExitStubs::release(this->job->board, this->exitStubItems);
    if (stubLine.has_value()) {
      this->job->logInfo(*stubLine);
    }
    this->exitStubItems->clear();
  }
}

void RoutingPipeline::runRoutingStage() {
  bool routerEnabled = this->job->routerSettings->getRunRouter() &&
                       (!this->job->routerSettings->autorouter->maxPasses.has_value() ||
                        *this->job->routerSettings->autorouter->maxPasses >= 0);

  if (routerEnabled || this->job->routerSettings->isFanoutEnabled()) {
    this->job->stage = RoutingStage::ROUTING;
  }

  if (routerEnabled && !this->job->thread->isStopAutoRouterRequested()) {
    RoutingJob* j = this->job;
    jrt::List<RoutingFallback_Strategy*>* strategies =
        RoutingFallback::parse(this->job->routerSettings, [j](const std::string& name) {
          j->logWarning("Unknown routing fallback strategy '" + name + "' ignored; known strategies: " +
                        knownStrategyNames() + ".");
        });
    // PORT NOTE: byte[] snapshot -> BasicBoard* frozen snapshot (BasicBoard::serialize).
    BasicBoard* input = nullptr;
    if (!strategies->isEmpty() && this->job->board != nullptr) {
      try {
        input = this->job->board->serialize(false);
      } catch (const jrt::RuntimeException& e) {
        this->job->logWarning("Routing fallback disabled; the input board could not be saved: " + e.getMessage());
      }
    }
    this->autorouter->runBatchLoop();
    RoutingFallbackRunner* fallback = new (jrt::kOp) RoutingFallbackRunner(this->autorouter);
    if (input != nullptr) {
      this->autorouter = fallback->run(this->job, this->autorouter, strategies, input);
      BasicBoard::discard(input, "fallback");  // stage E: run() only deserializes copies of the snapshot
    }
    if (RoutingSupervisor::explores(this->job->routerSettings)) {
      this->autorouter = fallback->explore(this->job, this->autorouter);
    }
  } else if (this->job->routerSettings->isFanoutEnabled() && !this->job->thread->isStopAutoRouterRequested()) {
    std::optional<int32_t> originalMaxPasses = this->job->routerSettings->autorouter->maxPasses;
    try {
      this->job->routerSettings->autorouter->maxPasses = 0;
      this->autorouter->runBatchLoop();
    } catch (...) {
      this->job->routerSettings->autorouter->maxPasses = originalMaxPasses;
      throw;
    }
    this->job->routerSettings->autorouter->maxPasses = originalMaxPasses;
  }

  this->job->board->finishAutoroute();
  for (RoutingPipeline_StageListener* listener : *this->stageListeners) {
    listener->afterRouting(this->autorouter);
  }
}

void RoutingPipeline::runOptimizationStage() {
  if (this->optimizer == nullptr || this->job->thread->isStopRequested()) {
    return;
  }

  this->job->stage = RoutingStage::OPTIMIZATION;
  for (RoutingPipeline_StageListener* listener : *this->stageListeners) {
    listener->beforeOptimization(this->optimizer);
  }
  this->optimizer->runBatchLoop();
  for (RoutingPipeline_StageListener* listener : *this->stageListeners) {
    listener->afterOptimization(this->optimizer);
  }
}

std::string RoutingPipeline::knownStrategyNames() {
  std::string names;
  for (RoutingFallback_Strategy* strategy : *RoutingFallback::KNOWN()) {
    if (!names.empty()) {
      names += ", ";
    }
    names += strategy->name;
  }
  return names;
}

void RoutingPipeline::normalizeRouterAlgorithm(RoutingJob* job) {
  std::optional<std::string> algorithm = job->routerSettings->autorouter->algorithm;
  if (!(algorithm.has_value() && RouterSettings::ALGORITHM_CURRENT == *algorithm)) {
    job->logWarning("The algorithm '" + jrt::str(algorithm) + "' is not supported. The default algorithm '" +
                    RouterSettings::ALGORITHM_CURRENT + "' will be used instead.");
    job->routerSettings->autorouter->algorithm = RouterSettings::ALGORITHM_CURRENT;
  }
}

}  // namespace fr
