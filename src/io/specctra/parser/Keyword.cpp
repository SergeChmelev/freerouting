#include "io/specctra/parser/Keyword.h"

#include "io/specctra/parser/Component.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Library.h"
#include "io/specctra/parser/Network.h"
#include "io/specctra/parser/Parser.h"
#include "io/specctra/parser/PartLibrary.h"
#include "io/specctra/parser/PlaceControl.h"
#include "io/specctra/parser/Placement.h"
#include "io/specctra/parser/Plane.h"
#include "io/specctra/parser/Resolution.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/Structure.h"
#include "io/specctra/parser/Wiring.h"

namespace fr::dsn {

Keyword* Keyword::ABSOLUTE() {
  static Keyword* v = new Keyword("absolute");
  return v;
}

Keyword* Keyword::ACTIVE() {
  static Keyword* v = new Keyword("active");
  return v;
}

Keyword* Keyword::AGAINST_PREFERRED_DIRECTION_TRACE_COSTS() {
  static Keyword* v = new Keyword("against_preferred_direction_trace_costs");
  return v;
}

Keyword* Keyword::ATTACH() {
  static Keyword* v = new Keyword("attach");
  return v;
}

Keyword* Keyword::AUTOROUTE() {
  static Keyword* v = new Keyword("autoroute");
  return v;
}

Keyword* Keyword::AUTOROUTE_SETTINGS() {
  static Keyword* v = new Keyword("autoroute_settings");
  return v;
}

Keyword* Keyword::BACK() {
  static Keyword* v = new Keyword("back");
  return v;
}

Keyword* Keyword::BOUNDARY() {
  static Keyword* v = new Keyword("boundary");
  return v;
}

Keyword* Keyword::CHAMFER() {
  static Keyword* v = new Keyword("chamfer");
  return v;
}

Keyword* Keyword::CIRCUIT() {
  static Keyword* v = new Keyword("circuit");
  return v;
}

Keyword* Keyword::CIRCLE() {
  static Keyword* v = new Keyword("circle");
  return v;
}

Keyword* Keyword::CLASS() {
  static Keyword* v = new Keyword("class");
  return v;
}

Keyword* Keyword::CLASS_CLASS() {
  static Keyword* v = new Keyword("class_class");
  return v;
}

Keyword* Keyword::CLASSES() {
  static Keyword* v = new Keyword("classes");
  return v;
}

ScopeKeyword* Keyword::COMPONENT_SCOPE() {
  static ScopeKeyword* v = new Component();
  return v;
}

Keyword* Keyword::CONSTANT() {
  static Keyword* v = new Keyword("constant");
  return v;
}

Keyword* Keyword::CONTROL() {
  static Keyword* v = new Keyword("control");
  return v;
}

Keyword* Keyword::CORNER_STYLE() {
  static Keyword* v = new Keyword("corner_style");
  return v;
}

Keyword* Keyword::CLEARANCE() {
  static Keyword* v = new Keyword("clearance");
  return v;
}

Keyword* Keyword::CLEARANCE_CLASS() {
  static Keyword* v = new Keyword("clearance_class");
  return v;
}

Keyword* Keyword::CLOSED_BRACKET() {
  static Keyword* v = new Keyword(")");
  return v;
}

Keyword* Keyword::DIFF_PAIR() {
  static Keyword* v = new Keyword("diff_pair");
  return v;
}

Keyword* Keyword::DIFF_PAIR_SKEW() {
  static Keyword* v = new Keyword("diff_pair_skew");
  return v;
}

Keyword* Keyword::FANOUT() {
  static Keyword* v = new Keyword("fanout");
  return v;
}

Keyword* Keyword::FILLET() {
  static Keyword* v = new Keyword("fillet");
  return v;
}

Keyword* Keyword::FLIP_STYLE() {
  static Keyword* v = new Keyword("flip_style");
  return v;
}

Keyword* Keyword::FIX() {
  static Keyword* v = new Keyword("fix");
  return v;
}

Keyword* Keyword::FORTYFIVE_DEGREE() {
  static Keyword* v = new Keyword("fortyfive_degree");
  return v;
}

Keyword* Keyword::FROMTO() {
  static Keyword* v = new Keyword("fromto");
  return v;
}

Keyword* Keyword::FRONT() {
  static Keyword* v = new Keyword("front");
  return v;
}

Keyword* Keyword::GENERATED_BY_FREEROUTING() {
  static Keyword* v = new Keyword("generated_by_freerouting");
  return v;
}

Keyword* Keyword::HORIZONTAL() {
  static Keyword* v = new Keyword("horizontal");
  return v;
}

Keyword* Keyword::HOST_CAD() {
  static Keyword* v = new Keyword("host_cad");
  return v;
}

Keyword* Keyword::HOST_VERSION() {
  static Keyword* v = new Keyword("host_version");
  return v;
}

Keyword* Keyword::IMAGE() {
  static Keyword* v = new Keyword("image");
  return v;
}

Keyword* Keyword::JUMPER() {
  static Keyword* v = new Keyword("jumper");
  return v;
}

Keyword* Keyword::KEEPOUT() {
  static Keyword* v = new Keyword("keepout");
  return v;
}

Keyword* Keyword::LAYER() {
  static Keyword* v = new Keyword("layer");
  return v;
}

Keyword* Keyword::LAYER_RULE() {
  static Keyword* v = new Keyword("layer_rule");
  return v;
}

Keyword* Keyword::LENGTH() {
  static Keyword* v = new Keyword("length");
  return v;
}

Keyword* Keyword::LENGTH_AMPLITUDE() {
  static Keyword* v = new Keyword("length_amplitude");
  return v;
}

Keyword* Keyword::LENGTH_GAP() {
  static Keyword* v = new Keyword("length_gap");
  return v;
}

ScopeKeyword* Keyword::LIBRARY_SCOPE() {
  static ScopeKeyword* v = new Library();
  return v;
}

Keyword* Keyword::LOCK_TYPE() {
  static Keyword* v = new Keyword("lock_type");
  return v;
}

Keyword* Keyword::LOGICAL_PART() {
  static Keyword* v = new Keyword("logical_part");
  return v;
}

Keyword* Keyword::LOGICAL_PART_MAPPING() {
  static Keyword* v = new Keyword("logical_part_mapping");
  return v;
}

Keyword* Keyword::MEANDER_PATTERN() {
  static Keyword* v = new Keyword("meander_pattern");
  return v;
}

Keyword* Keyword::NET() {
  static Keyword* v = new Keyword("net");
  return v;
}

Keyword* Keyword::NETWORK_OUT() {
  static Keyword* v = new Keyword("network_out");
  return v;
}

ScopeKeyword* Keyword::NETWORK_SCOPE() {
  static ScopeKeyword* v = new Network();
  return v;
}

Keyword* Keyword::NINETY_DEGREE() {
  static Keyword* v = new Keyword("ninety_degree");
  return v;
}

Keyword* Keyword::NONE() {
  static Keyword* v = new Keyword("none");
  return v;
}

Keyword* Keyword::NORMAL() {
  static Keyword* v = new Keyword("normal");
  return v;
}

Keyword* Keyword::OFF() {
  static Keyword* v = new Keyword("off");
  return v;
}

Keyword* Keyword::ON() {
  static Keyword* v = new Keyword("on");
  return v;
}

Keyword* Keyword::OPEN_BRACKET() {
  static Keyword* v = new Keyword("(");
  return v;
}

Keyword* Keyword::ORDER() {
  static Keyword* v = new Keyword("order");
  return v;
}

Keyword* Keyword::OUTLINE() {
  static Keyword* v = new Keyword("outline");
  return v;
}

Keyword* Keyword::PADSTACK() {
  static Keyword* v = new Keyword("padstack");
  return v;
}

ScopeKeyword* Keyword::PART_LIBRARY_SCOPE() {
  static ScopeKeyword* v = new PartLibrary();
  return v;
}

ScopeKeyword* Keyword::PARSER_SCOPE() {
  static ScopeKeyword* v = new Parser();
  return v;
}

Keyword* Keyword::PATTERN_TYPE() {
  static Keyword* v = new Keyword("pattern_type");
  return v;
}

ScopeKeyword* Keyword::PCB_SCOPE() {
  static ScopeKeyword* v = new ScopeKeyword("pcb");
  return v;
}

Keyword* Keyword::PIN() {
  static Keyword* v = new Keyword("pin");
  return v;
}

Keyword* Keyword::PINS() {
  static Keyword* v = new Keyword("pins");
  return v;
}

Keyword* Keyword::PLACE() {
  static Keyword* v = new Keyword("place");
  return v;
}

ScopeKeyword* Keyword::PLACE_CONTROL() {
  static ScopeKeyword* v = new PlaceControl();
  return v;
}

Keyword* Keyword::PLACE_KEEPOUT() {
  static Keyword* v = new Keyword("place_keepout");
  return v;
}

ScopeKeyword* Keyword::PLACEMENT_SCOPE() {
  static ScopeKeyword* v = new Placement();
  return v;
}

ScopeKeyword* Keyword::PLANE_SCOPE() {
  static ScopeKeyword* v = new Plane();
  return v;
}

Keyword* Keyword::PLANE_VIA_COSTS() {
  static Keyword* v = new Keyword("plane_via_costs");
  return v;
}

Keyword* Keyword::PREFERRED_DIRECTION() {
  static Keyword* v = new Keyword("preferred_direction");
  return v;
}

Keyword* Keyword::PREFERRED_DIRECTION_TRACE_COSTS() {
  static Keyword* v = new Keyword("preferred_direction_trace_costs");
  return v;
}

Keyword* Keyword::SNAP_ANGLE() {
  static Keyword* v = new Keyword("snap_angle");
  return v;
}

Keyword* Keyword::POLYGON() {
  static Keyword* v = new Keyword("polygon");
  return v;
}

Keyword* Keyword::PN() {
  static Keyword* v = new Keyword("PN");
  return v;
}

Keyword* Keyword::POLYGON_PATH() {
  static Keyword* v = new Keyword("polygon_path");
  return v;
}

Keyword* Keyword::POLYLINE_PATH() {
  static Keyword* v = new Keyword("polyline_path");
  return v;
}

Keyword* Keyword::POSITION() {
  static Keyword* v = new Keyword("position");
  return v;
}

Keyword* Keyword::POSTROUTE() {
  static Keyword* v = new Keyword("postroute");
  return v;
}

Keyword* Keyword::POWER() {
  static Keyword* v = new Keyword("power");
  return v;
}

Keyword* Keyword::PULL_TIGHT() {
  static Keyword* v = new Keyword("pull_tight");
  return v;
}

Keyword* Keyword::RADIUS() {
  static Keyword* v = new Keyword("radius");
  return v;
}

Keyword* Keyword::RECTANGLE() {
  static Keyword* v = new Keyword("rectangle");
  return v;
}

ScopeKeyword* Keyword::RESOLUTION_SCOPE() {
  static ScopeKeyword* v = new Resolution();
  return v;
}

Keyword* Keyword::ROTATE() {
  static Keyword* v = new Keyword("rotate");
  return v;
}

Keyword* Keyword::ROTATE_FIRST() {
  static Keyword* v = new Keyword("rotate_first");
  return v;
}

Keyword* Keyword::ROUTES() {
  static Keyword* v = new Keyword("routes");
  return v;
}

Keyword* Keyword::RULE() {
  static Keyword* v = new Keyword("rule");
  return v;
}

Keyword* Keyword::RULES() {
  static Keyword* v = new Keyword("rules");
  return v;
}

Keyword* Keyword::SESSION() {
  static Keyword* v = new Keyword("session");
  return v;
}

Keyword* Keyword::SHAPE() {
  static Keyword* v = new Keyword("shape");
  return v;
}

Keyword* Keyword::SHOVE_FIXED() {
  static Keyword* v = new Keyword("shove_fixed");
  return v;
}

Keyword* Keyword::SIDE() {
  static Keyword* v = new Keyword("side");
  return v;
}

Keyword* Keyword::SIGNAL() {
  static Keyword* v = new Keyword("signal");
  return v;
}

Keyword* Keyword::SINGLE_SIDED() {
  static Keyword* v = new Keyword("single_sided");
  return v;
}

Keyword* Keyword::SPARE() {
  static Keyword* v = new Keyword("spare");
  return v;
}

Keyword* Keyword::START_PASS_NO() {
  static Keyword* v = new Keyword("start_pass_no");
  return v;
}

Keyword* Keyword::START_RIPUP_COSTS() {
  static Keyword* v = new Keyword("start_ripup_costs");
  return v;
}

Keyword* Keyword::STRING_QUOTE() {
  static Keyword* v = new Keyword("string_quote");
  return v;
}

ScopeKeyword* Keyword::STRUCTURE_SCOPE() {
  static ScopeKeyword* v = new Structure();
  return v;
}

Keyword* Keyword::TYPE() {
  static Keyword* v = new Keyword("type");
  return v;
}

Keyword* Keyword::USE_LAYER() {
  static Keyword* v = new Keyword("use_layer");
  return v;
}

Keyword* Keyword::USE_NET() {
  static Keyword* v = new Keyword("use_net");
  return v;
}

Keyword* Keyword::USE_VIA() {
  static Keyword* v = new Keyword("use_via");
  return v;
}

Keyword* Keyword::VERTICAL() {
  static Keyword* v = new Keyword("vertical");
  return v;
}

Keyword* Keyword::VIA() {
  static Keyword* v = new Keyword("via");
  return v;
}

Keyword* Keyword::VIAS() {
  static Keyword* v = new Keyword("vias");
  return v;
}

Keyword* Keyword::VIA_AT_SMD() {
  static Keyword* v = new Keyword("via_at_smd");
  return v;
}

Keyword* Keyword::VIA_COSTS() {
  static Keyword* v = new Keyword("via_costs");
  return v;
}

Keyword* Keyword::VIA_KEEPOUT() {
  static Keyword* v = new Keyword("via_keepout");
  return v;
}

Keyword* Keyword::VIA_RULE() {
  static Keyword* v = new Keyword("via_rule");
  return v;
}

Keyword* Keyword::WIDTH() {
  static Keyword* v = new Keyword("width");
  return v;
}

Keyword* Keyword::WINDOW() {
  static Keyword* v = new Keyword("window");
  return v;
}

Keyword* Keyword::WIRE() {
  static Keyword* v = new Keyword("wire");
  return v;
}

Keyword* Keyword::WIRE_KEEPOUT() {
  static Keyword* v = new Keyword("wire_keepout");
  return v;
}

ScopeKeyword* Keyword::WIRING_SCOPE() {
  static ScopeKeyword* v = new Wiring();
  return v;
}

Keyword* Keyword::WRITE_RESOLUTION() {
  static Keyword* v = new Keyword("write_resolution");
  return v;
}

bool Keyword::matches(jrt::Object* token, Keyword* keyword) {
  if (token == keyword) {
    return true;
  }
  if (auto* str = jrt::fast_cast<JString>(token); str != nullptr && keyword != nullptr) {
    return jrt::equalsIgnoreCase(str->value, keyword->getName());
  }
  return false;
}

}  // namespace fr::dsn