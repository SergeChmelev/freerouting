// Port of app.freerouting.datastructures.TimeLimit
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Class used to cancel a performance critical algorithm after a time limit is exceeded. */
class TimeLimit : public virtual jrt::Object {
 public:
  /** Creates a new instance with a time limit of milliSeconds milliseconds. */
  explicit TimeLimit(int32_t milliSeconds)
      : timeStamp(jrt::System::currentTimeMillis()), timeLimit(milliSeconds) {}

  /** Returns true, if the time limit provided in the constructor of this class is exceeded. */
  bool limitExceeded() {
    int64_t currentTime = jrt::System::currentTimeMillis();
    return currentTime - this->timeStamp > this->timeLimit;
  }

  /** Multiplies this TimeLimit by factor. */
  void multiply(double factor) {
    if (factor <= 0) {
      return;
    }
    double newLimit = factor * this->timeLimit;
    newLimit = jrt::Math::min(newLimit, static_cast<double>(jrt::Integer::MAX_VALUE));
    this->timeLimit = jrt::d2i(newLimit);
  }

 private:
  const int64_t timeStamp;
  int32_t timeLimit;
};

}  // namespace fr
