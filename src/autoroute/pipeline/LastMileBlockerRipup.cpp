#include "autoroute/pipeline/LastMileBlockerRipup.h"

#include "autoroute/pipeline/RipupMemory.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/searchtree/SearchTreeObject.h"
#include "drc/AirLine.h"
#include "drc/DesignRulesChecker.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

int32_t LastMileBlockerRipup::ripBlockers(RoutingBoard* board) { return ripBlockers(board, nullptr); }

int32_t LastMileBlockerRipup::ripBlockers(RoutingBoard* board, RipupMemory* memory) {
  DesignRulesChecker* drc = new DesignRulesChecker(board, nullptr);
  jrt::Array<AirLine*>* airlines = drc->getAllAirlines();
  if (airlines->length == 0) {
    return 0;
  }
  jrt::Set<Item*>* blockers = new jrt::LinkedHashSet<Item*>();
  for (AirLine* airline : *airlines) {
    if (airline == nullptr || airline->net == nullptr) {
      continue;
    }
    IntBox* corridor = LastMileBlockerRipup::corridor(airline->fromCorner, airline->toCorner);
    jrt::Set<SearchTreeObject*>* overlapping = board->overlappingObjects(corridor, -1);
    for (SearchTreeObject* object : *overlapping) {
      auto* item = jrt::fast_cast<Item>(object);
      if (item == nullptr) {
        continue;
      }
      if (blockers->size() >= MAX_ITEMS) {
        break;
      }
      if (isRemovableBlocker(item, airline->net->netNumber, memory)) {
        blockers->add(item);
      }
    }
    if (blockers->size() >= MAX_ITEMS) {
      break;
    }
  }
  if (blockers->isEmpty()) {
    return 0;
  }
  jrt::List<Item*>* toRemove = new jrt::ArrayList<Item*>(blockers);
  board->removeItems(toRemove);
  return toRemove->size();
}

bool LastMileBlockerRipup::isRemovableBlocker(Item* item, int32_t airlineNetNumber) {
  return isRemovableBlocker(item, airlineNetNumber, nullptr);
}

bool LastMileBlockerRipup::isRemovableBlocker(Item* item, int32_t airlineNetNumber, RipupMemory* memory) {
  if (!(jrt::fast_cast<Trace>(item) != nullptr) && !(jrt::fast_cast<Via>(item) != nullptr)) {
    return false;
  }
  if (item->isDeletionForbidden() || item->isUserFixed()) {
    return false;
  }
  for (int32_t i = 0; i < item->netCount(); i++) {
    if (item->getNetNumber(i) == airlineNetNumber) {
      return false;
    }
  }
  // a net outside the priority net classes may not remove copper of those classes
  Net* airlineNet = item->board != nullptr ? item->board->rules->nets->get(airlineNetNumber) : nullptr;
  if (item->board != nullptr && (airlineNet == nullptr || !airlineNet->isRoutedFirst())) {
    for (int32_t i = 0; i < item->netCount(); i++) {
      Net* net = item->board->rules->nets->get(item->getNetNumber(i));
      if (net != nullptr && net->isRoutedFirst()) {
        return false;
      }
    }
  }
  if (memory != nullptr) {
    jrt::Set<int32_t>* keptAwayFrom = memory->netsNotToRip(airlineNetNumber);
    if (!keptAwayFrom->isEmpty()) {
      for (int32_t i = 0; i < item->netCount(); i++) {
        if (keptAwayFrom->contains(item->getNetNumber(i))) {
          return false;
        }
      }
    }
  }
  return item->netCount() > 0;
}

IntBox* LastMileBlockerRipup::corridor(FloatPoint* from, FloatPoint* to) {
  int32_t x1 = jrt::l2i(jrt::Math::round(from->x));
  int32_t y1 = jrt::l2i(jrt::Math::round(from->y));
  int32_t x2 = jrt::l2i(jrt::Math::round(to->x));
  int32_t y2 = jrt::l2i(jrt::Math::round(to->y));
  int32_t minX = jrt::Math::min(x1, x2);
  int32_t maxX = jrt::Math::max(x1, x2);
  int32_t minY = jrt::Math::min(y1, y2);
  int32_t maxY = jrt::Math::max(y1, y2);
  int32_t span = jrt::Math::max(maxX - minX, maxY - minY);
  int32_t margin = jrt::Math::max(span / 4, 1);
  return new IntBox(minX - margin, minY - margin, maxX + margin, maxY + margin);
}

}  // namespace fr
