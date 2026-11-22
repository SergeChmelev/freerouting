#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The different states a routing job can be in. */
enum class RoutingJobState : int32_t {
  INVALID,         // The job is in an invalid state
  QUEUED,          // The job is waiting to be processed
  READY_TO_START,  // The job is ready to start
  RUNNING,         // The job is currently being processed
  PAUSED,          // The job is paused and can be resumed
  COMPLETED,       // The job has been completed successfully
  TIMED_OUT,       // The job has been timed out
  STOPPING,        // The job is in the process of being stopped
  CANCELLED,       // The job has been cancelled by the user
  TERMINATED       // The job has been terminated due to an error
};

inline std::string RoutingJobState_name(RoutingJobState e) {
  switch (e) {
    case RoutingJobState::INVALID: return "INVALID";
    case RoutingJobState::QUEUED: return "QUEUED";
    case RoutingJobState::READY_TO_START: return "READY_TO_START";
    case RoutingJobState::RUNNING: return "RUNNING";
    case RoutingJobState::PAUSED: return "PAUSED";
    case RoutingJobState::COMPLETED: return "COMPLETED";
    case RoutingJobState::TIMED_OUT: return "TIMED_OUT";
    case RoutingJobState::STOPPING: return "STOPPING";
    case RoutingJobState::CANCELLED: return "CANCELLED";
    case RoutingJobState::TERMINATED: return "TERMINATED";
  }
  return "";
}
inline std::string RoutingJobState_toString(RoutingJobState e) { return RoutingJobState_name(e); }
inline jrt::Array<RoutingJobState>* RoutingJobState_values() {
  return jrt::Array<RoutingJobState>::of(
      {RoutingJobState::INVALID, RoutingJobState::QUEUED, RoutingJobState::READY_TO_START,
       RoutingJobState::RUNNING, RoutingJobState::PAUSED, RoutingJobState::COMPLETED,
       RoutingJobState::TIMED_OUT, RoutingJobState::STOPPING, RoutingJobState::CANCELLED,
       RoutingJobState::TERMINATED});
}
inline RoutingJobState RoutingJobState_valueOf(const std::string& name) {
  auto* values = RoutingJobState_values();
  for (RoutingJobState v : *values) {
    if (RoutingJobState_name(v) == name) return v;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.core.RoutingJobState." + name);
}

/** Returns true if this state represents a final terminal state. */
inline bool RoutingJobState_isTerminal(RoutingJobState self) {
  return self == RoutingJobState::COMPLETED || self == RoutingJobState::CANCELLED ||
         self == RoutingJobState::TIMED_OUT || self == RoutingJobState::TERMINATED ||
         self == RoutingJobState::INVALID;
}

}  // namespace fr
