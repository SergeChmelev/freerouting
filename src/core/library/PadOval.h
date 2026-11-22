// Port of app.freerouting.core.library.PadOval
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * The copper of a round or oval pad, as KiCad writes it: a wide path of one or two points. It is
 * every point within radius of the segment from start to end (a circle when they are the same
 * point). The padstack keeps the bounding octagon of this copper as its shape; clearance
 * measurements use this exact copper instead. (Java record.)
 *
 * start/end: the ends of the centre line, in board units, relative to the pad's origin;
 * radius: half the width of the path, in board units.
 */
class PadOval : public virtual jrt::Object {
 public:
  PadOval(FloatPoint* start, FloatPoint* end, double radius)
      : start_(start), end_(end), radius_(radius) {}

  FloatPoint* start() { return start_; }
  FloatPoint* end() { return end_; }
  double radius() { return radius_; }

  /** Record semantics: component-wise equals / 31*h hash / "PadOval[start=.., end=.., radius=..]". */
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  FloatPoint* start_;
  FloatPoint* end_;
  double radius_;
};

}  // namespace fr
