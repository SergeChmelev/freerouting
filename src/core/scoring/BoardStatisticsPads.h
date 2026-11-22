#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the pads of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsPads : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
};

}  // namespace fr
