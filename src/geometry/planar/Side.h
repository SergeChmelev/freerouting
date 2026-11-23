#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implementation of an enum class Side with the three values ON_THE_LEFT, ON_THE_RIGHT, COLLINEAR.
 * PORT NOTE: Java "enum-like" class with reference statics -> static accessor functions; the three
 * instances are unique, so identity comparison (==) works as in Java.
 */
class Side final : public virtual jrt::Object {
 public:
  static Side* ON_THE_LEFT();
  static Side* ON_THE_RIGHT();
  static Side* COLLINEAR();

  /** Returns ON_THE_LEFT if value > 0, ON_THE_RIGHT if value < 0, and COLLINEAR if value == 0. */
  static Side* of(double value);

  /** Returns the string of this instance. */
  std::string toString() override { return name; }

  /** Returns the opposite side of this side. */
  Side* negate();

 private:
  explicit Side(const std::string& name) : name(name) {}

  std::string name;
};

// Defined inline: called in the innermost geometry loops (sideOf), an out-of-line call per use
// showed up in profiles.
inline Side* Side::ON_THE_LEFT() {
  static Side* v = jrt::onHeap([] { return new (jrt::kGeom) Side("onTheLeft"); });
  return v;
}

inline Side* Side::ON_THE_RIGHT() {
  static Side* v = jrt::onHeap([] { return new (jrt::kGeom) Side("onTheRight"); });
  return v;
}

inline Side* Side::COLLINEAR() {
  static Side* v = jrt::onHeap([] { return new (jrt::kGeom) Side("collinear"); });
  return v;
}

}  // namespace fr
