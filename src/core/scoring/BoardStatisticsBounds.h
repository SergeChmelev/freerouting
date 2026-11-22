#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Board-only lower bounds used by the V2 optimizer score. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsBounds : public virtual jrt::Object {
 public:
  std::optional<float> minTraceLengthMm;
  std::optional<int32_t> minViaCount;
  std::optional<int32_t> minBendCount;
};

}  // namespace fr
