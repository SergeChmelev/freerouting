#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the nets (unrouted connections) of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsNets : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
  std::optional<int32_t> classCount;
};

}  // namespace fr
