#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Nullable settings for the router-board score. */
// PORT NOTE: Serializable/Cloneable and gson annotations dropped (JSON names in comments).
class RouterScoreSettings : public virtual jrt::Object {
 public:
  /** "version" */
  std::optional<RouterScoringVersion> version;
  /** "unrouted_connection_weight" */
  std::optional<float> unroutedConnectionWeight;
  /** "unrouted_free_fraction" */
  std::optional<float> unroutedFreeFraction;
  /** "unrouted_first_half_weight" */
  std::optional<float> unroutedFirstHalfWeight;
  /** "unrouted_second_half_weight" */
  std::optional<float> unroutedSecondHalfWeight;
  /** "clearance_violation_count_weight" */
  std::optional<float> clearanceViolationCountWeight;
  /** "clearance_violation_depth_weight" */
  std::optional<float> clearanceViolationDepthWeight;
  /** "clearance_violation_depth_scale" */
  std::optional<float> clearanceViolationDepthScale;

  /** Java clone(): shallow copy. */
  virtual RouterScoreSettings* clone();

  /** PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). */
  int32_t copyNewValuesFrom(RouterScoreSettings* source);
};

}  // namespace fr
