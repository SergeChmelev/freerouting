#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Basic parameters of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsBoard : public virtual jrt::Object {
 public:
  BoardStatisticsRectangle* boundingBox = nullptr;
  BoardStatisticsRectangle* size = nullptr;
  std::optional<float> areaCm2;
};

}  // namespace fr
