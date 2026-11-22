#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Selects the optimizer-board score formula independently from router scoring. */
enum class OptimizerScoringVersion : int32_t {
  /** Existing combined score retained for compatibility. */
  V1_LEGACY,
  /** Excess length, vias, and bends relative to stored lower bounds. */
  V2_LOWER_BOUND
};

inline std::string OptimizerScoringVersion_name(OptimizerScoringVersion e) {
  switch (e) {
    case OptimizerScoringVersion::V1_LEGACY: return "V1_LEGACY";
    case OptimizerScoringVersion::V2_LOWER_BOUND: return "V2_LOWER_BOUND";
  }
  return "";
}
inline std::string OptimizerScoringVersion_toString(OptimizerScoringVersion e) {
  return OptimizerScoringVersion_name(e);
}
inline jrt::Array<OptimizerScoringVersion>* OptimizerScoringVersion_values() {
  return jrt::Array<OptimizerScoringVersion>::of(
      {OptimizerScoringVersion::V1_LEGACY, OptimizerScoringVersion::V2_LOWER_BOUND});
}
inline OptimizerScoringVersion OptimizerScoringVersion_valueOf(const std::string& name) {
  if (name == "V1_LEGACY") return OptimizerScoringVersion::V1_LEGACY;
  if (name == "V2_LOWER_BOUND") return OptimizerScoringVersion::V2_LOWER_BOUND;
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.settings.OptimizerScoringVersion." + name);
}

}  // namespace fr
