#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Lower bound of the cost and via count to a destination (Java: private static final class). */
class DestinationDistance_LowerBound final : public virtual jrt::Object {
  friend class DestinationDistance;

 private:
  double cost = jrt::Integer::MAX_VALUE;
  int32_t viaCount = 0;

  void assign(double candidate, int32_t vias) {
    this->cost = candidate;
    this->viaCount = vias;
  }

  void consider(double candidate, int32_t vias) {
    if (candidate < this->cost) {
      assign(candidate, vias);
    }
  }
};

/**
 * Calculation of a good lower bound for the distance between a new MazeExpansionElement and the
 * destination set of the expansion.
 */
class DestinationDistance : public virtual jrt::Object {
 private:
  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts;
  jrt::Array<bool>* layerActive;
  int32_t layerCount;
  int32_t activeLayerCount;
  double minCheapViaCost;

 public:
  // (Java: package-private fields)
  double minComponentSideTraceCost = 0;
  double maxComponentSideTraceCost = 0;
  double minSolderSideTraceCost = 0;
  double maxSolderSideTraceCost = 0;
  double maxInnerSideTraceCost = 0;
  double minComponentInnerTraceCost = 0;
  double minSolderInnerTraceCost = 0;
  double minComponentSolderInnerTraceCost = 0;

 private:
  double minNormalViaCost;
  // PORT NOTE: Java initialises the three boxes with IntBox.EMPTY; done in the constructor
  // (IntBox::EMPTY()).
  IntBox* componentSideBox = nullptr;
  IntBox* solderSideBox = nullptr;
  IntBox* innerSideBox = nullptr;
  bool boxIsEmpty = true;
  bool componentSideBoxIsEmpty = true;
  bool solderSideBoxIsEmpty = true;
  bool innerSideBoxIsEmpty = true;

 public:
  /** Creates a new instance of DestinationDistance. */
  DestinationDistance(jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts,
                      jrt::Array<bool>* layerActive, double minNormalViaCost,
                      double minCheapViaCost);

 private:
  void considerInactiveInnerPlane(DestinationDistance_LowerBound* result,
                                  double innerSideMaxDelta, double innerSideMinDelta);

 public:
  /** Adds box on layer to the destination set. */
  virtual void join(IntBox* box, int32_t layer);

  /** Returns a lower bound for the distance from point on layer to the destination set. */
  virtual double calculate(FloatPoint* point, int32_t layer);

  /** (Java: package-private) */
  virtual double calculateWithPrepaidVia(FloatPoint* point, int32_t layer);

  /** Returns a lower bound for the distance from box on layer to the destination set. */
  virtual double calculate(IntBox* box, int32_t layer);

 private:
  DestinationDistance_LowerBound* lowerBound(IntBox* box, int32_t layer);

 public:
  /** Calculates cheap distance for box on layer using cheap via cost. */
  virtual double calculateCheapDistance(IntBox* box, int32_t layer);
};

}  // namespace fr
