// Port of app.freerouting.logger.LogEntries
#pragma once

#include <exception>

#include "fwd.h"
#include "jrt/jrt.h"
#include "logger/LogEntryType.h"

namespace fr {

/** Listener notified when a new log entry is added. */
class LogEntries_LogEntryAddedListener : public virtual jrt::Object {
 public:
  /** Called when a new log entry is stored. */
  virtual void logEntryAdded(LogEntry* logEntry) = 0;
};

/**
 * In-memory collection of log entries for UI display and export.
 * PORT NOTE: synchronized blocks dropped; Instant -> int64_t epoch ms; UUID ->
 * std::optional<std::string>; Throwable -> const std::exception*.
 */
class LogEntries : public virtual jrt::Object {
 public:
  LogEntries();

  /** Returns the number of warning entries currently stored. */
  int32_t getWarningCount();

  /** Returns the number of error entries currently stored. */
  int32_t getErrorCount();

  /** Removes all stored log entries. */
  void clear();

  /** Returns all entries as a single newline-delimited string. */
  std::string getAsString();

  /** Returns all entries as an array of formatted strings. */
  jrt::Array<std::string>* get();

  /** Returns entries filtered by timestamp (nullopt: all) and optional topic. */
  jrt::Array<LogEntry*>* getEntries(std::optional<int64_t> entriesSince,
                                    const std::optional<std::string>& topic);

  /** Adds a log entry without an associated exception. */
  LogEntry* add(LogEntryType type, const std::string& message,
                const std::optional<std::string>& topic);

  /** Adds a log entry, optionally with an associated exception. */
  LogEntry* add(LogEntryType type, const std::string& message,
                const std::optional<std::string>& topic, const std::exception* exception);

  /** Registers a listener notified when a new entry is added. */
  void addLogEntryAddedListener(LogEntries_LogEntryAddedListener* listener);

  /** Unregisters a previously added entry listener. */
  void removeLogEntryAddedListener(LogEntries_LogEntryAddedListener* listener);

 private:
  jrt::List<LogEntry*>* entries;
  jrt::List<LogEntries_LogEntryAddedListener*>* listeners;
};

}  // namespace fr
