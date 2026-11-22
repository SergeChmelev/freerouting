#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Receives lifecycle callbacks between pipeline stages (Java: RoutingPipeline.StageListener, an
 * interface with default no-op methods).
 *
 * Java callers implement it with anonymous classes; in C++ either subclass it (local class) or use
 * the lambda adapter `RoutingPipeline_StageListener::of(...)`.
 */
class RoutingPipeline_StageListener : public virtual jrt::Object {
 public:
  /** Invoked after fanout and autorouting have completed. */
  virtual void afterRouting(BatchAutorouter* autorouter) {}
  /** Invoked immediately before optimization starts. */
  virtual void beforeOptimization(BatchOptimizer* optimizer) {}
  /** Invoked after optimization has completed. */
  virtual void afterOptimization(BatchOptimizer* optimizer) {}

  /** Lambda adapter (C++ only): empty functions keep the default no-op behaviour. */
  static RoutingPipeline_StageListener* of(
      std::function<void(BatchAutorouter*)> afterRouting,
      std::function<void(BatchOptimizer*)> beforeOptimization = nullptr,
      std::function<void(BatchOptimizer*)> afterOptimization = nullptr) {
    class Adapter : public RoutingPipeline_StageListener {
     public:
      std::function<void(BatchAutorouter*)> a;
      std::function<void(BatchOptimizer*)> b;
      std::function<void(BatchOptimizer*)> c;
      void afterRouting(BatchAutorouter* autorouter) override {
        if (a) a(autorouter);
      }
      void beforeOptimization(BatchOptimizer* optimizer) override {
        if (b) b(optimizer);
      }
      void afterOptimization(BatchOptimizer* optimizer) override {
        if (c) c(optimizer);
      }
    };
    auto* adapter = new (jrt::kOp) Adapter();
    adapter->a = std::move(afterRouting);
    adapter->b = std::move(beforeOptimization);
    adapter->c = std::move(afterOptimization);
    return adapter;
  }
};

/** Sequences the shared fanout, autoroute, and optimization stages for a routing job. */
class RoutingPipeline : public virtual jrt::Object {
 private:
  RoutingJob* job;
  BatchAutorouter* autorouter;
  BatchOptimizer* optimizer;
  jrt::List<RoutingPipeline_StageListener*>* stageListeners;  // Java init: new ArrayList<>() (set in constructor)
  jrt::List<int32_t>* exitStubItems = new (jrt::kOp) jrt::ArrayList<int32_t>();

  RoutingPipeline(RoutingJob* job, std::function<BatchOptimizer*(RoutingJob*)> optimizerFactory);

 public:
  /** Creates the canonical routing pipeline for a routing job. */
  static RoutingPipeline* create(RoutingJob* job);
  /** Creates a pipeline using the GUI optimizer policy. */
  static RoutingPipeline* createForGui(RoutingJob* job);
  /** Creates a pipeline using the headless optimizer policy. */
  static RoutingPipeline* createForHeadless(RoutingJob* job);

  /** Returns the shared autorouter stage. */
  virtual BatchAutorouter* getAutorouter() { return this->autorouter; }
  /** Returns the configured optimizer stage, or nullptr when optimization is disabled. */
  virtual BatchOptimizer* getOptimizer() { return this->optimizer; }
  /** Registers a listener for stage transitions. */
  virtual void addStageListener(RoutingPipeline_StageListener* listener);
  /** Forwards board updates from both algorithm stages to the supplied listener. */
  virtual void addBoardUpdatedEventListener(BoardUpdatedEventListener* listener);
  /** Forwards task-state updates from both algorithm stages to the supplied listener. */
  virtual void addTaskStateChangedEventListener(TaskStateChangedEventListener* listener);
  /** Runs the configured stages in order. */
  virtual void run();

 private:
  void removeDanglingCopper();
  void releaseExitStubs();
  void runRoutingStage();
  void runOptimizationStage();
  static std::string knownStrategyNames();
  static void normalizeRouterAlgorithm(RoutingJob* job);
};

}  // namespace fr
