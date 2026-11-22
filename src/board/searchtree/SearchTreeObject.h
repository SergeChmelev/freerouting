#pragma once

#include "fwd.h"
#include "jrt/jrt.h"
#include "datastructures/ShapeTree.h"

namespace fr {

/**
 * Common ShapeSearchTree functionality for board.Items and autoroute.ExpansionRooms.
 *
 * PORT NOTE: implementers that override only isTraceObstacle(int) must add
 * `using SearchTreeObject::isTraceObstacle;` (C++ name hiding would otherwise hide the 2-arg
 * default overload when called through the implementing class).
 */
class SearchTreeObject : public virtual ShapeTree_Storable {
 public:
  /** Returns true if this object is an obstacle to objects containing the net number netNumber. */
  virtual bool isObstacle(int32_t netNumber) = 0;

  /** Returns true if this object is an obstacle to traces containing the net number netNumber. */
  virtual bool isTraceObstacle(int32_t netNumber) = 0;

  /**
   * Returns true if the tree shape with index shapeIndex of this object is an obstacle to traces
   * containing the net number netNumber. By default the same for all shapes of the object.
   */
  virtual bool isTraceObstacle(int32_t netNumber, int32_t shapeIndex) {
    (void)shapeIndex;
    return isTraceObstacle(netNumber);
  }

  /** Returns for this object the layer of the shape with index index. */
  virtual int32_t shapeLayer(int32_t index) = 0;

  /** Returns a unique identification number of this object. */
  virtual int32_t getId() = 0;
};

}  // namespace fr
