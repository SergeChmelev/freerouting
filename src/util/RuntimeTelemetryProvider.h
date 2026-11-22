// Port of app.freerouting.util.RuntimeTelemetryProvider
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Provides optional runtime telemetry (per-thread CPU time, per-thread allocation, heap usage).
 *
 * PORT NOTE: there is no ServiceLoader in C++. get() returns FALLBACK(), whose thread metrics are
 * -1 (unsupported) and whose heap usage is the Boehm GC heap size in MB (jrt::gc_heap_size()) in
 * place of Runtime.totalMemory() - freeMemory().
 */
class RuntimeTelemetryProvider : public virtual jrt::Object {
 public:
  /** Fallback returning -1 for thread CPU/allocation metrics and the heap usage in MB. */
  static RuntimeTelemetryProvider* FALLBACK();

  /** Returns the current thread's CPU time in nanoseconds, or -1 if unsupported. */
  virtual int64_t currentThreadCpuNanos() = 0;

  /** Returns the memory allocated on the heap by the current thread in bytes, or -1. */
  virtual int64_t currentThreadAllocatedBytes() = 0;

  /** Returns the current heap memory usage in megabytes. */
  virtual float currentHeapUsageMb() = 0;

  /** Returns the current thread's CPU time in seconds, or -1f if unsupported. */
  virtual float currentThreadCpuSeconds() {
    int64_t nanos = currentThreadCpuNanos();
    return nanos < 0LL ? -1.0f : static_cast<float>(nanos) / 1000000000.0f;
  }

  /** Returns the current thread's allocated heap memory in megabytes, or -1f if unsupported. */
  virtual float currentThreadAllocatedMb() {
    int64_t bytes = currentThreadAllocatedBytes();
    return bytes < 0LL ? -1.0f : static_cast<float>(bytes) / (1024.0f * 1024.0f);
  }

  /** Returns the fallback provider. */
  static RuntimeTelemetryProvider* fallback() { return FALLBACK(); }

  /** Returns the active RuntimeTelemetryProvider. */
  static RuntimeTelemetryProvider* get();
};

/** Lazy holder for the discovered provider. */
class RuntimeTelemetryProvider_Holder final : public virtual jrt::Object {
 public:
  static RuntimeTelemetryProvider* INSTANCE();

 private:
  RuntimeTelemetryProvider_Holder() = default;

  /** PORT NOTE: no ServiceLoader; always returns FALLBACK(). */
  static RuntimeTelemetryProvider* load();
};

}  // namespace fr
