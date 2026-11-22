#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A started, not yet ended timing section (Java: private static class Invocation). */
class PerformanceProfiler_Invocation : public virtual jrt::Object {
 public:
  std::string name;
  int64_t startTime;

  PerformanceProfiler_Invocation(const std::string& name, int64_t startTime)
      : name(name), startTime(startTime) {}
};

/** Performance info recorded for a single routing pass. */
class PerformanceProfiler_PassInfo : public virtual jrt::Object {
 public:
  int32_t passNo;
  int32_t unroutedItems;
  int64_t durationMs;
  int32_t ripupCost;

  PerformanceProfiler_PassInfo(int32_t passNo, int32_t unroutedItems, int64_t durationMs,
                               int32_t ripupCost)
      : passNo(passNo), unroutedItems(unroutedItems), durationMs(durationMs), ripupCost(ripupCost) {}
};

/** Store configuration metrics for reporting. */
class PerformanceProfiler_ConfigInfo : public virtual jrt::Object {
 public:
  int32_t viaCosts;
  int32_t planeViaCosts;
  jrt::Array<double>* preferredCosts;
  jrt::Array<double>* againstCosts;

  PerformanceProfiler_ConfigInfo(int32_t viaCosts, int32_t planeViaCosts,
                                 jrt::Array<double>* preferredCosts,
                                 jrt::Array<double>* againstCosts)
      : viaCosts(viaCosts),
        planeViaCosts(planeViaCosts),
        preferredCosts(preferredCosts),
        againstCosts(againstCosts) {}
};

/**
 * Simple performance profiler to measure time spent in different routing operations.
 *
 * PORT NOTE: static reference fields are accessor functions. AtomicLong map values become plain
 * int64_t values (single-threaded); the ThreadLocal invocation stack is a single ArrayDeque.
 */
class PerformanceProfiler : public virtual jrt::Object {
 private:
  static jrt::Map<std::string, int64_t>* timings();
  static jrt::Map<std::string, int64_t>* counts();
  static jrt::Deque<PerformanceProfiler_Invocation*>* stack();

  /** History of recorded passes. */
  static jrt::List<PerformanceProfiler_PassInfo*>* passHistory();

  static PerformanceProfiler_ConfigInfo*& configInfo();

 public:
  /** Start timing a section. */
  static void start(const std::string& section);

  /** End timing a section and record the duration. */
  static void end(const std::string& section);

  /** Record statistics for a completed pass. */
  static void recordPass(int32_t passNo, int32_t unroutedItems, int64_t durationMs,
                         int32_t ripupCost);

  /** Records configuration parameters for profiling output. */
  static void recordConfiguration(int32_t viaCosts, int32_t planeViaCosts,
                                  jrt::Array<double>* preferredCosts,
                                  jrt::Array<double>* againstCosts);

  /** Print profiling results sorted by total time. */
  static void printResults();

  /** Clear all profiling data. */
  static void reset();
};

}  // namespace fr
