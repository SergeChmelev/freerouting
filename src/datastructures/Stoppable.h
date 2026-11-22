// Port of app.freerouting.datastructures.Stoppable
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Interface for stoppable threads. */
class Stoppable : public virtual jrt::Object {
 public:
  /** Requests this thread to be stopped. */
  virtual void requestStop() = 0;

  /** Returns true, if this thread is requested to be stopped. */
  virtual bool isStopRequested() = 0;
};

}  // namespace fr
