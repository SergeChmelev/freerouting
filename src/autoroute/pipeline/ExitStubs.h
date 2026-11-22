#pragma once

#include <functional>
#include <regex>

#include "board/model/structure/FixedState.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Java record Mirror(IntPoint bend, IntPoint end). */
class ExitStubs_Mirror : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  IntPoint* bend_;
  IntPoint* end_;

 public:
  ExitStubs_Mirror(IntPoint* bend, IntPoint* end) : bend_(bend), end_(end) {}
  IntPoint* bend() { return bend_; }
  IntPoint* end() { return end_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Java record Stub(Pin pin, IntPoint end, int halfWidth, boolean inward, double slack, IntPoint
 * bend, Mirror mirror). bend and mirror may be nullptr.
 */
class ExitStubs_Stub : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  Pin* pin_;
  IntPoint* end_;
  int32_t halfWidth_;
  bool inward_;
  double slack_;
  IntPoint* bend_;
  ExitStubs_Mirror* mirror_;

 public:
  ExitStubs_Stub(
      Pin* pin, IntPoint* end, int32_t halfWidth, bool inward, double slack, IntPoint* bend, ExitStubs_Mirror* mirror)
      : pin_(pin), end_(end), halfWidth_(halfWidth), inward_(inward), slack_(slack), bend_(bend), mirror_(mirror) {}
  ExitStubs_Stub(Pin* pin, IntPoint* end, int32_t halfWidth, bool inward, double slack)
      : ExitStubs_Stub(pin, end, halfWidth, inward, slack, nullptr, nullptr) {}
  ExitStubs_Stub(Pin* pin, IntPoint* end, int32_t halfWidth, bool inward, double slack, IntPoint* bend)
      : ExitStubs_Stub(pin, end, halfWidth, inward, slack, bend, nullptr) {}

  Pin* pin() { return pin_; }
  IntPoint* end() { return end_; }
  int32_t halfWidth() { return halfWidth_; }
  bool inward() { return inward_; }
  double slack() { return slack_; }
  IntPoint* bend() { return bend_; }
  ExitStubs_Mirror* mirror() { return mirror_; }

  /** {centre, bend, end} when bend is set, else {centre, end}. */
  virtual jrt::Array<Point*>* corners(IntPoint* centre);
  /** The stub through the mirror's bend and end, or nullptr without a mirror. */
  virtual ExitStubs_Stub* mirrored() {
    return mirror_ != nullptr ? new ExitStubs_Stub(pin_, mirror_->end(), halfWidth_, inward_, slack_, mirror_->bend(), nullptr)
                              : nullptr;
  }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Java private record StubPlacement(Stub stub, List<Item> traces). */
class ExitStubs_StubPlacement : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  ExitStubs_Stub* stub_;
  jrt::List<Item*>* traces_;

 public:
  ExitStubs_StubPlacement(ExitStubs_Stub* stub, jrt::List<Item*>* traces) : stub_(stub), traces_(traces) {}
  ExitStubs_Stub* stub() { return stub_; }
  jrt::List<Item*>* traces() { return traces_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Java private record RowEntry(Stub stub, IntPoint centre, int lateral). */
class ExitStubs_RowEntry : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  ExitStubs_Stub* stub_;
  IntPoint* centre_;
  int32_t lateral_;

 public:
  ExitStubs_RowEntry(ExitStubs_Stub* stub, IntPoint* centre, int32_t lateral)
      : stub_(stub), centre_(centre), lateral_(lateral) {}
  ExitStubs_Stub* stub() { return stub_; }
  IntPoint* centre() { return centre_; }
  int32_t lateral() { return lateral_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Java local record RowKey(int dx, int dy, int line), declared inside ExitStubs.fans(); used as a
 * HashMap key - fully implemented.
 */
class ExitStubs_RowKey : public virtual jrt::Object {
 private:
  int32_t dx_;
  int32_t dy_;
  int32_t line_;

 public:
  ExitStubs_RowKey(int32_t dx, int32_t dy, int32_t line) : dx_(dx), dy_(dy), line_(line) {}
  int32_t dx() { return dx_; }
  int32_t dy() { return dy_; }
  int32_t line() { return line_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<ExitStubs_RowKey>(o);
    if (other == nullptr) return false;
    return dx_ == other->dx_ && dy_ == other->dy_ && line_ == other->line_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(dx_));
    h = jrt::hashCombine(h, jrt::hashOf(dy_));
    h = jrt::hashCombine(h, jrt::hashOf(line_));
    return h;
  }
  std::string toString() override {
    return "RowKey[dx=" + jrt::str(dx_) + ", dy=" + jrt::str(dy_) + ", line=" + jrt::str(line_) + "]";
  }
};

/** Java private record DiagEntry(Stub stub, IntPoint centre, double lateral). */
class ExitStubs_DiagEntry : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  ExitStubs_Stub* stub_;
  IntPoint* centre_;
  double lateral_;

 public:
  ExitStubs_DiagEntry(ExitStubs_Stub* stub, IntPoint* centre, double lateral)
      : stub_(stub), centre_(centre), lateral_(lateral) {}
  ExitStubs_Stub* stub() { return stub_; }
  IntPoint* centre() { return centre_; }
  double lateral() { return lateral_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Java private static class DiagRow. */
class ExitStubs_DiagRow : public virtual jrt::Object {
 public:
  int32_t dx;
  int32_t dy;
  double line;
  /** Java init: new ArrayList<>() (set in the constructor, .cpp). */
  jrt::List<ExitStubs_DiagEntry*>* row;

  ExitStubs_DiagRow(int32_t dx, int32_t dy, double line);
};

/** Java public record DiagonalResult(int dx, int dy, double halfLength, double halfWidth) - fully implemented. */
class ExitStubs_DiagonalResult : public virtual jrt::Object {
 private:
  int32_t dx_;
  int32_t dy_;
  double halfLength_;
  double halfWidth_;

 public:
  ExitStubs_DiagonalResult(int32_t dx, int32_t dy, double halfLength, double halfWidth)
      : dx_(dx), dy_(dy), halfLength_(halfLength), halfWidth_(halfWidth) {}
  int32_t dx() { return dx_; }
  int32_t dy() { return dy_; }
  double halfLength() { return halfLength_; }
  double halfWidth() { return halfWidth_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<ExitStubs_DiagonalResult>(o);
    if (other == nullptr) return false;
    return dx_ == other->dx_ && dy_ == other->dy_ && jrt::equalsOf(halfLength_, other->halfLength_) &&
           jrt::equalsOf(halfWidth_, other->halfWidth_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(dx_));
    h = jrt::hashCombine(h, jrt::hashOf(dy_));
    h = jrt::hashCombine(h, jrt::hashOf(halfLength_));
    h = jrt::hashCombine(h, jrt::hashOf(halfWidth_));
    return h;
  }
  std::string toString() override {
    return "DiagonalResult[dx=" + jrt::str(dx_) + ", dy=" + jrt::str(dy_) + ", halfLength=" + jrt::str(halfLength_) +
           ", halfWidth=" + jrt::str(halfWidth_) + "]";
  }
};

/** Java private record JointEntry(PolylineTrace trace, List<Point> points). */
class ExitStubs_JointEntry : public virtual jrt::Object {
  friend class ExitStubs;

 private:
  PolylineTrace* trace_;
  jrt::List<Point*>* points_;

 public:
  ExitStubs_JointEntry(PolylineTrace* trace, jrt::List<Point*>* points) : trace_(trace), points_(points) {}
  PolylineTrace* trace() { return trace_; }
  jrt::List<Point*>* points() { return points_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Erik Gavriluk (freerouting.net) (router.autorouter.exit_stubs): the straight way out of a
 * fine-pitch pin whose only way out meets the clearances to its neighbours exactly, placed before
 * the fanout as a designer draws it.
 */
class ExitStubs : public virtual jrt::Object {
 public:
  static constexpr FixedState PLACED_STATE = FixedState::USER_FIXED;
  static constexpr int32_t MIN_PINS = 8;
  // PORT NOTE: Java = 2 * ClearanceMatrix.clearance_safety_margin (16). Literal to avoid including
  // ClearanceMatrix.h; the .cpp must static_assert(TIGHT_SLACK == 2 * ClearanceMatrix::clearance_safety_margin).
  static constexpr int32_t TIGHT_SLACK = 2 * 16;
  static constexpr double INWARD_REACH_PAD_LENGTHS = 3.0;

 private:
  /** Java Pattern "Pad_([0-9]+(?:\\.[0-9]+)?)x([0-9]+(?:\\.[0-9]+)?)_" (Matcher.find -> std::regex_search). */
  static std::regex& KICAD_PAD_SIZE();
  /** Java Pattern "^(.*)#tolerance[0-9]+$". */
  static std::regex& TOLERANCE_COPY();

  ExitStubs() = default;

 public:
  /** Returns the line to log, or empty (Java null) when nothing was done. */
  static std::optional<std::string> apply(RouterSettings* settings, RoutingBoard* board, jrt::List<int32_t>* itemIds);
  static bool endReachable(RoutingBoard* board, ExitStubs_Stub* stub);
  static int32_t endProbeLength(
      BasicBoard* board, IntPoint* end, int32_t layer, int32_t half, int32_t net, int32_t traceClass);
  static jrt::List<ExitStubs_Stub*>* plan(BasicBoard* board, RouterSettings* settings);
  static jrt::List<ExitStubs_Stub*>* plan(BasicBoard* board);

 private:
  static jrt::List<ExitStubs_Stub*>* fans(BasicBoard* board, jrt::List<ExitStubs_Stub*>* stubs, int32_t allowance);
  static jrt::List<ExitStubs_Stub*>* diagonalFans(
      BasicBoard* board, jrt::List<ExitStubs_Stub*>* stubs, int32_t allowance, ClearanceMatrix* matrix, int32_t margin);
  static jrt::List<ExitStubs_Stub*>* diagonalFanOut(
      BasicBoard* board,
      jrt::List<ExitStubs_DiagEntry*>* run,
      int32_t dx,
      int32_t dy,
      int32_t allowance,
      ClearanceMatrix* matrix,
      int32_t margin,
      bool force);
  static jrt::List<ExitStubs_Stub*>* reachable(
      BasicBoard* board,
      jrt::List<ExitStubs_Stub*>* planned,
      std::function<jrt::List<ExitStubs_Stub*>*()> fanned,
      ClearanceMatrix* matrix,
      std::function<jrt::List<ExitStubs_Stub*>*()> staircase,
      int32_t runCount,
      jrt::Set<int32_t>* inwardPins);
  static jrt::List<ExitStubs_Stub*>* staircase(
      BasicBoard* board,
      jrt::List<ExitStubs_RowEntry*>* run,
      int32_t dx,
      int32_t dy,
      int32_t line,
      int32_t allowance,
      ClearanceMatrix* matrix,
      int32_t margin);

 public:
  static bool reachableAmong(
      BasicBoard* board, ExitStubs_Stub* stub, jrt::List<ExitStubs_Stub*>* run, ClearanceMatrix* matrix);
  static double segmentsDistance(FloatPoint* a, FloatPoint* b, FloatPoint* c, FloatPoint* d);

 private:
  static jrt::List<ExitStubs_Stub*>* fanOut(
      BasicBoard* board,
      jrt::List<ExitStubs_RowEntry*>* run,
      int32_t dx,
      int32_t dy,
      int32_t line,
      int32_t allowance,
      ClearanceMatrix* matrix,
      int32_t margin,
      bool force);
  static int32_t traceClass(BasicBoard* board, Pin* pin);
  static jrt::List<ExitStubs_Stub*>* candidates(
      BasicBoard* board,
      RouterSettings* settings,
      Pin* pin,
      jrt::List<Pin*>* partPins,
      jrt::Map<int32_t, int32_t>* pinsOfNet,
      double cx,
      double cy,
      int32_t allowance);
  static ExitStubs_Stub* outward(
      BasicBoard* board,
      RouterSettings* settings,
      Pin* pin,
      jrt::List<Pin*>* partPins,
      jrt::Map<int32_t, int32_t>* pinsOfNet,
      double cx,
      double cy,
      int32_t allowance,
      jrt::Array<ExitStubs_Stub*>* inwardHolder);
  static ExitStubs_Stub* diagonalOutward(
      BasicBoard* board,
      Pin* pin,
      TileShape* shape,
      IntPoint* centre,
      int32_t half,
      int32_t classHalf,
      int32_t net,
      int32_t layer,
      int32_t traceClass,
      double cx,
      double cy,
      int32_t allowance);

 public:
  static ExitStubs_DiagonalResult* diagonal(TileShape* shape);

 private:
  static bool routerPasses(
      BasicBoard* board, IntPoint* from, IntPoint* to, int32_t layer, int32_t half, int32_t net, int32_t traceClass);
  static bool isTolerancePin(BasicBoard* board, Pin* pin);
  static std::string padNumber(Pin* pin);
  static int32_t widest(
      BasicBoard* board,
      IntPoint* from,
      IntPoint* to,
      int32_t layer,
      int32_t half,
      int32_t classHalf,
      int32_t net,
      int32_t traceClass,
      int32_t allowance);

 public:
  /**
   * The point a stub starts from: the pin centre, the only point at which a trace joins a pin
   * (see DrillItem::getNormalContacts()).
   */
  static IntPoint* centre(Pin* pin);
  static double slack(
      BasicBoard* board,
      IntPoint* from,
      IntPoint* to,
      int32_t layer,
      int32_t half,
      int32_t net,
      int32_t traceClass,
      int32_t allowance,
      jrt::Array<int32_t>* largestHolder);
  static double pathSlack(
      BasicBoard* board,
      jrt::Array<Point*>* corners,
      int32_t layer,
      int32_t half,
      int32_t net,
      int32_t traceClass,
      int32_t allowance);
  static int32_t ruleClearance(BasicBoard* board, int32_t traceClass, Item* item, int32_t layer, int32_t allowance);
  static int32_t copperAllowance(BasicBoard* board, RouterSettings* settings);
  static int32_t modelExcess(BasicBoard* board, Item* item, TileShape* shape);
  static double segmentDistance(FloatPoint* a, FloatPoint* b, TileShape* shape);

 private:
  static double pointSegment(FloatPoint* p, FloatPoint* a, FloatPoint* b);
  static bool cross(FloatPoint* a, FloatPoint* b, FloatPoint* c, FloatPoint* d);
  static double side(FloatPoint* p, FloatPoint* q, FloatPoint* r);

 public:
  /** Returns the line to log, or empty (Java null). */
  static std::optional<std::string> release(RoutingBoard* board, jrt::Collection<int32_t>* itemIds);

 private:
  static int32_t snapNearJoints(RoutingBoard* board, PolylineTrace* stub);

 public:
  static IntPoint* nearestOnLine(jrt::Array<Point*>* line, IntPoint* at);
  static jrt::List<Point*>* joints(Trace* trace);
  /** PORT NOTE: Java Iterable<Integer> -> jrt::Collection<int32_t>*. */
  static int32_t open(RoutingBoard* board, jrt::Collection<int32_t>* nets);
};

}  // namespace fr
