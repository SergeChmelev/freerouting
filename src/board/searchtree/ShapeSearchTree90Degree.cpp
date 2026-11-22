// Port of app.freerouting.board.searchtree.ShapeSearchTree90Degree
#include "board/searchtree/ShapeSearchTree90Degree.h"

#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/structure/BoardOutline.h"
#include "board/searchtree/SearchTreeObject.h"
#include "datastructures/ArrayStack.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/OrthogonalBoundingDirections.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {

/** Java `finally { releaseScratchStack(stack); }`. */
struct ReleaseStackOnExit90 {
  ArrayStack<ShapeTree_TreeNode*>* stack;
  void (*release)(ArrayStack<ShapeTree_TreeNode*>*);
  ~ReleaseStackOnExit90() { release(stack); }
};

}  // namespace

ShapeSearchTree90Degree::ShapeSearchTree90Degree(BasicBoard* board,
                                                 int32_t compensatedClearanceClassNo)
    : ShapeSearchTree(OrthogonalBoundingDirections::INSTANCE(), board,
                      compensatedClearanceClassNo) {
  // PORT NOTE: Java computes key in the base constructor with the runtime class.
  key = getKey(this, OrthogonalBoundingDirections::INSTANCE(), compensatedClearanceClassNo);
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree90Degree::completeShape(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  return completeShapeUnlocked(room, netNumber, ignoreObject, ignoreShape);
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree90Degree::completeShapeUnlocked(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  auto* shapeToBeContained = dynamic_cast<IntBox*>(room->getContainedShape());
  if (shapeToBeContained == nullptr) {
    FRLogger::warn("BoxShapeSearchTree.complete_shape: unexpected shapeToBeContained");
    return new jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }
  if (this->root == nullptr) {
    return new jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }
  IntBox* startShape = board->getBoundingBox();
  if (room->getShape() != nullptr) {
    if (dynamic_cast<IntBox*>(room->getShape()) == nullptr) {
      FRLogger::warn("BoxShapeSearchTree.complete_shape: startShape of type IntBox expected");
      return new jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
    }
    startShape = dynamic_cast<IntBox*>(room->getShape())->intersection(startShape);
  }
  IntBox* boundingShape = startShape;
  int32_t roomLayer = room->getLayer();
  bool debugAnchor = isCompleteShapeDebugAnchor(netNumber, roomLayer, startShape);
  int32_t debugStep = 0;
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
  result->add(new IncompleteFreeSpaceExpansionRoom(startShape, roomLayer, shapeToBeContained));

  // Process obstacles inline during tree traversal with dynamic boundingShape updates.
  // This matches v1.9's algorithm exactly: as obstacles are processed, boundingShape
  // shrinks, which prunes subsequent tree traversal (just like v1.9 does).
  ArrayStack<ShapeTree_TreeNode*>* stack = acquireScratchStack();
  {
    ReleaseStackOnExit90 guard{stack, &MinAreaTree::releaseScratchStack};
    stack->push(this->root);
    ShapeTree_TreeNode* currentNode;

    for (;;) {
      currentNode = stack->pop();
      if (currentNode == nullptr) {
        break;
      }
      if (currentNode->boundingShape->intersects(boundingShape)) {
        if (auto* currentLeaf = dynamic_cast<ShapeTree_Leaf*>(currentNode)) {
          SearchTreeObject* currentObject = dynamic_cast<SearchTreeObject*>(currentLeaf->object);
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
            IntBox* currentObjectShape =
                currentObject->getTreeShape(this, shapeIndex)->boundingBox();
            if (debugAnchor) {
              traceCompleteShapeCandidate(debugStep, netNumber, roomLayer, currentObject,
                                          currentObjectShape);
            }
            jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newResult =
                new jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
            IntBox* newBoundingShape = IntBox::EMPTY();
            bool hadRoomsBeforeObstacle = !result->isEmpty();
            for (IncompleteFreeSpaceExpansionRoom* currentRoom : *result) {
              IntBox* currentShape = dynamic_cast<IntBox*>(currentRoom->getShape());
              bool overlaps = currentShape->overlaps(currentObjectShape);
              if (overlaps) {
                if (dynamic_cast<CompleteFreeSpaceExpansionRoom*>(currentObject) != nullptr &&
                    ignoreShape != nullptr) {
                  IntBox* intersection = currentShape->intersection(currentObjectShape);
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

                for (IncompleteFreeSpaceExpansionRoom* tmpShape : *newResult) {
                  newBoundingShape = newBoundingShape->union_(tmpShape->getShape()->boundingBox());
                }
              } else {
                if (debugAnchor) {
                  traceCompleteShapeDecision(debugStep, netNumber, roomLayer, "KEEP_NON_OVERLAP",
                                             overlaps, currentShape, currentObjectShape);
                }
                newResult->add(currentRoom);
                newBoundingShape = newBoundingShape->union_(currentShape->boundingBox());
              }
            }
            if (hadRoomsBeforeObstacle && newResult->isEmpty()) {
              if (FRLogger::isTraceEnabled()) {
                FRLogger::trace("COMPLETE_SHAPE_BLOCKED net=" + jrt::str(netNumber) +
                                ", layer=" + jrt::str(roomLayer) +
                                ", contained=" + describeBounds(shapeToBeContained) +
                                ", obstacle_type=" + jrt::className(currentObject) +
                                ", obstacle_id=" + jrt::str(obstacleId(currentObject)) +
                                ", obstacle_bounds=" + describeBounds(currentObjectShape));
              }
            }
            result = newResult;
            boundingShape = newBoundingShape;
          }
          if (debugAnchor) {
            debugStep++;
          }
        } else {
          stack->push(static_cast<ShapeTree_InnerNode*>(currentNode)->firstChild);
          stack->push(static_cast<ShapeTree_InnerNode*>(currentNode)->secondChild);
        }
      }
    }
  }
  return result;
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree90Degree::restrainShape(
    IncompleteFreeSpaceExpansionRoom* incompleteRoom, IntBox* obstacleShape) {
  // Search the edge line of obstacleShape, so that shapeToBeContained
  // are on the right side of this line, and that the line segment
  // intersects with the interior of shape.
  // If there are more than 1 such lines take the line which is
  // furthest away from the shapeToBeContained
  // Then intersect shape with the halfplane defined by the
  // opposite of this line.

  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();

  TileShape* containedShape = incompleteRoom->getContainedShape();
  if (containedShape == nullptr || containedShape->isEmpty()) {
    FRLogger::trace("BoxShapeSearchTree.restrain_shape: shapeToBeContained is empty");
    return result;
  }
  IntBox* roomShape = incompleteRoom->getShape()->boundingBox();
  IntBox* shapeToBeContained = incompleteRoom->getContainedShape()->boundingBox();
  int32_t cutLineDistance = 0;
  IntBox* restrainedShape = nullptr;

  if (roomShape->ll->x < obstacleShape->ur->x && roomShape->ur->x > obstacleShape->ur->x &&
      roomShape->ur->y > obstacleShape->ll->y && roomShape->ll->y < obstacleShape->ur->y) {
    // The right line segment of the obstacleShape intersects the interior of
    // shape
    int32_t currentDistance = shapeToBeContained->ll->x - obstacleShape->ur->x;
    if (currentDistance > cutLineDistance) {
      cutLineDistance = currentDistance;
      restrainedShape =
          new IntBox(obstacleShape->ur->x, roomShape->ll->y, roomShape->ur->x, roomShape->ur->y);
    }
  }
  if (roomShape->ll->x < obstacleShape->ll->x && roomShape->ur->x > obstacleShape->ll->x &&
      roomShape->ur->y > obstacleShape->ll->y && roomShape->ll->y < obstacleShape->ur->y) {
    // The left line segment of the obstacleShape intersects the interior of
    // shape
    int32_t currentDistance = obstacleShape->ll->x - shapeToBeContained->ur->x;
    if (currentDistance > cutLineDistance) {
      cutLineDistance = currentDistance;
      restrainedShape =
          new IntBox(roomShape->ll->x, roomShape->ll->y, obstacleShape->ll->x, roomShape->ur->y);
    }
  }
  if (roomShape->ll->y < obstacleShape->ll->y && roomShape->ur->y > obstacleShape->ll->y &&
      roomShape->ur->x > obstacleShape->ll->x && roomShape->ll->x < obstacleShape->ur->x) {
    // The lower line segment of the obstacleShape intersects the interior of
    // shape
    int32_t currentDistance = obstacleShape->ll->y - shapeToBeContained->ur->y;
    if (currentDistance > cutLineDistance) {
      cutLineDistance = currentDistance;
      restrainedShape =
          new IntBox(roomShape->ll->x, roomShape->ll->y, roomShape->ur->x, obstacleShape->ll->y);
    }
  }
  if (roomShape->ll->y < obstacleShape->ur->y && roomShape->ur->y > obstacleShape->ur->y &&
      roomShape->ur->x > obstacleShape->ll->x && roomShape->ll->x < obstacleShape->ur->x) {
    // The upper line segment of the obstacleShape intersects the interior of
    // shape
    int32_t currentDistance = shapeToBeContained->ll->y - obstacleShape->ur->y;
    if (currentDistance > cutLineDistance) {
      cutLineDistance = currentDistance;
      restrainedShape =
          new IntBox(roomShape->ll->x, obstacleShape->ur->y, roomShape->ur->x, roomShape->ur->y);
    }
  }
  if (restrainedShape != nullptr) {
    result->add(new IncompleteFreeSpaceExpansionRoom(restrainedShape, incompleteRoom->getLayer(),
                                                     shapeToBeContained));
    return result;
  }

  // Now shapeToBeContained intersects with the obstacleShape.
  // shapeToBeContained and shape evtl. need to be divided in two.
  IntBox* is = shapeToBeContained->intersection(obstacleShape);
  if (is->isEmpty()) {
    FRLogger::warn(
        "BoxShapeSearchTree.restrain_shape: Intersection between obstacleShape"
        " and shapeToBeContained expected");
    return result;
  }
  IntBox* newShape1 = nullptr;
  IntBox* newShape2 = nullptr;
  if (is->ll->x > roomShape->ll->x && is->ll->x == obstacleShape->ll->x &&
      is->ll->x < roomShape->ur->x) {
    newShape1 = new IntBox(roomShape->ll->x, roomShape->ll->y, is->ll->x, roomShape->ur->y);
    newShape2 = new IntBox(is->ll->x, roomShape->ll->y, roomShape->ur->x, roomShape->ur->y);
  } else if (is->ur->x > roomShape->ll->x && is->ur->x == obstacleShape->ur->x &&
             is->ur->x < roomShape->ur->x) {
    newShape2 = new IntBox(roomShape->ll->x, roomShape->ll->y, is->ur->x, roomShape->ur->y);
    newShape1 = new IntBox(is->ur->x, roomShape->ll->y, roomShape->ur->x, roomShape->ur->y);
  } else if (is->ll->y > roomShape->ll->y && is->ll->y == obstacleShape->ll->y &&
             is->ll->y < roomShape->ur->y) {
    newShape1 = new IntBox(roomShape->ll->x, roomShape->ll->y, roomShape->ur->x, is->ll->y);
    newShape2 = new IntBox(roomShape->ll->x, is->ll->y, roomShape->ur->x, roomShape->ur->y);
  } else if (is->ur->y > roomShape->ll->y && is->ur->y == obstacleShape->ur->y &&
             is->ur->y < roomShape->ur->y) {
    newShape2 = new IntBox(roomShape->ll->x, roomShape->ll->y, roomShape->ur->x, is->ur->y);
    newShape1 = new IntBox(roomShape->ll->x, is->ur->y, roomShape->ur->x, roomShape->ur->y);
  }
  if (newShape1 != nullptr) {
    IntBox* newShapeToBeContained = shapeToBeContained->intersection(newShape1);
    if (newShapeToBeContained->dimension() > 0) {
      result->add(new IncompleteFreeSpaceExpansionRoom(newShape1, incompleteRoom->getLayer(),
                                                       newShapeToBeContained));
      IncompleteFreeSpaceExpansionRoom* newIncompleteRoom = new IncompleteFreeSpaceExpansionRoom(
          newShape2, incompleteRoom->getLayer(), shapeToBeContained->intersection(newShape2));
      result->addAll(restrainShape(newIncompleteRoom, obstacleShape));
    }
  }
  return result;
}

std::string ShapeSearchTree90Degree::describeBounds(IntBox* bounds) {
  return "[(" + jrt::str(bounds->ll->x) + "," + jrt::str(bounds->ll->y) + ")..(" +
         jrt::str(bounds->ur->x) + "," + jrt::str(bounds->ur->y) + ")]";
}

bool ShapeSearchTree90Degree::isCompleteShapeDebugAnchor(int32_t netNumber, int32_t roomLayer,
                                                         IntBox* startShape) {
  return netNumber == 84 && roomLayer == 0 && startShape->ll->x == 1767436 &&
         startShape->ll->y == -1206395 && startShape->ur->x == 1994010 &&
         startShape->ur->y == -782336;
}

void ShapeSearchTree90Degree::traceCompleteShapeFilter(int32_t step, int32_t netNumber,
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

void ShapeSearchTree90Degree::traceCompleteShapeCandidate(int32_t step, int32_t netNumber,
                                                          int32_t roomLayer,
                                                          SearchTreeObject* object,
                                                          IntBox* obstacleShape) {
  FRLogger::trace("COMPLETE_SHAPE_OBS candidate" + std::string(", step=") + jrt::str(step) +
                  ", net=" + jrt::str(netNumber) + ", layer=" + jrt::str(roomLayer) +
                  ", obstacle=" + jrt::str(object) + ", obstacle_id=" +
                  jrt::str(obstacleId(object)) + ", obstacle_nets=" + obstacleNets(object) +
                  ", obstacle_bounds=" + describeBounds(obstacleShape));
}

void ShapeSearchTree90Degree::traceCompleteShapeDecision(int32_t step, int32_t netNumber,
                                                         int32_t roomLayer,
                                                         const std::string& action, bool overlap,
                                                         IntBox* roomShape,
                                                         IntBox* obstacleShape) {
  FRLogger::trace("COMPLETE_SHAPE_DECISION" + std::string(", step=") + jrt::str(step) +
                  ", net=" + jrt::str(netNumber) + ", layer=" + jrt::str(roomLayer) +
                  ", action=" + action + ", overlap=" + jrt::str(overlap) +
                  ", room_bounds=" + describeBounds(roomShape) +
                  ", obstacle_bounds=" + describeBounds(obstacleShape));
}

int32_t ShapeSearchTree90Degree::obstacleId(SearchTreeObject* object) {
  auto* item = dynamic_cast<Item*>(object);
  return item != nullptr ? item->getId() : -1;
}

std::string ShapeSearchTree90Degree::obstacleNets(SearchTreeObject* object) {
  auto* item = dynamic_cast<Item*>(object);
  return item != nullptr ? jrt::Arrays::toString(item->netNumbers) : std::string("[]");
}

jrt::Array<TileShape*>* ShapeSearchTree90Degree::calculateTreeShapes(DrillItem* drillItem) {
  if (this->board == nullptr) {
    return new jrt::Array<TileShape*>(0);
  }
  auto* result = new jrt::Array<TileShape*>(drillItem->tileShapeCount());
  for (int32_t i = 0; i < result->length; i++) {
    Shape* currentShape = drillItem->getShape(i);
    if (currentShape == nullptr) {
      currentShape = drillHoleObstacle(drillItem);
    }
    if (currentShape == nullptr) {
      (*result)[i] = nullptr;
    } else {
      IntBox* currentTileShape = currentShape->boundingBox();
      int32_t offsetWidth = this->clearanceCompensationValue(drillItem->clearanceClassIndex(),
                                                             drillItem->shapeLayer(i));
      offsetWidth += drillHoleClearanceDelta(drillItem, currentShape, drillItem->shapeLayer(i));
      if (currentTileShape == nullptr) {
        FRLogger::warn("BoxShapeSearchTree.calculate_tree_shapes: shape is null");
      } else {
        currentTileShape = currentTileShape->offset(offsetWidth);
      }
      (*result)[i] = currentTileShape;
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree90Degree::calculateTreeShapes(ObstacleArea* obstacleArea) {
  jrt::Array<TileShape*>* result = ShapeSearchTree::calculateTreeShapes(obstacleArea);
  for (int32_t i = 0; i < result->length; i++) {
    if ((*result)[i] != nullptr) {
      (*result)[i] = (*result)[i]->boundingBox();
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree90Degree::calculateTreeShapes(BoardOutline* outline) {
  jrt::Array<TileShape*>* result = ShapeSearchTree::calculateTreeShapes(outline);
  for (int32_t i = 0; i < result->length; i++) {
    if ((*result)[i] != nullptr) {
      (*result)[i] = (*result)[i]->boundingBox();
    }
  }
  return result;
}

TileShape* ShapeSearchTree90Degree::offsetShape(Polyline* polyline, int32_t halfWidth,
                                                int32_t no) {
  return polyline->offsetBox(halfWidth, no);
}

jrt::Array<TileShape*>* ShapeSearchTree90Degree::offsetShapes(Polyline* polyline,
                                                              int32_t halfWidth, int32_t fromNo,
                                                              int32_t toNo) {
  fromNo = jrt::Math::max(fromNo, 0);
  toNo = jrt::Math::min(toNo, polyline->lines->length - 1);
  int32_t shapeCount = jrt::Math::max(toNo - fromNo - 1, 0);
  auto* shapes = new jrt::Array<TileShape*>(shapeCount);
  for (int32_t j = fromNo; j < toNo - 1; j++) {
    (*shapes)[j - fromNo] = polyline->offsetBox(halfWidth, j);
  }
  return shapes;
}

}  // namespace fr
