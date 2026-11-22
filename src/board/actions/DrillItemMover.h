// Port of app.freerouting.board.actions.DrillItemMover.
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains internal auxiliary functions of class RoutingBoard for shoving vias and pins. */
class DrillItemMover final : public virtual jrt::Object {
 private:
  DrillItemMover() {}

 public:
  /**
   * Checks, if drillItem can be translated by vector by shoving obstacle traces and vias aside, so
   * that no clearance violations occur.
   */
  static bool check(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                    int32_t maxViaRecursionDepth, jrt::Collection<Item*>* ignoreItems,
                    RoutingBoard* board, TimeLimit* timeLimit);

  /**
   * Translates drillItem by vector by shoving obstacle traces and vias aside, so that no clearance
   * violations occur. If tidyRegion != null, it will be joined by the bounding octagons of the
   * translated shapes.
   */
  static bool insert(DrillItem* drillItem, Vector* vector, int32_t maxRecursionDepth,
                     int32_t maxViaRecursionDepth, IntOctagon* tidyRegion, RoutingBoard* board);

  static bool insertJoiningTidyRegion(DrillItem* drillItem, Vector* vector,
                                      int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                                      jrt::Array<IntOctagon*>* tidyRegionHolder,
                                      RoutingBoard* board);

 private:
  static Shape* movedCopper(DrillItem* drillItem, int32_t index, Vector* vector,
                            ShapeSearchTree* tree);

 public:
  /**
   * Shoves vias out of obstacleShape. Returns false, if the database is damaged, so that an undo is
   * necessary afterwards.
   */
  static bool shoveVias(TileShape* obstacleShape, ShapeEntrySide* fromSide, int32_t layer,
                        jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                        jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                        int32_t maxViaRecursionDepth, bool copperSharingAllowed,
                        RoutingBoard* board);

  /** Shoves vias out of obstacleShape with exactShape copper. */
  static bool shoveVias(TileShape* obstacleShape, ShapeEntrySide* fromSide, int32_t layer,
                        jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                        jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                        int32_t maxViaRecursionDepth, bool copperSharingAllowed,
                        RoutingBoard* board, Shape* exactShape);

  /**
   * Calculates possible new location for a via to shove outside obstacleShape. if extendedCheck is
   * true, more than 1 possible new locations are calculated.
   */
  static jrt::Array<IntPoint*>* tryShoveViaPoints(TileShape* obstacleShape, int32_t layer, Via* via,
                                                  int32_t clearanceClassIndex, bool extendedCheck,
                                                  RoutingBoard* board);
};

}  // namespace fr
