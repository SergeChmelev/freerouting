#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Connectivity and component lookups of a BasicBoard (connectable items by net, component items,
 * connected sets). (Java package-private methods are public in C++.)
 */
class BoardConnectivityQueries final : public virtual jrt::Object {
 private:
  BasicBoard* board;
  jrt::Map<int32_t, jrt::List<Item*>*>* connectableItemsByNet = nullptr;  // a HashMap
  int64_t connectableItemsStamp = -1;

 public:
  explicit BoardConnectivityQueries(BasicBoard* board) : board(board) {}

 private:
  jrt::Map<int32_t, jrt::List<Item*>*>* getConnectableIndex();

 public:
  /** Returns all connectable items containing the requested net. */
  jrt::Collection<Item*>* getConnectableItems(int32_t netNumber);

  /** Returns the number of connectable items containing the requested net. */
  int32_t connectableItemCount(int32_t netNumber);

  /** Returns all items belonging to the requested component. */
  jrt::Collection<Item*>* getComponentItems(int32_t componentId);

  /** Returns all pins belonging to the requested component. */
  jrt::Collection<Pin*>* getComponentPins(int32_t componentId);

  /** Returns a component pin by component ID and package pin index. */
  Pin* getPin(int32_t componentId, int32_t pinIndex);

  /** Returns the connected sets for the requested net. */
  jrt::Collection<jrt::Collection<Item*>*>* getConnectedSets(int32_t netNumber);
};

}  // namespace fr
