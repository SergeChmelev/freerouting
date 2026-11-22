#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Enumeration class for keywords of the specctra dsn file format.
 *
 * Scope keywords with individual read-scope methods are defined in separate classes
 * (subclasses of ScopeKeyword).
 *
 * PORT NOTE: the Java "public static final Keyword X = new Keyword(...)" instances are static
 * accessor functions (Keyword::X()), each returning one unique instance, so identity comparison
 * (token == Keyword::X()) works as in Java. Defined in Keyword.cpp.
 */
class Keyword : public virtual jrt::Object {
 public:
  static Keyword* ABSOLUTE();
  static Keyword* ACTIVE();
  static Keyword* AGAINST_PREFERRED_DIRECTION_TRACE_COSTS();
  static Keyword* ATTACH();
  static Keyword* AUTOROUTE();
  static Keyword* AUTOROUTE_SETTINGS();
  static Keyword* BACK();
  static Keyword* BOUNDARY();
  static Keyword* CHAMFER();
  static Keyword* CIRCUIT();
  static Keyword* CIRCLE();
  static Keyword* CLASS();
  static Keyword* CLASS_CLASS();
  static Keyword* CLASSES();
  static ScopeKeyword* COMPONENT_SCOPE();
  static Keyword* CONSTANT();
  static Keyword* CONTROL();
  static Keyword* CORNER_STYLE();
  static Keyword* CLEARANCE();
  static Keyword* CLEARANCE_CLASS();
  static Keyword* CLOSED_BRACKET();
  static Keyword* DIFF_PAIR();
  static Keyword* DIFF_PAIR_SKEW();
  static Keyword* FANOUT();
  static Keyword* FILLET();
  static Keyword* FLIP_STYLE();
  static Keyword* FIX();
  static Keyword* FORTYFIVE_DEGREE();
  static Keyword* FROMTO();
  static Keyword* FRONT();
  static Keyword* GENERATED_BY_FREEROUTING();
  static Keyword* HORIZONTAL();
  static Keyword* HOST_CAD();
  static Keyword* HOST_VERSION();
  static Keyword* IMAGE();
  static Keyword* JUMPER();
  static Keyword* KEEPOUT();
  static Keyword* LAYER();
  static Keyword* LAYER_RULE();
  static Keyword* LENGTH();
  static Keyword* LENGTH_AMPLITUDE();
  static Keyword* LENGTH_GAP();
  static ScopeKeyword* LIBRARY_SCOPE();
  static Keyword* LOCK_TYPE();
  static Keyword* LOGICAL_PART();
  static Keyword* LOGICAL_PART_MAPPING();
  static Keyword* MEANDER_PATTERN();
  static Keyword* NET();
  static Keyword* NETWORK_OUT();
  static ScopeKeyword* NETWORK_SCOPE();
  static Keyword* NINETY_DEGREE();
  static Keyword* NONE();
  static Keyword* NORMAL();
  static Keyword* OFF();
  static Keyword* ON();
  static Keyword* OPEN_BRACKET();
  static Keyword* ORDER();
  static Keyword* OUTLINE();
  static Keyword* PADSTACK();
  static ScopeKeyword* PART_LIBRARY_SCOPE();
  static ScopeKeyword* PARSER_SCOPE();
  static Keyword* PATTERN_TYPE();
  static ScopeKeyword* PCB_SCOPE();
  static Keyword* PIN();
  static Keyword* PINS();
  static Keyword* PLACE();
  static ScopeKeyword* PLACE_CONTROL();
  static Keyword* PLACE_KEEPOUT();
  static ScopeKeyword* PLACEMENT_SCOPE();
  static ScopeKeyword* PLANE_SCOPE();
  static Keyword* PLANE_VIA_COSTS();
  static Keyword* PREFERRED_DIRECTION();
  static Keyword* PREFERRED_DIRECTION_TRACE_COSTS();
  static Keyword* SNAP_ANGLE();
  static Keyword* POLYGON();
  static Keyword* PN();
  static Keyword* POLYGON_PATH();
  static Keyword* POLYLINE_PATH();
  static Keyword* POSITION();
  static Keyword* POSTROUTE();
  static Keyword* POWER();
  static Keyword* PULL_TIGHT();
  static Keyword* RADIUS();
  static Keyword* RECTANGLE();
  static ScopeKeyword* RESOLUTION_SCOPE();
  static Keyword* ROTATE();
  static Keyword* ROTATE_FIRST();
  static Keyword* ROUTES();
  static Keyword* RULE();
  static Keyword* RULES();
  static Keyword* SESSION();
  static Keyword* SHAPE();
  static Keyword* SHOVE_FIXED();
  static Keyword* SIDE();
  static Keyword* SIGNAL();
  static Keyword* SINGLE_SIDED();
  static Keyword* SPARE();
  static Keyword* START_PASS_NO();
  static Keyword* START_RIPUP_COSTS();
  static Keyword* STRING_QUOTE();
  static ScopeKeyword* STRUCTURE_SCOPE();
  static Keyword* TYPE();
  static Keyword* USE_LAYER();
  static Keyword* USE_NET();
  static Keyword* USE_VIA();
  static Keyword* VERTICAL();
  static Keyword* VIA();
  static Keyword* VIAS();
  static Keyword* VIA_AT_SMD();
  static Keyword* VIA_COSTS();
  static Keyword* VIA_KEEPOUT();
  static Keyword* VIA_RULE();
  static Keyword* WIDTH();
  static Keyword* WINDOW();
  static Keyword* WIRE();
  static Keyword* WIRE_KEEPOUT();
  static ScopeKeyword* WIRING_SCOPE();
  static Keyword* WRITE_RESOLUTION();

  /** Returns the name string of this Keyword. The name is used for debugging purposes. */
  std::string getName() { return name; }

  /**
   * Checks whether the token matches the given keyword either by reference equality or by
   * case-insensitive string name comparison (token is a Keyword or a JString).
   */
  static bool matches(jrt::Object* token, Keyword* keyword);

 protected:
  /** Prevents creating more instances. */
  explicit Keyword(const std::string& name) : name(name) {}

 private:
  std::string name;
};

}  // namespace fr::dsn