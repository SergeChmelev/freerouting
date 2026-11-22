// Port of app.freerouting.board.model.structure.Component
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "datastructures/UndoableObjects.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes board components consisting of an array of pins and other stuff like component keepouts. */
class Component : public virtual UndoableObjects_Storable, public virtual ItemInfoPrinter_Printable {
 public:
  /**
   * Creates a new instance of Component with the input parameters. If onFront is false, the
   * component will be placed on the back side. (package-private in Java)
   */
  Component(const std::string& name, FloatPoint* exactLocation, double rotationInDegree,
            bool onFront, Package* packageFront, Package* packageBack, int32_t id,
            bool positionFixed, const std::optional<std::string>& partNumber);

  /** Same, with an integer location (may be null). (package-private in Java) */
  Component(const std::string& name, Point* location, double rotationInDegree, bool onFront,
            Package* packageFront, Package* packageBack, int32_t id, bool positionFixed,
            const std::optional<std::string>& partNumber);

  /** Returns the location of this component (null if not placed). */
  Point* getLocation();

  /** Returns the exact location of this component (null if not placed). */
  FloatPoint* getExactLocation();

  /** Returns the placement transform of this component. */
  PlacementTransform* placementTransform(bool rotateFirst);

  /** Restores the location, rotation and side of original (used to undo a placement change). */
  void restorePose(Component* original);

  /** Returns the rotation of this component in degree. */
  double getRotationInDegree() { return rotationInDegree; }

  /** Returns false, if this component does not contain a library package. */
  bool isPlaced();

  /** Returns, if the component is placed on the front side of the board. */
  bool placedOnFront() { return this->onFront; }

  /** Translates the location of this Component by vector. */
  void translateBy(Vector* vector);

  /** Turns this component by factor times 90 degree around pole. */
  void turn90Degree(int32_t factor, IntPoint* pole);

  /** Turns this component by factor times 90 degree around pole. */
  void turn90Degree(int32_t factor, IntPoint* pole, bool flipStyleRotateFirst);

  /** Rotates this component by angleInDegree around pole. */
  void rotate(double angleInDegree, IntPoint* pole, bool flipStyleRotateFirst);

  /** Changes the placement side of this component and mirrors it at the vertical line through pole. */
  void changeSide(IntPoint* pole);

  /** Changes the placement side of this component and mirrors it at the vertical line through pole. */
  void changeSide(IntPoint* pole, bool flipStyleRotateFirst);

  /** Compares 2 components by name (ignoring case); 1 for other types. */
  int32_t compareTo(jrt::Object* other) override;

  /** Returns the part number of this component, or null. */
  std::optional<std::string> getPartNumber() { return this->partNumber; }

  /** Creates a copy of this component (covariant clone). */
  Component* clone() override;

  std::string toString() override { return this->name; }

  /** Returns information for pin swap and gate swap, if != null. */
  LogicalPart* getLogicalPart() { return this->logicalPart; }

  /** Sets the information for pin swap and gate swap. */
  void setLogicalPart(LogicalPart* logicalPart) { this->logicalPart = logicalPart; }

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  /** Returns the library package of this component (front or back, by the current side). */
  Package* getPackage();

  /** The name of the component. */
  const std::string name;

  /** The internal generated unique identification number of this component. */
  const int32_t id;

  /** If true, the component cannot be moved. */
  const bool positionFixed;

 private:
  /** The library package of the component if it is placed on the component side. */
  Package* libPackageFront;

  /** The library package of the component if it is placed on the solder side. */
  Package* libPackageBack;

  /** The part number of the component (may be null). */
  std::optional<std::string> partNumber;

  /** The location of the component (integer, derived from exactLocation). */
  Point* location;

  /** The exact location of the component. */
  FloatPoint* exactLocation;

  /** The rotation of the library package of the component in degree. */
  double rotationInDegree;

  /** Contains information for gate swapping and pin swapping, if != null. */
  LogicalPart* logicalPart = nullptr;

  /** If false, the component will be placed on the back side of the board. */
  bool onFront;
};

}  // namespace fr
