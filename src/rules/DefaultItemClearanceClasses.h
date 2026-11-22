// Port of app.freerouting.rules.DefaultItemClearanceClasses
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Defines the item classes for which default clearance values are stored. */
enum class DefaultItemClearanceClasses_ItemClass : int32_t { NONE, TRACE, VIA, PIN, SMD, AREA };

inline std::string DefaultItemClearanceClasses_ItemClass_name(
    DefaultItemClearanceClasses_ItemClass e) {
  switch (e) {
    case DefaultItemClearanceClasses_ItemClass::NONE:
      return "NONE";
    case DefaultItemClearanceClasses_ItemClass::TRACE:
      return "TRACE";
    case DefaultItemClearanceClasses_ItemClass::VIA:
      return "VIA";
    case DefaultItemClearanceClasses_ItemClass::PIN:
      return "PIN";
    case DefaultItemClearanceClasses_ItemClass::SMD:
      return "SMD";
    case DefaultItemClearanceClasses_ItemClass::AREA:
      return "AREA";
  }
  return "";
}

inline std::string DefaultItemClearanceClasses_ItemClass_toString(
    DefaultItemClearanceClasses_ItemClass e) {
  return DefaultItemClearanceClasses_ItemClass_name(e);
}

inline jrt::Array<DefaultItemClearanceClasses_ItemClass>* DefaultItemClearanceClasses_ItemClass_values() {
  using E = DefaultItemClearanceClasses_ItemClass;
  return jrt::Array<E>::of({E::NONE, E::TRACE, E::VIA, E::PIN, E::SMD, E::AREA});
}

inline DefaultItemClearanceClasses_ItemClass DefaultItemClearanceClasses_ItemClass_valueOf(
    const std::string& name) {
  for (auto e : *DefaultItemClearanceClasses_ItemClass_values()) {
    if (DefaultItemClearanceClasses_ItemClass_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant ItemClass." + name);
}

/** Stores the default clearance class for each item type. */
class DefaultItemClearanceClasses : public virtual jrt::Object {
 public:
  /** Creates a new instance of DefaultItemClearanceClasses. */
  DefaultItemClearanceClasses()
      : clearanceClasses(
            new (jrt::kOp) jrt::Array<int32_t>(DefaultItemClearanceClasses_ItemClass_values()->length)) {
    this->setAll(1);
  }

  /** Creates a copy of the given default clearance classes. */
  explicit DefaultItemClearanceClasses(DefaultItemClearanceClasses* classes)
      : clearanceClasses(classes->clearanceClasses->clone()) {}

  /** Returns the number of the default clearance class for the input item class. */
  int32_t get(DefaultItemClearanceClasses_ItemClass itemClass) {
    return (*this->clearanceClasses)[static_cast<int32_t>(itemClass)];
  }

  /** Sets the index of the default clearance class of the input item class to index. */
  void set(DefaultItemClearanceClasses_ItemClass itemClass, int32_t index) {
    (*this->clearanceClasses)[static_cast<int32_t>(itemClass)] = index;
  }

  /** Sets the indices of all default item clearance classes (except NONE) to index. */
  void setAll(int32_t index) {
    for (int32_t i = 1; i < this->clearanceClasses->length; i++) {
      (*this->clearanceClasses)[i] = index;
    }
  }

 private:
  jrt::Array<int32_t>* clearanceClasses;
};

}  // namespace fr
