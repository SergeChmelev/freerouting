// Port of app.freerouting.core.library.Packages
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes a library of component packages. */
class Packages : public virtual jrt::Object {
 public:
  /**
   * Creates a new instance of Packages. padstackList is the list of padstacks used for the pins of
   * the packages in this data structure.
   */
  explicit Packages(Padstacks* padstackList);

  /**
   * Returns the package with the input name and the input side or null, if no such package exists.
   * (Java's defensive null check on name is dropped.) Falls back to the base name without a
   * "::<digits>" suffix (Java regex SUFFIX_PATTERN "::\\d+$").
   */
  Package* get(const std::string& name, bool isFront);

  /** Returns the package with the specified ID. Package IDs start at 1. */
  Package* get(int32_t packageId);

  /** Returns the count of packages in this object. */
  int32_t count();

  /** Appends a new package with the specified data to this object. */
  Package* add(const std::string& name, jrt::Array<Package_Pin*>* pins,
               jrt::Array<Shape*>* outline, jrt::Array<double>* outlineWidths,
               jrt::Array<bool>* outlineIsClosed, jrt::Array<Package_Keepout*>* keepouts,
               jrt::Array<Package_Keepout*>* viaKeepouts,
               jrt::Array<Package_Keepout*>* placeKeepoutArr, bool isFront);

  /** Appends a new package with the specified pins. The package name is generated internally. */
  Package* add(jrt::Array<Package_Pin*>* pins);

  /** package-private in Java (read by Package.printInfo). */
  Padstacks* padstackList;

 private:
  /** The array of packages in this object (Java: Vector). */
  jrt::ArrayList<Package*>* packages;
};

}  // namespace fr
