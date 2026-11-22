#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the layers of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsLayers : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
  std::optional<int32_t> signalCount;
};

}  // namespace fr
