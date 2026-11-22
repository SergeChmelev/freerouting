// Port of app.freerouting.logger.LogEntryType
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Severity/type of an in-memory log entry. */
enum class LogEntryType : int32_t { Error, Warning, Info, Trace, Debug };

inline std::string LogEntryType_name(LogEntryType e) {
  switch (e) {
    case LogEntryType::Error:
      return "Error";
    case LogEntryType::Warning:
      return "Warning";
    case LogEntryType::Info:
      return "Info";
    case LogEntryType::Trace:
      return "Trace";
    case LogEntryType::Debug:
      return "Debug";
  }
  return "";
}

inline std::string LogEntryType_toString(LogEntryType e) { return LogEntryType_name(e); }

inline jrt::Array<LogEntryType>* LogEntryType_values() {
  return jrt::Array<LogEntryType>::of({LogEntryType::Error, LogEntryType::Warning,
                                       LogEntryType::Info, LogEntryType::Trace,
                                       LogEntryType::Debug});
}

inline LogEntryType LogEntryType_valueOf(const std::string& name) {
  for (LogEntryType e : *LogEntryType_values()) {
    if (LogEntryType_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant LogEntryType." + name);
}

}  // namespace fr
