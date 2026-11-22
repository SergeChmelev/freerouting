// Port of app.freerouting.board.model.items.Pin (+ nested TraceExitRestriction).
#pragma once

#include "board/model/items/DrillItem.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes an exit restriction from a trace from a pin pad. */
class Pin_TraceExitRestriction : public virtual jrt::Object {
 public:
  Direction* direction;  // final
  double minLength;      // final

  /** Creates a new instance of TraceExitRestriction. */
  Pin_TraceExitRestriction(Direction* direction, double minLength)
      : direction(direction), minLength(minLength) {}
};

/**
 * Class describing the functionality of an electrical Item on the board with a shape on 1 or
 * several layers.
 */
class Pin : public DrillItem {
 public:
  /** The index of this pin in its component (starting with 0). */
  int32_t pinIndex;  // final

 private:
  /**
   * The identifier of the net-tie cluster this pin belongs to (0 if not part of any net-tie
   * cluster). Pins sharing the same positive cluster ID within a footprint are electrically tied.
   */
  int32_t netTieClusterId = 0;

  /** How the host CAD system connects this pad to the fill of a plane of its net; null if unknown. */
  PadPlaneConnection* planeConnection = nullptr;

  /**
   * The pin, this pin was changed to by swapping or this pin, if no pin swap occurred.
   * PORT NOTE (board deep copy): not carried by copy(id) (as in Java); serialization would keep it
   * (pointing to the copy of the swapped pin).
   */
  Pin* changedTo = this;

  jrt::Array<Shape*>* precalculatedShapes = nullptr;  // transient

 public:
  /**
   * Creates a new instance of Pin with the input parameters. pinIndex is the index of the pin in
   * its component (starting with 0).
   */
  Pin(int32_t componentId, int32_t pinIndex, jrt::Array<int32_t>* netNumbers,
      int32_t clearanceClassIndex, int32_t id, FixedState fixedState, BasicBoard* board);

  using Item::isObstacle;

  /** Calculates the relative location of this pin to its component. */
  virtual Vector* relativeLocation();

  Point* getCenter() override;

  Padstack* getPadstack() override;

  Item* copy(int32_t id) override;

  /** Rebinding a copied pin invalidates both its connection point and its geometry. */
  void assignComponentId(int32_t id) override;

  /** How the host CAD system connects this pad to the fill of a plane, or null. */
  virtual PadPlaneConnection* getPlaneConnection() { return this->planeConnection; }

  /**
   * Sets how the host CAD system connects this pad to the fill of a plane of its net. It only
   * changes on which island of a fractured plane the pad counts.
   */
  virtual void setPlaneConnection(PadPlaneConnection* planeConnection) {
    this->planeConnection = planeConnection;
  }

  /** Return the name of this pin in the package of this component (nullopt if not found). */
  virtual std::optional<std::string> name();

  /** Return the name of the component of this pin (nullopt if none). */
  std::optional<std::string> componentName() override;

  /** Gets index of this pin in the library package of the pins component. */
  virtual int32_t getPinIndex() { return pinIndex; }

  /** Returns the net-tie cluster ID of this pin, or 0 if it is not part of any cluster. */
  virtual int32_t getNetTieClusterId() { return netTieClusterId; }

  /** Sets the net-tie cluster ID for this pin (clamped to be non-negative). */
  virtual void setNetTieClusterId(int32_t netTieClusterId) {
    this->netTieClusterId = jrt::Math::max(0, netTieClusterId);
  }

  Shape* getShape(int32_t index) override;

  /**
   * Transforms a shape defined in padstack-relative coordinates to absolute board coordinates
   * according to this pin's placement, component rotation, mirroring, and side.
   */
  virtual Shape* transformToBoard(Shape* padstackShape);

  /**
   * The exact copper of the round or oval pad behind the shape with index index, placed on the
   * board as transformToBoard places that shape; null when the pad is not round or oval.
   */
  virtual PadOval* getPlacedOval(int32_t index);

  /** Returns the layer of the padstack shape for shape index (package-private in Java). */
  virtual int32_t getPadstackLayer(int32_t index);

  /**
   * Calculates the allowed trace exit directions of the shape of this padstack on layer together
   * with the minimal trace line lengths into their directions.
   */
  virtual jrt::Collection<Pin_TraceExitRestriction*>* getTraceExitRestrictions(int32_t layer);

  /** Returns true, if this pin has exit restrictions on some layer. */
  virtual bool hasTraceExitRestrictions();

  /** Returns true, if vias throw the pads of this pins are allowed (currently SMD pins only). */
  virtual bool drillAllowed() { return this->firstLayer() == this->lastLayer(); }

  /** Whether a via may sit on this pad, even when the via-in-pad setting is off. */
  virtual bool viaMayAttach() { return false; }

  bool isObstacle(Item* other) override;

  static bool arePiecesOfOnePad(Pin* pin1, Pin* pin2) { return isSameLogicalPad(pin1, pin2); }

 private:
  static bool isSameLogicalPad(Pin* pin1, Pin* pin2);

 public:
  /**
   * Normalizes a pin name by stripping composite sub-pad suffixes (such as @1, #1, _1, or -1) to
   * identify the base logical pad. PORT NOTE: Java's `pinName == null -> ""` branch is
   * unreachable with std::string (all callers pass non-null names).
   */
  static std::string getBasePinName(const std::string& pinName);

 private:
  static bool isAllDigits(const std::string& str);

 public:
  void turn90Degree(int32_t factor, IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void changePlacementSide(IntPoint* pole) override;

  void clearDerivedData() override;

  /** Return all Pins, that can be swapped with this pin. */
  virtual jrt::Set<Pin*>* getSwappablePins();

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  /** Swaps the nets of this pin and other. Returns false on error. */
  virtual bool swap(Pin* other);

  /** Returns the pin, this pin was changed to by pin swapping, or this pin. */
  virtual Pin* getChangedTo() { return changedTo; }

  /** False, if this drillitem is places on the back side of the board. */
  bool isPlacedOnFront() override;

  /** Returns the smallest width of the pin shape on layer layer. */
  virtual double getMinWidth(int32_t layer);

  /**
   * Returns the neckdown half width for traces on layer. The neckdown width is used, when the pin
   * width is smaller than the trace width to enter or leave the pin with a trace.
   */
  virtual int32_t getTraceNeckdownHalfwidth(int32_t layer);

  /** Returns the largest width of the pin shape on layer layer. */
  virtual double getMaxWidth(int32_t layer);

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  std::string getHoverInfo(const std::string& locale) override;

  /**
   * Calculates the nearest exit restriction direction for changing tracePolyline. tracePolyline is
   * assumed to start at the pin center. Returns null, if there is no matching restriction.
   */
  virtual Direction* calcNearestExitRestrictionDirection(Polyline* tracePolyline,
                                                         int32_t traceHalfWidth, int32_t layer);

  /**
   * Calculates the nearest trace exit point of the pin on layer. Returns null, if the pin has no
   * trace exit restrictions.
   */
  virtual FloatPoint* nearestTraceExitCorner(FloatPoint* fromPoint, int32_t traceHalfWidth,
                                             int32_t layer);

  std::string toString() override;

  jrt::Set<Item*>* getNormalContacts() override;

  /**
   * The pins matching accept whose copper overlaps this pin's copper on a common layer, netted or
   * not. Exact shapes.
   */
  virtual jrt::List<Pin*>* copperOverlappingPins(std::function<bool(Pin*)> accept);

  /** The netted pins whose copper overlaps this netless pin's copper on a common layer (FR-102). */
  virtual jrt::List<Pin*>* overlappingNettedPins();
};

}  // namespace fr
