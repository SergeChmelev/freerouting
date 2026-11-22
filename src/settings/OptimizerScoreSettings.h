#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Nullable settings for the optimizer-board score. */
// PORT NOTE: Serializable/Cloneable and gson annotations dropped (JSON names in comments).
class OptimizerScoreSettings : public virtual jrt::Object {
 public:
  /** "version" */
  std::optional<OptimizerScoringVersion> version;
  /** "excess_wire_length_weight" */
  std::optional<float> excessWireLengthWeight;
  /** "excess_via_weight" */
  std::optional<float> excessViaWeight;
  /** "excess_bend_weight" */
  std::optional<float> excessBendWeight;
  /** "length_floor" */
  std::optional<float> lengthFloor;
  /** "difficulty_scale_floor" */
  std::optional<float> difficultyScaleFloor;

  /** Java clone(): shallow copy. */
  virtual OptimizerScoreSettings* clone();

  /** PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). */
  int32_t copyNewValuesFrom(OptimizerScoreSettings* source);
};

}  // namespace fr
