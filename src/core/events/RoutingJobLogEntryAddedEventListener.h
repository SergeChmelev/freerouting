#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Receives events when routing jobs receive log entries. */
class RoutingJobLogEntryAddedEventListener : public virtual jrt::Object {
 public:
  /** Handles a newly added routing job log entry. */
  virtual void onLogEntryAdded(RoutingJobLogEntryAddedEvent* event) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as this listener. */
  static RoutingJobLogEntryAddedEventListener* of(
      std::function<void(RoutingJobLogEntryAddedEvent*)> f);
};

namespace detail {
class LambdaRoutingJobLogEntryAddedEventListener : public RoutingJobLogEntryAddedEventListener {
 public:
  explicit LambdaRoutingJobLogEntryAddedEventListener(
      std::function<void(RoutingJobLogEntryAddedEvent*)> f)
      : f_(std::move(f)) {}
  void onLogEntryAdded(RoutingJobLogEntryAddedEvent* event) override { f_(event); }

 private:
  std::function<void(RoutingJobLogEntryAddedEvent*)> f_;
};
}  // namespace detail

inline RoutingJobLogEntryAddedEventListener* RoutingJobLogEntryAddedEventListener::of(
    std::function<void(RoutingJobLogEntryAddedEvent*)> f) {
  return new detail::LambdaRoutingJobLogEntryAddedEventListener(std::move(f));
}

}  // namespace fr
