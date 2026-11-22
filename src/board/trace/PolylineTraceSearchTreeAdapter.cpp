// Port of app.freerouting.board.trace.PolylineTraceSearchTreeAdapter.
#include "board/trace/PolylineTraceSearchTreeAdapter.h"

#include "board/facade/BasicBoard.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"

namespace fr {

jrt::Array<TileShape*>* PolylineTraceSearchTreeAdapter::calculateTreeShapes(
    PolylineTrace* trace, ShapeSearchTree* searchTree) {
  return searchTree->calculateTreeShapes(trace);
}

bool PolylineTraceSearchTreeAdapter::hasDefaultEntries(PolylineTrace* first,
                                                       PolylineTrace* second) {
  ShapeSearchTree* defaultTree = first->board->searchTreeManager->getDefaultTree();
  return first->getSearchTreeEntries(defaultTree) != nullptr &&
         second->getSearchTreeEntries(defaultTree) != nullptr;
}

void PolylineTraceSearchTreeAdapter::replaceGeometry(PolylineTrace* trace,
                                                     Polyline* newPolyline) {
  trace->board->searchTreeManager->remove(trace);
  trace->clearSearchTreeEntries();
  trace->setPolyline(newPolyline);
  trace->clearDerivedData();
  trace->board->searchTreeManager->insert(trace);
  trace->board->incrementRevision();
}

void PolylineTraceSearchTreeAdapter::mergeEntriesInFront(PolylineTrace* fromTrace,
                                                         PolylineTrace* toTrace,
                                                         Polyline* joinedPolyline,
                                                         int32_t fromEntryNo, int32_t toEntryNo) {
  toTrace->board->searchTreeManager->mergeEntriesInFront(fromTrace, toTrace, joinedPolyline,
                                                         fromEntryNo, toEntryNo);
}

void PolylineTraceSearchTreeAdapter::mergeEntriesAtEnd(PolylineTrace* fromTrace,
                                                       PolylineTrace* toTrace,
                                                       Polyline* joinedPolyline,
                                                       int32_t fromEntryNo, int32_t toEntryNo) {
  toTrace->board->searchTreeManager->mergeEntriesAtEnd(fromTrace, toTrace, joinedPolyline,
                                                       fromEntryNo, toEntryNo);
}

void PolylineTraceSearchTreeAdapter::changeEntries(PolylineTrace* trace, Polyline* newPolyline,
                                                   int32_t keepAtStartCount,
                                                   int32_t keepAtEndCount) {
  trace->board->searchTreeManager->changeEntries(trace, newPolyline, keepAtStartCount,
                                                 keepAtEndCount);
}

}  // namespace fr
