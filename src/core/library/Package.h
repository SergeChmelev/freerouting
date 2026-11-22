// Port of app.freerouting.core.library.Package
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A padstack instance at a precise package-local position. */
class Package_Pin : public virtual jrt::Object {
 public:
  /** Creates a pin with an integer package-local offset. */
  Package_Pin(const std::string& name, int32_t padstackId, Vector* relativeLocation,
              double rotationInDegree);

  /** Creates a pin retaining the fractional package-local offset. */
  Package_Pin(const std::string& name, int32_t padstackId, FloatPoint* relativeLocation,
              double rotationInDegree);

  /** Returns the high-precision relative location of this pin. */
  FloatPoint* getExactRelativeLocation();

  /** The name of the pin. */
  const std::string name;

  /** The ID of the padstack mask of the pin. */
  const int32_t padstackId;

  /** The location of the pin relative to its package (final). */
  Vector* relativeLocation;

  /** The rotation of the pin padstack. */
  const double rotationInDegree;

 private:
  /** Precise package-local offset; the integer vector is derived from this value. */
  FloatPoint* exactRelativeLocation;
};

/** Describes a named keepout belonging to a package. */
class Package_Keepout : public virtual jrt::Object {
 public:
  /** Creates a keepout with its area and layer. */
  Package_Keepout(const std::string& name, Area* area, int32_t layer)
      : name(name), area(area), layer(layer) {}

  const std::string name;
  Area* area;  // final
  const int32_t layer;
};

/**
 * Component package templates describing the padstacks and relative locations of the package pins,
 * and optional other stuff like an outline package keepouts.
 */
class Package : public virtual ItemInfoPrinter_Printable {
 public:
  /** Creates a new instance of Package. The package list contains this package. */
  Package(const std::string& name, int32_t id, jrt::Array<Package_Pin*>* pins,
          jrt::Array<Shape*>* outline, jrt::Array<double>* outlineWidths,
          jrt::Array<bool>* outlineIsClosed, jrt::Array<Package_Keepout*>* keepouts,
          jrt::Array<Package_Keepout*>* viaKeepouts, jrt::Array<Package_Keepout*>* placeKeepoutArr,
          bool isFront, Packages* packageList)
      : name(name),
        id(id),
        outline(outline),
        outlineWidths(outlineWidths),
        outlineIsClosed(outlineIsClosed),
        keepouts(keepouts),
        viaKeepouts(viaKeepouts),
        placeKeepoutArr(placeKeepoutArr),
        isFront(isFront),
        pins(pins),
        packageList(packageList) {}

  /** Comparable<Package>: compares 2 packages by name (ignoring case, then exact), then by id. */
  virtual int32_t compareTo(Package* other);

  /** Returns the pin with the input index from this package. */
  Package_Pin* getPin(int32_t pinIndex);

  /**
   * Returns the pin number of the pin with the input name from this package, or -1, if no such pin
   * exists. Pin numbers are from 0 to pinCount - 1.
   */
  int32_t getPinIndex(const std::string& name);

  /** Returns the pin count of this package. */
  int32_t pinCount() { return pins->length; }

  std::string toString() override { return this->name; }

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  /** The name of the package. */
  const std::string name;

  /** Internally generated package ID. */
  const int32_t id;

  /** The outline of the component, which may be null. */
  jrt::Array<Shape*>* outline;

  jrt::Array<double>* outlineWidths;
  jrt::Array<bool>* outlineIsClosed;
  jrt::Array<Package_Keepout*>* keepouts;
  jrt::Array<Package_Keepout*>* viaKeepouts;
  jrt::Array<Package_Keepout*>* placeKeepoutArr;

  /** If false, the package is placed on the back side of the board. */
  const bool isFront;

 private:
  /** The array of pins of this padstack. */
  jrt::Array<Package_Pin*>* pins;

  Packages* packageList;
};

}  // namespace fr
