#include "autoroute/maze/DestinationDistance.h"

#include "autoroute/maze/AutorouteControl.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"

namespace fr {

DestinationDistance::DestinationDistance(
    jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts, jrt::Array<bool>* layerActive,
    double minNormalViaCost, double minCheapViaCost)
    : traceCosts(traceCosts),
      layerActive(layerActive),
      layerCount(layerActive->length),
      activeLayerCount(0),
      minCheapViaCost(minCheapViaCost),
      minNormalViaCost(minNormalViaCost),
      componentSideBox(IntBox::EMPTY()),
      solderSideBox(IntBox::EMPTY()),
      innerSideBox(IntBox::EMPTY()) {
  int32_t currentActiveLayerCount = 0;
  for (int32_t ind = 0; ind < layerCount; ind++) {
    if ((*layerActive)[ind]) {
      ++currentActiveLayerCount;
    }
  }
  this->activeLayerCount = currentActiveLayerCount;

  if ((*layerActive)[0]) {
    if ((*traceCosts)[0]->horizontal() < (*traceCosts)[0]->vertical()) {
      minComponentSideTraceCost = (*traceCosts)[0]->horizontal();
      maxComponentSideTraceCost = (*traceCosts)[0]->vertical();
    } else {
      minComponentSideTraceCost = (*traceCosts)[0]->vertical();
      maxComponentSideTraceCost = (*traceCosts)[0]->horizontal();
    }
  }

  if ((*layerActive)[layerCount - 1]) {
    AutorouteControl_ExpansionCostFactor* currentTraceCost = (*traceCosts)[layerCount - 1];

    if (currentTraceCost->horizontal() < currentTraceCost->vertical()) {
      minSolderSideTraceCost = currentTraceCost->horizontal();
      maxSolderSideTraceCost = currentTraceCost->vertical();
    } else {
      minSolderSideTraceCost = currentTraceCost->vertical();
      maxSolderSideTraceCost = currentTraceCost->horizontal();
    }
  }

  // Note: for inner layers we assume, that cost in preferred direction is 1
  maxInnerSideTraceCost = jrt::Math::min(maxComponentSideTraceCost, maxSolderSideTraceCost);
  for (int32_t ind2 = 1; ind2 < layerCount - 1; ind2++) {
    if (!(*layerActive)[ind2]) {
      continue;
    }
    double currentMaxCost =
        jrt::Math::max((*traceCosts)[ind2]->horizontal(), (*traceCosts)[ind2]->vertical());

    maxInnerSideTraceCost = jrt::Math::min(maxInnerSideTraceCost, currentMaxCost);
  }
  minComponentInnerTraceCost = jrt::Math::min(minComponentSideTraceCost, maxInnerSideTraceCost);
  minSolderInnerTraceCost = jrt::Math::min(minSolderSideTraceCost, maxInnerSideTraceCost);
  minComponentSolderInnerTraceCost =
      jrt::Math::min(minComponentInnerTraceCost, minSolderInnerTraceCost);
}

void DestinationDistance::considerInactiveInnerPlane(DestinationDistance_LowerBound* result,
                                                     double innerSideMaxDelta,
                                                     double innerSideMinDelta) {
  if (!innerSideBoxIsEmpty) {
    result->consider(innerSideMaxDelta + innerSideMinDelta * minComponentSolderInnerTraceCost +
                         minNormalViaCost,
                     1);
  }
}

void DestinationDistance::join(IntBox* box, int32_t layer) {
  if (layer == 0) {
    componentSideBox = componentSideBox->union_(box);
    componentSideBoxIsEmpty = false;
  } else if (layer == layerCount - 1) {
    solderSideBox = solderSideBox->union_(box);
    solderSideBoxIsEmpty = false;
  } else {
    innerSideBox = innerSideBox->union_(box);
    innerSideBoxIsEmpty = false;
  }
  boxIsEmpty = false;
}

double DestinationDistance::calculate(FloatPoint* point, int32_t layer) {
  return calculate(point->boundingBox(), layer);
}

double DestinationDistance::calculateWithPrepaidVia(FloatPoint* point, int32_t layer) {
  DestinationDistance_LowerBound* bound = lowerBound(point->boundingBox(), layer);
  if (bound->viaCount <= 0) {
    return bound->cost;
  }
  return jrt::Math::max(0.0, bound->cost - minNormalViaCost);
}

double DestinationDistance::calculate(IntBox* box, int32_t layer) {
  return lowerBound(box, layer)->cost;
}

DestinationDistance_LowerBound* DestinationDistance::lowerBound(IntBox* box, int32_t layer) {
  if (boxIsEmpty) {
    return new DestinationDistance_LowerBound();
  }

  // The coordinate differences are int arithmetic in Java (wrapping with -fwrapv), converted to
  // double on assignment.
  double componentSideDeltaX;
  double componentSideDeltaY;

  if (box->ll->x > componentSideBox->ur->x) {
    componentSideDeltaX = box->ll->x - componentSideBox->ur->x;
  } else if (box->ur->x < componentSideBox->ll->x) {
    componentSideDeltaX = componentSideBox->ll->x - box->ur->x;
  } else {
    componentSideDeltaX = 0;
  }

  if (box->ll->y > componentSideBox->ur->y) {
    componentSideDeltaY = box->ll->y - componentSideBox->ur->y;
  } else if (box->ur->y < componentSideBox->ll->y) {
    componentSideDeltaY = componentSideBox->ll->y - box->ur->y;
  } else {
    componentSideDeltaY = 0;
  }

  double solderSideDeltaX;
  double solderSideDeltaY;

  if (box->ll->x > solderSideBox->ur->x) {
    solderSideDeltaX = box->ll->x - solderSideBox->ur->x;
  } else if (box->ur->x < solderSideBox->ll->x) {
    solderSideDeltaX = solderSideBox->ll->x - box->ur->x;
  } else {
    solderSideDeltaX = 0;
  }

  if (box->ll->y > solderSideBox->ur->y) {
    solderSideDeltaY = box->ll->y - solderSideBox->ur->y;
  } else if (box->ur->y < solderSideBox->ll->y) {
    solderSideDeltaY = solderSideBox->ll->y - box->ur->y;
  } else {
    solderSideDeltaY = 0;
  }

  double innerSideDeltaX;
  double innerSideDeltaY;

  if (box->ll->x > innerSideBox->ur->x) {
    innerSideDeltaX = box->ll->x - innerSideBox->ur->x;
  } else if (box->ur->x < innerSideBox->ll->x) {
    innerSideDeltaX = innerSideBox->ll->x - box->ur->x;
  } else {
    innerSideDeltaX = 0;
  }

  if (box->ll->y > innerSideBox->ur->y) {
    innerSideDeltaY = box->ll->y - innerSideBox->ur->y;
  } else if (box->ur->y < innerSideBox->ll->y) {
    innerSideDeltaY = innerSideBox->ll->y - box->ur->y;
  } else {
    innerSideDeltaY = 0;
  }

  double componentSideMaxDelta;
  double componentSideMinDelta;

  if (componentSideDeltaX > componentSideDeltaY) {
    componentSideMaxDelta = componentSideDeltaX;
    componentSideMinDelta = componentSideDeltaY;
  } else {
    componentSideMaxDelta = componentSideDeltaY;
    componentSideMinDelta = componentSideDeltaX;
  }

  double solderSideMaxDelta;
  double solderSideMinDelta;

  if (solderSideDeltaX > solderSideDeltaY) {
    solderSideMaxDelta = solderSideDeltaX;
    solderSideMinDelta = solderSideDeltaY;
  } else {
    solderSideMaxDelta = solderSideDeltaY;
    solderSideMinDelta = solderSideDeltaX;
  }

  double innerSideMaxDelta;
  double innerSideMinDelta;

  if (innerSideDeltaX > innerSideDeltaY) {
    innerSideMaxDelta = innerSideDeltaX;
    innerSideMinDelta = innerSideDeltaY;
  } else {
    innerSideMaxDelta = innerSideDeltaY;
    innerSideMinDelta = innerSideDeltaX;
  }

  DestinationDistance_LowerBound* result = new DestinationDistance_LowerBound();

  if (layer == 0) {  // calculate shortest distance to component side box
    // calculate one layer distance

    if (!componentSideBoxIsEmpty) {
      result->assign(box->weightedDistance(componentSideBox, (*traceCosts)[0]->horizontal(),
                                           (*traceCosts)[0]->vertical()),
                     0);
    }

    if (activeLayerCount <= 1) {
      return result;
    }

    // calculate two layer distance on component and solder side

    double tmpDistance;
    if (minSolderSideTraceCost < minComponentSideTraceCost) {
      tmpDistance = minSolderSideTraceCost * solderSideMaxDelta +
                    minComponentSideTraceCost * solderSideMinDelta + minNormalViaCost;
    } else {
      tmpDistance = minComponentSideTraceCost * solderSideMaxDelta +
                    minSolderSideTraceCost * solderSideMinDelta + minNormalViaCost;
    }

    result->consider(tmpDistance, 1);

    // calculate two layer distance on component and solde side
    // with two vias

    tmpDistance = componentSideMaxDelta + componentSideMinDelta * minComponentInnerTraceCost +
                  2 * minNormalViaCost;

    result->consider(tmpDistance, 2);

    if (activeLayerCount == 2) {
      considerInactiveInnerPlane(result, innerSideMaxDelta, innerSideMinDelta);
      return result;
    }

    // calculate two layer distance on component side and an inner side

    tmpDistance =
        innerSideMaxDelta + innerSideMinDelta * minComponentInnerTraceCost + minNormalViaCost;

    result->consider(tmpDistance, 1);

    // calculate three layer distance

    tmpDistance = solderSideMaxDelta + +minComponentSolderInnerTraceCost * solderSideMinDelta +
                  2 * minNormalViaCost;
    result->consider(tmpDistance, 2);

    tmpDistance = componentSideMaxDelta + componentSideMinDelta + 2 * minNormalViaCost;
    result->consider(tmpDistance, 2);

    if (activeLayerCount == 3) {
      return result;
    }

    tmpDistance = innerSideMaxDelta + innerSideMinDelta + 2 * minNormalViaCost;

    result->consider(tmpDistance, 2);

    // calculate four layer distance

    tmpDistance = solderSideMaxDelta + solderSideMinDelta + 3 * minNormalViaCost;

    result->consider(tmpDistance, 3);
    return result;
  }
  if (layer == layerCount - 1) {  // calculate the shortest distance to solder side box
    // calculate one layer distance

    if (!solderSideBoxIsEmpty) {
      result->assign(box->weightedDistance(solderSideBox, (*traceCosts)[layer]->horizontal(),
                                           (*traceCosts)[layer]->vertical()),
                     0);
    }

    // calculate two layer distance
    double tmpDistance;
    if (minComponentSideTraceCost < minSolderSideTraceCost) {
      tmpDistance = minComponentSideTraceCost * componentSideMaxDelta +
                    minSolderSideTraceCost * componentSideMinDelta + minNormalViaCost;
    } else {
      tmpDistance = minSolderSideTraceCost * componentSideMaxDelta +
                    minComponentSideTraceCost * componentSideMinDelta + minNormalViaCost;
    }
    result->consider(tmpDistance, 1);
    tmpDistance = solderSideMaxDelta + solderSideMinDelta * minSolderInnerTraceCost +
                  2 * minNormalViaCost;
    result->consider(tmpDistance, 2);
    if (activeLayerCount <= 2) {
      considerInactiveInnerPlane(result, innerSideMaxDelta, innerSideMinDelta);
      return result;
    }
    tmpDistance =
        innerSideMinDelta * minSolderInnerTraceCost + innerSideMaxDelta + minNormalViaCost;
    result->consider(tmpDistance, 1);

    // calculate three layer distance

    tmpDistance = componentSideMaxDelta +
                  minComponentSolderInnerTraceCost * componentSideMinDelta +
                  2 * minNormalViaCost;
    result->consider(tmpDistance, 2);
    tmpDistance = solderSideMaxDelta + solderSideMinDelta + 2 * minNormalViaCost;
    result->consider(tmpDistance, 2);
    if (activeLayerCount == 3) {
      return result;
    }
    tmpDistance = innerSideMaxDelta + innerSideMinDelta + 2 * minNormalViaCost;
    result->consider(tmpDistance, 2);

    // calculate four layer distance

    tmpDistance = componentSideMaxDelta + componentSideMinDelta + 3 * minNormalViaCost;
    result->consider(tmpDistance, 3);
    return result;
  }

  // calculate distance to inner layer box

  // calculate one layer distance

  if (!innerSideBoxIsEmpty) {
    result->assign(box->weightedDistance(innerSideBox, (*traceCosts)[layer]->horizontal(),
                                         (*traceCosts)[layer]->vertical()),
                   0);
  }

  // calculate two layer distance

  double tmpDistance = innerSideMaxDelta + innerSideMinDelta + minNormalViaCost;

  result->consider(tmpDistance, 1);
  tmpDistance = componentSideMaxDelta + componentSideMinDelta * minComponentInnerTraceCost +
                minNormalViaCost;
  result->consider(tmpDistance, 1);
  tmpDistance =
      solderSideMaxDelta + solderSideMinDelta * minSolderInnerTraceCost + minNormalViaCost;
  result->consider(tmpDistance, 1);

  // calculate three layer distance

  tmpDistance = componentSideMaxDelta + componentSideMinDelta + 2 * minNormalViaCost;
  result->consider(tmpDistance, 2);
  tmpDistance = solderSideMaxDelta + solderSideMinDelta + 2 * minNormalViaCost;
  result->consider(tmpDistance, 2);
  return result;
}

double DestinationDistance::calculateCheapDistance(IntBox* box, int32_t layer) {
  double minNormalViaCostSave = minNormalViaCost;

  minNormalViaCost = minCheapViaCost;
  double result = calculate(box, layer);

  minNormalViaCost = minNormalViaCostSave;
  return result;
}

}  // namespace fr
