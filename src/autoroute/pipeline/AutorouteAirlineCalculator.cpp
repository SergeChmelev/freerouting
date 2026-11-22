#include "autoroute/pipeline/AutorouteAirlineCalculator.h"

#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"

namespace fr {

FloatLine* AutorouteAirlineCalculator::calculateAirline(jrt::Collection<Item*>* fromItems,
                                                        jrt::Collection<Item*>* toItems) {
  FloatPoint* fromCorner = nullptr;
  FloatPoint* toCorner = nullptr;
  double minDistance = jrt::Double::MAX_VALUE;
  for (Item* currentFromItem : *fromItems) {
    if (!(jrt::fast_cast<DrillItem>(currentFromItem) != nullptr)) {
      continue;
    }
    FloatPoint* currentFromCorner = jrt::fast_cast<DrillItem>(currentFromItem)->getCenter()->toFloat();

    for (Item* currentToItem : *toItems) {
      if (!(jrt::fast_cast<DrillItem>(currentToItem) != nullptr)) {
        continue;
      }
      FloatPoint* currentToCorner = jrt::fast_cast<DrillItem>(currentToItem)->getCenter()->toFloat();
      double currentDistance = currentFromCorner->distanceSquare(currentToCorner);
      if (currentDistance < minDistance) {
        minDistance = currentDistance;
        fromCorner = currentFromCorner;
        toCorner = currentToCorner;
      }
    }
  }
  return new FloatLine(fromCorner, toCorner);
}

FloatPoint* AutorouteAirlineCalculator::nearestPointOnTrace(PolylineTrace* trace, FloatPoint* point) {
  double minDistance = jrt::Double::MAX_VALUE;
  FloatPoint* nearestPoint = nullptr;

  FloatPoint* firstCorner = trace->firstCorner()->toFloat();
  FloatPoint* lastCorner = trace->lastCorner()->toFloat();

  double distanceToFirst = point->distance(firstCorner);
  double distanceToLast = point->distance(lastCorner);

  if (distanceToFirst < minDistance) {
    minDistance = distanceToFirst;
    nearestPoint = firstCorner;
  }

  if (distanceToLast < minDistance) {
    minDistance = distanceToLast;
    nearestPoint = lastCorner;
  }

  for (int32_t i = 0; i < trace->cornerCount() - 1; i++) {
    FloatPoint* segmentStart = trace->polyline()->cornerApprox(i);
    FloatPoint* segmentEnd = trace->polyline()->cornerApprox(i + 1);
    FloatLine* segment = new FloatLine(segmentStart, segmentEnd);

    FloatPoint* projection = segment->perpendicularProjection(point);
    if (projection->isContainedInBox(segmentStart, segmentEnd, 0.01)) {
      double distance = point->distance(projection);
      if (distance < minDistance) {
        minDistance = distance;
        nearestPoint = projection;
      }
    }
  }

  return nearestPoint;
}

jrt::Array<FloatPoint*>* AutorouteAirlineCalculator::findClosestPointsBetweenTraces(
    PolylineTrace* firstTrace, PolylineTrace* secondTrace) {
  double minDistance = jrt::Double::MAX_VALUE;
  auto* result = new jrt::Array<FloatPoint*>(2);

  FloatPoint* firstTraceStart = firstTrace->firstCorner()->toFloat();
  FloatPoint* firstTraceEnd = firstTrace->lastCorner()->toFloat();
  FloatPoint* secondTraceStart = secondTrace->firstCorner()->toFloat();
  FloatPoint* secondTraceEnd = secondTrace->lastCorner()->toFloat();

  double distance = firstTraceStart->distance(secondTraceStart);
  if (distance < minDistance) {
    minDistance = distance;
    (*result)[0] = firstTraceStart;
    (*result)[1] = secondTraceStart;
  }

  distance = firstTraceStart->distance(secondTraceEnd);
  if (distance < minDistance) {
    minDistance = distance;
    (*result)[0] = firstTraceStart;
    (*result)[1] = secondTraceEnd;
  }

  distance = firstTraceEnd->distance(secondTraceStart);
  if (distance < minDistance) {
    minDistance = distance;
    (*result)[0] = firstTraceEnd;
    (*result)[1] = secondTraceStart;
  }

  distance = firstTraceEnd->distance(secondTraceEnd);
  if (distance < minDistance) {
    minDistance = distance;
    (*result)[0] = firstTraceEnd;
    (*result)[1] = secondTraceEnd;
  }

  for (int32_t i = 0; i < firstTrace->cornerCount() - 1; i++) {
    FloatPoint* firstSegmentStart = firstTrace->polyline()->cornerApprox(i);
    FloatPoint* firstSegmentEnd = firstTrace->polyline()->cornerApprox(i + 1);
    FloatLine* firstSegment = new FloatLine(firstSegmentStart, firstSegmentEnd);

    for (int32_t j = 0; j < secondTrace->cornerCount() - 1; j++) {
      FloatPoint* secondSegmentStart = secondTrace->polyline()->cornerApprox(j);
      FloatPoint* secondSegmentEnd = secondTrace->polyline()->cornerApprox(j + 1);
      FloatLine* secondSegment = new FloatLine(secondSegmentStart, secondSegmentEnd);

      FloatPoint* pointOnFirst = firstSegment->nearestSegmentPoint(secondSegmentStart);
      FloatPoint* pointOnSecond = secondSegment->perpendicularProjection(pointOnFirst);

      if (!pointOnSecond->isContainedInBox(secondSegmentStart, secondSegmentEnd, 0.01)) {
        double distToStart = pointOnFirst->distance(secondSegmentStart);
        double distToEnd = pointOnFirst->distance(secondSegmentEnd);
        pointOnSecond = distToStart < distToEnd ? secondSegmentStart : secondSegmentEnd;
      }

      pointOnFirst = firstSegment->nearestSegmentPoint(pointOnSecond);

      distance = pointOnFirst->distance(pointOnSecond);
      if (distance < minDistance) {
        minDistance = distance;
        (*result)[0] = pointOnFirst;
        (*result)[1] = pointOnSecond;
      }
    }
  }

  return result;
}

double AutorouteAirlineCalculator::calculateItemDistance(Item* item) {
  if (item->netCount() == 0) {
    return jrt::Double::MAX_VALUE;
  }

  int32_t netNumber = item->getNetNumber(0);
  jrt::Set<Item*>* unconnectedSet = item->getUnconnectedSet(netNumber);
  jrt::Set<Item*>* connectedSet = item->getConnectedSet(netNumber);

  if (unconnectedSet->isEmpty()) {
    return 0;
  }

  return calculateMinDistance(
      connectedSet->isEmpty() ? static_cast<jrt::Collection<Item*>*>(new jrt::HashSet<Item*>({item}))
                              : static_cast<jrt::Collection<Item*>*>(connectedSet),
      unconnectedSet);
}

double AutorouteAirlineCalculator::calculateMinDistance(jrt::Collection<Item*>* fromItems,
                                                        jrt::Collection<Item*>* toItems) {
  double minDistance = jrt::Double::MAX_VALUE;

  for (Item* fromItem : *fromItems) {
    FloatPoint* fromPoint = getItemReferencePoint(fromItem);
    if (fromPoint == nullptr) {
      continue;
    }

    for (Item* toItem : *toItems) {
      FloatPoint* toPoint = getItemReferencePoint(toItem);
      if (toPoint == nullptr) {
        continue;
      }

      double distance = fromPoint->distance(toPoint);
      if (distance < minDistance) {
        minDistance = distance;
      }
    }
  }

  return minDistance;
}

FloatPoint* AutorouteAirlineCalculator::getItemReferencePoint(Item* item) {
  if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
    return drillItem->getCenter()->toFloat();
  } else if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
    FloatPoint* first = trace->firstCorner()->toFloat();
    FloatPoint* last = trace->lastCorner()->toFloat();
    return new FloatPoint((first->x + last->x) / 2, (first->y + last->y) / 2);
  }
  return nullptr;
}

}  // namespace fr
