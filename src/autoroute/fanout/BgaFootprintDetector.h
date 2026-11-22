#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Java local record LocalPoint(Pin pin, double u, double v), declared inside analyzeComponent().
 * Java reads lp.u / lp.v directly; C++ uses the accessors (or the fields via friendship).
 */
class BgaFootprintDetector_LocalPoint : public virtual jrt::Object {
  friend class BgaFootprintDetector;

 private:
  Pin* pin_;
  double u_;
  double v_;

 public:
  BgaFootprintDetector_LocalPoint(Pin* pin, double u, double v) : pin_(pin), u_(u), v_(v) {}
  Pin* pin() { return pin_; }
  double u() { return u_; }
  double v() { return v_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Detects area-array BGA footprints on a PCB and classifies their lattice topology, local
 * coordinate frame, concentric Chebyshev ring depths, and pin routing dispositions.
 */
class BgaFootprintDetector : public virtual jrt::Object {
 private:
  BgaFootprintDetector() = default;

 public:
  /** Scans the board's components and returns descriptors for all components that qualify as BGAs. */
  static jrt::List<BgaEscapeDescriptor*>* detect(RoutingBoard* board, RouterSettings* settings);
  /** As detect(board, settings), reporting to an optional info logger (empty = Java null). */
  static jrt::List<BgaEscapeDescriptor*>* detect(
      RoutingBoard* board, RouterSettings* settings, std::function<void(const std::string&)> infoLogger);
  /**
   * Analyzes an SMD component to determine if it is an area-array BGA and computes its descriptor
   * (nullptr if it is not one).
   */
  static BgaEscapeDescriptor* analyzeComponent(
      RoutingBoard* board, Component* comp, jrt::List<Pin*>* pins, RouterSettings* settings);

 private:
  static int32_t countDistinct(jrt::List<double>* values, double tolerance);
};

}  // namespace fr
