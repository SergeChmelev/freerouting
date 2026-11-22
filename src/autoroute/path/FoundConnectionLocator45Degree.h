#pragma once

#include "autoroute/path/FoundConnectionLocator.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Calculates from the backtrack list the location of the traces and vias, which realize a
 * connection found by the maze search algorithm, for 90 and 45 degree routing. */
class FoundConnectionLocator45Degree : public FoundConnectionLocator {
 public:
  /** Creates a new instance (first construction phase; see FoundConnectionLocator). */
  FoundConnectionLocator45Degree(MazeSearchEngine_Result* mazeSearchResult, AutorouteControl* ctrl,
                                 ShapeSearchTree* searchTree, AngleRestriction angleRestriction,
                                 jrt::TreeSet<Item*>* rippedItemList,
                                 jrt::Map<Item*, int32_t>* ripupCosts)
      : FoundConnectionLocator(mazeSearchResult, ctrl, searchTree, angleRestriction,
                               rippedItemList, ripupCosts) {}

 private:
  static FloatPoint* roundToInteger(FloatPoint* point);

  static bool calcHorizontalFirstFromDoor(ExpandableObject* fromDoor, FloatPoint* fromPoint,
                                          FloatPoint* toPoint);

 protected:
  /** Calculates the corners of the next trace on the current layer. */
  jrt::Collection<FloatPoint*>* calculateNextTraceCorners() override;

 private:
  bool calcHorizontalFirstToDoor(ExpandableObject* toDoor, FloatPoint* fromPoint,
                                 FloatPoint* toPoint);
};

}  // namespace fr
