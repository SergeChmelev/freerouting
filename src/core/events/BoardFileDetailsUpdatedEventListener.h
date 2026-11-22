#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Receives events when board file details change. */
class BoardFileDetailsUpdatedEventListener : public virtual jrt::Object {
 public:
  /** Handles a board file details update. */
  virtual void onBoardFileDetailsUpdated(BoardFileDetailsUpdatedEvent* event) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as this listener. */
  static BoardFileDetailsUpdatedEventListener* of(
      std::function<void(BoardFileDetailsUpdatedEvent*)> f);
};

namespace detail {
class LambdaBoardFileDetailsUpdatedEventListener : public BoardFileDetailsUpdatedEventListener {
 public:
  explicit LambdaBoardFileDetailsUpdatedEventListener(
      std::function<void(BoardFileDetailsUpdatedEvent*)> f)
      : f_(std::move(f)) {}
  void onBoardFileDetailsUpdated(BoardFileDetailsUpdatedEvent* event) override { f_(event); }

 private:
  std::function<void(BoardFileDetailsUpdatedEvent*)> f_;
};
}  // namespace detail

inline BoardFileDetailsUpdatedEventListener* BoardFileDetailsUpdatedEventListener::of(
    std::function<void(BoardFileDetailsUpdatedEvent*)> f) {
  return new (jrt::kOp) detail::LambdaBoardFileDetailsUpdatedEventListener(std::move(f));
}

}  // namespace fr
