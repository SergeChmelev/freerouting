// Port of app.freerouting.autoroute.pipeline.AutorouteRuntimeMetrics
#include "autoroute/pipeline/AutorouteRuntimeMetrics.h"

#include "core/RouterJobResourceUsage.h"
#include "core/RoutingJob.h"
#include "util/RuntimeTelemetryProvider.h"

namespace fr {

float AutorouteRuntimeMetrics::cpuSecondsSnapshot(RoutingJob* job) {
  if (job == nullptr || job->resourceUsage == nullptr) {
    return 0.0f;
  }
  return job->resourceUsage->cpuTimeUsed;
}

float AutorouteRuntimeMetrics::allocatedMemoryMbSnapshot(RoutingJob* job) {
  if (job == nullptr || job->resourceUsage == nullptr) {
    return 0.0f;
  }
  return job->resourceUsage->maxMemoryUsed;
}

float AutorouteRuntimeMetrics::peakHeapMbSnapshot(RoutingJob* job) {
  if (job == nullptr || job->resourceUsage == nullptr) {
    return 0.0f;
  }
  return job->resourceUsage->peakMemoryUsed;
}

int64_t AutorouteRuntimeMetrics::currentThreadCpuNanos() {
  return RuntimeTelemetryProvider::get()->currentThreadCpuNanos();
}

float AutorouteRuntimeMetrics::currentThreadCpuSeconds() {
  return RuntimeTelemetryProvider::get()->currentThreadCpuSeconds();
}

int64_t AutorouteRuntimeMetrics::currentThreadAllocatedBytes() {
  return RuntimeTelemetryProvider::get()->currentThreadAllocatedBytes();
}

float AutorouteRuntimeMetrics::currentThreadAllocatedMb() {
  return RuntimeTelemetryProvider::get()->currentThreadAllocatedMb();
}

float AutorouteRuntimeMetrics::currentHeapUsageMb() {
  return RuntimeTelemetryProvider::get()->currentHeapUsageMb();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
std::string AutorouteRuntimeMetrics::formatOptionalMetric(float value, const std::string& format) {
  return value < 0.0f ? std::string("N/A") : jrt::format(format.c_str(), static_cast<double>(value));
}
#pragma GCC diagnostic pop

}  // namespace fr
