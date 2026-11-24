// Port of app.freerouting.board.facade.RoutingBoardOperations
#include "board/facade/RoutingBoardOperations.h"

#include "autoroute/maze/AutorouteControl.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/optimize/TraceTightener.h"
#include "board/state/ChangedArea.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"

namespace fr {

void RoutingBoardOperations::startMarkingChangedArea() {
  if (board->changedArea == nullptr) {
    // PORT NOTE (stage E): board state that may outlive the operation region it is started in
    // (optChangedArea consumes it later): in the board's domain.
    jrt::DomainScope domain(board);
    board->changedArea = new (jrt::kOp) ChangedArea(board->getLayerCount());
  }
}

void RoutingBoardOperations::joinChangedArea(FloatPoint* point, int32_t layer) {
  if (board->changedArea != nullptr) {
    board->changedArea->join(point, layer);
  }
}

void RoutingBoardOperations::markAllChangedArea() {
  startMarkingChangedArea();
  jrt::Array<FloatPoint*>* boardCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(4);
  (*boardCorners)[0] = board->boundingBox->ll->toFloat();
  (*boardCorners)[1] = new (jrt::kGeom) FloatPoint(board->boundingBox->ur->x, board->boundingBox->ll->y);
  (*boardCorners)[2] = board->boundingBox->ur->toFloat();
  (*boardCorners)[3] = new (jrt::kGeom) FloatPoint(board->boundingBox->ll->x, board->boundingBox->ur->y);
  for (int32_t layer = 0; layer < board->getLayerCount(); layer++) {
    for (FloatPoint* boardCorner : *boardCorners) {
      joinChangedArea(boardCorner, layer);
    }
  }
}

void RoutingBoardOperations::optChangedArea(
    jrt::Array<int32_t>* onlyNetNoArr, IntOctagon* clipShape, int32_t accuracy,
    jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts, Stoppable* stoppableThread,
    int32_t timeLimit, Point* keepPoint, int32_t keepPointLayer) {
  if (board->changedArea == nullptr) {
    return;
  }
  if (clipShape != IntOctagon::EMPTY()) {
    TraceTightener* pullTightAlgo =
        TraceTightener::getInstance(board, onlyNetNoArr, clipShape, accuracy, stoppableThread,
                                    timeLimit, keepPoint, keepPointLayer);
    pullTightAlgo->optChangedArea(traceCosts);
  }
  board->joinGraphicsUpdateBox(board->changedArea->surroundingBox());
  board->changedArea = nullptr;
}

bool RoutingBoardOperations::removeItemsAndPullTight(jrt::Collection<Item*>* itemList,
                                                     int32_t tidyWidth,
                                                     int32_t pullTightAccuracy) {
  bool result = true;
  IntOctagon* tidyRegion;
  bool calculateTidyRegion;
  if (tidyWidth < jrt::Integer::MAX_VALUE) {
    tidyRegion = IntOctagon::EMPTY();
    calculateTidyRegion = tidyWidth > 0;
  } else {
    tidyRegion = nullptr;
    calculateTidyRegion = false;
  }
  startMarkingChangedArea();
  jrt::Set<int32_t>* changedNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (Item* currentItem : *itemList) {
    if (currentItem->isDeletionForbidden() || currentItem->isUserFixed()) {
      result = false;
    } else {
      for (int32_t i = 0; i < currentItem->tileShapeCount(); i++) {
        TileShape* currentShape = currentItem->getTileShape(i);
        board->changedArea->join(currentShape, currentItem->shapeLayer(i));
        if (calculateTidyRegion) {
          tidyRegion = tidyRegion->union_(currentShape->boundingOctagon());
        }
      }
      board->removeItem(currentItem);
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        changedNets->add(currentItem->getNetNumber(i));
      }
    }
  }
  for (int32_t currentNetNumber : *changedNets) {
    board->combineTraces(currentNetNumber);
  }
  if (calculateTidyRegion) {
    tidyRegion = tidyRegion->enlarge(tidyWidth);
  }
  board->optChangedArea(new (jrt::kOp) jrt::Array<int32_t>(0), tidyRegion, pullTightAccuracy, nullptr,
                        nullptr, PULL_TIGHT_TIME_LIMIT);
  return result;
}

}  // namespace fr
