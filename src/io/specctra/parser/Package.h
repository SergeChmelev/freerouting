#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes the information of a pin in a package. */
class Package_PinInfo : public virtual jrt::Object {
 public:
  /** The name of the padstack of this pin. */
  std::string padstackName;

  /** The name of this pin. */
  std::string pinName;

  /** The x- and y-coordinates relative to the package location. */
  jrt::Array<double>* relCoor;

  /** The rotation of the pin relative to the package. */
  double rotation;

  Package_PinInfo(const std::string& padstackName, const std::string& pinName,
                  jrt::Array<double>* relCoor, double rotation)
      : padstackName(padstackName), pinName(pinName), relCoor(relCoor), rotation(rotation) {}
};

/** Class for reading and writing package scopes from dsn-files. */
class Package : public virtual jrt::Object {
 public:
  std::string name;

  /** List of objects of type PinInfo. */
  jrt::Array<Package_PinInfo*>* pinInfoArr;

  /** The outline of the package. */
  jrt::Collection<Shape*>* outline;

  /** Collection of keepouts belonging to this package. */
  jrt::Collection<Shape_ReadAreaScopeResult*>* keepouts;

  /** Collection of via keepouts belonging to this package. */
  jrt::Collection<Shape_ReadAreaScopeResult*>* viaKeepouts;

  /** Collection of place keepouts belonging to this package. */
  jrt::Collection<Shape_ReadAreaScopeResult*>* placeKeepouts;

  /** If false, the package is placed on the back side of the board. */
  bool isFront;

  /** Creates a new instance of Package. */
  Package(const std::string& name, jrt::Array<Package_PinInfo*>* pinInfoArr,
          jrt::Collection<Shape*>* outline,
          jrt::Collection<Shape_ReadAreaScopeResult*>* keepouts,
          jrt::Collection<Shape_ReadAreaScopeResult*>* viaKeepouts,
          jrt::Collection<Shape_ReadAreaScopeResult*>* placeKeepouts, bool isFront)
      : name(name),
        pinInfoArr(pinInfoArr),
        outline(outline),
        keepouts(keepouts),
        viaKeepouts(viaKeepouts),
        placeKeepouts(placeKeepouts),
        isFront(isFront) {}

  /** Reads an image (package) scope; null on error. */
  static Package* readScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter, fr::Package* boardPackage);

  /** Writes the placements of package to a Specctra dsn-file. */
  static void writePlacementScope(WriteScopeParameter* scopeParameter, fr::Package* boardPackage);

 private:
  static void writePackageKeepout(fr::Package_Keepout* keepout,
                                  WriteScopeParameter* scopeParameter, bool isViaKeepout);

  /** Reads the information of a single pin in a package. */
  static Package_PinInfo* readPinInfo(IJFlexScanner* scanner);

  static double readRotation(IJFlexScanner* scanner);

  /** Throws jrt::IOException. */
  static bool readPlacementSide(IJFlexScanner* scanner);
};

}  // namespace fr::dsn
