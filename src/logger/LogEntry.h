// Port of app.freerouting.logger.LogEntry
#pragma once

#include <exception>

#include "fwd.h"
#include "jrt/jrt.h"
#include "logger/LogEntryType.h"

namespace fr {

/**
 * Represents a single in-memory log entry.
 * PORT NOTE: Instant -> int64_t epoch milliseconds; UUID topic -> std::optional<std::string>
 * (UUID string form, nullopt for null); Throwable -> const std::exception* (not owned, only valid
 * during the logging call; kept for API parity).
 */
class LogEntry : public virtual jrt::Object {
 public:
  LogEntry(LogEntryType type, const std::string& message, const std::exception* exception,
           const std::optional<std::string>& topic);

  LogEntryType getType() { return this->type; }
  std::string getMessage() { return this->message; }
  std::optional<std::string> getTopic() { return this->topic; }
  const std::exception* getException() { return this->exception; }

  /** "%-7s" of the upper-case type name, a blank and the message. */
  std::string toString() override;

  // package-private in Java (read by LogEntries)
  /** Timestamp of the log entry. */
  int64_t timestamp;
  /** Type of the log entry. */
  LogEntryType type;
  /** Message of the log entry. */
  std::string message;
  /** Exception of the log entry (transient). */
  const std::exception* exception;
  /** Topic of the log entry, usually the ID of the user, session, or job. */
  std::optional<std::string> topic;
};

}  // namespace fr
