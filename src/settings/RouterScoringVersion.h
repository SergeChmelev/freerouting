#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Selects the router-board score formula independently from optimizer scoring. */
enum class RouterScoringVersion : int32_t {
  /** Existing combined score retained for compatibility. */
  V1_LEGACY,
  /** Completion plus discrete and continuous clearance penalties. */
  V2_CONTINUOUS
};

inline std::string RouterScoringVersion_name(RouterScoringVersion e) {
  switch (e) {
    case RouterScoringVersion::V1_LEGACY: return "V1_LEGACY";
    case RouterScoringVersion::V2_CONTINUOUS: return "V2_CONTINUOUS";
  }
  return "";
}
inline std::string RouterScoringVersion_toString(RouterScoringVersion e) {
  return RouterScoringVersion_name(e);
}
inline jrt::Array<RouterScoringVersion>* RouterScoringVersion_values() {
  return jrt::Array<RouterScoringVersion>::of(
      {RouterScoringVersion::V1_LEGACY, RouterScoringVersion::V2_CONTINUOUS});
}
inline RouterScoringVersion RouterScoringVersion_valueOf(const std::string& name) {
  if (name == "V1_LEGACY") return RouterScoringVersion::V1_LEGACY;
  if (name == "V2_CONTINUOUS") return RouterScoringVersion::V2_CONTINUOUS;
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.settings.RouterScoringVersion." + name);
}

}  // namespace fr
