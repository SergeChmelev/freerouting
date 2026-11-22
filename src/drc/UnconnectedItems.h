#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Information about an unconnected NET (not individual items). Each instance represents ONE net
 * that has multiple disconnected groups of items; allItems contains ALL items of the net.
 * (Constructors implemented in UnconnectedItems.cpp: jrt::ArrayList<Item*> needs Item complete.)
 */
class UnconnectedItems : public virtual jrt::Object {
 public:
  /** The first item that is unconnected (representative from first connected group). */
  Item* firstItem;

  /** The second item that is unconnected (representative from second connected group). */
  Item* secondItem;

  /** All items from the unconnected net (for better visibility of affected components). */
  jrt::List<Item*>* allItems;

  /** The type of the unconnected item. */
  std::string type;

  /** Creates a new instance of UnconnectedItems with two representative items. */
  UnconnectedItems(Item* firstItem, Item* secondItem);

  /** Creates a new instance of UnconnectedItems with all items from the net. */
  UnconnectedItems(Item* firstItem, Item* secondItem, jrt::List<Item*>* allItems);

  /** Creates a new instance of UnconnectedItems with a specific type. */
  UnconnectedItems(Item* firstItem, Item* secondItem, const std::string& type);

  /** Creates a new instance of UnconnectedItems with all items and a specific type. */
  UnconnectedItems(Item* firstItem, Item* secondItem, jrt::List<Item*>* allItems,
                   const std::string& type);
};

}  // namespace fr
