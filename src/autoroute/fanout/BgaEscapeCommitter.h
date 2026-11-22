#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Summary result of a BGA escape commit pass (Java record). */
class BgaEscapeCommitter_CommitResult : public virtual jrt::Object {
 private:
  int32_t pinsAttempted_;
  int32_t pinsCommitted_;
  int32_t viasInserted_;
  int32_t tracesInserted_;
  int32_t drcRejections_;
  jrt::Set<Pin*>* committedPins_;

 public:
  BgaEscapeCommitter_CommitResult(
      int32_t pinsAttempted,
      int32_t pinsCommitted,
      int32_t viasInserted,
      int32_t tracesInserted,
      int32_t drcRejections,
      jrt::Set<Pin*>* committedPins)
      : pinsAttempted_(pinsAttempted),
        pinsCommitted_(pinsCommitted),
        viasInserted_(viasInserted),
        tracesInserted_(tracesInserted),
        drcRejections_(drcRejections),
        committedPins_(committedPins) {}
  /** Java: this(..., Collections.emptySet()) - defined in the .cpp (needs a jrt::HashSet<Pin*>). */
  BgaEscapeCommitter_CommitResult(
      int32_t pinsAttempted, int32_t pinsCommitted, int32_t viasInserted, int32_t tracesInserted, int32_t drcRejections);

  int32_t pinsAttempted() { return pinsAttempted_; }
  int32_t pinsCommitted() { return pinsCommitted_; }
  int32_t viasInserted() { return viasInserted_; }
  int32_t tracesInserted() { return tracesInserted_; }
  int32_t drcRejections() { return drcRejections_; }
  jrt::Set<Pin*>* committedPins() { return committedPins_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Java local record PlaneViaKey(IntPoint location, int netNo), declared inside commit(); used as a
 * HashMap key (equals/hashCode follow the record contract, IntPoint equality by value).
 */
class BgaEscapeCommitter_PlaneViaKey : public virtual jrt::Object {
 private:
  IntPoint* location_;
  int32_t netNo_;

 public:
  BgaEscapeCommitter_PlaneViaKey(IntPoint* location, int32_t netNo) : location_(location), netNo_(netNo) {}
  IntPoint* location() { return location_; }
  int32_t netNo() { return netNo_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/** Java private record ResolvedVia(Padstack padstack, int clearanceClassIdx). */
class BgaEscapeCommitter_ResolvedVia : public virtual jrt::Object {
  friend class BgaEscapeCommitter;

 private:
  Padstack* padstack_;
  int32_t clearanceClassIdx_;

 public:
  BgaEscapeCommitter_ResolvedVia(Padstack* padstack, int32_t clearanceClassIdx)
      : padstack_(padstack), clearanceClassIdx_(clearanceClassIdx) {}
  Padstack* padstack() { return padstack_; }
  int32_t clearanceClassIdx() { return clearanceClassIdx_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Commits BGA escape proposals to the host RoutingBoard, with continuous design rules checking:
 * transforms local coordinates back to global board space using the inverse rotation, validates
 * via clearance and inserts vias and traces, rejecting proposals that violate the rules.
 */
class BgaEscapeCommitter : public virtual jrt::Object {
 private:
  BgaEscapeCommitter() = default;

 public:
  /** Validates and commits the given escape proposals into the board. Returns the commit summary. */
  static BgaEscapeCommitter_CommitResult* commit(
      RoutingBoard* board, BgaEscapeDescriptor* bga, jrt::List<BgaEscapeSolver_PinEscapeProposal*>* proposals);

 private:
  static IntPoint* toGlobalPoint(
      Point* pinCenter, double localPinU, double localPinV, double targetU, double targetV, double cos, double sin);
  static IntPoint* interpolatePoint(Point* from, Point* to, double ratio);
  static double getDistToPerimeter(BgaEscapeDescriptor_BgaPinDescriptor* pd, int32_t sector, BgaEscapeDescriptor* bga);

 public:
  static bool checkViaClearance(
      RoutingBoard* board,
      Padstack* viaPadstack,
      IntPoint* globalViaCenter,
      int32_t netNo,
      jrt::Array<int32_t>* netNumbers,
      int32_t viaClClass);

 private:
  static bool coversSpan(Padstack* ps, int32_t lowL, int32_t highL);
  static BgaEscapeCommitter_ResolvedVia* resolveVia(
      RoutingBoard* board, int32_t fromLayer, int32_t toLayer, NetClass* nc, int32_t defaultViaClClass);
};

}  // namespace fr
