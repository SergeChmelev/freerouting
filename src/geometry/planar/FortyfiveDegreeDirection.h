#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Enum for the eight 45-degree direction starting from right in counterclocksense to down45. */
enum class FortyfiveDegreeDirection : int32_t { RIGHT, RIGHT45, UP, UP45, LEFT, LEFT45, DOWN, DOWN45 };

inline std::string FortyfiveDegreeDirection_name(FortyfiveDegreeDirection e) {
  switch (e) {
    case FortyfiveDegreeDirection::RIGHT: return "RIGHT";
    case FortyfiveDegreeDirection::RIGHT45: return "RIGHT45";
    case FortyfiveDegreeDirection::UP: return "UP";
    case FortyfiveDegreeDirection::UP45: return "UP45";
    case FortyfiveDegreeDirection::LEFT: return "LEFT";
    case FortyfiveDegreeDirection::LEFT45: return "LEFT45";
    case FortyfiveDegreeDirection::DOWN: return "DOWN";
    case FortyfiveDegreeDirection::DOWN45: return "DOWN45";
  }
  return "";
}

inline std::string FortyfiveDegreeDirection_toString(FortyfiveDegreeDirection e) {
  return FortyfiveDegreeDirection_name(e);
}

inline jrt::Array<FortyfiveDegreeDirection>* FortyfiveDegreeDirection_values() {
  auto* a = new (jrt::kGeom) jrt::Array<FortyfiveDegreeDirection>(8);
  for (int32_t i = 0; i < 8; ++i) (*a)[i] = static_cast<FortyfiveDegreeDirection>(i);
  return a;
}

inline FortyfiveDegreeDirection FortyfiveDegreeDirection_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 8; ++i) {
    auto e = static_cast<FortyfiveDegreeDirection>(i);
    if (FortyfiveDegreeDirection_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant FortyfiveDegreeDirection." + s);
}

/** Java constant-specific method getDirection(): the IntDirection of this 45-degree direction. */
IntDirection* FortyfiveDegreeDirection_getDirection(FortyfiveDegreeDirection self);

}  // namespace fr
