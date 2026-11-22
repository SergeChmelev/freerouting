#pragma once

#include "autoroute/path/FoundConnectionLocator.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Calculates from the backtrack list the location of the traces and vias, which realize a
 * connection found by the maze search algorithm, for any-angle routing. (Java: package-private) */
class FoundConnectionLocatorAnyAngle : public FoundConnectionLocator {
 private:
  /** To avoid rounding errors in the calculation of tangential points. */
  static constexpr double cTolerance = 1.0;

 public:
  // PORT NOTE: protected in Java; public so that FoundConnectionLocator::getInstance can create it
  // (Java allows that through package access).
  FoundConnectionLocatorAnyAngle(MazeSearchEngine_Result* mazeSearchResult, AutorouteControl* ctrl,
                                 ShapeSearchTree* searchTree, AngleRestriction angleRestriction,
                                 jrt::TreeSet<Item*>* rippedItemList,
                                 jrt::Map<Item*, int32_t>* ripupCosts)
      : FoundConnectionLocator(mazeSearchResult, ctrl, searchTree, angleRestriction,
                               rippedItemList, ripupCosts) {}

 private:
  /** Calculates the left most corner of the shape of toInfo.door seen from the center of the
   * common room with the previous door. */
  static FloatPoint* calcDoorLeftCorner(FoundConnectionLocator_BacktrackElement* toInfo);

  /** Calculates the right most corner of the shape of toInfo.door seen from the center of the
   * common room with the previous door. */
  static FloatPoint* calcDoorRightCorner(FoundConnectionLocator_BacktrackElement* toInfo);

 protected:
  /** Calculates the corners of the next trace on the current layer. */
  jrt::Collection<FloatPoint*>* calculateNextTraceCorners() override;

 private:
  FloatPoint* rightTurnNextCorner(FloatPoint* fromCorner, double dist, FloatPoint* toCorner,
                                  FloatPoint* nextCorner);

  FloatPoint* leftTurnNextCorner(FloatPoint* fromCorner, double dist, FloatPoint* toCorner,
                                 FloatPoint* nextCorner);

  FloatPoint* rightLeftTangentialPoint(FloatPoint* fromPoint, FloatPoint* toPoint,
                                       FloatPoint* center, double dist);

  FloatPoint* leftRightTangentialPoint(FloatPoint* fromPoint, FloatPoint* toPoint,
                                       FloatPoint* center, double dist);
};

}  // namespace fr
