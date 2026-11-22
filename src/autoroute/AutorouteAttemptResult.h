#pragma once

#include "autoroute/AutorouteAttemptState.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Represents the outcome of an autoroute attempt, including its state and detail message. */
class AutorouteAttemptResult : public virtual jrt::Object {
 public:
  AutorouteAttemptState state;
  std::string details;

  /** Constructs an AutorouteAttemptResult with the specified state and empty details. */
  explicit AutorouteAttemptResult(AutorouteAttemptState state) : state(state), details("") {}

  /** Constructs an AutorouteAttemptResult with the specified state and detail message. */
  AutorouteAttemptResult(AutorouteAttemptState state, const std::string& details)
      : state(state), details(details) {}

  std::string toString() override {
    return jrt::toUpperCase(AutorouteAttemptState_toString(this->state)) + ": " + this->details;
  }
};

}  // namespace fr
