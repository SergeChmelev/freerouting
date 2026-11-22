// Port of app.freerouting.board.actions.ForcedViaInserter.
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Class with static functions for checking and inserting forced vias. */
class ForcedViaInserter final : public virtual jrt::Object {
 private:
  ForcedViaInserter() {}

 public:
  /**
   * Checks, if a Via is possible at the input layer after evtl. shoving aside obstacle traces.
   * roomShape is used for calculating the fromSide.
   */
  static ForcedPadRouter_CheckDrillResult checkLayer(
      double viaRadius, int32_t clearanceClassIndex, bool attachSmdAllowed, TileShape* roomShape,
      Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t maxRecursionDepth,
      int32_t maxViaRecursionDepth, RoutingBoard* board, int32_t traceHalfWidth,
      int32_t traceClearanceClass);

  /** As above; with circularCopper the via copper is checked as an exact circle. */
  static ForcedPadRouter_CheckDrillResult checkLayer(
      double viaRadius, int32_t clearanceClassIndex, bool attachSmdAllowed, TileShape* roomShape,
      Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t maxRecursionDepth,
      int32_t maxViaRecursionDepth, RoutingBoard* board, int32_t traceHalfWidth,
      int32_t traceClearanceClass, bool circularCopper);

  /** Checks, if a Via is possible with the input parameter after evtl. shoving aside obstacle traces. */
  static bool check(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                    int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, RoutingBoard* board,
                    jrt::Array<int32_t>* tracePenHalfwidthArr, int32_t traceClearanceClassIndex);

  /**
   * As above. attachSmdAllowed (Java Boolean, nullable) overrides the via rule's flag when
   * non-null.
   */
  static bool check(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                    int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, RoutingBoard* board,
                    jrt::Array<int32_t>* tracePenHalfwidthArr, int32_t traceClearanceClassIndex,
                    std::optional<bool> attachSmdAllowed);

  /**
   * Shoves aside traces, so that a via with the input parameters can be inserted without
   * clearance violations. If the shove failed, the database may be damaged, so that an undo
   * becomes necessary.
   */
  static bool insert(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                     int32_t traceClearanceClassIndex, jrt::Array<int32_t>* tracePenHalfwidthArr,
                     int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, RoutingBoard* board);

  /** As above; attachSmdAllowed (nullable) overrides the via rule's flag when non-null. */
  static bool insert(ViaInfo* viaInfo, Point* location, jrt::Array<int32_t>* netNumbers,
                     int32_t traceClearanceClassIndex, jrt::Array<int32_t>* tracePenHalfwidthArr,
                     int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, RoutingBoard* board,
                     std::optional<bool> attachSmdAllowed);

  /** Whether a via of viaPadstack at location has its drill hole overlapping an own-net SMD pad. */
  static bool holeInSmdPad(RoutingBoard* board, Padstack* viaPadstack, Point* location,
                           jrt::Array<int32_t>* netNumbers);

 private:
  /** Prepares new pad geometry for the same tree used by ForcedPadRouter and DrillItemMover. */
  static TileShape* compensatePadShape(TileShape* shape, int32_t clearanceClassIndex, int32_t layer,
                                       RoutingBoard* board);

  /**
   * Hole-clearance substitute shape for a copper-less layer of a via padstack: the drill still
   * passes through, so other copper must stay holeClearance away from it.
   */
  static Shape* holeCheckShape(Padstack* padstack, Point* location, RoutingBoard* board);

  static ShapeEntrySide* calculateFromSide(FloatPoint* viaLocation, TileShape* viaShape,
                                           Simplex* roomShape, double dist, bool is90Degree);

 public:
  /**
   * The fanout's check whether a via of viaPadstack and class viaClearanceClass at location comes
   * inside the clearance of a pad of a foreign net.
   */
  static bool crowdsOwnPad(RoutingBoard* board, Padstack* viaPadstack, int32_t viaClearanceClass,
                           Point* location, int32_t netNumber);
};

}  // namespace fr
