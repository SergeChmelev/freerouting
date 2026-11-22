// Port of app.freerouting.rules.CornerStyle
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Geometric corner style for trace length tuning meander patterns. */
enum class CornerStyle : int32_t { FILLET, CHAMFER };

inline std::string CornerStyle_name(CornerStyle e) {
  switch (e) {
    case CornerStyle::FILLET:
      return "FILLET";
    case CornerStyle::CHAMFER:
      return "CHAMFER";
  }
  return "";
}

/** Java toString(): the lower-case name. */
inline std::string CornerStyle_toString(CornerStyle e) {
  return jrt::toLowerCase(CornerStyle_name(e));
}

inline jrt::Array<CornerStyle>* CornerStyle_values() {
  return jrt::Array<CornerStyle>::of({CornerStyle::FILLET, CornerStyle::CHAMFER});
}

inline CornerStyle CornerStyle_valueOf(const std::string& name) {
  for (CornerStyle e : *CornerStyle_values()) {
    if (CornerStyle_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant CornerStyle." + name);
}

/**
 * Parses a corner style from a string in a case-insensitive manner. Returns FILLET if null, blank
 * or unrecognised.
 */
inline CornerStyle CornerStyle_parse(const std::optional<std::string>& name) {
  if (!name.has_value() || jrt::strip(*name).empty()) {
    return CornerStyle::FILLET;
  }
  try {
    return CornerStyle_valueOf(jrt::toUpperCase(jrt::trim(*name)));
  } catch (const jrt::IllegalArgumentException&) {
    return CornerStyle::FILLET;
  }
}

}  // namespace fr
