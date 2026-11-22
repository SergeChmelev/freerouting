#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Listener for board updated events. */
class BoardUpdatedEventListener : public virtual jrt::Object {
 public:
  /** Handles a board updated event. */
  virtual void onBoardUpdatedEvent(BoardUpdatedEvent* event) = 0;

  /**
   * Whether this listener reads the board statistics of the events. The router builds them, which
   * scans the whole board, only when one of its listeners does; otherwise they are null.
   */
  virtual bool readsBoardStatistics() { return true; }

  /** A listener that handles the events without reading their board statistics. */
  static BoardUpdatedEventListener* ignoringStatistics(std::function<void(BoardUpdatedEvent*)> handler);

  /** PORT NOTE: adapter for Java lambdas passed as a BoardUpdatedEventListener
   * (readsBoardStatistics() stays true, like a Java lambda). */
  static BoardUpdatedEventListener* of(std::function<void(BoardUpdatedEvent*)> f);
};

namespace detail {
class LambdaBoardUpdatedEventListener : public BoardUpdatedEventListener {
 public:
  LambdaBoardUpdatedEventListener(std::function<void(BoardUpdatedEvent*)> f, bool reads)
      : f_(std::move(f)), reads_(reads) {}
  void onBoardUpdatedEvent(BoardUpdatedEvent* event) override { f_(event); }
  bool readsBoardStatistics() override { return reads_; }

 private:
  std::function<void(BoardUpdatedEvent*)> f_;
  bool reads_;
};
}  // namespace detail

inline BoardUpdatedEventListener* BoardUpdatedEventListener::ignoringStatistics(
    std::function<void(BoardUpdatedEvent*)> handler) {
  return new (jrt::kOp) detail::LambdaBoardUpdatedEventListener(std::move(handler), false);
}

inline BoardUpdatedEventListener* BoardUpdatedEventListener::of(
    std::function<void(BoardUpdatedEvent*)> f) {
  return new (jrt::kOp) detail::LambdaBoardUpdatedEventListener(std::move(f), true);
}

}  // namespace fr
