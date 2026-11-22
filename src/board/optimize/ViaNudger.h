#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** What a run of the pass did, and the board before and after it. Lengths are in board units. */
class ViaNudger_Result : public virtual jrt::Object {
 public:
  ViaNudger_Result(int32_t passes, int32_t triedMoves, int32_t acceptedMoves, double lengthBefore, double lengthAfter, int32_t bendsBefore, int32_t bendsAfter, int32_t viasBefore, int32_t viasAfter, int32_t incompleteBefore, int32_t incompleteAfter, int32_t violationsBefore, int32_t violationsAfter)
      : passes_(passes), triedMoves_(triedMoves), acceptedMoves_(acceptedMoves), lengthBefore_(lengthBefore), lengthAfter_(lengthAfter), bendsBefore_(bendsBefore), bendsAfter_(bendsAfter), viasBefore_(viasBefore), viasAfter_(viasAfter), incompleteBefore_(incompleteBefore), incompleteAfter_(incompleteAfter), violationsBefore_(violationsBefore), violationsAfter_(violationsAfter) {}

  int32_t passes() { return passes_; }
  int32_t triedMoves() { return triedMoves_; }
  int32_t acceptedMoves() { return acceptedMoves_; }
  double lengthBefore() { return lengthBefore_; }
  double lengthAfter() { return lengthAfter_; }
  int32_t bendsBefore() { return bendsBefore_; }
  int32_t bendsAfter() { return bendsAfter_; }
  int32_t viasBefore() { return viasBefore_; }
  int32_t viasAfter() { return viasAfter_; }
  int32_t incompleteBefore() { return incompleteBefore_; }
  int32_t incompleteAfter() { return incompleteAfter_; }
  int32_t violationsBefore() { return violationsBefore_; }
  int32_t violationsAfter() { return violationsAfter_; }

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<ViaNudger_Result>(o);
    if (other == nullptr) return false;
    return passes_ == other->passes_ &&
           triedMoves_ == other->triedMoves_ &&
           acceptedMoves_ == other->acceptedMoves_ &&
           jrt::equalsOf(lengthBefore_, other->lengthBefore_) &&
           jrt::equalsOf(lengthAfter_, other->lengthAfter_) &&
           bendsBefore_ == other->bendsBefore_ &&
           bendsAfter_ == other->bendsAfter_ &&
           viasBefore_ == other->viasBefore_ &&
           viasAfter_ == other->viasAfter_ &&
           incompleteBefore_ == other->incompleteBefore_ &&
           incompleteAfter_ == other->incompleteAfter_ &&
           violationsBefore_ == other->violationsBefore_ &&
           violationsAfter_ == other->violationsAfter_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(passes_));
    h = jrt::hashCombine(h, jrt::hashOf(triedMoves_));
    h = jrt::hashCombine(h, jrt::hashOf(acceptedMoves_));
    h = jrt::hashCombine(h, jrt::hashOf(lengthBefore_));
    h = jrt::hashCombine(h, jrt::hashOf(lengthAfter_));
    h = jrt::hashCombine(h, jrt::hashOf(bendsBefore_));
    h = jrt::hashCombine(h, jrt::hashOf(bendsAfter_));
    h = jrt::hashCombine(h, jrt::hashOf(viasBefore_));
    h = jrt::hashCombine(h, jrt::hashOf(viasAfter_));
    h = jrt::hashCombine(h, jrt::hashOf(incompleteBefore_));
    h = jrt::hashCombine(h, jrt::hashOf(incompleteAfter_));
    h = jrt::hashCombine(h, jrt::hashOf(violationsBefore_));
    h = jrt::hashCombine(h, jrt::hashOf(violationsAfter_));
    return h;
  }
  std::string toString() override {
    return "Result[passes=" + jrt::str(passes_) +
           ", triedMoves=" + jrt::str(triedMoves_) +
           ", acceptedMoves=" + jrt::str(acceptedMoves_) +
           ", lengthBefore=" + jrt::str(lengthBefore_) +
           ", lengthAfter=" + jrt::str(lengthAfter_) +
           ", bendsBefore=" + jrt::str(bendsBefore_) +
           ", bendsAfter=" + jrt::str(bendsAfter_) +
           ", viasBefore=" + jrt::str(viasBefore_) +
           ", viasAfter=" + jrt::str(viasAfter_) +
           ", incompleteBefore=" + jrt::str(incompleteBefore_) +
           ", incompleteAfter=" + jrt::str(incompleteAfter_) +
           ", violationsBefore=" + jrt::str(violationsBefore_) +
           ", violationsAfter=" + jrt::str(violationsAfter_) + "]";
  }

 private:
  int32_t passes_;
  int32_t triedMoves_;
  int32_t acceptedMoves_;
  double lengthBefore_;
  double lengthAfter_;
  int32_t bendsBefore_;
  int32_t bendsAfter_;
  int32_t viasBefore_;
  int32_t viasAfter_;
  int32_t incompleteBefore_;
  int32_t incompleteAfter_;
  int32_t violationsBefore_;
  int32_t violationsAfter_;
};

/** (Java private record ViaNudger.Cost) */
class ViaNudger_Cost : public virtual jrt::Object {
 public:
  ViaNudger_Cost(double length, int32_t bends, int32_t vias)
      : length_(length), bends_(bends), vias_(vias) {}

  double length() { return length_; }
  int32_t bends() { return bends_; }
  int32_t vias() { return vias_; }

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<ViaNudger_Cost>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf(length_, other->length_) &&
           bends_ == other->bends_ &&
           vias_ == other->vias_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(length_));
    h = jrt::hashCombine(h, jrt::hashOf(bends_));
    h = jrt::hashCombine(h, jrt::hashOf(vias_));
    return h;
  }
  std::string toString() override {
    return "Cost[length=" + jrt::str(length_) +
           ", bends=" + jrt::str(bends_) +
           ", vias=" + jrt::str(vias_) + "]";
  }

 private:
  double length_;
  int32_t bends_;
  int32_t vias_;
};

/** (Java private record ViaNudger.ZoneRegion) */
class ViaNudger_ZoneRegion : public virtual jrt::Object {
 public:
  ViaNudger_ZoneRegion(int32_t layer, int32_t clearanceClass, IntBox* boundingBox,
                       jrt::Array<TileShape*>* tiles)
      : layer_(layer), clearanceClass_(clearanceClass), boundingBox_(boundingBox), tiles_(tiles) {}

  int32_t layer() { return layer_; }
  int32_t clearanceClass() { return clearanceClass_; }
  IntBox* boundingBox() { return boundingBox_; }
  jrt::Array<TileShape*>* tiles() { return tiles_; }

  // Record equals/hashCode/toString: in the .cpp (component types are incomplete here).
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  int32_t layer_;
  int32_t clearanceClass_;
  IntBox* boundingBox_;
  jrt::Array<TileShape*>* tiles_;
};

/**
 * Moves vias to shorten and straighten the traces of the whole board, not only the traces of the
 * via.
 *
 * ViaOptimizer moves a via towards the next corner of its own two traces, without shoving. This
 * pass tries a fixed set of translations for each via, with shoving and pull tight, and keeps a
 * translation only if the board as a whole gets better (no more vias, incompletes, clearance
 * violations, trace length and bends, and less of one of the last two). Vias touching a plane or
 * in/near a zone stay where they are; translations shoving a trace into a zone are refused. The
 * pass does nothing on a board that already has clearance violations. Deterministic: vias in
 * ascending id order, translations in a fixed order.
 */
class ViaNudger final : public virtual jrt::Object {
 private:
  /** The translations tried for a via, in units of half the pitch of its thinnest trace. */
  static jrt::Array<int32_t>* STEPS_IN_HALF_PITCHES();

  /** {1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1} */
  static jrt::Array<jrt::Array<int32_t>*>* DIRECTIONS();

  /** A bend is worth this many half pitches of trace when two improving moves are compared. */
  static constexpr double BEND_WEIGHT_IN_HALF_PITCHES = 0.5;

  static constexpr int32_t MAX_PASSES = 10;

  /** A via is tried again when a via moved within this many half pitches of it. */
  static constexpr int32_t RETRY_RADIUS_IN_HALF_PITCHES = 50;

  static constexpr int32_t MAX_SHOVE_RECURSION_DEPTH = 9;

  RoutingBoard* board;
  int32_t pullTightAccuracy;
  Stoppable* stoppable;

  /** The planes and copper zones of the board, pre-split into convex tiles. */
  jrt::List<ViaNudger_ZoneRegion*>* zones;

  ViaNudger(RoutingBoard* board, int32_t pullTightAccuracy, Stoppable* stoppable);

 public:
  /** Runs the pass on board until no via can be moved with a gain. */
  static ViaNudger_Result* run(RoutingBoard* board, int32_t pullTightAccuracy,
                               Stoppable* stoppable);

 private:
  ViaNudger_Result* run();

  bool move(Via* via, IntVector* move);

  bool isStopRequested();

  static bool isNear(FloatPoint* point, jrt::List<FloatPoint*>* others, int32_t halfPitch);

  Via* viaById(int32_t id);

  /**
   * A via that connects a plane stays where it is: the board only knows the outline of the
   * plane, not the copper that is left of it once it is filled around the other items.
   */
  static bool touchesConductionArea(Via* via);

  /**
   * RoutingBoard::moveDrillItem unfixes SHOVE_FIXED contacts before moving a drill item; skip
   * vias with any shove-fixed contact so we never unlock protected traces.
   */
  static bool hasShoveFixedContact(Via* via);

  /**
   * Whether via (optionally translated by translation) has any pad inside a zone or closer to
   * its outline than the clearance.
   */
  bool isInOrNearZone(Via* via, IntVector* translation);

  /** Whether copper with the given shape on layer intersects any zone or its clearance envelope. */
  bool shapeEntersZone(Shape* shape, int32_t layer, int32_t clearanceClass);

  /** The corners of every trace, hashed, by trace id. Empty on a board without zones. */
  jrt::Map<int32_t, int32_t>* traceSignatures();

  static int32_t signature(PolylineTrace* trace);

  /** Whether a trace that is new or changed since tracesBefore runs in or too close to a zone. */
  bool changedTraceEntersZone(jrt::Map<int32_t, int32_t>* tracesBefore);

  /** Half of the width plus the clearance of the thinnest trace that ends at via. */
  int32_t halfPitch(Via* via);

  ViaNudger_Cost* cost();
};

}  // namespace fr
