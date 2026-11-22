// Port of app.freerouting.board.actions.ForcedPadRouter (+ nested enum CheckDrillResult).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** CheckDrillResult. */
enum class ForcedPadRouter_CheckDrillResult : int32_t {
  DRILLABLE,
  DRILLABLE_WITH_ATTACH_SMD,
  NOT_DRILLABLE
};

inline std::string ForcedPadRouter_CheckDrillResult_name(ForcedPadRouter_CheckDrillResult e) {
  static const char* const kNames[] = {"DRILLABLE", "DRILLABLE_WITH_ATTACH_SMD", "NOT_DRILLABLE"};
  return kNames[static_cast<int32_t>(e)];
}
inline std::string ForcedPadRouter_CheckDrillResult_toString(ForcedPadRouter_CheckDrillResult e) {
  return ForcedPadRouter_CheckDrillResult_name(e);
}
inline jrt::Array<ForcedPadRouter_CheckDrillResult>* ForcedPadRouter_CheckDrillResult_values() {
  using E = ForcedPadRouter_CheckDrillResult;
  return jrt::Array<E>::of({E::DRILLABLE, E::DRILLABLE_WITH_ATTACH_SMD, E::NOT_DRILLABLE});
}
inline ForcedPadRouter_CheckDrillResult ForcedPadRouter_CheckDrillResult_valueOf(const std::string& s) {
  for (auto e : *ForcedPadRouter_CheckDrillResult_values()) {
    if (ForcedPadRouter_CheckDrillResult_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.board.actions.ForcedPadRouter.CheckDrillResult." + s);
}

/**
 * Class with functions for checking and inserting pads with eventually shoving aside obstacle
 * traces.
 */
class ForcedPadRouter : public virtual jrt::Object {
 private:
  RoutingBoard* board;  // final

 public:
  /** Creates a new instance of ForcedPadAlgo. */
  explicit ForcedPadRouter(RoutingBoard* board) : board(board) {}

 private:
  static TileShape* calcCheckShapeForFromSide(TileShape* shape, Point* shapeCenter, Line* borderLine);

 public:
  /** Checks, if line is in front of padShape when shoving from fromSide. */
  static bool inFrontOfPad(Line* line, TileShape* padShape, int32_t fromSide, int32_t width,
                           bool withSides);

  /**
   * Checks, if possible obstacle traces can be shoved aside, so that a pad with the input
   * parameters can be inserted without clearance violations. Returns false, if the check failed.
   * If ignoreItems != null, items in this list are not checked. If checkOnlyFront only trace
   * obstacles in the direction from fromSide are checked for performance reasons.
   */
  virtual ForcedPadRouter_CheckDrillResult checkForcedPad(
      TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
      int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
      int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
      TimeLimit* timeLimit);

  /** As above; holeOnly: the pad is only a drill hole (copper-less layer of a via). */
  virtual ForcedPadRouter_CheckDrillResult checkForcedPad(
      TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
      int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
      int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
      TimeLimit* timeLimit, bool holeOnly);

  /** As above; exactPadShape is the exact copper of the pad (may be null). */
  virtual ForcedPadRouter_CheckDrillResult checkForcedPad(
      TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer, jrt::Array<int32_t>* netNumbers,
      int32_t clearanceClassIndex, bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
      int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool checkOnlyFront,
      TimeLimit* timeLimit, bool holeOnly, Shape* exactPadShape);

  // ---- package-private in Java ----

  /**
   * Shoves aside traces, so that a pad with the input parameters can be inserted without clearance
   * violations. Returns false, if the shove failed. In this case the database may be damaged, so
   * that an undo becomes necessary.
   */
  virtual bool forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                         jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                         bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                         int32_t maxRecursionDepth, int32_t maxViaRecursionDepth);

  virtual bool forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                         jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                         bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                         int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool holeOnly);

  virtual bool forcedPad(TileShape* padShape, ShapeEntrySide* fromSide, int32_t layer,
                         jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                         bool copperSharingAllowed, jrt::Collection<Item*>* ignoreItems,
                         int32_t maxRecursionDepth, int32_t maxViaRecursionDepth, bool holeOnly,
                         Shape* exactPadShape);

  /**
   * Looks for a side of shape, so that a trace line from the shape center to the nearest point on
   * this side does not conflict with any obstacles.
   */
  virtual ShapeEntrySide* calcFromSide(TileShape* shape, Point* shapeCenter, int32_t layer,
                                       int32_t offset, int32_t clearanceClassIndex);
};

}  // namespace fr
