#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The possible results of auto-routing a connection. */
enum class AutorouteAttemptState : int32_t {
  UNKNOWN,             // Unknown result
  SKIPPED,             // Item was skipped
  NO_UNCONNECTED_NETS, // Item has no unconnected nets
  CONNECTED_TO_PLANE,  // Item is connected to a conduction plane
  ALREADY_CONNECTED,   // Item is already connected
  NO_CONNECTIONS,      // The item has no connections to nets
  ROUTED,              // Item was successfully routed
  FAILED,              // Routing failed
  INSERT_ERROR         // Error inserting item
};

inline std::string AutorouteAttemptState_name(AutorouteAttemptState e) {
  switch (e) {
    case AutorouteAttemptState::UNKNOWN: return "UNKNOWN";
    case AutorouteAttemptState::SKIPPED: return "SKIPPED";
    case AutorouteAttemptState::NO_UNCONNECTED_NETS: return "NO_UNCONNECTED_NETS";
    case AutorouteAttemptState::CONNECTED_TO_PLANE: return "CONNECTED_TO_PLANE";
    case AutorouteAttemptState::ALREADY_CONNECTED: return "ALREADY_CONNECTED";
    case AutorouteAttemptState::NO_CONNECTIONS: return "NO_CONNECTIONS";
    case AutorouteAttemptState::ROUTED: return "ROUTED";
    case AutorouteAttemptState::FAILED: return "FAILED";
    case AutorouteAttemptState::INSERT_ERROR: return "INSERT_ERROR";
  }
  return "";
}
inline std::string AutorouteAttemptState_toString(AutorouteAttemptState e) {
  return AutorouteAttemptState_name(e);
}
inline jrt::Array<AutorouteAttemptState>* AutorouteAttemptState_values() {
  auto* a = new jrt::Array<AutorouteAttemptState>(9);
  for (int32_t i = 0; i < 9; ++i) (*a)[i] = static_cast<AutorouteAttemptState>(i);
  return a;
}
inline AutorouteAttemptState AutorouteAttemptState_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 9; ++i) {
    auto e = static_cast<AutorouteAttemptState>(i);
    if (AutorouteAttemptState_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant AutorouteAttemptState." + s);
}

}  // namespace fr
