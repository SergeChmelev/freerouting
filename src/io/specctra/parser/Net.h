#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Net identifier: name + subnet number, ordered by name, then subnet number. */
class Net_Id : public virtual jrt::Object {
 public:
  std::string name;
  int32_t subnetNumber;

  Net_Id(const std::string& name, int32_t subnetNumber) : name(name), subnetNumber(subnetNumber) {}

  virtual int32_t compareTo(Net_Id* other) {
    int32_t result = jrt::compareStrings(this->name, other->name);
    if (result == 0) {
      result = this->subnetNumber - other->subnetNumber;
    }
    return result;
  }
};

/** Sorted tuple of component name and pin name. */
class Net_Pin : public virtual jrt::Object {
 public:
  std::string componentName;
  std::string pinName;

  Net_Pin(const std::string& componentName, const std::string& pinName)
      : componentName(componentName), pinName(pinName) {}

  virtual int32_t compareTo(Net_Pin* other) {
    int32_t result = jrt::compareStrings(this->componentName, other->componentName);
    if (result == 0) {
      result = jrt::compareStrings(this->pinName, other->pinName);
    }
    return result;
  }

  std::string toString() override { return "Pin{" + componentName + '-' + pinName + '}'; }
};

/** Class for reading and writing net scopes from dsn-files. */
class Net : public virtual jrt::Object {
 public:
  Net_Id* id;

  /** Creates a new instance of Net. */
  explicit Net(Net_Id* netId) : id(netId) {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter, fr::Net* net,
                         jrt::Collection<fr::Pin*>* pinList);

  static void writeMeanderProperties(fr::NetMeanderConstraint* meander,
                                     WriteScopeParameter* scopeParameter);

  static void writeNetId(fr::Net* net, fr::IndentFileWriter* file,
                         fr::IdentifierType* identifierType);

  static void writePin(WriteScopeParameter* scopeParameter, fr::Pin* pin);

  virtual jrt::Set<Net_Pin*>* getPins() { return pinList; }

  /** PORT NOTE: Java copies into a new TreeSet: pinList = new TreeSet<>(pinList). */
  virtual void setPins(jrt::Collection<Net_Pin*>* pinList);

 private:
  static void writeCircuit(fr::Net* net, WriteScopeParameter* scopeParameter);

  /** List of elements of type Pin. */
  jrt::Set<Net_Pin*>* pinList = nullptr;
};

}  // namespace fr::dsn
