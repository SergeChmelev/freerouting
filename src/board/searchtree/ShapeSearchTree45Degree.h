#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "board/searchtree/ShapeSearchTree.h"

namespace fr {

/**
 * A special simple ShapeSearchtree, where the shapes are of class IntOctagon. It is used in the
 * 45-degree autorouter algorithm.
 */
class ShapeSearchTree45Degree : public ShapeSearchTree {
 public:
  using ShapeSearchTree::calculateTreeShapes;

  /**
   * Creates a new instance of ShapeSearchTree45Degree (FortyfiveDegreeBoundingDirections). PORT
   * NOTE: recomputes `key` in its body (see ShapeSearchTree::key).
   */
  ShapeSearchTree45Degree(BasicBoard* board, int32_t compensatedClearanceClassNo);

 private:
  /**
   * Checks, if the border line segment with index obstacleBorderLineNo intersects with the inside
   * of roomShape.
   */
  static bool obstacleSegmentTouchesInside(IntOctagon* obstacleShape,
                                           int32_t obstacleBorderLineNo, IntOctagon* roomShape);

  static double signedLineDistance(IntOctagon* obstacleShape, int32_t obstacleLineNo,
                                   IntOctagon* containedShape);

 public:
  /**
   * Calculates a new incomplete room with a maximal TileShape contained in the shape of room, which
   * may overlap only with items of the input net on the input layer. If that is not possible,
   * several rooms are returned. The result room is not yet complete (doors not calculated).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShape(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape) override;

 private:
  // PORT NOTE: hides the private ShapeSearchTree methods of the same name, as in Java.
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShapeUnlocked(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape);

 protected:
  /**
   * Makes sure that on each layer there will be more than 1 IncompleteFreeSpaceExpansionRoom (the
   * sections are turned into bounding octagons).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* divideLargeRoom(
      jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* roomList,
      IntBox* boardBoundingBox) override;

 private:
  /**
   * Restrains the shape of incompleteRoom to an octagon shape, which does not intersect with the
   * interior of obstacleShape (recursive).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* restrainShape(
      IncompleteFreeSpaceExpansionRoom* incompleteRoom, IntOctagon* obstacleShape);

  /** PORT NOTE: restrainShape appending its rooms to `result` (same rooms, same order) instead
   * of returning a new collection per (recursive) call. */
  void restrainShapeInto(IncompleteFreeSpaceExpansionRoom* incompleteRoom,
                         IntOctagon* obstacleShape,
                         jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>* result);

 public:
  /**
   * Intersects roomShape with the half plane defined by the outside of the borderline with index
   * obstacleLineNo of obstacleShape. (Java package-private.)
   */
  virtual IntOctagon* calcOutsideRestrainedShape(IntOctagon* obstacleShape, int32_t obstacleLineNo,
                                                 IntOctagon* roomShape);

  /**
   * Intersects roomShape with the half plane defined by the inside of the borderline with index
   * obstacleLineNo of obstacleShape. (Java package-private.)
   */
  virtual IntOctagon* calcInsideRestrainedShape(IntOctagon* obstacleShape, int32_t obstacleLineNo,
                                                IntOctagon* roomShape);

  jrt::Array<TileShape*>* calculateTreeShapes(DrillItem* drillItem) override;

  jrt::Array<TileShape*>* calculateTreeShapes(ObstacleArea* obstacleArea) override;

  jrt::Array<TileShape*>* calculateTreeShapes(BoardOutline* outline) override;

 private:
  static std::string describeBounds(IntBox* bounds);

  /** Returns true for the specific room being diagnosed in a parity investigation. */
  static bool isCompleteShapeDebugAnchor(int32_t netNumber, int32_t roomLayer,
                                         IntOctagon* startShape);

  static void traceCompleteShapeFilter(int32_t step, int32_t netNumber, int32_t roomLayer,
                                       int32_t shapeIndex, int32_t objectLayer, bool isObstacle,
                                       bool sameLayer, bool ignoredObject,
                                       SearchTreeObject* object);

  static void traceCompleteShapeCandidate(int32_t step, int32_t netNumber, int32_t roomLayer,
                                          SearchTreeObject* object, IntOctagon* obstacleShape);

  static void traceCompleteShapeDecision(int32_t step, int32_t netNumber, int32_t roomLayer,
                                         const std::string& action, bool overlap,
                                         IntOctagon* roomShape, IntOctagon* obstacleShape);

  static int32_t obstacleId(SearchTreeObject* object);

  static std::string obstacleNets(SearchTreeObject* object);
};

}  // namespace fr
