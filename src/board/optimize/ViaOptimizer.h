#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains functions for optimizing and improving via locations. */
class ViaOptimizer final : public virtual jrt::Object {
 private:
  ViaOptimizer() {}

 public:
  /**
   * Optimizes the location of a via connected to at most 2 traces according to the trace costs
   * on the layers of the connected traces. If traceCosts == null, the horizontal and vertical
   * trace costs will be set to 1. Returns false, if the via was not changed.
   */
  static bool optViaLocation(RoutingBoard* board, Via* via,
                             jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                             int32_t tracePullTightAccuracy, int32_t maxRecursionDepth);

  /**
   * Same as above, within one fixpoint loop of the caller: history holds the locations the vias
   * have taken in that loop, and a via between two traces that is proven to cycle is not moved
   * back again. (Java package-private)
   */
  static bool optViaLocation(RoutingBoard* board, Via* via,
                             jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                             int32_t tracePullTightAccuracy, int32_t maxRecursionDepth,
                             ViaMoveHistory* history);

 private:
  /** Optimisations for vias with only 1 connected Trace (Plane or Fanout Vias). */
  static bool optPlaneOrFanoutVia(RoutingBoard* board, Via* via, int32_t tracePullTightAccuracy,
                                  int32_t maxRecursionDepth);

  /**
   * Tries to move the via into the direction of toLocation as far as possible. Returns the new
   * location of the via, or null, if no move was possible.
   */
  static Point* repositionVia(RoutingBoard* board, Via* via, IntPoint* toLocation,
                              int32_t traceHalfWidth, int32_t traceLayer, int32_t traceClClass);

  static bool repositionVia(RoutingBoard* board, Via* via, IntPoint* toLocation,
                            int32_t traceHalfWidth1, int32_t traceLayer1, int32_t traceClClass1,
                            IntPoint* connectLocation, int32_t traceHalfWidth2,
                            int32_t traceLayer2, int32_t traceClClass2);

  /**
   * Tries to reposition the via to a better location according to the trace costs. Returns
   * null, if no better location was found.
   */
  static Point* repositionVia(RoutingBoard* board, Via* via, int32_t firstTraceHalfWidth,
                              int32_t firstTraceClClass, int32_t firstTraceLayer,
                              AutorouteControl_ExpansionCostFactor* firstTraceCosts,
                              Point* firstTraceFromCorner, int32_t secondTraceHalfWidth,
                              int32_t secondTraceClClass, int32_t secondTraceLayer,
                              AutorouteControl_ExpansionCostFactor* secondTraceCosts,
                              Point* secondTraceFromCorner);

  /**
   * Checks if two points are within the specified tolerance distance (Manhattan distance,
   * matching DrillItem.getNormalContacts()).
   */
  static bool isWithinTolerance(Point* p1, Point* p2, int32_t tolerance);
};

}  // namespace fr
