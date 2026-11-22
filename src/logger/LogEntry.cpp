// Port of app.freerouting.logger.LogEntry
#include "logger/LogEntry.h"

namespace fr {

LogEntry::LogEntry(LogEntryType type, const std::string& message, const std::exception* exception,
                   const std::optional<std::string>& topic)
    : timestamp(jrt::System::currentTimeMillis()),
      type(type),
      message(message),
      exception(exception),
      topic(topic) {}

std::string LogEntry::toString() {
  return jrt::format("%-7s", jrt::toUpperCase(LogEntryType_toString(this->type)).c_str()) + " " +
         this->message;
}

}  // namespace fr
