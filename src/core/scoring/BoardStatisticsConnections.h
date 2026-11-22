#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics describing board connections. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsConnections : public virtual jrt::Object {
 public:
  std::optional<int32_t> maximumCount;
  std::optional<int32_t> incompleteCount;
  /** Open connections with an end blocked by the design (e.g. pins on foreign fixed copper). */
  std::optional<int32_t> designBlockedCount;
};

}  // namespace fr
