#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** What a run of the pass did, and the board before and after it. Lengths are in board units. */
class StepSlider_Result : public virtual jrt::Object {
 public:
  StepSlider_Result(int32_t passes, int32_t triedMoves, int32_t acceptedMoves, double lengthBefore, double lengthAfter, int32_t bendsBefore, int32_t bendsAfter, int32_t viasBefore, int32_t viasAfter, int32_t incompleteBefore, int32_t incompleteAfter, int32_t violationsBefore, int32_t violationsAfter)
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
    auto* other = dynamic_cast<StepSlider_Result*>(o);
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

/** (Java private record StepSlider.Cost) */
class StepSlider_Cost : public virtual jrt::Object {
 public:
  StepSlider_Cost(double length, int32_t bends, int32_t vias)
      : length_(length), bends_(bends), vias_(vias) {}

  double length() { return length_; }
  int32_t bends() { return bends_; }
  int32_t vias() { return vias_; }

  bool equals(jrt::Object* o) override {
    auto* other = dynamic_cast<StepSlider_Cost*>(o);
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

/**
 * Slides the steps of the traces along them, with shoving, to straighten bundles of traces.
 *
 * A step is a short segment between two segments of the same direction. This pass moves each step
 * forward or backward along its trace, over one or more segments, by translating the part of the
 * trace in between by the step vector, until the step merges with a parallel segment or cancels
 * against an opposite step. The trace is inserted again with shoving and pull tight. A move is
 * kept only if the board as a whole gets better. Moves into / too close to a zone are refused;
 * the pass does nothing on a board that already has clearance violations. Deterministic: traces
 * in ascending id order, steps in trace order, moves in a fixed order.
 */
class StepSlider final : public virtual jrt::Object {
 private:
  /** A step is at most this many pitches of its trace long. */
  static constexpr int32_t MAX_STEP_LENGTH_IN_PITCHES = 4;

  /** A step is moved over at most this many segments. */
  static constexpr int32_t MAX_SLIDE_SEGMENTS = 16;

  /** A bend is worth this many half pitches of trace when two improving moves are compared. */
  static constexpr double BEND_WEIGHT_IN_HALF_PITCHES = 0.5;

  static constexpr int32_t MAX_PASSES = 10;

  static constexpr int32_t MAX_SHOVE_RECURSION_DEPTH = 0;

  RoutingBoard* board;
  int32_t pullTightAccuracy;
  Stoppable* stoppable;

  /** The planes and copper zones of the board. */
  jrt::List<ConductionArea*>* zones;

  StepSlider(RoutingBoard* board, int32_t pullTightAccuracy, Stoppable* stoppable);

 public:
  /** Runs the pass on board until no step can be moved with a gain. */
  static StepSlider_Result* run(RoutingBoard* board, int32_t pullTightAccuracy,
                                Stoppable* stoppable);

 private:
  StepSlider_Result* run();

  /**
   * The corners of trace with one of its steps moved, for every step and every place it can be
   * moved to with a chance of a gain.
   */
  jrt::List<jrt::Array<Point*>*>* moves(PolylineTrace* trace);

  /** Keeps a moved trace that has fewer corners, or as many corners and fewer steps. */
  static void addIfPromising(jrt::List<jrt::Array<Point*>*>* result, jrt::Array<Point*>* moved,
                             int32_t cornerCount, int32_t steps);

 public:
  /**
   * The corners of the trace with the step from corners[k] to corners[k + 1] moved over |shift|
   * segments. The first and the last corner never move. Repeated and collinear corners are
   * dropped. (Java package-private)
   */
  static jrt::Array<Point*>* slide(jrt::Array<IntPoint*>* corners, int32_t k, int32_t shift,
                                   Vector* step);

 private:
  /** Drops repeated corners and corners in the middle of a straight line or of a turn back. */
  static jrt::Array<Point*>* withoutCollinearCorners(jrt::List<Point*>* corners);

 public:
  /**
   * The number of steps of a trace with these corners, whatever their length.
   * (Java package-private; also called with an IntPoint[] - PORT NOTE: callers convert the
   * IntPoint array to a Point array, Java arrays are covariant.)
   */
  static int32_t stepCount(jrt::Array<Point*>* corners);

  /**
   * Whether the segment from corners[k] to corners[k + 1] is a step: the segments before and
   * after it have the same direction, and it is not parallel to them. (Java package-private)
   */
  static bool isStep(jrt::Array<Point*>* corners, int32_t k);

 private:
  /** Replaces the trace with the given id by a trace through corners, inserted with shoving. */
  bool insert(int32_t traceId, jrt::Array<Point*>* corners);

  bool isStopRequested();

  PolylineTrace* traceById(int32_t id);

  /** Whether trace is inside a zone or closer to its outline than the clearance. */
  bool traceEntersZone(PolylineTrace* trace);

  /** The corners of every trace, hashed, by trace id. Empty on a board without zones. */
  jrt::Map<int32_t, int32_t>* traceSignatures();

  static int32_t signature(PolylineTrace* trace);

  /** Whether a trace that is new or changed since tracesBefore runs in or too close to a zone. */
  bool changedTraceEntersZone(jrt::Map<int32_t, int32_t>* tracesBefore);

  /** Half of the width plus the clearance of trace. */
  int32_t halfPitch(PolylineTrace* trace);

  StepSlider_Cost* cost();
};

}  // namespace fr
