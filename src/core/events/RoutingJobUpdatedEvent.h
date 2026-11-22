#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Event emitted when a routing job changes. */
// PORT NOTE: java.util.EventObject is replaced by the `source` field + getSource().
class RoutingJobUpdatedEvent : public virtual jrt::Object {
 public:
  /** Creates an event for the changed routing job. */
  RoutingJobUpdatedEvent(jrt::Object* source, RoutingJob* job) : source(source), job(job) {
    if (source == nullptr) {
      throw jrt::IllegalArgumentException("null source");
    }
  }

  /** EventObject.getSource(): the object on which the event initially occurred. */
  jrt::Object* getSource() { return source; }

  /** Returns the affected routing job. */
  RoutingJob* getJob() { return job; }

 protected:
  jrt::Object* source;

 private:
  RoutingJob* job;
};

}  // namespace fr
