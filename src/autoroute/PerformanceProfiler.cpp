#include "autoroute/PerformanceProfiler.h"

#include "logger/FRLogger.h"

#include <mutex>

namespace fr {

namespace {
std::mutex& mapsMutex() {
  static std::mutex m;
  return m;
}
}  // namespace

jrt::Map<std::string, int64_t>* PerformanceProfiler::timings() {
  static jrt::Map<std::string, int64_t>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::ConcurrentHashMap<std::string, int64_t>(); });
  return v;
}

jrt::Map<std::string, int64_t>* PerformanceProfiler::counts() {
  static jrt::Map<std::string, int64_t>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::ConcurrentHashMap<std::string, int64_t>(); });
  return v;
}

jrt::Deque<PerformanceProfiler_Invocation*>* PerformanceProfiler::stack() {
  // Java: ThreadLocal<Deque<Invocation>> (optimizer workers have their own stack).
  return jrt::perThread<jrt::ArrayDeque<PerformanceProfiler_Invocation*>>(
      [] { return new (jrt::kBoard) jrt::ArrayDeque<PerformanceProfiler_Invocation*>(); });
}

jrt::List<PerformanceProfiler_PassInfo*>* PerformanceProfiler::passHistory() {
  static jrt::List<PerformanceProfiler_PassInfo*>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::ArrayList<PerformanceProfiler_PassInfo*>(); });
  return v;
}

PerformanceProfiler_ConfigInfo*& PerformanceProfiler::configInfo() {
  static PerformanceProfiler_ConfigInfo* v = nullptr;
  return v;
}

void PerformanceProfiler::start(const std::string& section) {
  stack()->push(new (jrt::kOp) PerformanceProfiler_Invocation(section, jrt::System::nanoTime()));
}

void PerformanceProfiler::end(const std::string& section) {
  jrt::Deque<PerformanceProfiler_Invocation*>* s = stack();
  if (!s->isEmpty()) {
    PerformanceProfiler_Invocation* inv = s->pop();
    if (inv->name == section) {
      int64_t duration = jrt::System::nanoTime() - inv->startTime;
      // AtomicLong computeIfAbsent(...).addAndGet / incrementAndGet
      std::lock_guard<std::mutex> lock(mapsMutex());  // Java: ConcurrentHashMap + AtomicLong
      timings()->put(section, timings()->getOrDefault(section, 0) + duration);
      counts()->put(section, counts()->getOrDefault(section, 0) + 1);
    } else {
      // Imbalance or wrong nesting: ignored (see Java comments).
    }
  }
}

void PerformanceProfiler::recordPass(int32_t passNo, int32_t unroutedItems, int64_t durationMs,
                                     int32_t ripupCost) {
  std::lock_guard<std::mutex> lock(mapsMutex());
  passHistory()->add(new (jrt::kOp) PerformanceProfiler_PassInfo(passNo, unroutedItems, durationMs, ripupCost));
}

void PerformanceProfiler::recordConfiguration(int32_t viaCosts, int32_t planeViaCosts,
                                              jrt::Array<double>* preferredCosts,
                                              jrt::Array<double>* againstCosts) {
  std::lock_guard<std::mutex> lock(mapsMutex());
  configInfo() = new (jrt::kOp) PerformanceProfiler_ConfigInfo(viaCosts, planeViaCosts, preferredCosts, againstCosts);
}

void PerformanceProfiler::printResults() {
  FRLogger::debug("=== Performance Profile ===");

  PerformanceProfiler_ConfigInfo* ci = configInfo();
  if (ci != nullptr) {
    FRLogger::debug("=== Routing Parameters ===");
    FRLogger::debug(jrt::format("  Via Costs: %d, Plane Via Costs: %d", ci->viaCosts, ci->planeViaCosts));
    FRLogger::debug("  Layer Costs (Preferred / Against):");
    for (int32_t i = 0; i < ci->preferredCosts->length; i++) {
      FRLogger::debug(jrt::format("    Layer %d: %.1f / %.1f", i + 1, (*ci->preferredCosts)[i],
                                  (*ci->againstCosts)[i]));
    }
    FRLogger::debug("");
  }

  jrt::ArrayList<jrt::MapEntry<std::string, int64_t>*>* sorted =
      new (jrt::kOp) jrt::ArrayList<jrt::MapEntry<std::string, int64_t>*>();
  for (auto* e : *timings()->entrySet()) {
    sorted->add(e);
  }
  sorted->sort([](jrt::MapEntry<std::string, int64_t>* e1, jrt::MapEntry<std::string, int64_t>* e2) {
    return jrt::Long::compare(e2->getValue(), e1->getValue());
  });
  for (auto* entry : *sorted) {
    std::string section = entry->getKey();
    int64_t totalMs = entry->getValue() / 1000000;
    int64_t count = counts()->get(section);
    int64_t avgMs = count > 0 ? totalMs / count : 0;

    FRLogger::debug(jrt::format("  %-40s: %8lld ms total, %8lld calls, %6lld ms avg", section.c_str(),
                                (long long)totalMs, (long long)count, (long long)avgMs));
  }

  if (!passHistory()->isEmpty()) {
    FRLogger::debug("");
    FRLogger::debug("=== Pass History ===");
    passHistory()->sort(jrt::comparingInt<PerformanceProfiler_PassInfo*>(
        [](PerformanceProfiler_PassInfo* p) { return p->passNo; }));
    for (PerformanceProfiler_PassInfo* pass : *passHistory()) {
      FRLogger::debug(jrt::format("  Pass %-3d: %4d unrouted items, %8.2f s, %5d ripup cost", pass->passNo,
                                  pass->unroutedItems, pass->durationMs / 1000.0, pass->ripupCost));
    }
  }

  FRLogger::debug("===========================");
}

void PerformanceProfiler::reset() {
  std::lock_guard<std::mutex> lock(mapsMutex());
  timings()->clear();
  counts()->clear();
}

}  // namespace fr
