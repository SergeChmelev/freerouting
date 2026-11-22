#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the bends (of traces) of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsBends : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
  std::optional<int32_t> ninetyDegreeCount;
  std::optional<int32_t> fortyFiveDegreeCount;
  std::optional<int32_t> otherAngleCount;
};

}  // namespace fr
