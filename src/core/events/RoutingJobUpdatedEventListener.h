#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Receives events when routing jobs change. */
class RoutingJobUpdatedEventListener : public virtual jrt::Object {
 public:
  /** Handles a routing job update. */
  virtual void onRoutingJobUpdated(RoutingJobUpdatedEvent* event) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as this listener. */
  static RoutingJobUpdatedEventListener* of(std::function<void(RoutingJobUpdatedEvent*)> f);
};

namespace detail {
class LambdaRoutingJobUpdatedEventListener : public RoutingJobUpdatedEventListener {
 public:
  explicit LambdaRoutingJobUpdatedEventListener(std::function<void(RoutingJobUpdatedEvent*)> f)
      : f_(std::move(f)) {}
  void onRoutingJobUpdated(RoutingJobUpdatedEvent* event) override { f_(event); }

 private:
  std::function<void(RoutingJobUpdatedEvent*)> f_;
};
}  // namespace detail

inline RoutingJobUpdatedEventListener* RoutingJobUpdatedEventListener::of(
    std::function<void(RoutingJobUpdatedEvent*)> f) {
  return new (jrt::kOp) detail::LambdaRoutingJobUpdatedEventListener(std::move(f));
}

}  // namespace fr
