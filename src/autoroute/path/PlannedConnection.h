#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** One trace segment of a planned connection. */
class PlannedConnection_Segment final : public virtual jrt::Object {
 public:
  jrt::Array<IntPoint*>* corners;
  int32_t layer;

  PlannedConnection_Segment(jrt::Array<IntPoint*>* corners, int32_t layer)
      : corners(corners), layer(layer) {}
};

/** An immutable snapshot of a found connection, which can be inserted later. */
class PlannedConnection final : public virtual jrt::Object {
 public:
  int32_t netNumber;
  int32_t startItemId;
  int32_t targetItemId;
  int32_t startLayer;
  int32_t targetLayer;
  jrt::Array<int32_t>* rippedItemIds;
  jrt::Array<PlannedConnection_Segment*>* segments;

 private:
  PlannedConnection(int32_t netNumber, int32_t startItemId, int32_t targetItemId,
                    int32_t startLayer, int32_t targetLayer, jrt::Array<int32_t>* rippedItemIds,
                    jrt::Array<PlannedConnection_Segment*>* segments)
      : netNumber(netNumber),
        startItemId(startItemId),
        targetItemId(targetItemId),
        startLayer(startLayer),
        targetLayer(targetLayer),
        rippedItemIds(rippedItemIds),
        segments(segments) {}

 public:
  /** Creates a planned connection from a located connection and the items ripped for it. */
  static PlannedConnection* from(int32_t netNumber, FoundConnectionLocator* connection,
                                 jrt::Collection<Item*>* rippedItems);

 private:
  static jrt::Array<IntPoint*>* copy(jrt::Array<IntPoint*>* corners);
};

}  // namespace fr
