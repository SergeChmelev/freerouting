// Port of app.freerouting.logger.TraceEventListener
#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Listener for FRLogger trace events that were marked as interesting. */
class TraceEventListener : public virtual jrt::Object {
 public:
  /** Called when an interesting trace event is published. */
  virtual void onTraceEvent(TraceEvent* event) = 0;

  /** Adapter for Java lambdas (@FunctionalInterface). */
  static TraceEventListener* of(std::function<void(TraceEvent*)> f) {
    class Fn : public TraceEventListener {
     public:
      explicit Fn(std::function<void(TraceEvent*)> f) : f_(std::move(f)) {}
      void onTraceEvent(TraceEvent* event) override { f_(event); }

     private:
      std::function<void(TraceEvent*)> f_;
    };
    return new (jrt::kOp) Fn(std::move(f));
  }
};

}  // namespace fr
