// Port of app.freerouting.board.facade.RoutingBoardSearchFacade
#include "board/facade/RoutingBoardSearchFacade.h"

#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/BoardOutline.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"

namespace fr {

double RoutingBoardSearchFacade::checkTraceSegment(Point* fromPoint, Point* toPoint, int32_t layer,
                                                   jrt::Array<int32_t>* netNumbers,
                                                   int32_t traceHalfWidth, int32_t clClassNo,
                                                   bool onlyNotShovableObstacles) {
  if (fromPoint->equals(toPoint)) {
    return 0;
  }
  Polyline* currentPolyline = new Polyline(fromPoint, toPoint);
  LineSegment* currentLineSegment = new LineSegment(currentPolyline, 1);
  return checkTraceSegment(currentLineSegment, layer, netNumbers, traceHalfWidth, clClassNo,
                           onlyNotShovableObstacles);
}

double RoutingBoardSearchFacade::checkTraceSegment(LineSegment* lineSegment, int32_t layer,
                                                   jrt::Array<int32_t>* netNumbers,
                                                   int32_t traceHalfWidth, int32_t clClassNo,
                                                   bool onlyNotShovableObstacles) {
  Polyline* checkPolyline = lineSegment->toPolyline();
  if (checkPolyline->lines->length != 3) {
    return 0;
  }
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  if (defaultTree->isClearanceCompensationUsed() &&
      defaultTree->compensatedClearanceClassNo != clClassNo) {
    // This API receives a physical width, so an uncompensated tree can check arbitrary class
    // pairs exactly instead of relying on another class's compensation.
    defaultTree = board->searchTreeManager->getUncompensatedTree();
  }
  int32_t queryHalfWidth =
      traceHalfWidth + defaultTree->clearanceCompensationValue(clClassNo, layer);
  TileShape* shapeToCheck = checkPolyline->offsetShape(queryHalfWidth, 0);
  FloatPoint* fromPoint = lineSegment->startPointApprox();
  FloatPoint* toPoint = lineSegment->endPointApprox();
  double lineLength = toPoint->distance(fromPoint);
  double okLength = jrt::Integer::MAX_VALUE;
  jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries =
      defaultTree->overlappingTreeEntriesWithClearance(shapeToCheck, layer, netNumbers, clClassNo);

  for (ShapeTree_TreeEntry* currentObstacleEntry : *obstacleEntries) {
    auto* currentObstacle = dynamic_cast<Item*>(currentObstacleEntry->object);
    if (currentObstacle == nullptr) {
      continue;
    }
    if (auto* outline = dynamic_cast<BoardOutline*>(currentObstacle)) {
      if (outline->exemptsTrace(currentObstacleEntry->shapeIndexInObject, netNumbers)) {
        continue;
      }
    }
    if (onlyNotShovableObstacles && currentObstacle->isRoutable() &&
        !currentObstacle->isShoveFixed()) {
      continue;
    }
    TileShape* currentObstacleShape = currentObstacleEntry->object->getTreeShape(
        defaultTree, currentObstacleEntry->shapeIndexInObject);
    TileShape* currentOffsetShape;
    FloatPoint* nearestObstaclePoint;
    double shortenValue;
    if (defaultTree->isClearanceCompensationUsed()) {
      currentOffsetShape = shapeToCheck;
      shortenValue = queryHalfWidth;
    } else {
      int32_t clearanceValue =
          board->clearanceValue(currentObstacle->clearanceClassIndex(), clClassNo, layer);
      currentOffsetShape = dynamic_cast<TileShape*>(shapeToCheck->offset(clearanceValue));
      shortenValue = traceHalfWidth + clearanceValue;
    }
    TileShape* intersection = currentObstacleShape->intersection(currentOffsetShape);
    if (intersection->isEmpty()) {
      continue;
    }
    nearestObstaclePoint = intersection->nearestPointApprox(fromPoint);
    double projection = fromPoint->scalarProduct(toPoint, nearestObstaclePoint) / lineLength;
    projection = jrt::Math::max(0.0, projection - shortenValue - 1);
    if (projection < okLength) {
      okLength = projection;
      if (okLength <= 0) {
        return 0;
      }
    }
  }
  return okLength;
}

bool RoutingBoardSearchFacade::checkMoveItem(Item* item, Vector* vector,
                                             jrt::Collection<Item*>* ignoreItems) {
  int32_t netCount = item->netNumbers->length;
  if (netCount > 1) {
    return false;
  }
  int32_t contactCount = 0;
  if (dynamic_cast<Connectable*>(item) != nullptr) {
    contactCount = item->getAllContacts()->size();
  }
  if (dynamic_cast<Trace*>(item) != nullptr && contactCount > 0) {
    return false;
  }
  if (ignoreItems != nullptr) {
    ignoreItems->add(item);
  }
  for (int32_t i = 0; i < item->tileShapeCount(); i++) {
    TileShape* movedShape = dynamic_cast<TileShape*>(item->getTileShape(i)->translateBy(vector));
    if (!movedShape->isContainedIn(board->boundingBox)) {
      return false;
    }
    jrt::Set<Item*>* obstacles = board->overlappingItemsWithClearance(
        movedShape, item->shapeLayer(i), item->netNumbers, item->clearanceClassIndex());
    for (Item* currentItem : *obstacles) {
      if (ignoreItems != nullptr) {
        if (!ignoreItems->contains(currentItem) && currentItem->isObstacle(item)) {
          return false;
        }
      } else if (currentItem != item && currentItem->isObstacle(item)) {
        return false;
      }
    }
  }
  return true;
}

bool RoutingBoardSearchFacade::checkChangeNet(Item* item, int32_t newNetNo) {
  jrt::Array<int32_t>* netNumbers = jrt::Array<int32_t>::of({newNetNo});
  for (int32_t i = 0; i < item->tileShapeCount(); i++) {
    TileShape* currentShape = item->getTileShape(i);
    jrt::Set<Item*>* obstacles = board->overlappingItemsWithClearance(
        currentShape, item->shapeLayer(i), netNumbers, item->clearanceClassIndex());
    for (Item* currentObject : *obstacles) {
      if (currentObject != item) {
        if (auto* connectable = dynamic_cast<Connectable*>(currentObject)) {
          if (!connectable->containsNet(newNetNo)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

Item* RoutingBoardSearchFacade::pickNearestRoutingItem(Point* location, int32_t layer,
                                                       Item* fromItem) {
  TileShape* pointShape = TileShape::getInstance(location);
  jrt::Collection<Item*>* foundItems = board->overlappingItems(pointShape, layer);
  FloatPoint* pickLocation = location->toFloat();
  double minDist = jrt::Integer::MAX_VALUE;
  Item* nearestItem = nullptr;
  jrt::Set<Item*>* ignoreSet = nullptr;
  for (Item* currentItem : *foundItems) {
    if (!currentItem->isConnectable()) {
      continue;
    }
    bool candidateFound = false;
    double currentDistance = 0;
    if (auto* currentTrace = dynamic_cast<PolylineTrace*>(currentItem)) {
      if (layer < 0 || currentTrace->getLayer() == layer) {
        if (dynamic_cast<DrillItem*>(nearestItem) != nullptr) {
          continue;
        }
        int32_t traceRadius = currentTrace->getHalfWidth();
        currentDistance = currentTrace->polyline()->distance(pickLocation);
        if (currentDistance < minDist && currentDistance <= traceRadius) {
          candidateFound = true;
        }
      }
    } else if (auto* currentDrillItem = dynamic_cast<DrillItem*>(currentItem)) {
      if (layer < 0 || currentDrillItem->isOnLayer(layer)) {
        FloatPoint* drillItemCenter = currentDrillItem->getCenter()->toFloat();
        currentDistance = drillItemCenter->distance(pickLocation);
        if (currentDistance < minDist || dynamic_cast<Trace*>(nearestItem) != nullptr) {
          candidateFound = true;
        }
      }
    } else if (auto* currentArea = dynamic_cast<ConductionArea*>(currentItem)) {
      if ((layer < 0 || currentArea->getLayer() == layer) && nearestItem == nullptr) {
        candidateFound = true;
        currentDistance = jrt::Integer::MAX_VALUE;
      }
    }
    if (candidateFound) {
      if (fromItem != nullptr) {
        if (ignoreSet == nullptr) {
          ignoreSet = fromItem->getConnectedSet(-1);
        }
        if (ignoreSet->contains(currentItem)) {
          continue;
        }
      }
      minDist = currentDistance;
      nearestItem = currentItem;
    }
  }
  return nearestItem;
}

}  // namespace fr
