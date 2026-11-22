#include "board/optimize/ViaOptimizer.h"

#include "autoroute/maze/AutorouteControl.h"
#include "board/actions/DrillItemMover.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Via.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/optimize/ViaMoveHistory.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"

namespace fr {

bool ViaOptimizer::optViaLocation(RoutingBoard* board, Via* via,
                                  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                                  int32_t tracePullTightAccuracy, int32_t maxRecursionDepth) {
  return optViaLocation(board, via, traceCosts, tracePullTightAccuracy, maxRecursionDepth,
                        new ViaMoveHistory());
}

bool ViaOptimizer::optViaLocation(RoutingBoard* board, Via* via,
                                  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                                  int32_t tracePullTightAccuracy, int32_t maxRecursionDepth,
                                  ViaMoveHistory* history) {
  if (via->isShoveFixed()) {
    return false;
  }
  if (maxRecursionDepth <= 0) {
    FRLogger::debug("OptViaAlgo.opt_via_location: probably endless loop");
    return false;
  }
  jrt::Collection<Item*>* contacts = via->getNormalContacts();
  bool isPlaneOrFanoutVia = contacts->size() == 1;
  PolylineTrace* firstTrace = nullptr;
  PolylineTrace* secondTrace = nullptr;
  if (!isPlaneOrFanoutVia) {
    if (contacts->size() != 2) {
      return false;
    }
    jrt::Iterator<Item*>* it = contacts->iterator();
    Item* currentItem = it->next();
    if (currentItem->isShoveFixed() || jrt::fast_cast<PolylineTrace>(currentItem) == nullptr) {
      auto* area = jrt::fast_cast<ConductionArea>(currentItem);
      if (area != nullptr && !area->isFractured()) {
        isPlaneOrFanoutVia = true;
      } else {
        return false;
      }
    } else {
      firstTrace = jrt::fast_cast<PolylineTrace>(currentItem);
    }
    currentItem = it->next();
    if (currentItem->isShoveFixed() || jrt::fast_cast<PolylineTrace>(currentItem) == nullptr) {
      auto* area = jrt::fast_cast<ConductionArea>(currentItem);
      if (area != nullptr && !area->isFractured()) {
        isPlaneOrFanoutVia = true;
      } else {
        return false;
      }
    } else {
      secondTrace = jrt::fast_cast<PolylineTrace>(currentItem);
    }
  }
  if (isPlaneOrFanoutVia) {
    return optPlaneOrFanoutVia(board, via, tracePullTightAccuracy, maxRecursionDepth);
  }
  Point* viaCenter = via->getCenter();
  int32_t firstLayer = firstTrace->getLayer();
  int32_t secondLayer = secondTrace->getLayer();
  Point* firstTraceFromCorner;
  Point* secondTraceFromCorner;

  // calculate firstTraceFromCorner and secondTraceFromCorner
  // Use tolerance-based comparison to match connectivity detection logic
  int32_t tolerance = jrt::d2i(via->minWidth() / 2) + 1;

  if (isWithinTolerance(firstTrace->firstCorner(), viaCenter, tolerance)) {
    firstTraceFromCorner = firstTrace->polyline()->corner(1);
  } else if (isWithinTolerance(firstTrace->lastCorner(), viaCenter, tolerance)) {
    firstTraceFromCorner = firstTrace->polyline()->corner(firstTrace->polyline()->cornerCount() - 2);
  } else {
    // Via is not connected at trace endpoints - skip optimization
    return false;
  }

  if (isWithinTolerance(secondTrace->firstCorner(), viaCenter, tolerance)) {
    secondTraceFromCorner = secondTrace->polyline()->corner(1);
  } else if (isWithinTolerance(secondTrace->lastCorner(), viaCenter, tolerance)) {
    secondTraceFromCorner =
        secondTrace->polyline()->corner(secondTrace->polyline()->cornerCount() - 2);
  } else {
    // Via is not connected at trace endpoints - skip optimization
    return false;
  }

  AutorouteControl_ExpansionCostFactor* firstLayerTraceCosts;
  AutorouteControl_ExpansionCostFactor* secondLayerTraceCosts;
  if (traceCosts != nullptr) {
    firstLayerTraceCosts = (*traceCosts)[firstLayer];
    secondLayerTraceCosts = (*traceCosts)[secondLayer];
  } else {
    firstLayerTraceCosts = new AutorouteControl_ExpansionCostFactor(1, 1);
    secondLayerTraceCosts = firstLayerTraceCosts;
  }

  Point* newLocation =
      repositionVia(board, via, firstTrace->getHalfWidth(), firstTrace->clearanceClassIndex(),
                    firstTrace->getLayer(), firstLayerTraceCosts, firstTraceFromCorner,
                    secondTrace->getHalfWidth(), secondTrace->clearanceClassIndex(),
                    secondTrace->getLayer(), secondLayerTraceCosts, secondTraceFromCorner);
  if (newLocation == nullptr || newLocation->equals(viaCenter)) {
    return false;
  }
  if (!history->allowsMove(via, viaCenter, newLocation, tracePullTightAccuracy)) {
    // The via has already come back twice: it cycles, leave it where it is.
    return false;
  }
  Vector* delta = newLocation->differenceBy(viaCenter);
  if (!DrillItemMover::insert(via, delta, 9, 9, nullptr, board)) {
    FRLogger::warn("OptViaAlgo.opt_via_location: move via failed");
    return false;
  }
  history->recordMove(via, viaCenter, newLocation, tracePullTightAccuracy);
  ItemSelectionFilter* filter =
      new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
  jrt::Collection<Item*>* pickedItems =
      board->pickItems(newLocation, firstTrace->getLayer(), filter);
  for (Item* currentItem : *pickedItems) {
    jrt::fast_cast<PolylineTrace>(currentItem)->pullTight(true, tracePullTightAccuracy, nullptr);
  }
  pickedItems = board->pickItems(newLocation, secondTrace->getLayer(), filter);
  for (Item* currentItem : *pickedItems) {
    jrt::fast_cast<PolylineTrace>(currentItem)->pullTight(true, tracePullTightAccuracy, nullptr);
  }
  filter = new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::VIAS);
  pickedItems = board->pickItems(newLocation, firstTrace->getLayer(), filter);
  for (Item* currentItem : *pickedItems) {
    optViaLocation(board, jrt::fast_cast<Via>(currentItem), traceCosts, tracePullTightAccuracy,
                   maxRecursionDepth - 1, history);
    break;
  }
  return true;
}

bool ViaOptimizer::optPlaneOrFanoutVia(RoutingBoard* board, Via* via,
                                       int32_t tracePullTightAccuracy,
                                       int32_t maxRecursionDepth) {
  if (maxRecursionDepth <= 0) {
    FRLogger::debug("OptViaAlgo.opt_plane_or_fanout_via: probably endless loop");
    return false;
  }
  jrt::Collection<Item*>* contactList = via->getNormalContacts();
  if (contactList->isEmpty()) {
    return false;
  }
  ConductionArea* contactPlane = nullptr;
  PolylineTrace* contactTrace = nullptr;
  for (Item* currentContact : *contactList) {
    auto* area = jrt::fast_cast<ConductionArea>(currentContact);
    if (area != nullptr && !area->isFractured()) {
      if (contactPlane != nullptr) {
        return false;
      }
      contactPlane = area;
    } else if (auto* trace = jrt::fast_cast<PolylineTrace>(currentContact)) {
      if (currentContact->isShoveFixed() || contactTrace != nullptr) {
        return false;
      }
      contactTrace = trace;
    } else {
      return false;
    }
  }
  if (contactTrace == nullptr) {
    return false;
  }
  Point* viaCenter = via->getCenter();

  // Use tolerance based on via size, matching the logic in opt_via_location
  int32_t tolerance = jrt::d2i(via->minWidth() / 2) + 1;

  bool atFirstCorner;
  if (isWithinTolerance(contactTrace->firstCorner(), viaCenter, tolerance)) {
    atFirstCorner = true;
  } else if (isWithinTolerance(contactTrace->lastCorner(), viaCenter, tolerance)) {
    atFirstCorner = false;
  } else {
    // Via is not connected at trace endpoints - skip optimization
    return false;
  }
  Polyline* tracePolyline = contactTrace->polyline();
  Point* checkCorner;
  if (atFirstCorner) {
    checkCorner = tracePolyline->corner(1);
  } else {
    checkCorner = tracePolyline->corner(tracePolyline->cornerCount() - 2);
  }
  IntPoint* roundedCheckCorner = checkCorner->toFloat()->round();
  int32_t traceHalfWidth = contactTrace->getHalfWidth();
  int32_t traceLayer = contactTrace->getLayer();
  int32_t traceClClassNo = contactTrace->clearanceClassIndex();
  Point* newViaLocation =
      repositionVia(board, via, roundedCheckCorner, traceHalfWidth, traceLayer, traceClClassNo);
  if (newViaLocation == nullptr && tracePolyline->cornerCount() >= 3) {
    // try to project the via to the previous line
    Point* prevCorner;

    if (atFirstCorner) {
      prevCorner = tracePolyline->corner(2);
    } else {
      prevCorner = tracePolyline->corner(tracePolyline->cornerCount() - 3);
    }
    FloatPoint* floatCheckCorner = checkCorner->toFloat();
    FloatPoint* floatViaCenter = viaCenter->toFloat();
    FloatPoint* floatPrevCorner = prevCorner->toFloat();
    if (floatCheckCorner->scalarProduct(floatViaCenter, floatPrevCorner) != 0) {
      FloatLine* currentLine = new FloatLine(floatCheckCorner, floatPrevCorner);
      Point* projection = currentLine->perpendicularProjection(floatViaCenter)->round();
      Vector* diffVector = projection->differenceBy(viaCenter);
      bool projectionOk = true;
      AngleRestriction angleRestriction = board->rules->getTraceAngleRestriction();
      if (projection->equals(viaCenter) ||
          (angleRestriction == AngleRestriction::NINETY_DEGREE && !diffVector->isOrthogonal()) ||
          (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE &&
           !diffVector->isMultipleOf45Degree())) {
        projectionOk = false;
      }
      if (projectionOk) {
        if (DrillItemMover::check(via, diffVector, 0, 0, nullptr, board, nullptr)) {
          double okLength = board->checkTraceSegment(viaCenter, projection, traceLayer,
                                                     via->netNumbers, traceHalfWidth,
                                                     traceClClassNo, false);
          if (okLength >= jrt::Integer::MAX_VALUE) {
            newViaLocation = projection;
          }
        }
      }
    }
  }
  if (newViaLocation == nullptr) {
    return false;
  }
  if (contactPlane != nullptr) {
    // check, that the new location is inside the contact plane
    ItemSelectionFilter* filter =
        new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::CONDUCTION);
    jrt::Collection<Item*>* pickedItems =
        board->pickItems(newViaLocation, contactPlane->getLayer(), filter);
    bool contactOk = false;
    for (Item* currentItem : *pickedItems) {
      if (currentItem == contactPlane) {
        contactOk = true;
        break;
      }
    }
    if (!contactOk) {
      return false;
    }
  }
  Vector* diffVector = newViaLocation->differenceBy(viaCenter);
  if (!DrillItemMover::insert(via, diffVector, 9, 9, nullptr, board)) {
    FRLogger::warn("OptViaAlgo.opt_plane_or_fanout_via: move via failed");
    return false;
  }
  ItemSelectionFilter* filter =
      new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
  jrt::Collection<Item*>* pickedItems =
      board->pickItems(newViaLocation, contactTrace->getLayer(), filter);
  for (Item* currentItem : *pickedItems) {
    jrt::fast_cast<PolylineTrace>(currentItem)->pullTight(true, tracePullTightAccuracy, nullptr);
  }
  if (newViaLocation->equals(checkCorner)) {
    optPlaneOrFanoutVia(board, via, tracePullTightAccuracy, maxRecursionDepth - 1);
  }
  return true;
}

Point* ViaOptimizer::repositionVia(RoutingBoard* board, Via* via, IntPoint* toLocation,
                                   int32_t traceHalfWidth, int32_t traceLayer,
                                   int32_t traceClClass) {
  Point* fromLocation = via->getCenter();

  if (fromLocation->equals(toLocation)) {
    return nullptr;
  }

  double okLength = board->checkTraceSegment(fromLocation, toLocation, traceLayer,
                                             via->netNumbers, traceHalfWidth, traceClClass, false);
  if (okLength <= 0) {
    return nullptr;
  }
  FloatPoint* floatFromLocation = fromLocation->toFloat();
  FloatPoint* floatToLocation = toLocation->toFloat();
  FloatPoint* newFloatToLocation;
  if (okLength >= jrt::Integer::MAX_VALUE) {
    newFloatToLocation = floatToLocation;
  } else {
    newFloatToLocation = floatFromLocation->changeLength(floatToLocation, okLength);
  }
  Point* newToLocation = newFloatToLocation->round();
  Vector* delta = newToLocation->differenceBy(fromLocation);
  bool checkOk = DrillItemMover::check(via, delta, 0, 0, nullptr, board, nullptr);

  if (checkOk) {
    return newToLocation;
  }

  const double minLength = 0.3 * traceHalfWidth + 1;

  okLength = jrt::Math::min(okLength, floatFromLocation->distance(floatToLocation));

  double currentLength = okLength / 2;

  okLength = 0;
  Point* result = nullptr;

  while (currentLength >= minLength) {
    Point* checkPoint =
        floatFromLocation->changeLength(floatToLocation, okLength + currentLength)->round();

    delta = checkPoint->differenceBy(fromLocation);
    if (DrillItemMover::check(via, delta, 0, 0, nullptr, board, nullptr)) {
      okLength += currentLength;
      result = checkPoint;
    }
    currentLength /= 2;
  }
  return result;
}

bool ViaOptimizer::repositionVia(RoutingBoard* board, Via* via, IntPoint* toLocation,
                                 int32_t traceHalfWidth1, int32_t traceLayer1,
                                 int32_t traceClClass1, IntPoint* connectLocation,
                                 int32_t traceHalfWidth2, int32_t traceLayer2,
                                 int32_t traceClClass2) {
  Point* fromLocation = via->getCenter();

  if (fromLocation->equals(toLocation)) {
    FRLogger::trace("OptViaAlgo.reposition_via: fromLocation equal toLocation");
    return false;
  }

  Vector* delta = toLocation->differenceBy(fromLocation);

  if (board->rules->getTraceAngleRestriction() == AngleRestriction::NONE &&
      delta->lengthApprox() <= 1.5) {
    // TraceTightenerAnyAngle.reduce_corners may not be able to remove the new
    // generated overlap
    // because of numerical stability problems
    // That would result in an endless loop with removing the generated acute angle
    // in
    // reposition_via.
    return false;
  }

  jrt::Array<int32_t>* netNumbers = via->netNumbers;

  double okLength = board->checkTraceSegment(fromLocation, toLocation, traceLayer1, netNumbers,
                                             traceHalfWidth1, traceClClass1, false);

  if (okLength < jrt::Integer::MAX_VALUE) {
    return false;
  }

  okLength = board->checkTraceSegment(toLocation, connectLocation, traceLayer2, netNumbers,
                                      traceHalfWidth2, traceClClass2, false);

  if (okLength < jrt::Integer::MAX_VALUE) {
    return false;
  }
  return DrillItemMover::check(via, delta, 0, 0, nullptr, board, nullptr);
}

Point* ViaOptimizer::repositionVia(RoutingBoard* board, Via* via, int32_t firstTraceHalfWidth,
                                   int32_t firstTraceClClass, int32_t firstTraceLayer,
                                   AutorouteControl_ExpansionCostFactor* firstTraceCosts,
                                   Point* firstTraceFromCorner, int32_t secondTraceHalfWidth,
                                   int32_t secondTraceClClass, int32_t secondTraceLayer,
                                   AutorouteControl_ExpansionCostFactor* secondTraceCosts,
                                   Point* secondTraceFromCorner) {
  Point* viaLocation = via->getCenter();

  Vector* firstDelta = firstTraceFromCorner->differenceBy(viaLocation);
  Vector* secondDelta = secondTraceFromCorner->differenceBy(viaLocation);
  double scalarProduct = firstDelta->scalarProduct(secondDelta);

  FloatPoint* floatViaLocation = viaLocation->toFloat();
  FloatPoint* floatFirstTraceFromCorner = firstTraceFromCorner->toFloat();
  FloatPoint* floatSecondTraceFromCorner = secondTraceFromCorner->toFloat();
  double firstTraceFromCornerDistance = floatViaLocation->distance(floatFirstTraceFromCorner);
  double secondTraceFromCornerDistance = floatViaLocation->distance(floatSecondTraceFromCorner);
  IntPoint* roundedFirstTraceFromCorner = floatFirstTraceFromCorner->round();
  IntPoint* roundedSecondTraceFromCorner = floatSecondTraceFromCorner->round();

  // handle case of overlapping lines first

  if (viaLocation->sideOf(firstTraceFromCorner, secondTraceFromCorner) == Side::COLLINEAR() &&
      scalarProduct > 0) {
    if (secondTraceFromCornerDistance < firstTraceFromCornerDistance) {
      return repositionVia(board, via, roundedSecondTraceFromCorner, firstTraceHalfWidth,
                           firstTraceLayer, firstTraceClClass);
    }
    return repositionVia(board, via, roundedFirstTraceFromCorner, secondTraceHalfWidth,
                         secondTraceLayer, secondTraceClClass);
  }
  Point* result;

  double currentWeightedDistance1 = floatViaLocation->weightedDistance(
      floatFirstTraceFromCorner, firstTraceCosts->horizontal(), firstTraceCosts->vertical());
  double currentWeightedDistance2 = floatViaLocation->weightedDistance(
      floatFirstTraceFromCorner, secondTraceCosts->horizontal(), secondTraceCosts->vertical());

  if (currentWeightedDistance1 > currentWeightedDistance2) {
    // try to move the via in direction of firstTraceFromCorner
    result = repositionVia(board, via, roundedFirstTraceFromCorner, secondTraceHalfWidth,
                           secondTraceLayer, secondTraceClClass);
    if (result != nullptr) {
      return result;
    }
  }

  currentWeightedDistance1 = floatViaLocation->weightedDistance(
      floatSecondTraceFromCorner, secondTraceCosts->horizontal(), secondTraceCosts->vertical());
  currentWeightedDistance2 = floatViaLocation->weightedDistance(
      floatSecondTraceFromCorner, firstTraceCosts->horizontal(), firstTraceCosts->vertical());

  if (currentWeightedDistance1 > currentWeightedDistance2) {
    // try to move the via in direction of secondTraceFromCorner
    result = repositionVia(board, via, roundedSecondTraceFromCorner, firstTraceHalfWidth,
                           firstTraceLayer, firstTraceClClass);
    if (result != nullptr) {
      return result;
    }
  }
  if (scalarProduct > 0 &&
      board->rules->getTraceAngleRestriction() != AngleRestriction::NINETY_DEGREE) {
    // acute angle
    IntPoint* toPoint1;
    IntPoint* toPoint2;
    FloatPoint* floatToPoint1;
    FloatPoint* floatToPoint2;
    if (firstTraceFromCornerDistance < secondTraceFromCornerDistance) {
      toPoint1 = roundedFirstTraceFromCorner;
      floatToPoint1 = floatFirstTraceFromCorner;
      floatToPoint2 =
          floatViaLocation->changeLength(floatSecondTraceFromCorner, firstTraceFromCornerDistance);
      toPoint2 = floatToPoint2->round();
    } else {
      floatToPoint1 =
          floatViaLocation->changeLength(floatFirstTraceFromCorner, secondTraceFromCornerDistance);
      toPoint1 = floatToPoint1->round();
      toPoint2 = roundedSecondTraceFromCorner;
      floatToPoint2 = floatSecondTraceFromCorner;
    }
    currentWeightedDistance1 = floatToPoint1->weightedDistance(
        floatToPoint2, firstTraceCosts->horizontal(), firstTraceCosts->vertical());
    currentWeightedDistance2 = floatToPoint1->weightedDistance(
        floatToPoint2, secondTraceCosts->horizontal(), secondTraceCosts->vertical());

    if (currentWeightedDistance1 > currentWeightedDistance2) {
      // try moving the via first into the direction of toPoint1
      result = repositionVia(board, via, toPoint1, secondTraceHalfWidth, secondTraceLayer,
                             secondTraceClClass);
      if (result == nullptr) {
        result = repositionVia(board, via, toPoint2, firstTraceHalfWidth, firstTraceLayer,
                               firstTraceClClass);
      }
    } else {
      // try moving the via first into the direction of toPoint2
      result = repositionVia(board, via, toPoint2, firstTraceHalfWidth, firstTraceLayer,
                             firstTraceClClass);
      if (result == nullptr) {
        result = repositionVia(board, via, toPoint1, secondTraceHalfWidth, secondTraceLayer,
                               secondTraceClClass);
      }
    }
    if (result != nullptr) {
      return result;
    }
  }

  // try decomposition in axisparallel parts

  if (!firstDelta->isOrthogonal()) {
    FloatPoint* floatCheckLocation =
        new FloatPoint(floatViaLocation->x, floatFirstTraceFromCorner->y);

    currentWeightedDistance1 = floatViaLocation->weightedDistance(
        floatFirstTraceFromCorner, firstTraceCosts->horizontal(), firstTraceCosts->vertical());
    currentWeightedDistance2 = floatViaLocation->weightedDistance(
        floatCheckLocation, secondTraceCosts->horizontal(), secondTraceCosts->vertical());
    double currentWeightedDistance3 = floatCheckLocation->weightedDistance(
        floatFirstTraceFromCorner, firstTraceCosts->horizontal(), firstTraceCosts->vertical());

    if (currentWeightedDistance1 > currentWeightedDistance2 + currentWeightedDistance3) {
      IntPoint* checkLocation = floatCheckLocation->round();
      bool checkOk = repositionVia(board, via, checkLocation, secondTraceHalfWidth,
                                   secondTraceLayer, secondTraceClClass,
                                   roundedFirstTraceFromCorner, firstTraceHalfWidth,
                                   firstTraceLayer, firstTraceClClass);
      if (checkOk) {
        return checkLocation;
      }
    }

    floatCheckLocation = new FloatPoint(floatFirstTraceFromCorner->x, floatViaLocation->y);

    currentWeightedDistance2 = floatViaLocation->weightedDistance(
        floatCheckLocation, secondTraceCosts->horizontal(), secondTraceCosts->vertical());
    currentWeightedDistance3 = floatCheckLocation->weightedDistance(
        floatFirstTraceFromCorner, firstTraceCosts->horizontal(), firstTraceCosts->vertical());

    if (currentWeightedDistance1 > currentWeightedDistance2 + currentWeightedDistance3) {
      IntPoint* checkLocation = floatCheckLocation->round();
      bool checkOk = repositionVia(board, via, checkLocation, secondTraceHalfWidth,
                                   secondTraceLayer, secondTraceClClass,
                                   roundedFirstTraceFromCorner, firstTraceHalfWidth,
                                   firstTraceLayer, firstTraceClClass);
      if (checkOk) {
        return checkLocation;
      }
    }
  }

  if (!secondDelta->isOrthogonal()) {
    FloatPoint* floatCheckLocation =
        new FloatPoint(floatViaLocation->x, floatSecondTraceFromCorner->y);

    currentWeightedDistance1 = floatViaLocation->weightedDistance(
        floatSecondTraceFromCorner, secondTraceCosts->horizontal(), secondTraceCosts->vertical());
    currentWeightedDistance2 = floatViaLocation->weightedDistance(
        floatCheckLocation, firstTraceCosts->horizontal(), firstTraceCosts->vertical());
    double currentWeightedDistance3 = floatCheckLocation->weightedDistance(
        floatSecondTraceFromCorner, secondTraceCosts->horizontal(), secondTraceCosts->vertical());

    if (currentWeightedDistance1 > currentWeightedDistance2 + currentWeightedDistance3) {
      IntPoint* checkLocation = floatCheckLocation->round();
      bool checkOk = repositionVia(board, via, checkLocation, firstTraceHalfWidth,
                                   firstTraceLayer, firstTraceClClass,
                                   roundedSecondTraceFromCorner, secondTraceHalfWidth,
                                   secondTraceLayer, secondTraceClClass);
      if (checkOk) {
        return checkLocation;
      }
    }

    floatCheckLocation = new FloatPoint(floatSecondTraceFromCorner->x, floatViaLocation->y);

    currentWeightedDistance2 = floatViaLocation->weightedDistance(
        floatCheckLocation, firstTraceCosts->horizontal(), firstTraceCosts->vertical());
    currentWeightedDistance3 = floatCheckLocation->weightedDistance(
        floatSecondTraceFromCorner, secondTraceCosts->horizontal(), secondTraceCosts->vertical());

    if (currentWeightedDistance1 > currentWeightedDistance2 + currentWeightedDistance3) {
      IntPoint* checkLocation = floatCheckLocation->round();
      bool checkOk = repositionVia(board, via, checkLocation, firstTraceHalfWidth,
                                   firstTraceLayer, firstTraceClClass,
                                   roundedSecondTraceFromCorner, secondTraceHalfWidth,
                                   secondTraceLayer, secondTraceClClass);
      if (checkOk) {
        return checkLocation;
      }
    }
  }
  return nullptr;
}

bool ViaOptimizer::isWithinTolerance(Point* p1, Point* p2, int32_t tolerance) {
  if (p1 == nullptr || p2 == nullptr) {
    return false;
  }
  // Convert to FloatPoint for distance calculation
  FloatPoint* fp1 = p1->toFloat();
  FloatPoint* fp2 = p2->toFloat();

  // Use Manhattan distance (|x1-x2| + |y1-y2|) which is faster than Euclidean
  // and sufficient for connectivity detection
  double dx = jrt::Math::abs(fp1->x - fp2->x);
  double dy = jrt::Math::abs(fp1->y - fp2->y);
  return (dx + dy) <= tolerance;
}

}  // namespace fr
