// Port of app.freerouting.board.model.items.BoardItemType.
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Neutral semantic categories for board items. The categories describe the domain object
 * represented by an item without prescribing how it is rendered.
 */
enum class BoardItemType : int32_t {
  TRACE,
  PIN,
  VIA,
  OBSTACLE_AREA,
  VIA_OBSTACLE_AREA,
  WIRE_OBSTACLE_AREA,
  CONDUCTION_AREA,
  COMPONENT_OBSTACLE_AREA,
  BOARD_OUTLINE,
  COMPONENT_OUTLINE,
  OTHER
};

inline std::string BoardItemType_name(BoardItemType e) {
  static const char* const kNames[] = {
      "TRACE",          "PIN",           "VIA",                     "OBSTACLE_AREA",
      "VIA_OBSTACLE_AREA", "WIRE_OBSTACLE_AREA", "CONDUCTION_AREA", "COMPONENT_OBSTACLE_AREA",
      "BOARD_OUTLINE",  "COMPONENT_OUTLINE", "OTHER"};
  return kNames[static_cast<int32_t>(e)];
}
inline std::string BoardItemType_toString(BoardItemType e) { return BoardItemType_name(e); }
inline jrt::Array<BoardItemType>* BoardItemType_values() {
  using E = BoardItemType;
  return jrt::Array<E>::of({E::TRACE, E::PIN, E::VIA, E::OBSTACLE_AREA, E::VIA_OBSTACLE_AREA,
                            E::WIRE_OBSTACLE_AREA, E::CONDUCTION_AREA, E::COMPONENT_OBSTACLE_AREA,
                            E::BOARD_OUTLINE, E::COMPONENT_OUTLINE, E::OTHER});
}
inline BoardItemType BoardItemType_valueOf(const std::string& s) {
  for (auto e : *BoardItemType_values()) {
    if (BoardItemType_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.board.model.items.BoardItemType." + s);
}

}  // namespace fr
