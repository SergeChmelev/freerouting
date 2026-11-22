#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Functional callback interface for receiving progress updates during headless routing. */
class HeadlessRoutingProgressListener : public virtual jrt::Object {
 public:
  /** Invoked when routing progress occurs. */
  virtual void onProgress(HeadlessRoutingProgress* progress) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as this listener. */
  static HeadlessRoutingProgressListener* of(std::function<void(HeadlessRoutingProgress*)> f);
};

namespace detail {
class LambdaHeadlessRoutingProgressListener : public HeadlessRoutingProgressListener {
 public:
  explicit LambdaHeadlessRoutingProgressListener(std::function<void(HeadlessRoutingProgress*)> f)
      : f_(std::move(f)) {}
  void onProgress(HeadlessRoutingProgress* progress) override { f_(progress); }

 private:
  std::function<void(HeadlessRoutingProgress*)> f_;
};
}  // namespace detail

inline HeadlessRoutingProgressListener* HeadlessRoutingProgressListener::of(
    std::function<void(HeadlessRoutingProgress*)> f) {
  return new detail::LambdaHeadlessRoutingProgressListener(std::move(f));
}

}  // namespace fr
