// Port of app.freerouting.rules.BoardRules
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains the rules and constraints required for items to be inserted into a routing board. */
class BoardRules : public virtual jrt::Object {
 public:
  // PORT NOTE (board deepCopy hook): independent deep copy of all mutable rule state (nets, net
  // classes, clearance matrix, via infos/rules, ...). The caller re-points nets to the new board.
  BoardRules* deepCopy();
  /** Creates a new instance of this class. */
  BoardRules(LayerStructure* layerStructure, ClearanceMatrix* clearanceMatrix);

  /**
   * Initializes default length-tuning meander constraints for all pattern types using the given
   * millimeter-to-board scale factor, preserving any constraints explicitly configured from DSN.
   */
  void initDefaultMeanderConstraints(double mmToBoardScale);

  /** Returns whether an explicit meander constraint was configured for the given pattern type. */
  bool hasExplicitMeanderConstraint(MeanderPatternType type);

  /**
   * Returns the default length-tuning meander constraint for the specified pattern type.
   * PORT NOTE: Java accepts a nullable type (null -> SINGLE_TRACK); hence std::optional.
   */
  NetMeanderConstraint* getDefaultMeanderConstraint(std::optional<MeanderPatternType> type);

  /** Sets the default meander constraint for the type (ignored if constraint is null). */
  void setDefaultMeanderConstraint(MeanderPatternType type, NetMeanderConstraint* constraint);

  /**
   * Resolves the effective meander constraint for the given net: net-specific, then net class,
   * then the board default for fallbackType.
   */
  NetMeanderConstraint* getMeanderConstraint(Net* net, MeanderPatternType fallbackType);

  /**
   * Resolves the effective meander constraint for the given net, falling back to the board default
   * (DIFF_PAIR if the net belongs to a registered differential pair, SINGLE_TRACK otherwise).
   */
  NetMeanderConstraint* getMeanderConstraint(Net* net);

  /** Gets the default item clearance class. */
  static int32_t defaultClearanceClass() { return 1; }

  /** Returns the clearance class used for items with no clearances. */
  static int32_t clearanceClassNone() { return 0; }

  /** Returns the trace half-width used for routing with the input net on the input layer. */
  int32_t getTraceHalfWidth(int32_t netNumber, int32_t layer);

  /** Returns true, if the trace widths used for routing for the input net differ on the layers. */
  bool traceWidthsAreLayerDependent(int32_t netNumber);

  /** Returns the smallest of all default trace half-widths. */
  int32_t getMinTraceHalfWidth() { return minTraceHalfWidth; }

  /**
   * Returns the smallest trace half-width of all net classes on the layers they route on, or
   * getMinTraceHalfWidth if that is smaller.
   */
  int32_t getMinNetClassTraceHalfWidth();

  /** Erik Gavriluk (freerouting.net): the narrowest trace half-width allowed (0: no limit). */
  int32_t getMinAllowedTraceHalfWidth() { return minAllowedTraceHalfWidth; }

  /** Erik Gavriluk (freerouting.net): sets the narrowest trace half-width allowed (0: no limit). */
  void setMinAllowedTraceHalfWidth(int32_t value) {
    this->minAllowedTraceHalfWidth = jrt::Math::max(0, value);
  }

  /** Returns the biggest of all default trace half-widths. */
  int32_t getMaxTraceHalfWidth() { return maxTraceHalfWidth; }

  /** Returns the clearance around drilled holes. */
  int32_t getHoleClearance() { return holeClearance; }

  /** Sets the clearance around drilled holes. */
  void setHoleClearance(int32_t value) { this->holeClearance = jrt::Math::max(0, value); }

  /**
   * KiCad's hole-to-hole minimum: the edge-to-edge distance a via's drill keeps from every other
   * drilled hole, of any net. 0 means no minimum.
   */
  int32_t getMinHoleToHole() { return minHoleToHole; }

  /** Sets the hole-to-hole minimum. */
  void setMinHoleToHole(int32_t value) { this->minHoleToHole = jrt::Math::max(0, value); }

  /**
   * Whether to measure a pin's or via's copper where the search tree's octagons overlap, instead
   * of refusing the pair on the octagons.
   */
  bool isExactCopper() { return !this->exactCopper.has_value() || *this->exactCopper; }

  /** Sets whether to use exact copper clearance. */
  void setExactCopper(bool value) { this->exactCopper = value; }

  /** Whether a via may sit in an SMD pad of its own net, drill hole included. */
  bool isViaInPad() { return this->viaInPad; }

  /** Whether a via may overlap an SMD pad of its own net with its drill hole outside the pad. */
  bool isViaTouchingPad() { return this->viaTouchingPad; }

  /** Sets the via-in-pad permissions resolved from the router settings. */
  void setViaInPad(bool viaInPad, bool viaTouchingPad) {
    this->viaInPad = viaInPad;
    this->viaTouchingPad = viaTouchingPad;
  }

  /** Changes the default trace half-width used for routing on the input layer. */
  void setDefaultTraceHalfWidth(int32_t layer, int32_t value);

  /** Returns the default trace half-width used on the input layer. */
  int32_t getDefaultTraceHalfWidth(int32_t layer);

  /** Changes the default trace half-width used for routing on all layers to the input value. */
  void setDefaultTraceHalfWidths(int32_t value);

  /** Returns the net rule used for all nets for which no special rule was set. */
  NetClass* getDefaultNetClass();

  /** Returns an empty new net rule with an internally created name. */
  NetClass* getNewNetClass();

  /** Returns an empty new net rule with the given name. */
  NetClass* getNewNetClass(const std::string& name);

  /**
   * Creates a default via rule for netClass with name name. If more than one via info with the
   * same layer range is found, only the via info with the smallest pad size is inserted.
   */
  void createDefaultViaRule(NetClass* netClass, const std::string& name);

  /** Creates the default net class. */
  void createDefaultNetClass();

  /** Appends a new net class initialized with default data and a default name. */
  NetClass* appendNetClass();

  /**
   * Appends a new net class initialized with default data and returns that class. If a class with
   * name exists, this class is returned without appending a new class.
   */
  NetClass* appendNetClass(const std::string& name);

  /** Returns the default via rule for routing, or null if no via rule exists. */
  ViaRule* getDefaultViaRule();

  /** Returns the via rule with the given name, or null if no such rule exists. */
  ViaRule* getViaRule(const std::string& name);

  /** Changes the clearance class index of all objects with index fromIndex to toIndex. */
  void changeClearanceClassIndex(int32_t fromIndex, int32_t toIndex,
                                 jrt::Collection<Item*>* boardItems);

  /**
   * Removes the clearance class with number index. Returns false if that was not possible because
   * there were still items assigned to this class.
   */
  bool removeClearanceClass(int32_t index, jrt::Collection<Item*>* boardItems);

  /**
   * Returns the minimum distance between the pin border and the next corner of a connected trace
   * for a pin with connection restrictions. If <= 0, there are no exit restrictions.
   */
  double getPinEdgeToTurnDist() { return this->pinEdgeToTurnDist; }

  /** Sets the minimum distance between the pin border and the next corner of a connected trace. */
  void setPinEdgeToTurnDist(double value) { this->pinEdgeToTurnDist = value; }

  /** Returns whether the router ignores conduction areas. */
  bool getIgnoreConduction() { return this->ignoreConduction; }

  /** Sets whether the router should ignore conduction areas. */
  void setIgnoreConduction(bool value) { this->ignoreConduction = value; }

  /** The angle restriction for traces: 90 degree, 45 degree or none. */
  AngleRestriction getTraceAngleRestriction() { return this->traceAngleRestriction; }

  /** Sets the angle restriction for traces: 90 degree, 45 degree or none. */
  void setTraceAngleRestriction(AngleRestriction angleRestriction) {
    this->traceAngleRestriction = angleRestriction;
  }

  /**
   * If true, shapes of type Simplex are always used in the autorouter algorithm. If false, IntBox
   * shapes are used in 90 degree and IntOctagon shapes in 45 degree autorouting.
   */
  bool getUseSlowAutorouteAlgorithm() { return useSlowAutorouteAlgorithm; }

  /** See getUseSlowAutorouteAlgorithm. */
  void setUseSlowAutorouteAlgorithm(bool value) { useSlowAutorouteAlgorithm = value; }

  /** Returns the maximum diameter of the default via on its first and last layer. */
  double getDefaultViaDiameter();

  /** The matrix describing the spacing restrictions between item clearance classes. */
  ClearanceMatrix* clearanceMatrix;  // final

  /** Describes the electrical nets on the board. */
  Nets* nets;  // final

  ViaInfos* viaInfos;                  // final
  jrt::ArrayList<ViaRule*>* viaRules;  // final (Java: Vector)
  NetClasses* netClasses;              // final
  DifferentialPairs* differentialPairs;  // final

  double clearanceToleranceUm = 1.0;  // transient
  jrt::Map<int32_t, PinClearanceSource*>* pinClearanceSources = nullptr;  // transient

 private:
  LayerStructure* layerStructure;  // final

  /** The angle restriction for traces: 90 degree, 45 degree or none. */
  AngleRestriction traceAngleRestriction;

  /** If true, the router ignores conduction areas. */
  bool ignoreConduction = true;

  /** The smallest of all default trace half-widths. */
  int32_t minTraceHalfWidth = 0;

  /** The biggest of all default trace half-widths. */
  int32_t maxTraceHalfWidth = 0;

  /**
   * The minimum distance of the pad border to the first turn of a connected trace to a pin with
   * restricted exit directions. If the value is <= 0, there are no exit restrictions.
   */
  double pinEdgeToTurnDist = 0;

  bool useSlowAutorouteAlgorithm = false;
  int32_t holeClearance = 0;
  int32_t minHoleToHole = 0;
  std::optional<bool> exactCopper = true;
  bool viaInPad = false;
  bool viaTouchingPad = false;

  /** Java: EnumMap (ordinal order) -> jrt::TreeMap. */
  jrt::Map<MeanderPatternType, NetMeanderConstraint*>* defaultMeanderConstraints;
  /** Java: HashSet. */
  jrt::Set<MeanderPatternType>* explicitMeanderConstraints;

  int32_t minAllowedTraceHalfWidth = 0;
};

}  // namespace fr
