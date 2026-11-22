#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Functional cooperative cancellation hook for headless routing execution. Allows embedding
 * runtimes to signal cancellation to an active routing pipeline without thread interrupts.
 */
class CancellationToken : public virtual jrt::Object {
 public:
  /** Returns true if cancellation has been requested. */
  virtual bool isCancelled() = 0;

  /** Returns a cancellation token that is never cancelled (a new instance per call, like Java). */
  static CancellationToken* none();

  /** PORT NOTE: adapter for Java lambdas passed as CancellationToken. */
  static CancellationToken* of(std::function<bool()> f);
};

namespace detail {
class LambdaCancellationToken : public CancellationToken {
 public:
  explicit LambdaCancellationToken(std::function<bool()> f) : f_(std::move(f)) {}
  bool isCancelled() override { return f_(); }

 private:
  std::function<bool()> f_;
};
}  // namespace detail

inline CancellationToken* CancellationToken::of(std::function<bool()> f) {
  return new (jrt::kOp) detail::LambdaCancellationToken(std::move(f));
}

inline CancellationToken* CancellationToken::none() {
  return of([] { return false; });
}

}  // namespace fr
