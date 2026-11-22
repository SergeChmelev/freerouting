#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Board-only complexity and difficulty values used by V2 scoring. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsDifficulty : public virtual jrt::Object {
 public:
  std::optional<int32_t> pinCount;
  std::optional<int32_t> signalLayerCount;
  std::optional<int32_t> complexityC;
  std::optional<float> difficultyD;
  std::optional<float> boardAreaCm2;
};

}  // namespace fr
