#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes a routing connection ending at the next fork or terminal item. */
class Connection final : public virtual jrt::Object {
 private:
  static constexpr double DETOUR_ADD = 100;
  static constexpr double DETOUR_ITEM_COST = 0.1;

 public:
  /** The start point of the connection. */
  Point* startPoint;

  /** The layer of the start point. */
  int32_t startLayer;

  /** The end point of the connection. */
  Point* endPoint;

  /** The layer of the end point. */
  int32_t endLayer;

  /** The items of the connection. */
  jrt::Set<Item*>* itemList;

 private:
  /** Creates a new instance of Connection. */
  Connection(Point* startPoint, int32_t startLayer, Point* endPoint, int32_t endLayer,
             jrt::Set<Item*>* itemList)
      : startPoint(startPoint),
        startLayer(startLayer),
        endPoint(endPoint),
        endLayer(endLayer),
        itemList(itemList) {}

 public:
  /** Gets the connection this item belongs to. A connection ends at the next fork or terminal
   * item. Returns null, if item is not a route item, or if it is a via belonging to more than 1
   * connection. */
  static Connection* get(Item* item);

  /** Returns the cumulative length of the traces in this connection. */
  double traceLength();

  /** Returns an estimation of the detour of this connection compared to the airline distance. */
  double getDetour();
};

}  // namespace fr
