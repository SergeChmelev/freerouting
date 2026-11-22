#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Event emitted when board file details change. */
// PORT NOTE: java.util.EventObject is replaced by the `source` field + getSource().
class BoardFileDetailsUpdatedEvent : public virtual jrt::Object {
 public:
  /** Creates an event for the changed file details. */
  BoardFileDetailsUpdatedEvent(jrt::Object* source, BoardFileDetails* details)
      : source(source), details(details) {
    if (source == nullptr) {
      throw jrt::IllegalArgumentException("null source");
    }
  }

  /** EventObject.getSource(): the object on which the event initially occurred. */
  jrt::Object* getSource() { return source; }

  /** Returns the changed file details. */
  BoardFileDetails* getDetails() { return details; }

 protected:
  jrt::Object* source;

 private:
  BoardFileDetails* details;
};

}  // namespace fr
