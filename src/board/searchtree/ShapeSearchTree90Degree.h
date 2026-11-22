#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "board/searchtree/ShapeSearchTree.h"

namespace fr {

/**
 * A special simple ShapeSearchtree, where the shapes are of class IntBox. It is used in the
 * 90-degree autorouter algorithm.
 */
class ShapeSearchTree90Degree : public ShapeSearchTree {
 public:
  using ShapeSearchTree::calculateTreeShapes;

  /**
   * Creates a new instance of ShapeSearchTree90Degree (OrthogonalBoundingDirections). PORT NOTE:
   * recomputes `key` in its body (see ShapeSearchTree::key).
   */
  ShapeSearchTree90Degree(BasicBoard* board, int32_t compensatedClearanceClassNo);

  /**
   * Calculates a new incomplete room with a maximal TileShape contained in the shape of room, which
   * may overlap only with items of the input net on the input layer. Obstacles are processed inline
   * during tree traversal with a shrinking bounding shape (v1.9 semantics).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShape(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape) override;

 private:
  // PORT NOTE: hides the private ShapeSearchTree methods of the same name, as in Java.
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShapeUnlocked(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape);

  /**
   * Restrains the shape of incompleteRoom to a box shape, which does not intersect with the
   * interior of obstacleShape (recursive).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* restrainShape(
      IncompleteFreeSpaceExpansionRoom* incompleteRoom, IntBox* obstacleShape);

  static std::string describeBounds(IntBox* bounds);

  /** Returns true for the specific room being diagnosed in a parity investigation. */
  static bool isCompleteShapeDebugAnchor(int32_t netNumber, int32_t roomLayer, IntBox* startShape);

  static void traceCompleteShapeFilter(int32_t step, int32_t netNumber, int32_t roomLayer,
                                       int32_t shapeIndex, int32_t objectLayer, bool isObstacle,
                                       bool sameLayer, bool ignoredObject,
                                       SearchTreeObject* object);

  static void traceCompleteShapeCandidate(int32_t step, int32_t netNumber, int32_t roomLayer,
                                          SearchTreeObject* object, IntBox* obstacleShape);

  static void traceCompleteShapeDecision(int32_t step, int32_t netNumber, int32_t roomLayer,
                                         const std::string& action, bool overlap,
                                         IntBox* roomShape, IntBox* obstacleShape);

  static int32_t obstacleId(SearchTreeObject* object);

  static std::string obstacleNets(SearchTreeObject* object);

 public:
  jrt::Array<TileShape*>* calculateTreeShapes(DrillItem* drillItem) override;

  jrt::Array<TileShape*>* calculateTreeShapes(ObstacleArea* obstacleArea) override;

  jrt::Array<TileShape*>* calculateTreeShapes(BoardOutline* outline) override;

  /** Used for creating the shapes of a polyline_trace for this tree. */
  TileShape* offsetShape(Polyline* polyline, int32_t halfWidth, int32_t no) override;

  /** Used for creating the shapes of a polyline_trace for this tree. */
  jrt::Array<TileShape*>* offsetShapes(Polyline* polyline, int32_t halfWidth, int32_t fromNo,
                                       int32_t toNo) override;
};

}  // namespace fr
