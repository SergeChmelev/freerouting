#include "drc/UnconnectedItems.h"

#include "board/model/items/Item.h"

namespace fr {

UnconnectedItems::UnconnectedItems(Item* firstItem, Item* secondItem)
    : UnconnectedItems(firstItem, secondItem, jrt::ArrayList<Item*>::of({firstItem, secondItem}),
                       "unconnectedItems") {}

UnconnectedItems::UnconnectedItems(Item* firstItem, Item* secondItem, jrt::List<Item*>* allItems)
    : UnconnectedItems(firstItem, secondItem, allItems, "unconnectedItems") {}

UnconnectedItems::UnconnectedItems(Item* firstItem, Item* secondItem, const std::string& type)
    : UnconnectedItems(firstItem, secondItem, jrt::ArrayList<Item*>::of({firstItem, secondItem}),
                       type) {}

UnconnectedItems::UnconnectedItems(Item* firstItem, Item* secondItem, jrt::List<Item*>* allItems,
                                   const std::string& type)
    : firstItem(firstItem), secondItem(secondItem), type(type) {
  // Java: allItems != null ? new ArrayList<>(allItems) : Arrays.asList(firstItem, secondItem)
  if (allItems != nullptr) {
    this->allItems = new jrt::ArrayList<Item*>(allItems);
  } else {
    this->allItems = jrt::ArrayList<Item*>::of({firstItem, secondItem});
  }
}

}  // namespace fr
