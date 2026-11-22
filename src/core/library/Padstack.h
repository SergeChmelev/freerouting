// Port of app.freerouting.core.library.Padstack
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes padstack masks for pins or vias located at the origin. */
class Padstack : public virtual ItemInfoPrinter_Printable {
 public:
  /** Creates a new Padstack with one shape per board layer. (package-private in Java) */
  Padstack(const std::string& name, int32_t id, jrt::Array<ConvexShape*>* shapes,
           bool isDrillable, bool placedAbsolute, Padstacks* padstackList)
      : Padstack(name, id, shapes, nullptr, isDrillable, placedAbsolute, padstackList) {}

  /**
   * Creates a new Padstack with one shape per board layer and auxiliary shapes for custom pads.
   * (package-private in Java)
   */
  Padstack(const std::string& name, int32_t id, jrt::Array<ConvexShape*>* shapes,
           jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes, bool isDrillable,
           bool placedAbsolute, Padstacks* padstackList)
      : name(name),
        id(id),
        attachAllowed(isDrillable),
        placedAbsolute(placedAbsolute),
        shapes(shapes),
        auxiliaryShapes(auxiliaryShapes),
        padstackList(padstackList) {}

  /** Comparable<Padstack>: compares by name (ignoring case, then exact), then by id. */
  virtual int32_t compareTo(Padstack* other);

  std::optional<double> getExplicitDrillRadius() { return this->explicitDrillRadius; }

  void setExplicitDrillRadius(std::optional<double> explicitDrillRadius) {
    this->explicitDrillRadius = explicitDrillRadius;
    this->cachedDrillRadius = std::nullopt;
  }

  /**
   * Returns the drill radius of this padstack in board units. The result is cached after the first
   * computation to avoid repeated parsing of the name.
   */
  double getDrillRadius();

  /** Gets the shape of this padstack on the specified layer. */
  ConvexShape* getShape(int32_t layer);

  /** The exact copper of the round or oval pad on this layer; null when it is not one. */
  PadOval* getOval(int32_t layer);

  /** Sets the exact copper of the round or oval pads, one entry (or null) per layer. */
  void setOvals(jrt::Array<PadOval*>* ovals) { this->ovals = ovals; }

  /** Gets any auxiliary shapes on the specified layer for non-convex custom pads. */
  jrt::Array<ConvexShape*>* getAuxiliaryShapes(int32_t layer);

  /** Returns whether this padstack has auxiliary shapes on any layer. */
  bool hasAuxiliaryShapes();

  /** Returns whether this padstack represents non-convex custom pad geometry. */
  bool hasNonConvexGeometry() { return hasNonConvexGeometry_ || hasAuxiliaryShapes(); }

  /** Returns the first layer of this padstack with a shape != null. */
  int32_t fromLayer();

  /** Returns the last layer of this padstack with a shape != null. */
  int32_t toLayer();

  /** Returns the layer count of the board of this padstack. */
  int32_t boardLayerCount() { return shapes->length; }

  std::string toString() override { return this->name; }

  /**
   * Calculates the allowed trace exit directions on a layer. If the length of the pad is smaller
   * than factor times its height, connection also to the long side is allowed.
   */
  jrt::Collection<Direction*>* getTraceExitDirections(int32_t layer, double factor);

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  const std::string name;
  const int32_t id;

  /** Whether vias of the own net may overlap with this padstack. */
  const bool attachAllowed;

  /**
   * If false, the layers of the padstack are mirrored, if it is placed on the back side. The
   * default is false.
   */
  const bool placedAbsolute;

  /**
   * True if this padstack contains non-convex custom pad geometry.
   * PORT NOTE: renamed from Java's hasNonConvexGeometry (clashes with the method of that name).
   */
  bool hasNonConvexGeometry_ = false;

  /**
   * True for padstacks whose copper-layer shapes were synthesized from the drill radius because the
   * source padstack had no copper at all (non-plated holes); not real copper.
   */
  bool holeOnly = false;

  /** Native drill radius in board units; zero means no drill. Null retains DSN inference. */
  std::optional<double> explicitDrillRadius;

 private:
  double getSmallestRadius();

  jrt::Array<ConvexShape*>* shapes;

  /** Auxiliary convex shapes per layer for non-convex custom pads. */
  jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes;

  /** The exact copper of round and oval pads per layer, behind their bounding shapes; or null. */
  jrt::Array<PadOval*>* ovals = nullptr;

  /** Pointer to the padstack list containing this padstack. */
  Padstacks* padstackList;

  /** Cached drill radius to avoid repeated parsing on every call. */
  std::optional<double> cachedDrillRadius;
};

}  // namespace fr
