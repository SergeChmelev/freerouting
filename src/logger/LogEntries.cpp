// Port of app.freerouting.logger.LogEntries
#include "logger/LogEntries.h"

#include "logger/LogEntry.h"

namespace fr {

LogEntries::LogEntries()
    : entries(new (jrt::kBoard) jrt::ArrayList<LogEntry*>()),
      listeners(new (jrt::kBoard) jrt::ArrayList<LogEntries_LogEntryAddedListener*>()) {}

int32_t LogEntries::getWarningCount() {
  int32_t count = 0;
  for (LogEntry* e : *entries) {
    if (e->type == LogEntryType::Warning) {
      count++;
    }
  }
  return count;
}

int32_t LogEntries::getErrorCount() {
  int32_t count = 0;
  for (LogEntry* e : *entries) {
    if (e->type == LogEntryType::Error) {
      count++;
    }
  }
  return count;
}

void LogEntries::clear() { entries->clear(); }

std::string LogEntries::getAsString() {
  std::string result;
  bool first = true;
  for (LogEntry* e : *entries) {
    if (!first) {
      result += "\n";
    }
    first = false;
    result += e->toString();
  }
  result += "\n";
  return result;
}

jrt::Array<std::string>* LogEntries::get() {
  auto* result = new (jrt::kOp) jrt::Array<std::string>(entries->size());
  int32_t i = 0;
  for (LogEntry* e : *entries) {
    (*result)[i++] = e->toString();
  }
  return result;
}

jrt::Array<LogEntry*>* LogEntries::getEntries(std::optional<int64_t> entriesSince,
                                              const std::optional<std::string>& topic) {
  std::vector<LogEntry*> filtered;
  for (LogEntry* e : *entries) {
    if ((!entriesSince.has_value() || e->timestamp > *entriesSince) &&
        (!topic.has_value() || (e->topic.has_value() && *e->topic == *topic))) {
      filtered.push_back(e);
    }
  }
  return new (jrt::kOp) jrt::Array<LogEntry*>(filtered);
}

LogEntry* LogEntries::add(LogEntryType type, const std::string& message,
                          const std::optional<std::string>& topic) {
  return this->add(type, message, topic, nullptr);
}

LogEntry* LogEntries::add(LogEntryType type, const std::string& message,
                          const std::optional<std::string>& topic,
                          const std::exception* exception) {
  LogEntry* logEntry = new (jrt::kBoard) LogEntry(type, message, exception, topic);

  entries->add(logEntry);

  // Raise the event
  for (LogEntries_LogEntryAddedListener* listener : *listeners) {
    listener->logEntryAdded(logEntry);
  }

  return logEntry;
}

void LogEntries::addLogEntryAddedListener(LogEntries_LogEntryAddedListener* listener) {
  listeners->add(listener);
}

void LogEntries::removeLogEntryAddedListener(LogEntries_LogEntryAddedListener* listener) {
  listeners->remove(listener);
}

}  // namespace fr
