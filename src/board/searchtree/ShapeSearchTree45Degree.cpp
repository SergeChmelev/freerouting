// Port of app.freerouting.board.searchtree.ShapeSearchTree45Degree
#include "board/searchtree/ShapeSearchTree45Degree.h"

#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/structure/BoardOutline.h"
#include "board/searchtree/SearchTreeObject.h"
#include "datastructures/ArrayStack.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FortyfiveDegreeBoundingDirections.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {

/** Java `finally { releaseScratchStack(stack); }`. */
struct ReleaseStackOnExit45 {
  ArrayStack<ShapeTree_TreeNode*>* stack;
  void (*release)(ArrayStack<ShapeTree_TreeNode*>*);
  ~ReleaseStackOnExit45() { release(stack); }
};

}  // namespace

ShapeSearchTree45Degree::ShapeSearchTree45Degree(BasicBoard* board,
                                                 int32_t compensatedClearanceClassNo)
    : ShapeSearchTree(FortyfiveDegreeBoundingDirections::INSTANCE(), board,
                      compensatedClearanceClassNo) {
  // PORT NOTE: Java computes key in the base constructor with the runtime class.
  key = getKey(this, FortyfiveDegreeBoundingDirections::INSTANCE(), compensatedClearanceClassNo);
}

bool ShapeSearchTree45Degree::obstacleSegmentTouchesInside(IntOctagon* obstacleShape,
                                                           int32_t obstacleBorderLineNo,
                                                           IntOctagon* roomShape) {
  int32_t currentBorderLineNo = obstacleBorderLineNo;
  int32_t currentObstacleCornerX = obstacleShape->cornerX(obstacleBorderLineNo);
  int32_t currentObstacleCornerY = obstacleShape->cornerY(obstacleBorderLineNo);
  for (int32_t j = 0; j < 5; j++) {
    if (roomShape->sideOfBorderLine(currentObstacleCornerX, currentObstacleCornerY,
                                    currentBorderLineNo) != Side::ON_THE_LEFT()) {
      return false;
    }
    currentBorderLineNo = (currentBorderLineNo + 1) % 8;
  }

  int32_t nextObstacleBorderLineNo = (obstacleBorderLineNo + 1) % 8;
  int32_t nextObstacleCornerX = obstacleShape->cornerX(nextObstacleBorderLineNo);
  int32_t nextObstacleCornerY = obstacleShape->cornerY(nextObstacleBorderLineNo);
  currentBorderLineNo = (obstacleBorderLineNo + 5) % 8;
  for (int32_t j = 0; j < 3; j++) {
    if (roomShape->sideOfBorderLine(nextObstacleCornerX, nextObstacleCornerY,
                                    currentBorderLineNo) != Side::ON_THE_LEFT()) {
      return false;
    }
    currentBorderLineNo = (currentBorderLineNo + 1) % 8;
  }
  return true;
}

double ShapeSearchTree45Degree::signedLineDistance(IntOctagon* obstacleShape,
                                                   int32_t obstacleLineNo,
                                                   IntOctagon* containedShape) {
  switch (obstacleLineNo) {
    case 0:
      return obstacleShape->bottomY - containedShape->topY;
    case 2:
      return containedShape->leftX - obstacleShape->rightX;
    case 4:
      return containedShape->bottomY - obstacleShape->topY;
    case 6:
      return obstacleShape->leftX - containedShape->rightX;

    // factor 0.5 used instead to 1 / sqrt(2) to prefer orthogonal lines slightly to diagonal
    // restraining lines.
    case 1:
      return 0.5 * (containedShape->upperLeftDiagonalX - obstacleShape->lowerRightDiagonalX);
    case 3:
      return 0.5 * (containedShape->lowerLeftDiagonalX - obstacleShape->upperRightDiagonalX);
    case 5:
      return 0.5 * (obstacleShape->upperLeftDiagonalX - containedShape->lowerRightDiagonalX);
    case 7:
      return 0.5 * (obstacleShape->lowerLeftDiagonalX - containedShape->upperRightDiagonalX);
    default: {
      FRLogger::warn("ShapeSearchTree45Degree.signed_line_distance: obstacleLineNo out of range");
      return 0;
    }
  }
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree45Degree::completeShape(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  return completeShapeUnlocked(room, netNumber, ignoreObject, ignoreShape);
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree45Degree::completeShapeUnlocked(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  TileShape* containedRaw = room->getContainedShape();
  if (containedRaw == nullptr) {
    FRLogger::warn(
        "ShapeSearchTree45Degree.completeShape: contained shape is null,"
        " skipping expansion room");
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }
  if (!containedRaw->isIntOctagon()) {
    // The contained shape is not an IntOctagon (e.g. a Simplex from a non-45° trace segment).
    // Use the bounding octagon as a safe conservative approximation so the expansion room is
    // not silently discarded, which was previously causing incomplete routing connections.
    FRLogger::debug(
        "ShapeSearchTree45Degree.completeShape: non-IntOctagon contained shape,"
        " using bounding octagon approximation");
  }
  IntOctagon* shapeToBeContained = containedRaw->boundingOctagon();
  if (shapeToBeContained == nullptr) {
    // boundingOctagon() returned null — this can happen for empty/degenerate shapes (e.g. a
    // zero-length trace segment). Discard the expansion room gracefully rather than throw NPE.
    FRLogger::debug(
        "ShapeSearchTree45Degree.completeShape: boundingOctagon() returned null"
        " for contained shape of type " +
        jrt::className(containedRaw) + ", skipping expansion room");
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }

  if (this->root == nullptr) {
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }

  IntOctagon* startShape = board->getBoundingBox()->boundingOctagon();
  if (room->getShape() != nullptr) {
    if (jrt::fast_cast<IntOctagon>(room->getShape()) == nullptr) {
      FRLogger::warn(
          "ShapeSearchTree45Degree.complete_shape: startShape of type IntOctagon expected");
      return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
    }
    startShape = room->getShape()->boundingOctagon()->intersection(startShape);
  }

  IntOctagon* boundingShape = startShape;
  int32_t roomLayer = room->getLayer();
  bool debugAnchor = isCompleteShapeDebugAnchor(netNumber, roomLayer, startShape);
  int32_t debugStep = 0;
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
  result->add(new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(startShape, roomLayer, shapeToBeContained));
  ArrayStack<ShapeTree_TreeNode*>* stack = acquireScratchStack();
  {
    ReleaseStackOnExit45 guard{stack, &MinAreaTree::releaseScratchStack};
    stack->push(this->root);
    ShapeTree_TreeNode* currentNode;

    for (;;) {
      currentNode = stack->pop();
      if (currentNode == nullptr) {
        break;
      }
      if (currentNode->boundingShape->intersects(boundingShape)) {
        if (auto* currentLeaf = jrt::fast_cast<ShapeTree_Leaf>(currentNode)) {
          SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
          int32_t shapeIndex = currentLeaf->shapeIndexInObject;
          bool isObstacle = currentObject->isTraceObstacle(netNumber, shapeIndex);
          int32_t objectLayer = currentObject->shapeLayer(shapeIndex);
          bool sameLayer = objectLayer == roomLayer;
          bool ignoredObject = currentObject == ignoreObject;
          if (debugAnchor) {
            traceCompleteShapeFilter(debugStep, netNumber, roomLayer, shapeIndex, objectLayer,
                                     isObstacle, sameLayer, ignoredObject, currentObject);
          }
          if (isObstacle && sameLayer && !ignoredObject) {
            IntOctagon* currentObjectShape =
                currentObject->getTreeShape(this, shapeIndex)->boundingOctagon();
            if (debugAnchor) {
              traceCompleteShapeCandidate(debugStep, netNumber, roomLayer, currentObject,
                                          currentObjectShape);
            }
            if (!debugAnchor) {
              bool overlapsAny = false;
              for (IncompleteFreeSpaceExpansionRoom* roomPiece : *result) {
                if (jrt::fast_cast<IntOctagon>(roomPiece->getShape())
                        ->overlaps(currentObjectShape)) {
                  overlapsAny = true;
                  break;
                }
              }
              if (!overlapsAny) {
                continue;
              }
            }
            jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newResult =
                new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
            IntOctagon* newBoundingShape = IntOctagon::EMPTY();
            bool hadRoomsBeforeObstacle = !result->isEmpty();
            for (IncompleteFreeSpaceExpansionRoom* currentRoom : *result) {
              IntOctagon* currentShape = jrt::fast_cast<IntOctagon>(currentRoom->getShape());
              bool overlaps = currentShape->overlaps(currentObjectShape);
              if (overlaps) {
                if (jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(currentObject) != nullptr &&
                    ignoreShape != nullptr) {
                  IntOctagon* intersection = currentShape->intersection(currentObjectShape);
                  if (ignoreShape->contains(static_cast<TileShape*>(intersection))) {
                    if (debugAnchor) {
                      traceCompleteShapeDecision(debugStep, netNumber, roomLayer,
                                                 "SKIP_BY_IGNORE_SHAPE", overlaps, currentShape,
                                                 currentObjectShape);
                    }
                    // ignore also all objects, whose intersection is contained in the
                    // 2-dim overlap-door with the fromRoom.
                    if (!ignoreShape->contains(static_cast<TileShape*>(currentShape))) {
                      newResult->add(currentRoom);
                      newBoundingShape = newBoundingShape->union_(currentShape);
                    }
                    continue;
                  }
                }
                if (debugAnchor) {
                  traceCompleteShapeDecision(debugStep, netNumber, roomLayer, "RESTRAIN",
                                             overlaps, currentShape, currentObjectShape);
                }
                jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newRestrainedShapes =
                    restrainShape(currentRoom, currentObjectShape);
                newResult->addAll(newRestrainedShapes);

                for (IncompleteFreeSpaceExpansionRoom* tmpShape : *newRestrainedShapes) {
                  newBoundingShape =
                      newBoundingShape->union_(jrt::fast_cast<IntOctagon>(tmpShape->getShape()));
                }
              } else {
                if (debugAnchor) {
                  traceCompleteShapeDecision(debugStep, netNumber, roomLayer, "KEEP_NON_OVERLAP",
                                             overlaps, currentShape, currentObjectShape);
                }
                newResult->add(currentRoom);
                newBoundingShape = newBoundingShape->union_(currentShape);
              }
            }
            if (hadRoomsBeforeObstacle && newResult->isEmpty()) {
              if (FRLogger::isTraceEnabled()) {
                FRLogger::trace("COMPLETE_SHAPE_BLOCKED net=" + jrt::str(netNumber) +
                                ", layer=" + jrt::str(roomLayer) + ", contained=" +
                                describeBounds(shapeToBeContained->boundingBox()) +
                                ", obstacle_type=" + jrt::className(currentObject) +
                                ", obstacle_id=" + jrt::str(obstacleId(currentObject)) +
                                ", obstacle_bounds=" +
                                describeBounds(currentObjectShape->boundingBox()));
              }
            }
            result = newResult;
            if (result->isEmpty()) {
              // Erik Gavriluk (freerouting.net): with no piece left, the empty union reaches no
              // node.
              // Stop the walk early rather than continuing to pop and reject remaining nodes.
              break;
            }
            boundingShape = newBoundingShape;
          }
          if (debugAnchor) {
            debugStep++;
          }
        } else {
          ShapeTree_TreeNode* first = static_cast<ShapeTree_InnerNode*>(currentNode)->firstChild;
          ShapeTree_TreeNode* second = static_cast<ShapeTree_InnerNode*>(currentNode)->secondChild;
          if (first != nullptr && first->boundingShape->intersects(boundingShape)) {
            stack->push(first);
          }
          if (second != nullptr && second->boundingShape->intersects(boundingShape)) {
            stack->push(second);
          }
        }
      }
    }
  }

  result = divideLargeRoom(result, board->getBoundingBox());
  // remove rooms with shapes equal to the contained shape to prevent endless loop.
  result->removeIf([](IncompleteFreeSpaceExpansionRoom* expansionRoom) {
    return expansionRoom->getContainedShape()->contains(expansionRoom->getShape());
  });
  return result;
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree45Degree::divideLargeRoom(
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* roomList, IntBox* boardBoundingBox) {
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      ShapeSearchTree::divideLargeRoom(roomList, boardBoundingBox);
  for (IncompleteFreeSpaceExpansionRoom* currentRoom : *result) {
    currentRoom->setShape(currentRoom->getShape()->boundingOctagon());
    currentRoom->setContainedShape(currentRoom->getContainedShape()->boundingOctagon());
  }
  return result;
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree45Degree::restrainShape(
    IncompleteFreeSpaceExpansionRoom* incompleteRoom, IntOctagon* obstacleShape) {
  // Search the edge line of obstacleShape, so that shapeToBeContained
  // are on the right side of this line, and that the line segment
  // intersects with the interior of shape.
  // If there are more than 1 such lines take the line which is
  // furthest away from the shapeToBeContained
  // Then intersect shape with the halfplane defined by the
  // opposite of this line.

  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();

  TileShape* containedShape = incompleteRoom->getContainedShape();
  if (containedShape == nullptr || containedShape->isEmpty()) {
    FRLogger::debug("ShapeSearchTree45Degree.restrain_shape: shapeToBeContained is empty");
    return result;
  }
  IntOctagon* shapeToBeContained;
  if (containedShape->isIntOctagon()) {
    shapeToBeContained = containedShape->boundingOctagon();
  } else if (jrt::fast_cast<Simplex>(containedShape) != nullptr) {
    shapeToBeContained = containedShape->boundingOctagon();
    if (shapeToBeContained == nullptr) {
      FRLogger::warn("restrain_shape: cannot convert Simplex to IntOctagon");
      return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
    }
  } else {
    FRLogger::warn("restrain_shape: incompatible shape type");
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }

  IntOctagon* roomShape;
  if (jrt::fast_cast<IntOctagon>(incompleteRoom->getShape()) != nullptr) {
    roomShape = incompleteRoom->getShape()->boundingOctagon();
  } else if (jrt::fast_cast<Simplex>(incompleteRoom->getShape()) != nullptr) {
    roomShape = incompleteRoom->getShape()->boundingOctagon();
    if (roomShape == nullptr) {
      FRLogger::warn("restrain_shape: cannot convert room shape Simplex to IntOctagon");
      return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
    }
  } else {
    FRLogger::warn("restrain_shape: unsupported room shape type");
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }

  double cutLineDistance = -1;
  int32_t restrainingLineNo = -1;

  for (int32_t obstacleLineNo = 0; obstacleLineNo < 8; obstacleLineNo++) {
    double currentDistance = signedLineDistance(obstacleShape, obstacleLineNo, shapeToBeContained);
    if (currentDistance > cutLineDistance) {
      if (obstacleSegmentTouchesInside(obstacleShape, obstacleLineNo, roomShape)) {
        cutLineDistance = currentDistance;
        restrainingLineNo = obstacleLineNo;
      }
    }
  }
  if (cutLineDistance >= 0) {
    IntOctagon* restrainedShape =
        calcOutsideRestrainedShape(obstacleShape, restrainingLineNo, roomShape);
    result->add(new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(restrainedShape, incompleteRoom->getLayer(),
                                                     shapeToBeContained));
    return result;
  }

  // There is no cut line, so that all shapeToBeContained is
  // completely on the right side of that line. Search a cut line, so that
  // at least part of shapeToBeContained is on the right side.
  if (shapeToBeContained->dimension() < 1) {
    // There is already a completed expansion room around shapeToBeContained.
    return result;
  }

  restrainingLineNo = -1;
  for (int32_t obstacleLineNo = 0; obstacleLineNo < 8; obstacleLineNo++) {
    if (obstacleSegmentTouchesInside(obstacleShape, obstacleLineNo, roomShape)) {
      Line* currentLine = obstacleShape->borderLine(obstacleLineNo);
      if (shapeToBeContained->sideOf(currentLine) == Side::COLLINEAR()) {
        // currentLine intersects with the interior of shapeToBeContained
        restrainingLineNo = obstacleLineNo;
        break;
      }
    }
  }
  if (restrainingLineNo < 0) {
    // cut line not found, parts or the whole of shape may be already
    // occupied from somewhere else.
    return result;
  }
  IntOctagon* restrainedShape =
      calcOutsideRestrainedShape(obstacleShape, restrainingLineNo, roomShape);
  if (restrainedShape->dimension() == 2) {
    IntOctagon* newShapeToBeContained = shapeToBeContained->intersection(restrainedShape);
    if (newShapeToBeContained->dimension() > 0) {
      result->add(new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(
          restrainedShape, incompleteRoom->getLayer(), newShapeToBeContained));
    }
  }

  IntOctagon* restPiece = calcInsideRestrainedShape(obstacleShape, restrainingLineNo, roomShape);
  if (restPiece->dimension() >= 2) {
    TileShape* restShapeToBeContained = shapeToBeContained->intersection(restPiece);
    if (restShapeToBeContained->dimension() >= 0) {
      IncompleteFreeSpaceExpansionRoom* restIncompleteRoom = new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(
          restPiece, incompleteRoom->getLayer(), restShapeToBeContained);
      result->addAll(restrainShape(restIncompleteRoom, obstacleShape));
    }
  }
  return result;
}

IntOctagon* ShapeSearchTree45Degree::calcOutsideRestrainedShape(IntOctagon* obstacleShape,
                                                                int32_t obstacleLineNo,
                                                                IntOctagon* roomShape) {
  int32_t lx = roomShape->leftX;
  int32_t ly = roomShape->bottomY;
  int32_t rx = roomShape->rightX;
  int32_t uy = roomShape->topY;
  int32_t ulx = roomShape->upperLeftDiagonalX;
  int32_t lrx = roomShape->lowerRightDiagonalX;
  int32_t llx = roomShape->lowerLeftDiagonalX;
  int32_t urx = roomShape->upperRightDiagonalX;

  switch (obstacleLineNo) {
    case 0:
      uy = obstacleShape->bottomY;
      break;
    case 2:
      lx = obstacleShape->rightX;
      break;
    case 4:
      ly = obstacleShape->topY;
      break;
    case 6:
      rx = obstacleShape->leftX;
      break;
    case 1:
      ulx = obstacleShape->lowerRightDiagonalX;
      break;
    case 3:
      llx = obstacleShape->upperRightDiagonalX;
      break;
    case 5:
      lrx = obstacleShape->upperLeftDiagonalX;
      break;
    case 7:
      urx = obstacleShape->lowerLeftDiagonalX;
      break;
    default:
      FRLogger::warn(
          "ShapeSearchTree45Degree.calc_outside_restrained_shape:"
          " obstacleLineNo out of range");
  }

  IntOctagon* result = new (jrt::kGeom) IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
  return result->normalize();
}

IntOctagon* ShapeSearchTree45Degree::calcInsideRestrainedShape(IntOctagon* obstacleShape,
                                                               int32_t obstacleLineNo,
                                                               IntOctagon* roomShape) {
  int32_t lx = roomShape->leftX;
  int32_t ly = roomShape->bottomY;
  int32_t rx = roomShape->rightX;
  int32_t uy = roomShape->topY;
  int32_t ulx = roomShape->upperLeftDiagonalX;
  int32_t lrx = roomShape->lowerRightDiagonalX;
  int32_t llx = roomShape->lowerLeftDiagonalX;
  int32_t urx = roomShape->upperRightDiagonalX;

  switch (obstacleLineNo) {
    case 0:
      ly = obstacleShape->bottomY;
      break;
    case 2:
      rx = obstacleShape->rightX;
      break;
    case 4:
      uy = obstacleShape->topY;
      break;
    case 6:
      lx = obstacleShape->leftX;
      break;
    case 1:
      lrx = obstacleShape->lowerRightDiagonalX;
      break;
    case 3:
      urx = obstacleShape->upperRightDiagonalX;
      break;
    case 5:
      ulx = obstacleShape->upperLeftDiagonalX;
      break;
    case 7:
      llx = obstacleShape->lowerLeftDiagonalX;
      break;
    default:
      FRLogger::warn(
          "ShapeSearchTree45Degree.calc_inside_restrained_shape:"
          " obstacleLineNo out of range");
  }

  IntOctagon* result = new (jrt::kGeom) IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
  return result->normalize();
}

jrt::Array<TileShape*>* ShapeSearchTree45Degree::calculateTreeShapes(DrillItem* drillItem) {
  if (this->board == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(drillItem->tileShapeCount());
  for (int32_t i = 0; i < result->length; i++) {
    Shape* currentShape = drillItem->getShape(i);
    if (currentShape == nullptr) {
      currentShape = drillHoleObstacle(drillItem);
    }
    if (currentShape == nullptr) {
      (*result)[i] = nullptr;
    } else {
      TileShape* currentTileShape = currentShape->boundingOctagon();
      if (currentTileShape->isIntBox()) {
        currentTileShape = currentShape->boundingBox();

        // To avoid small corner cutoffs when taking the offset as an octagon.
        // That may complicate the room division in the maze expand algorithm unnecessary.
      }

      int32_t offsetWidth = this->clearanceCompensationValue(drillItem->clearanceClassIndex(),
                                                             drillItem->shapeLayer(i));
      offsetWidth += drillHoleClearanceDelta(drillItem, currentShape, drillItem->shapeLayer(i));
      currentTileShape = jrt::fast_cast<TileShape>(currentTileShape->offset(offsetWidth));
      (*result)[i] = currentTileShape->boundingOctagon();
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree45Degree::calculateTreeShapes(ObstacleArea* obstacleArea) {
  jrt::Array<TileShape*>* result = ShapeSearchTree::calculateTreeShapes(obstacleArea);
  for (int32_t i = 0; i < result->length; i++) {
    if ((*result)[i] != nullptr) {
      (*result)[i] = (*result)[i]->boundingOctagon();
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree45Degree::calculateTreeShapes(BoardOutline* outline) {
  jrt::Array<TileShape*>* result = ShapeSearchTree::calculateTreeShapes(outline);
  for (int32_t i = 0; i < result->length; i++) {
    if ((*result)[i] != nullptr) {
      (*result)[i] = (*result)[i]->boundingOctagon();
    }
  }
  return result;
}

std::string ShapeSearchTree45Degree::describeBounds(IntBox* bounds) {
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

bool ShapeSearchTree45Degree::isCompleteShapeDebugAnchor(int32_t netNumber, int32_t roomLayer,
                                                         IntOctagon* startShape) {
  return netNumber == 77 && roomLayer == 0 && startShape->leftX == 1762393 &&
         startShape->bottomY == -1080137 && startShape->rightX == 1910447 &&
         startShape->topY == -1006110;
}

void ShapeSearchTree45Degree::traceCompleteShapeFilter(int32_t step, int32_t netNumber,
                                                       int32_t roomLayer, int32_t shapeIndex,
                                                       int32_t objectLayer, bool isObstacle,
                                                       bool sameLayer, bool ignoredObject,
                                                       SearchTreeObject* object) {
  FRLogger::trace("COMPLETE_SHAPE_FILTER" + std::string(", step=") + jrt::str(step) +
                  ", net=" + jrt::str(netNumber) + ", layer=" + jrt::str(roomLayer) +
                  ", shapeIndex=" + jrt::str(shapeIndex) + ", object_layer=" +
                  jrt::str(objectLayer) + ", is_trace_obstacle=" + jrt::str(isObstacle) +
                  ", same_layer=" + jrt::str(sameLayer) + ", ignored_object=" +
                  jrt::str(ignoredObject) +
                  ", accepted=" + jrt::str(isObstacle && sameLayer && !ignoredObject) +
                  ", obstacle_id=" + jrt::str(obstacleId(object)) +
                  ", obstacle_nets=" + obstacleNets(object) + ", obstacle=" + jrt::str(object));
}

void ShapeSearchTree45Degree::traceCompleteShapeCandidate(int32_t step, int32_t netNumber,
                                                          int32_t roomLayer,
                                                          SearchTreeObject* object,
                                                          IntOctagon* obstacleShape) {
  FRLogger::trace("COMPLETE_SHAPE_OBS candidate" + std::string(", step=") + jrt::str(step) +
                  ", net=" + jrt::str(netNumber) + ", layer=" + jrt::str(roomLayer) +
                  ", obstacle=" + jrt::str(object) + ", obstacle_id=" +
                  jrt::str(obstacleId(object)) + ", obstacle_nets=" + obstacleNets(object) +
                  ", obstacle_bounds=" + describeBounds(obstacleShape->boundingBox()));
}

void ShapeSearchTree45Degree::traceCompleteShapeDecision(int32_t step, int32_t netNumber,
                                                         int32_t roomLayer,
                                                         const std::string& action, bool overlap,
                                                         IntOctagon* roomShape,
                                                         IntOctagon* obstacleShape) {
  FRLogger::trace("COMPLETE_SHAPE_DECISION" + std::string(", step=") + jrt::str(step) +
                  ", net=" + jrt::str(netNumber) + ", layer=" + jrt::str(roomLayer) +
                  ", action=" + action + ", overlap=" + jrt::str(overlap) +
                  ", room_bounds=" + describeBounds(roomShape->boundingBox()) +
                  ", obstacle_bounds=" + describeBounds(obstacleShape->boundingBox()));
}

int32_t ShapeSearchTree45Degree::obstacleId(SearchTreeObject* object) {
  auto* item = jrt::fast_cast<Item>(object);
  return item != nullptr ? item->getId() : -1;
}

std::string ShapeSearchTree45Degree::obstacleNets(SearchTreeObject* object) {
  auto* item = jrt::fast_cast<Item>(object);
  return item != nullptr ? jrt::Arrays::toString(item->netNumbers) : std::string("[]");
}

}  // namespace fr
