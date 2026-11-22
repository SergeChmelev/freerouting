#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Strategy for selecting items to route during autorouting passes. */
enum class ItemSelectionStrategy : int32_t { SEQUENTIAL, PRIORITIZED };

inline std::string ItemSelectionStrategy_name(ItemSelectionStrategy e) {
  switch (e) {
    case ItemSelectionStrategy::SEQUENTIAL: return "SEQUENTIAL";
    case ItemSelectionStrategy::PRIORITIZED: return "PRIORITIZED";
  }
  return "";
}
inline std::string ItemSelectionStrategy_toString(ItemSelectionStrategy e) {
  return ItemSelectionStrategy_name(e);
}
inline jrt::Array<ItemSelectionStrategy>* ItemSelectionStrategy_values() {
  return jrt::Array<ItemSelectionStrategy>::of(
      {ItemSelectionStrategy::SEQUENTIAL, ItemSelectionStrategy::PRIORITIZED});
}
inline ItemSelectionStrategy ItemSelectionStrategy_valueOf(const std::string& s) {
  if (s == "SEQUENTIAL") return ItemSelectionStrategy::SEQUENTIAL;
  if (s == "PRIORITIZED") return ItemSelectionStrategy::PRIORITIZED;
  throw jrt::IllegalArgumentException("No enum constant ItemSelectionStrategy." + s);
}

}  // namespace fr
