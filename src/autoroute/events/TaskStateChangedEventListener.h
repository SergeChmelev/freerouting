#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Listener for task state changed events. */
class TaskStateChangedEventListener : public virtual jrt::Object {
 public:
  /** Handles a task state changed event. */
  virtual void onTaskStateChangedEvent(TaskStateChangedEvent* event) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as a TaskStateChangedEventListener. */
  static TaskStateChangedEventListener* of(std::function<void(TaskStateChangedEvent*)> f);
};

namespace detail {
class LambdaTaskStateChangedEventListener : public TaskStateChangedEventListener {
 public:
  explicit LambdaTaskStateChangedEventListener(std::function<void(TaskStateChangedEvent*)> f)
      : f_(std::move(f)) {}
  void onTaskStateChangedEvent(TaskStateChangedEvent* event) override { f_(event); }

 private:
  std::function<void(TaskStateChangedEvent*)> f_;
};
}  // namespace detail

inline TaskStateChangedEventListener* TaskStateChangedEventListener::of(
    std::function<void(TaskStateChangedEvent*)> f) {
  return new detail::LambdaTaskStateChangedEventListener(std::move(f));
}

}  // namespace fr
