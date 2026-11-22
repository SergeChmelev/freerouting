#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Listener for board snapshot events. */
class BoardSnapshotEventListener : public virtual jrt::Object {
 public:
  /** Handles a board snapshot event. */
  virtual void onBoardSnapshotEvent(BoardSnapshotEvent* event) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as a BoardSnapshotEventListener. */
  static BoardSnapshotEventListener* of(std::function<void(BoardSnapshotEvent*)> f);
};

namespace detail {
class LambdaBoardSnapshotEventListener : public BoardSnapshotEventListener {
 public:
  explicit LambdaBoardSnapshotEventListener(std::function<void(BoardSnapshotEvent*)> f)
      : f_(std::move(f)) {}
  void onBoardSnapshotEvent(BoardSnapshotEvent* event) override { f_(event); }

 private:
  std::function<void(BoardSnapshotEvent*)> f_;
};
}  // namespace detail

inline BoardSnapshotEventListener* BoardSnapshotEventListener::of(
    std::function<void(BoardSnapshotEvent*)> f) {
  return new detail::LambdaBoardSnapshotEventListener(std::move(f));
}

}  // namespace fr
