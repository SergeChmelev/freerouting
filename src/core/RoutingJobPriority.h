#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Priority assigned to a routing job. */
enum class RoutingJobPriority : int32_t {
  LOWEST,       // 0.0
  LOW,          // 2.0
  BELOWNORMAL,  // 4.0
  NORMAL,       // 5.0
  ABOVENORMAL,  // 6.0
  HIGH,         // 8.0
  HIGHEST       // 10.0
};

inline std::string RoutingJobPriority_name(RoutingJobPriority e) {
  switch (e) {
    case RoutingJobPriority::LOWEST: return "LOWEST";
    case RoutingJobPriority::LOW: return "LOW";
    case RoutingJobPriority::BELOWNORMAL: return "BELOWNORMAL";
    case RoutingJobPriority::NORMAL: return "NORMAL";
    case RoutingJobPriority::ABOVENORMAL: return "ABOVENORMAL";
    case RoutingJobPriority::HIGH: return "HIGH";
    case RoutingJobPriority::HIGHEST: return "HIGHEST";
  }
  return "";
}
inline std::string RoutingJobPriority_toString(RoutingJobPriority e) {
  return RoutingJobPriority_name(e);
}
inline jrt::Array<RoutingJobPriority>* RoutingJobPriority_values() {
  return jrt::Array<RoutingJobPriority>::of(
      {RoutingJobPriority::LOWEST, RoutingJobPriority::LOW, RoutingJobPriority::BELOWNORMAL,
       RoutingJobPriority::NORMAL, RoutingJobPriority::ABOVENORMAL, RoutingJobPriority::HIGH,
       RoutingJobPriority::HIGHEST});
}
inline RoutingJobPriority RoutingJobPriority_valueOf(const std::string& name) {
  auto* values = RoutingJobPriority_values();
  for (RoutingJobPriority v : *values) {
    if (RoutingJobPriority_name(v) == name) return v;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.core.RoutingJobPriority." + name);
}

/** Returns the numeric priority value (constructor value clamped to [0, 10]). */
inline float RoutingJobPriority_getValue(RoutingJobPriority self) {
  float value = 0.0f;
  switch (self) {
    case RoutingJobPriority::LOWEST: value = 0.0f; break;
    case RoutingJobPriority::LOW: value = 2.0f; break;
    case RoutingJobPriority::BELOWNORMAL: value = 4.0f; break;
    case RoutingJobPriority::NORMAL: value = 5.0f; break;
    case RoutingJobPriority::ABOVENORMAL: value = 6.0f; break;
    case RoutingJobPriority::HIGH: value = 8.0f; break;
    case RoutingJobPriority::HIGHEST: value = 10.0f; break;
  }
  return jrt::Math::max(0.0f, jrt::Math::min(value, 10.0f));
}

}  // namespace fr
