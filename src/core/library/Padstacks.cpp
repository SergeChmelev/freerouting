// Port of app.freerouting.core.library.Padstacks
#include "core/library/Padstacks.h"

#include "board/model/structure/LayerStructure.h"
#include "core/library/Padstack.h"
#include "logger/FRLogger.h"

namespace fr {

Padstacks::Padstacks(LayerStructure* layerStructure) {
  boardLayerStructure = layerStructure;
  padstacks = new jrt::ArrayList<Padstack*>();
}

Padstack* Padstacks::get(const std::string& name) {
  // match exact case first so padstacks differing only in case stay distinct
  for (Padstack* currentPadstack : *padstacks) {
    if (currentPadstack != nullptr && currentPadstack->name == name) {
      return currentPadstack;
    }
  }
  for (Padstack* currentPadstack : *padstacks) {
    if (currentPadstack != nullptr && jrt::equalsIgnoreCase(currentPadstack->name, name)) {
      return currentPadstack;
    }
  }
  return nullptr;
}

Padstack* Padstacks::get(int32_t padstackId) {
  if (padstackId <= 0 || padstackId > padstacks->size()) {
    int32_t padstackCount = padstacks->size();
    FRLogger::warn("Padstacks.get: 1 <= padstackId <= " + jrt::str(padstackCount) + " expected");
    return nullptr;
  }
  Padstack* result = padstacks->get(padstackId - 1);
  if (result != nullptr && result->id != padstackId) {
    FRLogger::warn("Padstacks.get: inconsistent padstack ID");
  }
  return result;
}

int32_t Padstacks::count() { return padstacks->size(); }

double Padstacks::getMaxDrillRadius() {
  double max = 0.0;
  for (Padstack* p : *padstacks) {
    if (p != nullptr) {
      max = jrt::Math::max(max, p->getDrillRadius());
    }
  }
  return max;
}

Padstack* Padstacks::add(const std::string& name, jrt::Array<ConvexShape*>* shapes,
                         jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes, bool drillAllowed,
                         bool placedAbsolute) {
  auto* newPadstack = new Padstack(name, padstacks->size() + 1, shapes, auxiliaryShapes,
                                   drillAllowed, placedAbsolute, this);
  padstacks->add(newPadstack);
  return newPadstack;
}

Padstack* Padstacks::add(const std::string& name, jrt::Array<ConvexShape*>* shapes,
                         bool drillAllowed, bool placedAbsolute) {
  return add(name, shapes, nullptr, drillAllowed, placedAbsolute);
}

Padstack* Padstacks::add(jrt::Array<ConvexShape*>* shapes) {
  std::string newName = "padstack#" + jrt::str(padstacks->size() + 1);
  return add(newName, shapes, false, false);
}

Padstack* Padstacks::add(jrt::Array<ConvexShape*>* shapes,
                         jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes) {
  std::string newName = "padstack#" + jrt::str(padstacks->size() + 1);
  return add(newName, shapes, auxiliaryShapes, false, false);
}

Padstack* Padstacks::add(ConvexShape* shape, int32_t fromLayer, int32_t toLayer) {
  auto* shapes = new jrt::Array<ConvexShape*>(boardLayerStructure->layers->length);
  int32_t firstLayer = jrt::Math::max(fromLayer, 0);
  int32_t lastLayer = jrt::Math::min(toLayer, boardLayerStructure->layers->length - 1);
  for (int32_t i = firstLayer; i <= lastLayer; i++) {
    (*shapes)[i] = shape;
  }
  return add(shapes);
}

}  // namespace fr
