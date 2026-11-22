#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Clearance class information of a pin or keepout of a placed component. */
class ComponentPlacement_ItemClearanceInfo : public virtual jrt::Object {
 public:
  std::string name;
  /** Nullable in Java. */
  std::optional<std::string> clearanceClass;

  ComponentPlacement_ItemClearanceInfo(const std::string& name,
                                       const std::optional<std::string>& clearanceClass)
      : name(name), clearanceClass(clearanceClass) {}
};

/** The structure of an entry in the list locations. */
class ComponentPlacement_ComponentLocation : public virtual jrt::Object {
 public:
  std::string name;

  /** The x- and y-coordinates of the location. */
  jrt::Array<double>* coor;

  /**
   * True, if the component is placed at the component side. Else the component is placed at the
   * solder side.
   */
  bool isFront;

  /** The rotation of the component in degree. */
  double rotation;

  /** If true, the component cannot be moved. */
  bool positionFixed;

  /** The entries of this map are of type ItemClearanceInfo, the keys are the pin names. */
  jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* pin_infos;

  jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* keepout_infos;

  jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* via_keepout_infos;

  jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* place_keepout_infos;

  /** Nullable in Java. */
  std::optional<std::string> partNumber;

  ComponentPlacement_ComponentLocation(
      const std::string& name, jrt::Array<double>* coor, bool isFront, double rotation,
      bool positionFixed, jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* pinInfos,
      jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* keepoutInfos,
      jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* viaKeepoutInfos,
      jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* placeKeepoutInfos,
      const std::optional<std::string>& partNumber)
      : name(name),
        coor(coor),
        isFront(isFront),
        rotation(rotation),
        positionFixed(positionFixed),
        pin_infos(pinInfos),
        keepout_infos(keepoutInfos),
        via_keepout_infos(viaKeepoutInfos),
        place_keepout_infos(placeKeepoutInfos),
        partNumber(partNumber) {}
};

/** Describes placement data for a library component. */
class ComponentPlacement : public virtual jrt::Object {
 public:
  /** The name of the corresponding library component. */
  std::string libName;

  /** The list of ComponentLocations of the library component on the board. */
  jrt::Collection<ComponentPlacement_ComponentLocation*>* locations;

  /** Creates a new instance of ComponentPlacement. */
  explicit ComponentPlacement(const std::string& libName)
      : libName(libName), locations(new (jrt::kOp) jrt::LinkedList<ComponentPlacement_ComponentLocation*>()) {}
};

}  // namespace fr::dsn
