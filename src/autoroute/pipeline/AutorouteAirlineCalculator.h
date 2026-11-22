#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Calculates visual and diagnostic airline geometry for autoroute connections. */
class AutorouteAirlineCalculator : public virtual jrt::Object {
 private:
  AutorouteAirlineCalculator() = default;

 public:
  static FloatLine* calculateAirline(jrt::Collection<Item*>* fromItems, jrt::Collection<Item*>* toItems);
  static FloatPoint* nearestPointOnTrace(PolylineTrace* trace, FloatPoint* point);
  /**
   * Finds the closest points between two traces. Returns an array with two FloatPoints:
   * [point_on_first_trace, point_on_second_trace].
   */
  static jrt::Array<FloatPoint*>* findClosestPointsBetweenTraces(
      PolylineTrace* firstTrace, PolylineTrace* secondTrace);
  /**
   * Calculates the shortest reference-point distance for an item and its incomplete connections.
   * Returns Double::MAX_VALUE if no connections exist.
   */
  static double calculateItemDistance(Item* item);

 private:
  static double calculateMinDistance(jrt::Collection<Item*>* fromItems, jrt::Collection<Item*>* toItems);
  static FloatPoint* getItemReferencePoint(Item* item);
};

}  // namespace fr
