#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The different stages a routing job can be in. */
enum class RoutingStage : int32_t {
  IDLE,         // The job hasn't started yet
  ROUTING,      // The board is currently being processed by the auto-router
  OPTIMIZATION  // The board is routed, and it is currently being optimized
};

inline std::string RoutingStage_name(RoutingStage e) {
  switch (e) {
    case RoutingStage::IDLE: return "IDLE";
    case RoutingStage::ROUTING: return "ROUTING";
    case RoutingStage::OPTIMIZATION: return "OPTIMIZATION";
  }
  return "";
}
inline std::string RoutingStage_toString(RoutingStage e) { return RoutingStage_name(e); }
inline jrt::Array<RoutingStage>* RoutingStage_values() {
  return jrt::Array<RoutingStage>::of(
      {RoutingStage::IDLE, RoutingStage::ROUTING, RoutingStage::OPTIMIZATION});
}
inline RoutingStage RoutingStage_valueOf(const std::string& name) {
  if (name == "IDLE") return RoutingStage::IDLE;
  if (name == "ROUTING") return RoutingStage::ROUTING;
  if (name == "OPTIMIZATION") return RoutingStage::OPTIMIZATION;
  throw jrt::IllegalArgumentException("No enum constant app.freerouting.core.RoutingStage." + name);
}

}  // namespace fr
