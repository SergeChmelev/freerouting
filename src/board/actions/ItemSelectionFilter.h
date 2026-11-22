// Port of app.freerouting.board.actions.ItemSelectionFilter (+ nested enum SelectableChoices).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The possible choices in the filter. */
enum class ItemSelectionFilter_SelectableChoices : int32_t {
  TRACES,
  VIAS,
  PINS,
  CONDUCTION,
  KEEPOUT,
  VIA_KEEPOUT,
  COMPONENT_KEEPOUT,
  BOARD_OUTLINE,
  FIXED,
  UNFIXED
};

inline std::string ItemSelectionFilter_SelectableChoices_name(ItemSelectionFilter_SelectableChoices e) {
  static const char* const kNames[] = {"TRACES",      "VIAS",          "PINS",
                                       "CONDUCTION",  "KEEPOUT",       "VIA_KEEPOUT",
                                       "COMPONENT_KEEPOUT", "BOARD_OUTLINE", "FIXED",
                                       "UNFIXED"};
  return kNames[static_cast<int32_t>(e)];
}
inline std::string ItemSelectionFilter_SelectableChoices_toString(ItemSelectionFilter_SelectableChoices e) {
  return ItemSelectionFilter_SelectableChoices_name(e);
}
inline jrt::Array<ItemSelectionFilter_SelectableChoices>* ItemSelectionFilter_SelectableChoices_values() {
  using E = ItemSelectionFilter_SelectableChoices;
  return jrt::Array<E>::of({E::TRACES, E::VIAS, E::PINS, E::CONDUCTION, E::KEEPOUT, E::VIA_KEEPOUT,
                            E::COMPONENT_KEEPOUT, E::BOARD_OUTLINE, E::FIXED, E::UNFIXED});
}
inline ItemSelectionFilter_SelectableChoices ItemSelectionFilter_SelectableChoices_valueOf(const std::string& s) {
  for (auto e : *ItemSelectionFilter_SelectableChoices_values()) {
    if (ItemSelectionFilter_SelectableChoices_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.board.actions.ItemSelectionFilter.SelectableChoices." + s);
}

/** Filter for selecting items on the board. */
class ItemSelectionFilter : public virtual jrt::Object {
 private:
  /** The filter array of the item types. */
  jrt::Array<bool>* values;  // final

 public:
  /** Creates a new filter with all item types selected. */
  ItemSelectionFilter();

  /** Creates a new filter with only itemType selected. */
  explicit ItemSelectionFilter(ItemSelectionFilter_SelectableChoices itemType);

  /** Creates a new filter with only itemTypes selected. */
  explicit ItemSelectionFilter(jrt::Array<ItemSelectionFilter_SelectableChoices>* itemTypes);

  /** Copy constructor. */
  explicit ItemSelectionFilter(ItemSelectionFilter* itemSelectionFilter);

  /** Selects or deselects an item type. */
  virtual void setSelected(ItemSelectionFilter_SelectableChoices choice, bool value) {
    (*values)[static_cast<int32_t>(choice)] = value;
  }

  /** Selects all item types. */
  virtual void selectAll();

  /** Deselects all item types. */
  virtual void deselectAll();

  /** Filters a collection of items with this filter. */
  virtual jrt::Set<Item*>* filter(jrt::Set<Item*>* items);

  /** Looks, if the input item type is selected. */
  virtual bool isSelected(ItemSelectionFilter_SelectableChoices choice) {
    return (*values)[static_cast<int32_t>(choice)];
  }
};

}  // namespace fr
