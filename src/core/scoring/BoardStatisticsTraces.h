#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the traces (routed connections) of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsTraces : public virtual jrt::Object {
 public:
  // The total number of traces.
  std::optional<int32_t> totalCount;
  // The total number of segments in all traces.
  std::optional<int32_t> totalSegmentCount;
  // The total length of all traces (raw board units).
  std::optional<float> totalLength;
  /** Total trace length normalised to millimetres (derived from totalLength). */
  std::optional<float> totalLengthMm;
  std::optional<float> totalWeightedLength;
  // The average length of the traces.
  std::optional<float> averageLength;
  // The total vertical length of all traces.
  std::optional<float> totalVerticalLength;
  // The total horizontal length of all traces.
  std::optional<float> totalHorizontalLength;
  // The total angled (non-horizontal and non-vertical) length of all traces.
  std::optional<float> totalAngledLength;
};

}  // namespace fr
