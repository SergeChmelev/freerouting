// Port of app.freerouting.rules.NetClasses
#include "rules/NetClasses.h"

#include "rules/NetClass.h"
#include "rules/ViaRule.h"

namespace fr {

NetClasses::NetClasses() : classArr(new jrt::ArrayList<NetClass*>()) {}

int32_t NetClasses::count() { return classArr->size(); }

NetClass* NetClasses::get(int32_t index) {
  // Java: assert index >= 0 && index <= classArr.size() - 1;
  return classArr->get(index);
}

NetClass* NetClasses::get(const std::string& name) {
  for (NetClass* currentClass : *this->classArr) {
    if (currentClass->getName() == name) {
      return currentClass;
    }
  }
  return nullptr;
}

NetClass* NetClasses::append(const std::string& name, LayerStructure* layerStructure,
                             ClearanceMatrix* clearanceMatrix, bool ignoredByAutorouter) {
  NetClass* newClass = new NetClass(name, layerStructure, clearanceMatrix, ignoredByAutorouter);
  classArr->add(newClass);
  return newClass;
}

NetClass* NetClasses::append(LayerStructure* layerStructure, ClearanceMatrix* clearanceMatrix) {
  std::string newName;
  int32_t index = 0;
  do {
    ++index;
    newName = "class" + jrt::str(index);
  } while (this->get(newName) != nullptr);
  return append(newName, layerStructure, clearanceMatrix, false);
}

NetClass* NetClasses::find(int32_t traceHalfWidth, int32_t traceClearanceClass,
                           ViaRule* viaRule) {
  for (NetClass* currentClass : *this->classArr) {
    if (currentClass->getTraceClearanceClass() == traceClearanceClass &&
        currentClass->getViaRule() == viaRule) {
      bool traceWidthsEqual = true;
      for (int32_t i = 0; i < currentClass->layerCount(); i++) {
        if (currentClass->getTraceHalfWidth(i) != traceHalfWidth) {
          traceWidthsEqual = false;
          break;
        }
      }
      if (traceWidthsEqual) {
        return currentClass;
      }
    }
  }
  return nullptr;
}

NetClass* NetClasses::find(jrt::Array<int32_t>* traceHalfWidthArr, int32_t traceClearanceClass,
                           ViaRule* viaRule) {
  for (NetClass* currentClass : *this->classArr) {
    if (currentClass->getTraceClearanceClass() == traceClearanceClass &&
        currentClass->getViaRule() == viaRule &&
        traceHalfWidthArr->length == currentClass->layerCount()) {
      bool traceWidthsEqual = true;
      for (int32_t i = 0; i < currentClass->layerCount(); i++) {
        if (currentClass->getTraceHalfWidth(i) != (*traceHalfWidthArr)[i]) {
          traceWidthsEqual = false;
          break;
        }
      }
      if (traceWidthsEqual) {
        return currentClass;
      }
    }
  }
  return nullptr;
}

bool NetClasses::remove(NetClass* netClass) { return this->classArr->remove(netClass); }

}  // namespace fr
