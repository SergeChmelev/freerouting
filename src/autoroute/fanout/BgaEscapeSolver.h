#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Encapsulates an escaped pin proposal ready for continuous DRC validation (Java record). */
class BgaEscapeSolver_PinEscapeProposal : public virtual jrt::Object {
 private:
  BgaEscapeDescriptor_BgaPinDescriptor* pinDescriptor_;
  IntPoint* viaCenter_;
  int32_t targetLayer_;
  int32_t cost_;
  bool escaped_;

 public:
  BgaEscapeSolver_PinEscapeProposal(
      BgaEscapeDescriptor_BgaPinDescriptor* pinDescriptor, IntPoint* viaCenter, int32_t targetLayer, int32_t cost,
      bool escaped)
      : pinDescriptor_(pinDescriptor), viaCenter_(viaCenter), targetLayer_(targetLayer), cost_(cost), escaped_(escaped) {}
  BgaEscapeDescriptor_BgaPinDescriptor* pinDescriptor() { return pinDescriptor_; }
  IntPoint* viaCenter() { return viaCenter_; }
  int32_t targetLayer() { return targetLayer_; }
  int32_t cost() { return cost_; }
  bool escaped() { return escaped_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Deterministic CPU wavefront solver for multi-layer BGA escape routing. Evaluates concentric
 * Chebyshev rings from the outer perimeter inward, assigning surface-layer direct escapes and
 * 4-quadrant interstitial dogbone vias.
 */
class BgaEscapeSolver : public virtual jrt::Object {
 private:
  BgaEscapeSolver() = default;

 public:
  /**
   * Computes the pitch-harmonic grid step in board units:
   * deltaGrid = clamp(min(floor(P / 8), floor((wMin + cMin) / 2)), deltaMin, deltaMax).
   */
  static int32_t computeGridStep(RoutingBoard* board, double pitchUm, double pitchBoardUnits);
  /** Solves deterministic CPU BGA escape routing for the specified area-array component. */
  static jrt::List<BgaEscapeSolver_PinEscapeProposal*>* solve(RoutingBoard* board, BgaEscapeDescriptor* bga);
  /** As solve(board, bga), reporting progress to an optional info logger (empty = Java null). */
  static jrt::List<BgaEscapeSolver_PinEscapeProposal*>* solve(
      RoutingBoard* board, BgaEscapeDescriptor* bga, std::function<void(const std::string&)> infoLogger);
  /**
   * Deterministic ring-staged wavefront solver: evaluates 4-quadrant interstitial dogbone sites in
   * concentric Chebyshev ring order from the outer perimeter inward using a quantized local grid.
   */
  static jrt::List<BgaEscapeSolver_PinEscapeProposal*>* executeDeterministicWavefront(
      BgaEscapeDescriptor* bga, int32_t gridStepBoardUnits);
};

}  // namespace fr
