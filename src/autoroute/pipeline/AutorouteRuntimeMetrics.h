#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Collects optional runtime metrics used by autoroute stage diagnostics. */
class AutorouteRuntimeMetrics : public virtual jrt::Object {
 private:
  AutorouteRuntimeMetrics() = default;

 public:
  static float cpuSecondsSnapshot(RoutingJob* job);
  static float allocatedMemoryMbSnapshot(RoutingJob* job);
  static float peakHeapMbSnapshot(RoutingJob* job);
  static int64_t currentThreadCpuNanos();
  static float currentThreadCpuSeconds();
  static int64_t currentThreadAllocatedBytes();
  static float currentThreadAllocatedMb();
  static float currentHeapUsageMb();
  /** Java: String.format(Locale.US, format, value); "N/A" for negative values. */
  static std::string formatOptionalMetric(float value, const std::string& format);
  static double nanosToMillis(int64_t nanos) { return nanos / 1000000.0; }
};

}  // namespace fr
