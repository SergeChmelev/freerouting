// Port of app.freerouting.board.model.structure.FixedState
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Sorted fixed states of board items. */
enum class FixedState : int32_t {
  UNFIXED,       // The item is not fixed, it is allowed to move or delete it.
  SHOVE_FIXED,   // The item is fixed by the user against shoves.
  USER_FIXED,    // The item is fixed by the user.
  SYSTEM_FIXED,  // The item is temporarily locked by an algorithm/stage.
  INPUT_FIXED    // The item is fixed by the input design.
};

inline std::string FixedState_name(FixedState e) {
  switch (e) {
    case FixedState::UNFIXED:
      return "UNFIXED";
    case FixedState::SHOVE_FIXED:
      return "SHOVE_FIXED";
    case FixedState::USER_FIXED:
      return "USER_FIXED";
    case FixedState::SYSTEM_FIXED:
      return "SYSTEM_FIXED";
    case FixedState::INPUT_FIXED:
      return "INPUT_FIXED";
  }
  return "";
}

inline std::string FixedState_toString(FixedState e) { return FixedState_name(e); }

inline jrt::Array<FixedState>* FixedState_values() {
  return jrt::Array<FixedState>::of({FixedState::UNFIXED, FixedState::SHOVE_FIXED,
                                     FixedState::USER_FIXED, FixedState::SYSTEM_FIXED,
                                     FixedState::INPUT_FIXED});
}

inline FixedState FixedState_valueOf(const std::string& name) {
  for (FixedState e : *FixedState_values()) {
    if (FixedState_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant FixedState." + name);
}

/** Whether the item must not be changed by algorithms (SYSTEM_FIXED or INPUT_FIXED). */
inline bool FixedState_isFix(FixedState self) {
  return self == FixedState::SYSTEM_FIXED || self == FixedState::INPUT_FIXED;
}

/** Whether the route is preserved (USER_FIXED or INPUT_FIXED). */
inline bool FixedState_isPreservedRoute(FixedState self) {
  return self == FixedState::USER_FIXED || self == FixedState::INPUT_FIXED;
}

}  // namespace fr
