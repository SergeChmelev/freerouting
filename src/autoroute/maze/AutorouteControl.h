#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Horizontal and vertical costs for traces on a board layer (Java record). */
class AutorouteControl_ExpansionCostFactor : public virtual jrt::Object {
 public:
  AutorouteControl_ExpansionCostFactor(double horizontal, double vertical)
      : horizontal_(horizontal), vertical_(vertical) {}

  double horizontal() { return horizontal_; }
  double vertical() { return vertical_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = dynamic_cast<AutorouteControl_ExpansionCostFactor*>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf<double>(horizontal_, other->horizontal_) &&
           jrt::equalsOf<double>(vertical_, other->vertical_);
  }
  int32_t hashCode() override {
    return jrt::hashCombine(jrt::hashCombine(0, jrt::Double::hashCode(horizontal_)),
                            jrt::Double::hashCode(vertical_));
  }
  std::string toString() override {
    return "ExpansionCostFactor[horizontal=" + jrt::str(horizontal_) +
           ", vertical=" + jrt::str(vertical_) + "]";
  }

 private:
  double horizontal_;
  double vertical_;
};

/** Array of via costs from one layer to the other layers (Java: package-private static class). */
class AutorouteControl_ViaCost final : public virtual jrt::Object {
 public:
  jrt::Array<int32_t>* toLayer;

  // PORT NOTE: private in Java (only AutorouteControl constructs it); public here.
  explicit AutorouteControl_ViaCost(int32_t layerCount)
      : toLayer(new jrt::Array<int32_t>(layerCount)) {}
};

/** Via mask from a layer range (Java: package-private static class). */
class AutorouteControl_ViaMask : public virtual jrt::Object {
 public:
  int32_t fromLayer;
  int32_t toLayer;
  bool attachSmdAllowed;

  AutorouteControl_ViaMask(int32_t fromLayer, int32_t toLayer, bool attachSmdAllowed)
      : fromLayer(fromLayer), toLayer(toLayer), attachSmdAllowed(attachSmdAllowed) {}
};

/** Structure for controlling the autoroute algorithm. */
class AutorouteControl : public virtual jrt::Object {
 public:
  RouterSettings* settings;

  /** The horizontal and vertical trace costs on each layer. */
  jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts;

  /** Bend cost on each layer. */
  jrt::Array<double>* bendCosts;

  /** If true, the autorouter may use trace necking at pins. */
  bool withNeckdown;

  /** Defines for each layer, if it may used for routing. */
  jrt::Array<bool>* layerActive;

  /** The number of layers of the board. */
  int32_t layerCount;

  /** The currently used trace half widths in the autoroute algorithm on each layer. */
  jrt::Array<int32_t>* traceHalfWidth;

  /** The currently used compensated trace half widths on each layer. */
  jrt::Array<int32_t>* compensatedTraceHalfWidth;

  /** Array of length layerCount. The trace clearance offsets on each layer. */
  jrt::Array<int32_t>* traceClearanceOffset;

  /** The via radii on each layer. */
  jrt::Array<double>* viaRadii;

  /** The additional costs for inserting a via from each layer to each other layer. */
  jrt::Array<AutorouteControl_ViaCost*>* addViaCosts;

  /** The currently used clearance class for traces in the autoroute algorithm. */
  int32_t traceClearanceClassIndex = 0;

  /** True, if layer change by inserting of vias is allowed. */
  bool viasAllowed = false;

  /** True, if vias may drill to the pad of SMD pins. */
  bool attachSmdAllowed = false;

  bool viaInPad;
  bool viaTouchingPad;
  bool centreOfGravityViaCandidate;
  double fanoutViaPadGap;

  /** The minimum cost value of all normal vias. */
  double minNormalViaCost = 0;

  /** If true, the autoroute algorithm may rip up existing traces and vias. */
  bool ripupAllowed = false;

  int32_t ripupCosts = 0;
  int32_t ripupPassNo = 0;

  /** Nets that must not be ripped (Java: Collections.emptySet() by default). */
  jrt::Set<int32_t>* netsNotToRip = jrt::Collections::emptySet<int32_t>();

  bool isFanout = false;

  // PORT NOTE: nullable String -> std::optional<std::string>.
  std::optional<std::string> fanoutStartPinName;
  Point* fanoutStartPinCenter = nullptr;
  int32_t fanoutStartPinLayer = -1;
  bool ignoreEscapeWindow = false;

  /** If true, vias not connected to anything are removed by the autorouter. */
  bool removeUnconnectedVias = false;

  /** The currently used via rule. */
  ViaRule* viaRule = nullptr;

  /** The net number of the net currently routed. */
  int32_t netNumber = 0;

  /** The clearance class used for vias of the current net. */
  int32_t viaClearanceClass = 0;

  /** The possible vias, which can be used by the autorouter. */
  jrt::Array<AutorouteControl_ViaMask*>* viaInfos = nullptr;

  /** The lower bound of the first layer of all vias. */
  int32_t viaLowerBound = 0;

  /** The upper bound of the last layer of all vias. */
  int32_t viaUpperBound = 0;

  double maxViaRadius = 0;

  /** The width of the region around changed traces, where traces are pulled tight. */
  int32_t tidyRegionWidth = 0;

  /** The pull tight accuracy of traces. */
  int32_t pullTightAccuracy = 0;

  /** The maximum recursion depth for shoving traces. */
  int32_t maxShoveTraceRecursionDepth = 0;

  /** The maximum recursion depth for shoving vias. */
  int32_t maxShoveViaRecursionDepth = 0;

  /** The maximum recursion depth for traces springing over obstacles. */
  int32_t maxSpringOverRecursionDepth = 0;

  /** The minimum cost value of cheap vias. */
  double minCheapViaCost = 0;

  /** Creates a new instance of AutorouteControl for the input net. */
  AutorouteControl(RoutingBoard* board, int32_t netNumber, RouterSettings* settings);

  /** Creates a new instance of AutorouteControl for the input net. */
  AutorouteControl(RoutingBoard* board, int32_t netNumber, RouterSettings* settings,
                   int32_t viaCosts,
                   jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts);

 private:
  /** Creates a new instance of AutorouteControl. */
  AutorouteControl(RoutingBoard* board, RouterSettings* settings,
                   jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts);

 public:
  /** Whether a via of viaInfo may be attached to a pad. */
  virtual bool viaAttachAllowed(ViaInfo* viaInfo);

  /** Whether the hole of a via of viaInfo is allowed at location. */
  virtual bool viaHoleAllowed(RoutingBoard* board, ViaInfo* viaInfo, Point* location,
                              jrt::Array<int32_t>* netNumbers);

 private:
  static bool isPureSmdNet(RoutingBoard* board, int32_t netNumber);

  static bool hasSmdPin(RoutingBoard* board, int32_t netNumber);

  void initNet(int32_t netNumber, RoutingBoard* board, int32_t viaCosts);

 public:
  /** Returns the trace clearance offset on layer. */
  virtual int32_t getTraceClearanceOffset(int32_t layer);

  /** Recalculates the trace clearance offset on layer. */
  virtual void updateTraceClearanceOffset(int32_t layer, RoutingBoard* board);

  /** Rebuilds the via information for netNumber with viaCosts. */
  virtual void rebuildViaInfo(RoutingBoard* board, int32_t viaCosts, int32_t netNumber);

 private:
  void applySignalLayerViaSurcharge(RoutingBoard* board, int32_t viaCosts, int32_t netNumber,
                                    double viaCostFactor);

 public:
  /** Returns the user's neck width as a trace half width in board units. */
  static int32_t neckHalfWidth(RoutingBoard* board, RouterSettings* settings);
};

}  // namespace fr
