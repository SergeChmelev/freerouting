#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the components of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsItems : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
  std::optional<int32_t> traceCount;
  std::optional<int32_t> viaCount;
  std::optional<int32_t> conductionAreaCount;
  std::optional<int32_t> drillItemCount;
  std::optional<int32_t> pinCount;
  std::optional<int32_t> componentOutlineCount;
  std::optional<int32_t> otherCount;
};

}  // namespace fr
