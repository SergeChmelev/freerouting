// Port of app.freerouting.board.actions.ItemIdGenerator.
#pragma once

#include "datastructures/IdGenerator.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Creates unique identification numbers for items on the board with overflow protection built-in.
 * The counter starts at 1 and increments monotonically. Once it reaches MAX_ID it wraps around to
 * 1 (with a warning).
 */
class ItemIdGenerator : public virtual IdGenerator {
 private:
  static constexpr int32_t MAX_ID = jrt::Integer::MAX_VALUE / 2;
  int32_t lastGeneratedId = 0;

  /** Tracks how many times the counter has wrapped around (for diagnostics). */
  int64_t wrapAroundCount = 0;

 public:
  /** Creates a new ItemIdGenerator. */
  ItemIdGenerator() {}

  /** Creates a new unique identification number. */
  int32_t newId() override;

  /** Returns the maximum generated ID number so far. */
  int32_t maxGeneratedId() override { return lastGeneratedId; }
};

}  // namespace fr
