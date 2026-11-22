#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains internal auxiliary functions of class RoutingBoard for shoving traces. */
class TraceShover : public virtual jrt::Object {
 private:
  RoutingBoard* board;

 public:
  explicit TraceShover(RoutingBoard* board) : board(board) {}

  /**
   * Checks if a shove with the input parameters is possible without clearance violations. The
   * result is the maximum length of a trace from the start of the line segment to the end of the
   * line segment, for which the algorithm succeeds. If the algorithm succeeds completely, the
   * result will be equal to Integer.MAX_VALUE.
   */
  static double check(RoutingBoard* board, LineSegment* lineSegment, bool shoveToTheLeft,
                      int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t traceHalfWidth,
                      int32_t clearanceClassIndex, int32_t maxRecursionDepth,
                      int32_t maxViaRecursionDepth);

  /**
   * Checks if a shove with the input parameters is possible without clearance violations. dir is
   * used internally to prevent the check from bouncing back. Returns false, if the shove failed.
   * fromSide, dir and timeLimit may be null.
   */
  virtual bool check(TileShape* traceShape, ShapeEntrySide* fromSide, Direction* dir,
                     int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                     int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                     int32_t maxSpringOverRecursionDepth, TimeLimit* timeLimit);

 private:
  bool measuredCheck(ShoveCheckCache* cache, ShoveCheckCache_Key* key, TileShape* traceShape,
                     ShapeEntrySide* fromSide, Direction* dir, int32_t layer,
                     jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                     int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                     int32_t maxSpringOverRecursionDepth, int64_t stamp);

  bool checkUncached(TileShape* traceShape, ShapeEntrySide* fromSide, Direction* dir,
                     int32_t layer, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                     int32_t maxRecursionDepth, int32_t maxViaRecursionDepth,
                     int32_t maxSpringOverRecursionDepth, TimeLimit* timeLimit);

 public:
  /**
   * Puts in a trace segment with the input parameters and shoves obstacles out of the way. If
   * the shove does not work, the database may be damaged. To prevent this, call check first.
   */
  virtual bool insert(TileShape* traceShape, ShapeEntrySide* fromSide, int32_t layer,
                      jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                      jrt::Collection<Item*>* ignoreItems, int32_t maxRecursionDepth,
                      int32_t maxViaRecursionDepth, int32_t maxSpringOverRecursionDepth);

  /** (Java package-private) */
  virtual jrt::Collection<Item*>* getIgnoreItemsAtTiePins(TileShape* traceShape, int32_t layer,
                                                          jrt::Array<int32_t>* netNumbers);

 private:
  /**
   * Checks, if there are obstacles in the way of polyline and tries to wrap the polyline trace
   * around these obstacles in counterclock sense. Returns null, if that is not possible. Returns
   * polyline, if there were no obstacles. If contactPins != null, all pins not contained in
   * contactPins are regarded as obstacles, even if they are of the own net.
   */
  Polyline* springOver(Polyline* polyline, int32_t halfWidth, int32_t layer,
                       jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                       bool overConnectedPins, int32_t recursionDepth,
                       jrt::Set<Pin*>* contactPins);

 public:
  /**
   * Like springOver, but looks for the shortest way around the obstacles (tries both senses).
   * Returns null if not possible, polyline if there were no obstacles.
   */
  virtual Polyline* springOverObstacles(Polyline* polyline, int32_t halfWidth, int32_t layer,
                                        jrt::Array<int32_t>* netNumbers,
                                        int32_t clearanceClassIndex,
                                        jrt::Set<Pin*>* contactPins);
};

}  // namespace fr
