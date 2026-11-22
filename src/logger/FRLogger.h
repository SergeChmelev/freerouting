// Port of app.freerouting.logger.FRLogger
#pragma once

#include <exception>
#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * PORT NOTE: not a Java type. Minimal stand-in for the java.text.DecimalFormat instances
 * FRLogger exposes ("0.00" and "+0.00;-0.00", Locale.US, RoundingMode.HALF_EVEN on the exact
 * binary value of the double, like DecimalFormat). Callers write
 * FRLogger::defaultFloatFormat()->format(x).
 */
class FRLogger_DecimalFormat : public virtual jrt::Object {
 public:
  explicit FRLogger_DecimalFormat(const std::string& pattern) : pattern(pattern) {}

  std::string format(double value);

 private:
  const std::string pattern;
};

/**
 * Provides centralized logging functionality for the application.
 *
 * PORT NOTE: Log4j2 is replaced by plain output to stderr ("LEVEL message"). The Log4j level
 * configuration is replaced by FRLogger::logLevel (LEVEL_* constants, default LEVEL_INFO); getLogger()
 * is dropped. DebugControl (debug package, out of scope) is treated as absent: isActive(),
 * isInterested() and check() are false. java.util.UUID topics are std::optional<std::string>
 * (nullopt == null). Throwable parameters are const std::exception* (nullptr == null); call sites
 * pass &e from `catch (const jrt::Exception& e)`. java.time.Instant -> int64_t epoch ms.
 */
class FRLogger final {
 public:
  // Log levels (Log4j order). A message is printed if its level <= logLevel.
  static constexpr int32_t LEVEL_OFF = 0;
  static constexpr int32_t LEVEL_ERROR = 1;
  static constexpr int32_t LEVEL_WARN = 2;
  static constexpr int32_t LEVEL_INFO = 3;
  static constexpr int32_t LEVEL_DEBUG = 4;
  static constexpr int32_t LEVEL_TRACE = 5;

  /** PORT NOTE: replaces the Log4j configuration of the logger's level. */
  static inline int32_t logLevel = LEVEL_INFO;
  /** PORT EXTENSION (not in Java): optional receiver of every emitted log line (level, text),
   * e.g. a UI log panel. Called on the logging thread. */
  static inline std::function<void(int32_t, const std::string&)> sink;
  /** PORT EXTENSION: whether emitted lines are also printed to stderr. */
  static inline bool consoleOutput = true;

  static FRLogger_DecimalFormat* defaultFloatFormat();
  static FRLogger_DecimalFormat* defaultSignedFloatFormat();

  static inline bool granularTraceEnabled = false;

  /** Enables or disables logging globally. */
  static void setEnabled(bool value) { enabled = value; }

  /** Formats a duration in seconds into a human-readable string (hours, minutes, seconds). */
  static std::string formatDuration(double totalSeconds);

  /** Formats a score with details about incomplete items and violations. */
  static std::string formatScore(float score, int32_t incomplete, int32_t violations);

  /** Builds a structured key-value trace payload string. */
  static std::string buildTracePayload(const std::string& event,
                                       const std::optional<std::string>& phase,
                                       const std::optional<std::string>& action,
                                       const std::optional<std::string>& kvPairs);

  /** Formats labels for one or more net numbers. */
  static std::string formatNetLabel(BasicBoard* board, jrt::Array<int32_t>* netNumbers);

  /** Formats a label for a single net number. */
  static std::string formatNetLabel(BasicBoard* board, int32_t netNumber);

  /** Records the start time for a performance trace. */
  static void traceEntry(const std::string& perfId);

  /** Records the end of a performance trace and logs the duration. Returns seconds. */
  static double traceExit(const std::string& perfId);

  /** Records the end of a performance trace with an optional result object. Returns seconds. */
  static double traceExit(const std::string& perfId, jrt::Object* result);

  /** Logs an INFO message. */
  static LogEntry* info(const std::string& msg, const std::optional<std::string>& topic);

  /** Logs an INFO message without a topic. */
  static LogEntry* info(const std::string& msg) { return info(msg, std::nullopt); }

  /** Logs a WARNING message. */
  static LogEntry* warn(const std::string& msg, const std::optional<std::string>& topic);

  /** Logs a WARNING message without a topic. */
  static LogEntry* warn(const std::string& msg) { return warn(msg, std::nullopt); }

  /** Logs a DEBUG message. */
  static LogEntry* debug(const std::string& msg, const std::optional<std::string>& topic);

  /** Logs a DEBUG message without a topic. */
  static LogEntry* debug(const std::string& msg) { return debug(msg, std::nullopt); }

  /** Logs an ERROR message with an exception (nullptr for none). */
  static LogEntry* error(const std::string& msg, const std::optional<std::string>& topic,
                         const std::exception* exception);

  /** Logs an ERROR message with an exception, but without a topic. */
  static LogEntry* error(const std::string& msg, const std::exception* exception) {
    return error(msg, std::nullopt, exception);
  }

  /** Checks if TRACE level logging is enabled. */
  static bool isTraceEnabled() { return enabled && logLevel >= LEVEL_TRACE; }

  /**
   * Returns true when a granular trace(method, operation, message, impactedItems) call has an
   * effect. Hot call sites check it first, so that they do not build a message nobody reads.
   * (DebugControl.getInstance().isActive() is always false in the port.)
   */
  static bool isGranularTraceActive() {
    return enabled && granularTraceEnabled && isTraceEnabled();
  }

  /** Logs a TRACE message. Always returns null. */
  static LogEntry* trace(const std::string& msg);

  /** Logs a granular TRACE message without impacted points. */
  static bool trace(const std::string& method, const std::string& operation,
                    const std::string& message, const std::string& impactedItems) {
    return trace(method, operation, message, impactedItems, nullptr);
  }

  /** Logs a granular TRACE message and triggers a debug check. */
  static bool trace(const std::string& method, const std::string& operation,
                    const std::string& message, const std::string& impactedItems,
                    jrt::Array<Point*>* impactedPoints);

  /** Disables logging. */
  static void disableLogging() { enabled = false; }

  /** Gets the collection of log entries recorded by this logger. */
  static LogEntries* getLogEntries() { return logEntries(); }

  /** Adds a listener that will be notified of interesting trace events. */
  static void addTraceEventListener(TraceEventListener* listener);

  /** Removes a listener from the list of trace event listeners. */
  static void removeTraceEventListener(TraceEventListener* listener);

 private:
  FRLogger() = delete;

  static inline const std::string LOGGER_NAME = "app.freerouting.Freerouting";

  /** perfId.hashCode() -> start time (epoch ms). */
  static jrt::HashMap<int32_t, int64_t>* perfData();
  static LogEntries* logEntries();
  static jrt::ArrayList<TraceEventListener*>* traceEventListeners();
  static inline bool enabled = true;

  /** PORT NOTE: replaces logger.<level>(msg): prints to stderr if level <= logLevel. */
  static void emit(int32_t level, const std::string& msg, const std::exception* exception);

  static void publishTraceEvent(TraceEvent* event);
};

}  // namespace fr
