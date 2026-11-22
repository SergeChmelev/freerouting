// Port of app.freerouting.board.model.items.PadPlaneConnection (record, + nested Mode, Spoke).
// Fully implemented (tiny type): see PadPlaneConnection.cpp.
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The kinds of connection between a pad and the fill of a plane. */
enum class PadPlaneConnection_Mode : int32_t {
  /** Not set here: the plane decides. */
  INHERITED,
  /** The fill keeps its clearance to the pad: the plane never connects it. */
  NONE,
  /** The fill stops at the thermal gap; up to four spokes cross it. */
  THERMAL,
  /** The fill covers the pad. */
  SOLID,
  /** A thermal relief for a through-hole pad, a solid connection for any other pad. */
  THT_THERMAL
};

inline std::string PadPlaneConnection_Mode_name(PadPlaneConnection_Mode e) {
  static const char* const kNames[] = {"INHERITED", "NONE", "THERMAL", "SOLID", "THT_THERMAL"};
  return kNames[static_cast<int32_t>(e)];
}
inline std::string PadPlaneConnection_Mode_toString(PadPlaneConnection_Mode e) {
  return PadPlaneConnection_Mode_name(e);
}
inline jrt::Array<PadPlaneConnection_Mode>* PadPlaneConnection_Mode_values() {
  using E = PadPlaneConnection_Mode;
  return jrt::Array<E>::of({E::INHERITED, E::NONE, E::THERMAL, E::SOLID, E::THT_THERMAL});
}
inline PadPlaneConnection_Mode PadPlaneConnection_Mode_valueOf(const std::string& s) {
  for (auto e : *PadPlaneConnection_Mode_values()) {
    if (PadPlaneConnection_Mode_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.board.model.items.PadPlaneConnection.Mode." + s);
}

/** One spoke of a thermal relief: its centre line from the edge of the pad to its far end. */
class PadPlaneConnection_Spoke : public virtual jrt::Object {
 private:
  double startX_;
  double startY_;
  double endX_;
  double endY_;
  double halfWidth_;

 public:
  PadPlaneConnection_Spoke(double startX, double startY, double endX, double endY, double halfWidth)
      : startX_(startX), startY_(startY), endX_(endX), endY_(endY), halfWidth_(halfWidth) {}

  double startX() { return startX_; }
  double startY() { return startY_; }
  double endX() { return endX_; }
  double endY() { return endY_; }
  double halfWidth() { return halfWidth_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * How the host CAD system connects a pad to the fill of a plane of its net, when the design file
 * does not say it: solidly, through the spokes of a thermal relief, or not at all.
 */
class PadPlaneConnection : public virtual jrt::Object {
 private:
  PadPlaneConnection_Mode mode_;
  bool throughHole_;
  double centerX_;
  double centerY_;
  double halfWidth_;
  double halfHeight_;
  double orientationDeg_;
  double spokeAngleDeg_;
  bool circular_;
  int32_t thermalGap_;
  int32_t spokeWidth_;

 public:
  PadPlaneConnection(PadPlaneConnection_Mode mode, bool throughHole, double centerX, double centerY,
                     double halfWidth, double halfHeight, double orientationDeg,
                     double spokeAngleDeg, bool circular, int32_t thermalGap, int32_t spokeWidth)
      : mode_(mode),
        throughHole_(throughHole),
        centerX_(centerX),
        centerY_(centerY),
        halfWidth_(halfWidth),
        halfHeight_(halfHeight),
        orientationDeg_(orientationDeg),
        spokeAngleDeg_(spokeAngleDeg),
        circular_(circular),
        thermalGap_(thermalGap),
        spokeWidth_(spokeWidth) {}

  PadPlaneConnection_Mode mode() { return mode_; }
  bool throughHole() { return throughHole_; }
  double centerX() { return centerX_; }
  double centerY() { return centerY_; }
  double halfWidth() { return halfWidth_; }
  double halfHeight() { return halfHeight_; }
  double orientationDeg() { return orientationDeg_; }
  double spokeAngleDeg() { return spokeAngleDeg_; }
  bool circular() { return circular_; }
  int32_t thermalGap() { return thermalGap_; }
  int32_t spokeWidth() { return spokeWidth_; }

  /** The connection of this pad to a plane whose own setting is planeMode. */
  virtual PadPlaneConnection_Mode resolve(PadPlaneConnection_Mode planeMode);

  /** The thermal gap of this pad on a plane whose gap is planeGap. */
  virtual double gapOn(double planeGap) { return this->thermalGap_ > 0 ? this->thermalGap_ : planeGap; }

  /**
   * The four spokes of the thermal relief of this pad, as the zone filler of KiCad builds them: one
   * every 90 degrees from the spoke angle, from the centre of the pad to the bounding box of its
   * shape enlarged by the thermal gap and half the minimum width of the fill. No spoke is built
   * when the spoke would be narrower than the minimum width of the fill.
   */
  virtual jrt::Array<PadPlaneConnection_Spoke*>* spokes(double planeGap, double planeSpokeWidth,
                                                        double minWidth, double endSlack);

 private:
  /** The distance from the centre of a box to its edge along the direction (cos, sin). */
  static double toBoxEdge(double cos, double sin, double halfWidth, double halfHeight);

 public:
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

}  // namespace fr
