// Port of app.freerouting.board.trace.PolylineTraceNormalization.
#include "board/trace/PolylineTraceNormalization.h"

#include "board/facade/BasicBoard.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"

namespace fr {

bool PolylineTraceNormalization::normalize(PolylineTrace* trace, IntOctagon* clipShape,
                                           int32_t normalizationDepth) {
  if (normalizationDepth > MAX_NORMALIZATION_DEPTH) {
    // Return false (no further change at this depth level) rather than throwing an exception.
    FRLogger::debug("PolylineTrace.normalize: max normalization depth (" +
                    jrt::str(MAX_NORMALIZATION_DEPTH) + ") reached for trace #" +
                    jrt::str(trace->getId()) + " on net " +
                    jrt::str(trace->netCount() > 0 ? (*trace->netNumbers)[0] : -1) +
                    " \xE2\x80\x94 stopping recursion, trace kept as-is.");
    return false;
  }

  bool debugNet49 = trace->netNumbers != nullptr && trace->netNumbers->length > 0 &&
                    (*trace->netNumbers)[0] == 49 && normalizationDepth == 0;

  bool observersActivated = false;
  BasicBoard* routingBoard = trace->board;
  if (trace->board != nullptr) {
    // Let the observers know the trace changes.
    observersActivated = !routingBoard->observersActive();
    if (observersActivated) {
      routingBoard->startNotifyObservers();
    }
  }
  jrt::Collection<PolylineTrace*>* splitPieces = trace->split(clipShape);
  bool result = splitPieces->size() != 1;
  if (debugNet49) {
    FRLogger::trace("compare_trace_normalize_net49 depth=" + jrt::str(normalizationDepth) +
                    ", thisId=" + jrt::str(trace->getId()) +
                    ", thisOnBoard=" + jrt::str(trace->isOnTheBoard()) +
                    ", thisFirst=" + jrt::str(trace->firstCorner()) +
                    ", thisLast=" + jrt::str(trace->lastCorner()) +
                    ", splitPieces=" + jrt::str(splitPieces->size()));
    for (PolylineTrace* piece : *splitPieces) {
      FRLogger::trace("compare_trace_normalize_net49  piece id=" + jrt::str(piece->getId()) +
                      ", onBoard=" + jrt::str(piece->isOnTheBoard()) +
                      ", first=" + jrt::str(piece->firstCorner()) +
                      ", last=" + jrt::str(piece->lastCorner()));
    }
  }
  for (PolylineTrace* currentSplitTrace : *splitPieces) {
    if (currentSplitTrace->isOnTheBoard()) {
      bool traceCombined = currentSplitTrace->combine();
      if (debugNet49) {
        FRLogger::trace(
            "compare_trace_normalize_net49  after_combine id=" +
            jrt::str(currentSplitTrace->getId()) +
            ", onBoard=" + jrt::str(currentSplitTrace->isOnTheBoard()) +
            ", combined=" + jrt::str(traceCombined) + ", first=" +
            (currentSplitTrace->isOnTheBoard() ? jrt::str(currentSplitTrace->firstCorner())
                                               : std::string("N/A")) +
            ", last=" +
            (currentSplitTrace->isOnTheBoard() ? jrt::str(currentSplitTrace->lastCorner())
                                               : std::string("N/A")));
      }
      if (currentSplitTrace->cornerCount() == 2 &&
          currentSplitTrace->firstCorner()->equals(currentSplitTrace->lastCorner())) {
        // remove trace with only 1 corner - only if deletion is allowed.
        // USER_FIXED traces cannot be removed; skipping silently prevents an infinite loop in
        // normalizeTraces (where 'result=true' would cause the outer while to spin forever).
        if (!currentSplitTrace->isDeletionForbidden()) {
          trace->board->removeItem(currentSplitTrace);
          result = true;
        } else {
          int32_t netNumber =
              currentSplitTrace->netCount() > 0 ? (*currentSplitTrace->netNumbers)[0] : -1;
          // A degenerate user-fixed trace (first==last corner) cannot be removed because
          // deletion is forbidden. Expected for boards with malformed DSN geometry.
          FRLogger::debug(
              "PolylineTrace.normalize: skipping removal of degenerate user-fixed trace #" +
              jrt::str(currentSplitTrace->getId()) + " on net " + jrt::str(netNumber) +
              " (first==last corner)");
        }
      } else if (traceCombined) {
        normalize(currentSplitTrace, clipShape, normalizationDepth + 1);
        result = true;
      }
    }
  }
  if (observersActivated) {
    routingBoard->endNotifyObservers();
  }
  return result;
}

}  // namespace fr
