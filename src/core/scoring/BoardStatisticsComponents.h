#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the components of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsComponents : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
};

}  // namespace fr
