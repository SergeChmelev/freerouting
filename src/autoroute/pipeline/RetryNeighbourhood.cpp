#include "autoroute/pipeline/RetryNeighbourhood.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/state/Communication.h"
#include "geometry/planar/IntBox.h"

namespace fr {

int32_t RetryNeighbourhood::ripNearOpenPins(BasicBoard* board, int32_t attempt) {
  RetryNeighbourhood_Reach* reach = RetryNeighbourhood_Reach::of(attempt);
  if (!reach->anyNet()) {
    return 0;
  }
  int32_t radius = RetryNeighbourhood::radius(board, reach->radiusMm());
  jrt::Map<int32_t, Item*>* found = new jrt::LinkedHashMap<int32_t, Item*>();
  for (Pin* pin : *board->getPins()) {
    if (pin->netCount() != 1 || pin->hasIgnoredNets()) {
      continue;
    }
    int32_t net = pin->getNetNumber(0);
    if (pin->getUnconnectedSet(net)->isEmpty()) {
      continue;
    }
    IntBox* box = pin->boundingBox()->offset(radius);
    for (int32_t layer = 0; layer < board->getLayerCount(); layer++) {
      for (SearchTreeObject* object : *board->overlappingObjects(box, layer)) {
        auto* item = jrt::fast_cast<Item>(object);
        if (item == nullptr) {
          continue;
        }
        if (!(jrt::fast_cast<Trace>(item) != nullptr) && !(jrt::fast_cast<Via>(item) != nullptr)) {
          continue;
        }
        if (item->netCount() != 1 || item->containsNet(net)) {
          continue;
        }
        if (item->isUserFixed() || item->isDeletionForbidden()) {
          continue;
        }
        found->putIfAbsent(item->getId(), item);
      }
    }
  }
  jrt::List<Item*>* items = new jrt::ArrayList<Item*>(found->values());
  items->sort(jrt::comparingInt<Item*>([](Item* i) { return i->getId(); }));
  int32_t removed = 0;
  for (Item* item : *items) {
    board->removeItem(item);
    removed++;
  }
  return removed;
}

int32_t RetryNeighbourhood::radius(BasicBoard* board, double mm) {
  return jrt::d2i(jrt::Math::ceil(mm * 1000.0 * board->communication->getResolution(Unit::UM)));
}

}  // namespace fr
