// Port of app.freerouting.board.actions.ItemIdGenerator.
#include "board/actions/ItemIdGenerator.h"

#include "logger/FRLogger.h"

namespace fr {

ItemIdGenerator* ItemIdGenerator::copy() {
  auto* c = new (jrt::kBoard) ItemIdGenerator();
  c->lastGeneratedId = lastGeneratedId;
  c->wrapAroundCount = wrapAroundCount;
  return c;
}

int32_t ItemIdGenerator::newId() {
  if (lastGeneratedId >= MAX_ID) {
    // Wrap around to 1 instead of overflowing into negative territory.
    // Emit a single warning per wrap so the log is not flooded.
    wrapAroundCount++;
    FRLogger::warn("IdGenerator: ID counter reached " + jrt::str(MAX_ID) +
                   " and wrapped around to 1 (wrap #" + jrt::str(wrapAroundCount) + ")." +
                   " IDs that were previously assigned to now-deleted items may be" +
                   " assigned again to newly created items." +
                   " Consider restarting the router to regenerate IDs from scratch.");
    lastGeneratedId = 0;
  }
  ++lastGeneratedId;
  return lastGeneratedId;
}

}  // namespace fr
