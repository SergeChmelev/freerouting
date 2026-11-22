#include "autoroute/BoardHistory.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "core/scoring/BoardStatistics.h"
#include "settings/RouterSettings.h"
#include "settings/RoutingCostSettings.h"

namespace fr {

BoardHistory::BoardHistory(RouterSettings* routerSettings) : BoardHistory(routerSettings, MAX_HISTORY_SIZE) {}

BoardHistory::BoardHistory(RouterSettings* routerSettings, int32_t maxHistorySize) {
  this->routerSettings = routerSettings;
  this->maxHistorySize = maxHistorySize;
}

BoardHistory::BoardHistory(RoutingCostSettings* scoringSettings)
    : BoardHistory(createLegacyRouterSettings(scoringSettings), MAX_HISTORY_SIZE) {}

BoardHistory::BoardHistory(RoutingCostSettings* scoringSettings, int32_t maxHistorySize)
    : BoardHistory(createLegacyRouterSettings(scoringSettings), maxHistorySize) {}

void BoardHistory::add(RoutingBoard* board) {
  if (contains(board)) {
    return;
  }

  if (boards->size() >= maxHistorySize) {
    // Compute the new board's score before the expensive serialisation so we can
    // skip adding boards that would not improve the history.
    float newScore = (new BoardStatistics(board))->getRouterScore(routerSettings);

    // Find the worst-scoring entry via a linear scan (O(n), n <= MAX_HISTORY_SIZE).
    int32_t worstIndex = 0;
    float worstScore = boards->get(0)->score;
    for (int32_t i = 1; i < boards->size(); i++) {
      if (boards->get(i)->score < worstScore) {
        worstScore = boards->get(i)->score;
        worstIndex = i;
      }
    }

    // Only evict the worst entry if the new board scores strictly better.
    if (newScore <= worstScore) {
      return;
    }
    boards->removeAt(worstIndex);
  }

  boards->add(new BoardHistory_BoardHistoryEntry(board, routerSettings));
}

RouterSettings* BoardHistory::createLegacyRouterSettings(RoutingCostSettings* scoringSettings) {
  RouterSettings* settings = new RouterSettings();
  settings->scoring = scoringSettings;
  return settings;
}

void BoardHistory::clear() { boards->clear(); }

bool BoardHistory::contains(RoutingBoard* board) {
  std::string hash = board->getHash();
  for (BoardHistory_BoardHistoryEntry* entry : *boards) {
    if (entry->hash == hash) {
      return true;
    }
  }
  return false;
}

void BoardHistory::remove(RoutingBoard* board) {
  std::string hash = board->getHash();
  for (int32_t i = 0; i < boards->size(); i++) {
    if (boards->get(i)->hash == hash) {
      boards->removeAt(i);
      return;
    }
  }
}

float BoardHistory::getMaxScore() {
  float maxScore = 0;
  for (BoardHistory_BoardHistoryEntry* entry : *boards) {
    if (entry->score > maxScore) {
      maxScore = entry->score;
    }
  }
  return maxScore;
}

RoutingBoard* BoardHistory::restoreBoard(int32_t maxAllowedRestoreCount) {
  if (maxAllowedRestoreCount <= 0) {
    maxAllowedRestoreCount = jrt::Integer::MAX_VALUE;
  }

  // Sort the boards by score
  boards->sort([](BoardHistory_BoardHistoryEntry* o1, BoardHistory_BoardHistoryEntry* o2) {
    return jrt::Float::compare(o2->score, o1->score);
  });

  for (BoardHistory_BoardHistoryEntry* entry : *boards) {
    if (entry->restoreCount <= maxAllowedRestoreCount) {
      entry->restoreCount++;
      RoutingBoard* restored = jrt::fast_cast<RoutingBoard>(BasicBoard::deserialize(entry->board));
      // The baseline is transient: without it, every pre-existing violation counts as new.
      restored->clearanceBaseline = entry->clearanceBaseline;
      return restored;
    }
  }
  return nullptr;
}

RoutingBoard* BoardHistory::restoreBestBoard() { return restoreBoard(0); }

int32_t BoardHistory::size() { return boards->size(); }

int32_t BoardHistory::getRank(RoutingBoard* board) {
  std::string hash = board->getHash();
  for (int32_t i = 0; i < boards->size(); i++) {
    if (boards->get(i)->hash == hash) {
      return i + 1;
    }
  }
  return -1;
}

BoardHistory_BoardHistoryEntry::BoardHistory_BoardHistoryEntry(RoutingBoard* board,
                                                               RouterSettings* routerSettings) {
  this->board = board->serialize(false);
  this->hash = board->getHash();
  this->score = (new BoardStatistics(board))->getRouterScore(routerSettings);
  // Read after the statistics, which wait for the post-load DRC that builds the baseline.
  this->clearanceBaseline = board->clearanceBaseline;
  this->restoreCount = 0;
}

}  // namespace fr
