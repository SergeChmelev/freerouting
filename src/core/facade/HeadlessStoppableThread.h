#pragma once

#include <optional>

#include "core/StoppableThread.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Lightweight in-memory thread adapter bridging CancellationToken (and the job deadline) to
 * Freerouting's internal StoppableThread stop requests.
 */
// PORT NOTE: package-private in Java (constructors/isTimedOut public here); `final`,
// `volatile` and `synchronized` dropped. The deadline check uses wall-clock time exactly like
// Java (a timeout may only stop work, never change how it is done).
class HeadlessStoppableThread final : public StoppableThread {
 public:
  explicit HeadlessStoppableThread(CancellationToken* cancellationToken);
  /** @param deadlineMillis epoch millis after which the job counts as timed out; null = none. */
  HeadlessStoppableThread(CancellationToken* cancellationToken,
                          std::optional<int64_t> deadlineMillis);

  bool isTimedOut();

 private:
  bool checkTimeout();

 public:
  bool isStopRequested() override;
  bool isStopAutoRouterRequested() override;

 protected:
  /** In headless in-memory mode, execution runs synchronously on the calling thread. */
  void threadAction() override {}

 private:
  CancellationToken* cancellationToken;
  std::optional<int64_t> deadlineMillis;
  bool timedOut = false;
};

}  // namespace fr
