// Port of app.freerouting.board.model.structure.AngleRestriction
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Restricts the trace angles: none, 45 degree or 90 degree. ordinal() and values() rely on the order. */
enum class AngleRestriction : int32_t { NONE, FORTYFIVE_DEGREE, NINETY_DEGREE };

inline std::string AngleRestriction_name(AngleRestriction e) {
  switch (e) {
    case AngleRestriction::NONE:
      return "NONE";
    case AngleRestriction::FORTYFIVE_DEGREE:
      return "FORTYFIVE_DEGREE";
    case AngleRestriction::NINETY_DEGREE:
      return "NINETY_DEGREE";
  }
  return "";
}

inline std::string AngleRestriction_toString(AngleRestriction e) { return AngleRestriction_name(e); }

inline jrt::Array<AngleRestriction>* AngleRestriction_values() {
  return jrt::Array<AngleRestriction>::of({AngleRestriction::NONE,
                                           AngleRestriction::FORTYFIVE_DEGREE,
                                           AngleRestriction::NINETY_DEGREE});
}

inline AngleRestriction AngleRestriction_valueOf(const std::string& name) {
  for (AngleRestriction e : *AngleRestriction_values()) {
    if (AngleRestriction_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant AngleRestriction." + name);
}

/** Java static valueOf(int): values()[i]. */
inline AngleRestriction AngleRestriction_valueOf(int32_t i) { return (*AngleRestriction_values())[i]; }

/** Java getValue(): the ordinal. */
inline int32_t AngleRestriction_getValue(AngleRestriction self) {
  return static_cast<int32_t>(self);
}

}  // namespace fr
