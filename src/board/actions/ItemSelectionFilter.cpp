// Port of app.freerouting.board.actions.ItemSelectionFilter.
#include "board/actions/ItemSelectionFilter.h"

#include "board/model/items/Item.h"

namespace fr {

namespace {
int32_t ord(ItemSelectionFilter_SelectableChoices c) { return static_cast<int32_t>(c); }
}  // namespace

ItemSelectionFilter::ItemSelectionFilter() {
  this->values = new jrt::Array<bool>(ItemSelectionFilter_SelectableChoices_values()->length);
  jrt::Arrays::fill(this->values, true);
  (*this->values)[ord(ItemSelectionFilter_SelectableChoices::KEEPOUT)] = false;
  (*this->values)[ord(ItemSelectionFilter_SelectableChoices::VIA_KEEPOUT)] = false;
  (*this->values)[ord(ItemSelectionFilter_SelectableChoices::COMPONENT_KEEPOUT)] = false;
  (*this->values)[ord(ItemSelectionFilter_SelectableChoices::CONDUCTION)] = false;
  (*this->values)[ord(ItemSelectionFilter_SelectableChoices::BOARD_OUTLINE)] = false;
}

ItemSelectionFilter::ItemSelectionFilter(ItemSelectionFilter_SelectableChoices itemType) {
  this->values = new jrt::Array<bool>(ItemSelectionFilter_SelectableChoices_values()->length);
  (*values)[ord(itemType)] = true;
  (*values)[ord(ItemSelectionFilter_SelectableChoices::FIXED)] = true;
  (*values)[ord(ItemSelectionFilter_SelectableChoices::UNFIXED)] = true;
}

ItemSelectionFilter::ItemSelectionFilter(
    jrt::Array<ItemSelectionFilter_SelectableChoices>* itemTypes) {
  this->values = new jrt::Array<bool>(ItemSelectionFilter_SelectableChoices_values()->length);
  for (int32_t i = 0; i < itemTypes->length; i++) {
    (*values)[ord((*itemTypes)[i])] = true;
  }
  (*values)[ord(ItemSelectionFilter_SelectableChoices::FIXED)] = true;
  (*values)[ord(ItemSelectionFilter_SelectableChoices::UNFIXED)] = true;
}

ItemSelectionFilter::ItemSelectionFilter(ItemSelectionFilter* itemSelectionFilter) {
  this->values = itemSelectionFilter->values->clone();
}

void ItemSelectionFilter::selectAll() { jrt::Arrays::fill(values, true); }

void ItemSelectionFilter::deselectAll() { jrt::Arrays::fill(values, false); }

jrt::Set<Item*>* ItemSelectionFilter::filter(jrt::Set<Item*>* items) {
  jrt::Set<Item*>* result = new jrt::TreeSet<Item*>();
  for (Item* currentItem : *items) {
    if (currentItem->isSelectedByFilter(this)) {
      result->add(currentItem);
    }
  }
  return result;
}

}  // namespace fr
