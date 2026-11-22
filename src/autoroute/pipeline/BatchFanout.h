#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Listener for fanout pass progress updates (Java: @FunctionalInterface
 * BatchFanout.FanoutProgressListener). Lambda adapter: BatchFanout_FanoutProgressListener::of(fn).
 */
class BatchFanout_FanoutProgressListener : public virtual jrt::Object {
 public:
  /** Reports fanout pass progress. */
  virtual void onProgress(BatchFanout_FanoutPassStatus* status) = 0;

  /** Lambda adapter (C++ only). */
  static BatchFanout_FanoutProgressListener* of(std::function<void(BatchFanout_FanoutPassStatus*)> fn) {
    class Adapter : public BatchFanout_FanoutProgressListener {
     public:
      std::function<void(BatchFanout_FanoutPassStatus*)> f;
      void onProgress(BatchFanout_FanoutPassStatus* status) override { f(status); }
    };
    auto* adapter = new (jrt::kOp) Adapter();
    adapter->f = std::move(fn);
    return adapter;
  }
};

/**
 * Statistics about how many SMD pins were successfully escaped after a fanout pass. A pin is
 * considered escaped when it has at least one Trace or Via directly connected to it (without
 * clearance violations), or a Via that itself has a Trace connected to it.
 * Java record EscapeStatistics(int totalSmdPins, int escapedCount, double escapedPercentage) -
 * fully implemented (toString is overridden in Java).
 */
class BatchFanout_EscapeStatistics : public virtual jrt::Object {
 private:
  int32_t totalSmdPins_;
  int32_t escapedCount_;
  double escapedPercentage_;

 public:
  BatchFanout_EscapeStatistics(int32_t totalSmdPins, int32_t escapedCount, double escapedPercentage)
      : totalSmdPins_(totalSmdPins), escapedCount_(escapedCount), escapedPercentage_(escapedPercentage) {}
  int32_t totalSmdPins() { return totalSmdPins_; }
  int32_t escapedCount() { return escapedCount_; }
  double escapedPercentage() { return escapedPercentage_; }

  /** Creates escape statistics from board statistics. */
  static BatchFanout_EscapeStatistics* fromBoardStatistics(BoardStatistics* stats);

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<BatchFanout_EscapeStatistics>(o);
    if (other == nullptr) return false;
    return totalSmdPins_ == other->totalSmdPins_ && escapedCount_ == other->escapedCount_ &&
           jrt::equalsOf(escapedPercentage_, other->escapedPercentage_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(totalSmdPins_));
    h = jrt::hashCombine(h, jrt::hashOf(escapedCount_));
    h = jrt::hashCombine(h, jrt::hashOf(escapedPercentage_));
    return h;
  }
  /** Java: String.format("%d/%d (%.1f%%)", escapedCount, totalSmdPins, escapedPercentage). */
  std::string toString() override {
    return jrt::format("%d/%d (%.1f%%)", escapedCount_, totalSmdPins_, escapedPercentage_);
  }
};

/** Status snapshot for a single fanout pass (Java record). */
class BatchFanout_FanoutPassStatus : public virtual jrt::Object {
 private:
  int32_t passNo_;
  int32_t ripupCosts_;
  int32_t totalPins_;
  int32_t pinsToGo_;
  int32_t routedCount_;
  int32_t notRoutedCount_;
  int32_t insertErrorCount_;
  int32_t extraViasThisPass_;
  int32_t extraViasTotal_;
  int64_t passDurationMillis_;
  BoardStatistics* boardStatistics_;
  bool passCompleted_;
  BatchFanout_EscapeStatistics* escapeStatistics_;

 public:
  BatchFanout_FanoutPassStatus(
      int32_t passNo,
      int32_t ripupCosts,
      int32_t totalPins,
      int32_t pinsToGo,
      int32_t routedCount,
      int32_t notRoutedCount,
      int32_t insertErrorCount,
      int32_t extraViasThisPass,
      int32_t extraViasTotal,
      int64_t passDurationMillis,
      BoardStatistics* boardStatistics,
      bool passCompleted,
      BatchFanout_EscapeStatistics* escapeStatistics)
      : passNo_(passNo),
        ripupCosts_(ripupCosts),
        totalPins_(totalPins),
        pinsToGo_(pinsToGo),
        routedCount_(routedCount),
        notRoutedCount_(notRoutedCount),
        insertErrorCount_(insertErrorCount),
        extraViasThisPass_(extraViasThisPass),
        extraViasTotal_(extraViasTotal),
        passDurationMillis_(passDurationMillis),
        boardStatistics_(boardStatistics),
        passCompleted_(passCompleted),
        escapeStatistics_(escapeStatistics) {}
  int32_t passNo() { return passNo_; }
  int32_t ripupCosts() { return ripupCosts_; }
  int32_t totalPins() { return totalPins_; }
  int32_t pinsToGo() { return pinsToGo_; }
  int32_t routedCount() { return routedCount_; }
  int32_t notRoutedCount() { return notRoutedCount_; }
  int32_t insertErrorCount() { return insertErrorCount_; }
  int32_t extraViasThisPass() { return extraViasThisPass_; }
  int32_t extraViasTotal() { return extraViasTotal_; }
  int64_t passDurationMillis() { return passDurationMillis_; }
  BoardStatistics* boardStatistics() { return boardStatistics_; }
  bool passCompleted() { return passCompleted_; }
  BatchFanout_EscapeStatistics* escapeStatistics() { return escapeStatistics_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Summary of a complete fanout run (Java record). */
class BatchFanout_FanoutRunSummary : public virtual jrt::Object {
 private:
  int32_t completedPassCount_;
  int64_t totalDurationMillis_;
  BatchFanout_EscapeStatistics* escapeStatistics_;
  bool isTimedOut_;

 public:
  BatchFanout_FanoutRunSummary(
      int32_t completedPassCount,
      int64_t totalDurationMillis,
      BatchFanout_EscapeStatistics* escapeStatistics,
      bool isTimedOut)
      : completedPassCount_(completedPassCount),
        totalDurationMillis_(totalDurationMillis),
        escapeStatistics_(escapeStatistics),
        isTimedOut_(isTimedOut) {}
  int32_t completedPassCount() { return completedPassCount_; }
  int64_t totalDurationMillis() { return totalDurationMillis_; }
  BatchFanout_EscapeStatistics* escapeStatistics() { return escapeStatistics_; }
  bool isTimedOut() { return isTimedOut_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * A board component with its SMD pins (Java: private static class BatchFanout.Component
 * implements Comparable<Component>). Sorts components with more pins first.
 */
class BatchFanout_Component : public virtual jrt::Object {
 public:
  Component* boardComponent;
  int32_t smdPinCount;
  /** Java SortedSet<Pin> of the nested Component.Pin (initialised to a TreeSet). */
  jrt::TreeSet<BatchFanout_Component_Pin*>* smdPins;
  /** The center of gravity of all SMD pins of this component. */
  FloatPoint* gravityCenterOfSmdPins;
  std::string pinSortingOrder;

  BatchFanout_Component(
      Component* boardComponent,
      jrt::Collection<Pin*>* boardSmdPinList,
      const std::string& pinSortingOrder,
      RoutingBoard* routingBoard);

  /** Sort the components, so that components with more pins come first. */
  virtual int32_t compareTo(BatchFanout_Component* other);
};

/**
 * One SMD pin of a BatchFanout_Component (Java: inner, non-static class Component.Pin implements
 * Comparable<Pin>; reads the outer gravityCenterOfSmdPins and pinSortingOrder).
 */
class BatchFanout_Component_Pin : public virtual jrt::Object {
 public:
  BatchFanout_Component* outer;
  Pin* boardPin;
  double distanceToComponentCenter;
  double distanceToClosestOnNet;
  int32_t surroundingsDensity;

  BatchFanout_Component_Pin(
      BatchFanout_Component* outer,
      Pin* boardPin,
      jrt::Collection<Pin*>* boardSmdPinList,
      RoutingBoard* routingBoard);

  virtual int32_t compareTo(BatchFanout_Component_Pin* other);
};

/** Handles the sequencing of the fanout inside the batch autorouter. */
class BatchFanout : public virtual jrt::Object {
 private:
  StoppableThread* thread;
  RoutingBoard* routingBoard;
  RouterSettings* settings;
  jrt::TreeSet<BatchFanout_Component*>* sortedComponents;
  int32_t totalSmdPinCount;
  int32_t alreadyConnectedPinCount;
  /** Java init: new ProgressThrottler(1000) (set in constructor). */
  ProgressThrottler* progressThrottler;
  int32_t lastNotRoutedCount = 0;
  int32_t extraViasTotal = 0;

 public:
  int32_t totalItemsFanouted = 0;

 private:
  std::optional<int64_t> deadlineMs;
  bool isTimedOut = false;
  /** Pin id to the component generation observed when that pin failed. */
  jrt::Map<int32_t, int32_t>* failedPinGeneration = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();
  /** Successful escapes in a component. A change allows a previously failed pin to be retried. */
  jrt::Map<int32_t, int32_t>* componentGeneration = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();

  BatchFanout(RoutingBoard* board, RouterSettings* settings, StoppableThread* thread);
  BatchFanout(
      RoutingBoard* board, RouterSettings* settings, StoppableThread* thread, jrt::Set<Pin*>* bgaCommittedPins);

 public:
  /**
   * plane_as_priority: a pad of a net with a plane on a power layer is left to the autorouter,
   * which routes it to the plane first and gives it a via of its own.
   */
  static bool leftToPlaneRouting(RoutingBoard* board, RouterSettings* settings, Pin* pin);

 private:
  /**
   * A pad closer to another pad of its package than a via and two clearances (QFN, TSSOP, SOT-23)
   * keeps its fanout: it needs the fanout's escape stub to leave the package at all.
   */
  static bool isFinePitch(RoutingBoard* board, Pin* pin, int32_t netNo);

 public:
  /** Performs fanout routing for SMD components on board. */
  static BatchFanout_FanoutRunSummary* fanoutBoard(
      RoutingBoard* board, RouterSettings* settings, StoppableThread* thread);
  /** Performs fanout routing for SMD components on board with a progress listener. */
  static BatchFanout_FanoutRunSummary* fanoutBoard(
      RoutingBoard* board,
      RouterSettings* settings,
      StoppableThread* thread,
      BatchFanout_FanoutProgressListener* progressListener);
  /** With a progress listener and an optional info logger (empty std::function = Java null). */
  static BatchFanout_FanoutRunSummary* fanoutBoard(
      RoutingBoard* board,
      RouterSettings* settings,
      StoppableThread* thread,
      BatchFanout_FanoutProgressListener* progressListener,
      std::function<void(const std::string&)> infoLogger);

 private:
  /** Routes a fanout pass and returns the number of new fanouted SMD-pins in this pass. */
  int32_t fanoutPass(int32_t passNo, BatchFanout_FanoutProgressListener* progressListener);

 public:
  /**
   * A pin is retried when it has not failed (failedGeneration empty = Java null), or when its
   * component has escaped another pin since that failure.
   */
  static bool retryFanout(std::optional<int32_t> failedGeneration, int32_t currentGeneration);

 private:
  void maybePublishProgress(
      BatchFanout_FanoutProgressListener* progressListener,
      int32_t passNo,
      int32_t ripupCosts,
      int32_t pinsToGo,
      int32_t routedCount,
      int32_t notRoutedCount,
      int32_t insertErrorCount,
      int32_t extraViasThisPass,
      bool passCompleted,
      int64_t passStart,
      BoardStatistics* progressStats);
  void publishProgress(
      BatchFanout_FanoutProgressListener* progressListener,
      int32_t passNo,
      int32_t ripupCosts,
      int32_t pinsToGo,
      int32_t routedCount,
      int32_t notRoutedCount,
      int32_t insertErrorCount,
      int32_t extraViasThisPass,
      BatchFanout_EscapeStatistics* escapeStatistics,
      bool passCompleted,
      int64_t passStart,
      BoardStatistics* boardStatistics);
};

}  // namespace fr
