#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The diagnostic geometry category used by GUI/debug adapters. */
enum class AutorouteDiagnostic_Kind : int32_t { FREE_SPACE_ROOM, OBSTACLE_ROOM, EXPANSION_DRILL };

inline std::string AutorouteDiagnostic_Kind_name(AutorouteDiagnostic_Kind e) {
  switch (e) {
    case AutorouteDiagnostic_Kind::FREE_SPACE_ROOM: return "FREE_SPACE_ROOM";
    case AutorouteDiagnostic_Kind::OBSTACLE_ROOM: return "OBSTACLE_ROOM";
    case AutorouteDiagnostic_Kind::EXPANSION_DRILL: return "EXPANSION_DRILL";
  }
  return "";
}
inline std::string AutorouteDiagnostic_Kind_toString(AutorouteDiagnostic_Kind e) {
  return AutorouteDiagnostic_Kind_name(e);
}
inline jrt::Array<AutorouteDiagnostic_Kind>* AutorouteDiagnostic_Kind_values() {
  return jrt::Array<AutorouteDiagnostic_Kind>::of({AutorouteDiagnostic_Kind::FREE_SPACE_ROOM,
                                                   AutorouteDiagnostic_Kind::OBSTACLE_ROOM,
                                                   AutorouteDiagnostic_Kind::EXPANSION_DRILL});
}
inline AutorouteDiagnostic_Kind AutorouteDiagnostic_Kind_valueOf(const std::string& s) {
  if (s == "FREE_SPACE_ROOM") return AutorouteDiagnostic_Kind::FREE_SPACE_ROOM;
  if (s == "OBSTACLE_ROOM") return AutorouteDiagnostic_Kind::OBSTACLE_ROOM;
  if (s == "EXPANSION_DRILL") return AutorouteDiagnostic_Kind::EXPANSION_DRILL;
  throw jrt::IllegalArgumentException("No enum constant AutorouteDiagnostic.Kind." + s);
}

/** Receives diagnostic snapshots without introducing a GUI dependency into autoroute. */
class AutorouteDiagnostic_Sink : public virtual jrt::Object {
 public:
  /** Accepts one diagnostic snapshot. */
  virtual void accept(AutorouteDiagnostic* diagnostic) = 0;

  /** Adapter for Java lambdas passed as Sink. */
  static AutorouteDiagnostic_Sink* of(std::function<void(AutorouteDiagnostic*)> f);
};

/** A headless snapshot of geometry exposed for optional autorouter diagnostics (Java record). */
class AutorouteDiagnostic : public virtual jrt::Object {
 public:
  /** Creates a validated diagnostic snapshot (throws on null kind/shape or negative intensity). */
  AutorouteDiagnostic(AutorouteDiagnostic_Kind kind, TileShape* shape, int32_t layer,
                      double intensity);

  AutorouteDiagnostic_Kind kind() { return kind_; }
  TileShape* shape() { return shape_; }
  int32_t layer() { return layer_; }
  double intensity() { return intensity_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  AutorouteDiagnostic_Kind kind_;
  TileShape* shape_;
  int32_t layer_;
  double intensity_;
};

}  // namespace fr
