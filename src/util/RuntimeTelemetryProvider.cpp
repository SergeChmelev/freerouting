// Port of app.freerouting.util.RuntimeTelemetryProvider
#include "util/RuntimeTelemetryProvider.h"

namespace fr {

namespace {
class FallbackRuntimeTelemetryProvider : public RuntimeTelemetryProvider {
 public:
  int64_t currentThreadCpuNanos() override { return -1LL; }

  int64_t currentThreadAllocatedBytes() override { return -1LL; }

  float currentHeapUsageMb() override {
    // PORT NOTE: Runtime.totalMemory() - freeMemory() -> Boehm GC heap size.
    int64_t used = jrt::Math::max(static_cast<int64_t>(0),
                                  static_cast<int64_t>(jrt::heap_live_bytes()));
    return static_cast<float>(used) / (1024.0f * 1024.0f);
  }
};
}  // namespace

RuntimeTelemetryProvider* RuntimeTelemetryProvider::FALLBACK() {
  static RuntimeTelemetryProvider* v = jrt::onHeap([&] { return new (jrt::kBoard) FallbackRuntimeTelemetryProvider(); });
  return v;
}

RuntimeTelemetryProvider* RuntimeTelemetryProvider::get() {
  return RuntimeTelemetryProvider_Holder::INSTANCE();
}

RuntimeTelemetryProvider* RuntimeTelemetryProvider_Holder::INSTANCE() {
  static RuntimeTelemetryProvider* v = jrt::onHeap([&] { return load(); });
  return v;
}

RuntimeTelemetryProvider* RuntimeTelemetryProvider_Holder::load() {
  // PORT NOTE: no ServiceLoader in C++; the fallback is always used.
  return RuntimeTelemetryProvider::FALLBACK();
}

}  // namespace fr
