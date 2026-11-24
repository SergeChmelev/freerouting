// Port of app.freerouting.logger.FRLogger
#include "platform/platform.h"
#include "logger/FRLogger.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <ctime>

#include "board/facade/BasicBoard.h"
#include "logger/LogEntries.h"
#include "logger/LogEntry.h"
#include "logger/TraceEvent.h"
#include "logger/TraceEventListener.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// FRLogger_DecimalFormat
// ---------------------------------------------------------------------------------------------

std::string FRLogger_DecimalFormat::format(double value) {
  // Patterns used: "0.00" and "+0.00;-0.00". DecimalFormat rounds HALF_EVEN on the exact binary
  // value of the double, which is what glibc's printf does in the default rounding mode.
  bool explicitPlus = pattern.find(';') != std::string::npos && !pattern.empty() && pattern[0] == '+';
  if (std::isnan(value)) {
    return "NaN";
  }
  bool negative = std::signbit(value);
  std::string body;
  if (std::isinf(value)) {
    body = "\xE2\x88\x9E";  // U+221E
  } else {
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.2f", std::fabs(value));
    body = buf;
  }
  if (negative) {
    return "-" + body;
  }
  return explicitPlus ? "+" + body : body;
}

// ---------------------------------------------------------------------------------------------
// statics
// ---------------------------------------------------------------------------------------------

FRLogger_DecimalFormat* FRLogger::defaultFloatFormat() {
  static FRLogger_DecimalFormat* v = jrt::onHeap([&] { return new (jrt::kBoard) FRLogger_DecimalFormat("0.00"); });
  return v;
}

FRLogger_DecimalFormat* FRLogger::defaultSignedFloatFormat() {
  static FRLogger_DecimalFormat* v = jrt::onHeap([&] { return new (jrt::kBoard) FRLogger_DecimalFormat("+0.00;-0.00"); });
  return v;
}

jrt::HashMap<int32_t, int64_t>* FRLogger::perfData() {
  static jrt::HashMap<int32_t, int64_t>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::HashMap<int32_t, int64_t>(); });
  return v;
}

std::recursive_mutex& FRLogger::mutex() {
  static std::recursive_mutex m;
  return m;
}

LogEntries* FRLogger::logEntries() {
  static LogEntries* v = jrt::onHeap([&] { return new (jrt::kBoard) LogEntries(); });
  return v;
}

jrt::ArrayList<TraceEventListener*>* FRLogger::traceEventListeners() {
  static jrt::ArrayList<TraceEventListener*>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::ArrayList<TraceEventListener*>(); });
  return v;
}

// ---------------------------------------------------------------------------------------------
// emit (replaces Log4j)
// ---------------------------------------------------------------------------------------------

void FRLogger::emit(int32_t level, const std::string& msg, const std::exception* exception) {
  if (level > logLevel || level <= LEVEL_OFF) {
    return;
  }
  if (sink) {
    sink(level, exception != nullptr ? msg + " (" + exception->what() + ")" : msg);
  }
  if (!consoleOutput) {
    return;
  }
  static const char* const NAMES[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  int ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
  std::tm tm{};
  jrt::platform::local_time(t, &tm);
  char ts[64];
  std::snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  std::fprintf(stderr, "%s %-5s %s\n", ts, NAMES[level], msg.c_str());
  if (exception != nullptr) {
    std::fprintf(stderr, "    exception: %s\n", exception->what());
  }
  // A caller reading the log through a pipe (the KiCad plugin's progress window) sees each line
  // at once: the Windows C runtime buffers stderr when it is not a console.
  std::fflush(stderr);
}

// ---------------------------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------------------------

std::string FRLogger::formatDuration(double totalSeconds) {
  double seconds = totalSeconds;
  double minutes = seconds / 60.0;
  double hours = minutes / 60.0;

  hours = jrt::Math::floor(hours);
  minutes = jrt::Math::floor(std::fmod(minutes, 60.0));
  seconds = std::fmod(seconds, 60.0);

  std::string hoursText =
      hours > 0 ? jrt::str(jrt::d2i(hours)) + (hours == 1 ? " hour " : " hours ") : "";

  std::string minutesText =
      minutes > 0 ? jrt::str(jrt::d2i(minutes)) + (minutes == 1 ? " minute " : " minutes ") : "";

  return hoursText + minutesText + defaultFloatFormat()->format(seconds) + " seconds";
}

std::string FRLogger::formatScore(float score, int32_t incomplete, int32_t violations) {
  std::string sb = defaultFloatFormat()->format(static_cast<double>(score));

  // Always include unrouted and violations for a consistent, parseable format
  sb += " (";
  sb += jrt::str(incomplete) + " unrouted";
  sb += " and ";
  sb += jrt::str(violations) + (violations == 1 ? " violation" : " violations");
  sb += ")";

  return sb;
}

std::string FRLogger::buildTracePayload(const std::string& event,
                                        const std::optional<std::string>& phase,
                                        const std::optional<std::string>& action,
                                        const std::optional<std::string>& kvPairs) {
  std::string sb;
  sb += "event=" + event;
  if (phase.has_value() && !phase->empty()) {
    sb += " phase=" + *phase;
  }
  if (action.has_value() && !action->empty()) {
    sb += " action=" + *action;
  }
  if (kvPairs.has_value() && !kvPairs->empty()) {
    sb += " " + *kvPairs;
  }
  return sb;
}

std::string FRLogger::formatNetLabel(BasicBoard* board, jrt::Array<int32_t>* netNumbers) {
  if (netNumbers == nullptr || netNumbers->length == 0) {
    return "No net";
  }
  std::string sb;
  for (int32_t i = 0; i < netNumbers->length; i++) {
    if (i > 0) {
      sb += ", ";
    }
    sb += formatNetLabel(board, (*netNumbers)[i]);
  }
  return sb;
}

std::string FRLogger::formatNetLabel(BasicBoard* board, int32_t netNumber) {
  if (board == nullptr || board->rules == nullptr || board->rules->nets == nullptr) {
    return "Net #" + jrt::str(netNumber) + " (Unknown)";
  }
  if (netNumber <= board->rules->nets->maxNetNumber()) {
    return jrt::str(board->rules->nets->get(netNumber));
  }
  return "Net #" + jrt::str(netNumber) + " (Unknown)";
}

void FRLogger::traceEntry(const std::string& perfId) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return;
  }

  perfData()->put(jrt::stringHashCode(perfId), jrt::System::currentTimeMillis());
}

double FRLogger::traceExit(const std::string& perfId) {
  if (!enabled) {
    return 0.0;
  }

  return traceExit(perfId, nullptr);
}

double FRLogger::traceExit(const std::string& perfId, jrt::Object* result) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return 0.0;
  }

  int64_t timeElapsed = 0;
  // Java: a missing start time throws a NullPointerException that is ignored.
  std::optional<int64_t> start = perfData()->getOpt(jrt::stringHashCode(perfId));
  if (start.has_value()) {
    timeElapsed = jrt::System::currentTimeMillis() - *start;
  }

  perfData()->remove(jrt::stringHashCode(perfId));
  if (timeElapsed < 0) {
    timeElapsed = 0;
  }

  std::string logMessage =
      "Method '" +
      jrt::replace(perfId, "{}", result != nullptr ? result->toString() : std::string("(null)")) +
      "' was performed in " + FRLogger::formatDuration(timeElapsed / 1000.0) + ".";

  FRLogger::trace(logMessage);

  return timeElapsed / 1000.0;
}

LogEntry* FRLogger::info(const std::string& msg, const std::optional<std::string>& topic) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return nullptr;
  }

  emit(LEVEL_INFO, msg, nullptr);

  return logEntries()->add(LogEntryType::Info, msg, topic);
}

LogEntry* FRLogger::warn(const std::string& msg, const std::optional<std::string>& topic) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return nullptr;
  }

  emit(LEVEL_WARN, msg, nullptr);

  return logEntries()->add(LogEntryType::Warning, msg, topic);
}

LogEntry* FRLogger::debug(const std::string& msg, const std::optional<std::string>& topic) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return nullptr;
  }

  emit(LEVEL_DEBUG, msg, nullptr);

  return nullptr;
}

LogEntry* FRLogger::error(const std::string& msg, const std::optional<std::string>& topic,
                          const std::exception* exception) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return nullptr;
  }

  emit(LEVEL_ERROR, msg, exception);

  return logEntries()->add(LogEntryType::Error, msg, topic, exception);
}

LogEntry* FRLogger::trace(const std::string& msg) {
  std::lock_guard<std::recursive_mutex> lock(mutex());  // optimizer workers log too
  if (!enabled) {
    return nullptr;
  }

  emit(LEVEL_TRACE, msg, nullptr);

  return nullptr;
}

bool FRLogger::trace(const std::string& method, const std::string& operation,
                     const std::string& message, const std::string& impactedItems,
                     jrt::Array<Point*>* impactedPoints) {
  if (enabled) {
    // PORT NOTE: DebugControl is out of scope; isInterested() is treated as false.
    // Debug aid (not in Java): FR_TRACE_ALL=1 logs every granular trace event, matching the
    // -Dfr.traceAll=true switch of the debug reference build (tools/parity/).
    static const bool traceAll = jrt::System::getenv("FR_TRACE_ALL").value_or("") == "1";
    if (granularTraceEnabled && (impactedItems.empty() || traceAll) && logLevel >= LEVEL_TRACE) {
      std::string formattedMessage =
          "[" + method + "] [" + operation + "] " + message + ": " + impactedItems;
      emit(LEVEL_TRACE, formattedMessage, nullptr);
    }
  }

  // PORT NOTE: DebugControl.getInstance().check(...) is always false in the port.
  bool wasInterestingTraceEvent = false;
  if (wasInterestingTraceEvent) {
    publishTraceEvent(new (jrt::kBoard) TraceEvent(method, operation, message, impactedItems, impactedPoints,
                                     jrt::System::currentTimeMillis()));
  }

  return wasInterestingTraceEvent;
}

void FRLogger::addTraceEventListener(TraceEventListener* listener) {
  traceEventListeners()->add(listener);
}

void FRLogger::removeTraceEventListener(TraceEventListener* listener) {
  traceEventListeners()->remove(listener);
}

void FRLogger::publishTraceEvent(TraceEvent* event) {
  // CopyOnWriteArrayList: iterate over a snapshot
  auto snapshot = traceEventListeners()->toVector();
  for (TraceEventListener* listener : snapshot) {
    listener->onTraceEvent(event);
  }
}

}  // namespace fr
