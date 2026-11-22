#include "autoroute/path/PlannedConnection.h"

#include "autoroute/path/FoundConnectionLocator.h"
#include "board/model/items/Item.h"
#include "geometry/planar/IntPoint.h"

namespace fr {

PlannedConnection* PlannedConnection::from(int32_t netNumber, FoundConnectionLocator* connection,
                                           jrt::Collection<Item*>* rippedItems) {
  if (connection == nullptr || connection->connectionItems == nullptr
      || connection->startItem == nullptr || connection->targetItem == nullptr) {
    return nullptr;
  }
  auto* segments =
      new jrt::Array<PlannedConnection_Segment*>(connection->connectionItems->size());
  int32_t index = 0;
  for (FoundConnectionLocator_ResultItem* item : *connection->connectionItems) {
    (*segments)[index++] = new PlannedConnection_Segment(copy(item->corners), item->layer);
  }
  auto* rippedIds = new jrt::Array<int32_t>(rippedItems == nullptr ? 0 : rippedItems->size());
  if (rippedItems != nullptr) {
    int32_t rippedIndex = 0;
    for (Item* rippedItem : *rippedItems) {
      (*rippedIds)[rippedIndex++] = rippedItem->getId();
    }
  }
  return new PlannedConnection(netNumber, connection->startItem->getId(),
                               connection->targetItem->getId(), connection->startLayer,
                               connection->targetLayer, rippedIds, segments);
}

jrt::Array<IntPoint*>* PlannedConnection::copy(jrt::Array<IntPoint*>* corners) {
  if (corners == nullptr) {
    return new jrt::Array<IntPoint*>(0);
  }
  auto* copied = new jrt::Array<IntPoint*>(corners->length);
  jrt::arraycopy(corners, 0, copied, 0, corners->length);
  return copied;
}

}  // namespace fr
