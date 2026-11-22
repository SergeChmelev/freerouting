#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Defines the stop state of a StoppableThread. */
enum class StopRequestState : int32_t {
  /** No stop is requested. */
  NONE,
  /** Only the auto-router is requested to stop. */
  AUTO_ROUTER_ONLY,
  /** The entire thread is requested to stop. */
  ALL
};

inline std::string StopRequestState_name(StopRequestState e) {
  switch (e) {
    case StopRequestState::NONE: return "NONE";
    case StopRequestState::AUTO_ROUTER_ONLY: return "AUTO_ROUTER_ONLY";
    case StopRequestState::ALL: return "ALL";
  }
  return "";
}
inline std::string StopRequestState_toString(StopRequestState e) {
  return StopRequestState_name(e);
}
inline jrt::Array<StopRequestState>* StopRequestState_values() {
  return jrt::Array<StopRequestState>::of(
      {StopRequestState::NONE, StopRequestState::AUTO_ROUTER_ONLY, StopRequestState::ALL});
}
inline StopRequestState StopRequestState_valueOf(const std::string& name) {
  if (name == "NONE") return StopRequestState::NONE;
  if (name == "AUTO_ROUTER_ONLY") return StopRequestState::AUTO_ROUTER_ONLY;
  if (name == "ALL") return StopRequestState::ALL;
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.core.StopRequestState." + name);
}

}  // namespace fr
