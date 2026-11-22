#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Represents the execution state of an autorouting task. */
enum class TaskState : int32_t { IDLE, STARTED, RUNNING, FINISHED, CANCELLED, TIMED_OUT };

inline std::string TaskState_name(TaskState e) {
  switch (e) {
    case TaskState::IDLE: return "IDLE";
    case TaskState::STARTED: return "STARTED";
    case TaskState::RUNNING: return "RUNNING";
    case TaskState::FINISHED: return "FINISHED";
    case TaskState::CANCELLED: return "CANCELLED";
    case TaskState::TIMED_OUT: return "TIMED_OUT";
  }
  return "";
}
inline std::string TaskState_toString(TaskState e) { return TaskState_name(e); }
inline jrt::Array<TaskState>* TaskState_values() {
  auto* a = new (jrt::kOp) jrt::Array<TaskState>(6);
  for (int32_t i = 0; i < 6; ++i) (*a)[i] = static_cast<TaskState>(i);
  return a;
}
inline TaskState TaskState_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 6; ++i) {
    auto e = static_cast<TaskState>(i);
    if (TaskState_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant TaskState." + s);
}

}  // namespace fr
