#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Class with functionality for optimising traces and vias (pull tight). Concrete variants:
 * TraceTightener90, TraceTightener45, TraceTightenerAnyAngle, chosen by getInstance() from the
 * board's trace angle restriction.
 */
class TraceTightener : public virtual jrt::Object {
 protected:
  static constexpr double c_max_cos_angle = 0.999;
  // with angles to close to 180 degree the algorithm becomes numerically unstable
  static constexpr double c_min_corner_dist_square = 0.9;

 public:
  /**
   * The most rounds a fixpoint loop of this algorithm may run. Its steps can undo each other, so
   * the loops need a bound to terminate. Counting rounds instead of milliseconds gives the same
   * result on every machine and under any load. (Java package-private)
   */
  static constexpr int32_t MAX_ROUNDS = 1000;

 protected:
  RoutingBoard* board;

 public:
  /** If only_net_no > 0, only nets with this net numbers are optimized. */
  jrt::Array<int32_t>* onlyNetNoArr;

 private:
  /** If stoppableThread != null, the algorithm can be requested to be stopped. */
  Stoppable* stoppableThread;

  TimeLimit* timeLimit;

  /**
   * If keepPoint != null, traces containing the keepPoint must also contain the keepPoint after
   * optimizing.
   */
  Point* keepPoint;

  int32_t keepPointLayer;

 protected:
  int32_t currentLayer = 0;
  int32_t currentHalfWidth = 0;
  jrt::Array<int32_t>* currentNetNumbers = nullptr;
  int32_t currentClearanceClassIndex = 0;
  IntOctagon* currentClipShape = nullptr;
  jrt::Set<Pin*>* contactPins = nullptr;
  int32_t minTranslateDist = 0;

  /**
   * The minimum room an acute-angle chamfer at a contact with another trace needs (FR-079): at
   * least 2 units and at least a quarter of the full chamfer ((sqrt(2) - 1) * half_width / 4).
   */
  virtual double getMinContactChamferRoom();

  /** Creates a new instance of TraceTightener. (Java package-private) */
  TraceTightener(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                 Stoppable* stoppableThread, int32_t timeLimit, Point* keepPoint,
                 int32_t keepPointLayer);

 public:
  /**
   * Returns a new instance of TraceTightener. If onlyNetNo > 0, only traces with net number
   * notNo are optimized. If stoppableThread != null, the algorithm can be requested to be
   * stopped. If timeLimit > 0; the algorithm will be stopped after timeLimit Milliseconds.
   */
  static TraceTightener* getInstance(RoutingBoard* board, jrt::Array<int32_t>* onlyNetNoArr,
                                     IntOctagon* clipShape, int32_t minTranslateDist,
                                     Stoppable* stoppableThread, int32_t timeLimit,
                                     Point* keepPoint, int32_t keepPointLayer);

  /**
   * Function for optimizing the route in an internal marked area. If clipShape != null, the
   * optimizing area is restricted to clipShape. traceCosts is used for optimizing vias and may
   * be null.
   */
  virtual void optChangedArea(jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts);

  /**
   * Function for optimizing a single trace polygon. contactPins are the pins at the end corners
   * of polyline. Other pins are regarded as obstacles, even if they are of the own net.
   */
  virtual Polyline* pullTight(Polyline* polyline, int32_t layer, int32_t halfWidth,
                              jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                              jrt::Set<Pin*>* contactPins);

  virtual Polyline* pullTight(Polyline* polyline) = 0;

 protected:
  /** Returns whether a fixpoint loop that has run the given number of rounds must stop. */
  virtual bool mustStopAfter(int32_t rounds);

  /** Terminates the pull tight algorithm, if the user has made a stop request. */
  virtual bool isStopRequested();

 public:
  /** Tries to shorten polyline by relocating its lines. (Java package-private) */
  virtual Polyline* repositionLines(Polyline* polyline);

 protected:
  /**
   * Tries to reposition the line with index no to make the polyline consisting of lines
   * shorter.
   */
  virtual Line* repositionLine(jrt::Array<Line*>* lines, int32_t no);

 public:
  /**
   * Tries to skip line segments of length 0. A check is necessary before skipping because new
   * dog ears may occur. (Java package-private, non-overridden)
   */
  virtual Polyline* skipSegmentsOfLength0(Polyline* polyline);

  /** Smoothens acute angles with contact traces. Returns true, if something was changed. */
  virtual bool smoothenEndCornersAtTrace(PolylineTrace* trace);

 private:
  /** Smoothens acute angles with contact traces. Returns true, if something was changed. */
  bool smoothenEndCornersAtTrace1(PolylineTrace* trace);

 public:
  /**
   * Splits the traces containing this.keepPoint if this.keepPoint != null. Returns true, if
   * something was split.
   */
  virtual bool splitTracesAtKeepPoint();

 private:
  /** Smoothens acute angles with contact traces. Returns null, if nothing was changed. */
  Polyline* smoothenEndCornersAtTrace2(PolylineTrace* trace);

 protected:
  /** Wraps around pins of the own net to avoid acid traps (currently disabled in Java). */
  virtual Polyline* avoidAcidTraps(Polyline* polyline);

 public:
  /** (Java package-private abstract; subclasses make it public) */
  virtual Polyline* smoothenStartCornerAtTrace(PolylineTrace* trace) = 0;

  virtual Polyline* smoothenEndCornerAtTrace(PolylineTrace* trace) = 0;
};

}  // namespace fr
