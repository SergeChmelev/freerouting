#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the vias of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsVias : public virtual jrt::Object {
 public:
  // Total count of vias
  std::optional<int32_t> totalCount;
  // Through-hole vias are spanning all layers
  std::optional<int32_t> throughHoleCount;
  // Blind vias are connecting outer to inner layers
  std::optional<int32_t> blindCount;
  // Buried vias are connecting inner layers only
  std::optional<int32_t> buriedCount;
};

}  // namespace fr
