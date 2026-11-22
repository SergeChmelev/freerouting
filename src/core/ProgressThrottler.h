#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Utility to throttle progress updates and notifications. Fully implemented here (no .cpp). */
class ProgressThrottler : public virtual jrt::Object {
 public:
  /** Creates a throttler with the specified minimum update interval. */
  explicit ProgressThrottler(int64_t intervalMs) : intervalMs(intervalMs), lastUpdateMs(0) {}

  /** Returns true if the throttled action should run. */
  bool shouldUpdate() {
    int64_t now = jrt::System::currentTimeMillis();
    if (lastUpdateMs == 0) {
      lastUpdateMs = now;
      return true;
    }
    if (now - lastUpdateMs >= intervalMs) {
      lastUpdateMs = now;
      return true;
    }
    return false;
  }

  /** Resets the throttler timer. */
  void reset() { this->lastUpdateMs = 0; }

 private:
  int64_t intervalMs;
  int64_t lastUpdateMs;
};

}  // namespace fr
