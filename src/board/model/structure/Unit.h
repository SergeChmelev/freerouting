// Port of app.freerouting.board.model.structure.Unit
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Physical units of measure. */
enum class Unit : int32_t { MIL, INCH, MM, UM };

inline std::string Unit_name(Unit e) {
  switch (e) {
    case Unit::MIL:
      return "MIL";
    case Unit::INCH:
      return "INCH";
    case Unit::MM:
      return "MM";
    case Unit::UM:
      return "UM";
  }
  return "";
}

/** Java toString(): the lower-case name. */
inline std::string Unit_toString(Unit e) { return jrt::toLowerCase(Unit_name(e)); }

inline jrt::Array<Unit>* Unit_values() {
  return jrt::Array<Unit>::of({Unit::MIL, Unit::INCH, Unit::MM, Unit::UM});
}

inline Unit Unit_valueOf(const std::string& name) {
  for (Unit e : *Unit_values()) {
    if (Unit_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant Unit." + name);
}

/** The enum field micrometers: the size of one unit in micrometers. */
inline double Unit_micrometers(Unit self) {
  switch (self) {
    case Unit::MIL:
      return 25.4;
    case Unit::INCH:
      return 25400;
    case Unit::MM:
      return 1000;
    case Unit::UM:
      return 1;
  }
  return 1;
}

/** Scales value from fromUnit to toUnit. */
inline double Unit_scale(double value, Unit fromUnit, Unit toUnit) {
  return value * Unit_micrometers(fromUnit) / Unit_micrometers(toUnit);
}

/** Parses a unit name case-insensitively; nullopt (Java null) if unknown. */
inline std::optional<Unit> Unit_fromString(const std::string& string) {
  try {
    return Unit_valueOf(jrt::toUpperCase(string));
  } catch (const jrt::IllegalArgumentException&) {
    return std::nullopt;
  }
}

}  // namespace fr
