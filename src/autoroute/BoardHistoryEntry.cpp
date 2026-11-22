#include "autoroute/BoardHistoryEntry.h"

#include "board/facade/RoutingBoard.h"
#include "core/scoring/BoardStatistics.h"
#include "settings/RouterSettings.h"

namespace fr {

BoardHistoryEntry::BoardHistoryEntry(RoutingBoard* board, RouterSettings* routerSettings) {
  this->board = board;
  this->statistics = board->getStatistics();
  this->score = this->statistics->getRouterScore(routerSettings);
  this->hash = board->getHash();
  this->timestamp = jrt::System::currentTimeMillis();
}

}  // namespace fr
