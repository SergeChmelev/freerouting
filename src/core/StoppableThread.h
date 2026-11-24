#pragma once

#include <atomic>

#include "core/StopRequestState.h"
#include "datastructures/Stoppable.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Runs an interactive action that can be stopped by the user. Only the stop-request API matters to
 * the routing algorithms (they poll isStopRequested() / isStopAutoRouterRequested()).
 */
// PORT NOTE: single-threaded port. Java's `extends Thread` is dropped (no real thread is created;
// run() simply calls threadAction() on the calling thread) and `synchronized` is removed.
// Fully implemented here (no .cpp).
class StoppableThread : public virtual Stoppable {
 protected:
  /** Creates a new stoppable thread. */
  StoppableThread() = default;

  /** Performs the thread's action. */
  virtual void threadAction() = 0;

 public:
  /** Java Thread.run(): performs threadAction() (synchronously). */
  virtual void run() { threadAction(); }

  /** Requests the thread to stop, including fanout, Autorouter, and Optimizer tasks. */
  void requestStop() override { this->stopRequestState = StopRequestState::ALL; }

  bool isStopRequested() override { return this->stopRequestState == StopRequestState::ALL; }

  /** Requests the Autorouter to stop while other tasks continue. */
  virtual void requestStopAutoRouter() {
    StopRequestState expected = StopRequestState::NONE;
    this->stopRequestState.compare_exchange_strong(expected, StopRequestState::AUTO_ROUTER_ONLY);
  }

  /** Returns whether the Autorouter should stop. */
  virtual bool isStopAutoRouterRequested() {
    return this->stopRequestState != StopRequestState::NONE;
  }

  /**
   * Clears an autorouter-only stop so another fallback attempt can run. A stop of the whole job is
   * left in place.
   */
  virtual void resetAutoRouterStopRequest() {
    StopRequestState expected = StopRequestState::AUTO_ROUTER_ONLY;
    this->stopRequestState.compare_exchange_strong(expected, StopRequestState::NONE);
  }

  /** Resets an AUTO_ROUTER_ONLY stop request (alias of resetAutoRouterStopRequest()). */
  virtual void resetStopAutoRouterRequest() { resetAutoRouterStopRequest(); }

 private:
  // Atomic: polled by the optimizer's worker threads while another thread may request a stop.
  std::atomic<StopRequestState> stopRequestState{StopRequestState::NONE};
};

}  // namespace fr
