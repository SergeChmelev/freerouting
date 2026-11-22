#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Represents an incomplete connection (airline) between two items on the board. Each airline is
 * associated with a net and connects two specific items (fromItem and toItem) at specific
 * locations (fromCorner and toCorner).
 */
class AirLine : public virtual jrt::Object {
 public:
  /** The net this airline belongs to. */
  Net* net;

  /** The item where the airline starts. */
  Item* fromItem;

  /** The exact starting coordinate of the airline. */
  FloatPoint* fromCorner;

  /** The item where the airline ends. */
  Item* toItem;

  /** The exact ending coordinate of the airline. */
  FloatPoint* toCorner;

  /** Creates an airline between two net items at the given corner coordinates. */
  AirLine(Net* net, Item* fromItem, FloatPoint* fromCorner, Item* toItem, FloatPoint* toCorner)
      : net(net), fromItem(fromItem), fromCorner(fromCorner), toItem(toItem), toCorner(toCorner) {}

  /** Comparable<AirLine>: compares by net name. */
  virtual int32_t compareTo(AirLine* other);

  std::string toString() override;
};

}  // namespace fr
