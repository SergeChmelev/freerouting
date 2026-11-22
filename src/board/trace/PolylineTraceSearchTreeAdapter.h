// Port of app.freerouting.board.trace.PolylineTraceSearchTreeAdapter (package-private final class).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Keeps PolylineTrace geometry changes synchronized with every active search tree. The adapter is
 * intentionally stateless; search-tree ownership remains with the board's SearchTreeManager.
 */
class PolylineTraceSearchTreeAdapter final : public virtual jrt::Object {
 private:
  PolylineTraceSearchTreeAdapter() {}

 public:
  static jrt::Array<TileShape*>* calculateTreeShapes(PolylineTrace* trace,
                                                     ShapeSearchTree* searchTree);

  static bool hasDefaultEntries(PolylineTrace* first, PolylineTrace* second);

  /**
   * Replaces the trace geometry through the safe full-remove/reinsert path. This order is
   * significant: the old entries are removed before the geometry and derived data change.
   */
  static void replaceGeometry(PolylineTrace* trace, Polyline* newPolyline);

  static void mergeEntriesInFront(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                  Polyline* joinedPolyline, int32_t fromEntryNo,
                                  int32_t toEntryNo);

  static void mergeEntriesAtEnd(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                Polyline* joinedPolyline, int32_t fromEntryNo, int32_t toEntryNo);

  static void changeEntries(PolylineTrace* trace, Polyline* newPolyline, int32_t keepAtStartCount,
                            int32_t keepAtEndCount);
};

}  // namespace fr
