#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Represents a historical snapshot entry of a board routing state with its score and statistics.
 * Java: implements Comparable<BoardHistoryEntry>.
 */
class BoardHistoryEntry : public virtual jrt::Object {
 public:
  RoutingBoard* board;
  float score;
  std::string hash;
  BoardStatistics* statistics;
  // PORT NOTE: java.time.Instant has no jrt equivalent; stored as epoch milliseconds
  // (jrt::System::currentTimeMillis()).
  int64_t timestamp;
  int32_t restoreCount = 0;

  /** Constructs a BoardHistoryEntry for the given board and complete router settings. */
  BoardHistoryEntry(RoutingBoard* board, RouterSettings* routerSettings);

  virtual int32_t compareTo(BoardHistoryEntry* o) { return jrt::Float::compare(this->score, o->score); }
};

}  // namespace fr
