#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Event emitted when a routing job receives a log entry. */
// PORT NOTE: java.util.EventObject is replaced by the `source` field + getSource().
class RoutingJobLogEntryAddedEvent : public virtual jrt::Object {
 public:
  /** Creates an event for a newly added log entry. */
  RoutingJobLogEntryAddedEvent(jrt::Object* source, RoutingJob* job, LogEntry* logEntry)
      : source(source), job(job), logEntry(logEntry) {
    if (source == nullptr) {
      throw jrt::IllegalArgumentException("null source");
    }
  }

  /** EventObject.getSource(): the object on which the event initially occurred. */
  jrt::Object* getSource() { return source; }

  /** Returns the affected routing job. */
  RoutingJob* getJob() { return job; }

  /** Returns the added log entry. */
  LogEntry* getLogEntry() { return logEntry; }

 protected:
  jrt::Object* source;

 private:
  RoutingJob* job;
  LogEntry* logEntry;
};

}  // namespace fr
