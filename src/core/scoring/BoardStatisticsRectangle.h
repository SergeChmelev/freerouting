#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * 2D rectangle representation for board bounding box and dimensions in scoring statistics
 * (replaces java.awt.geom.Rectangle2D.Float).
 */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatisticsRectangle : public virtual jrt::Object {
 public:
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  /** Default constructor for deserialization. */
  BoardStatisticsRectangle() {}

  /** Constructs a rectangle with the specified coordinates and dimensions. */
  BoardStatisticsRectangle(float x, float y, float width, float height)
      : x(x), y(y), width(width), height(height) {}
};

}  // namespace fr
