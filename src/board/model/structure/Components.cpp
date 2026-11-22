// Port of app.freerouting.board.model.structure.Components
#include "board/model/structure/Components.h"

#include "board/model/structure/Component.h"
#include "board/state/BoardObservers.h"
#include "core/library/Package.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

Components* Components::deepCopy() {
  auto* result = new Components();
  // Every component (also those only reachable from the undo/redo history) is cloned exactly
  // once; the same original maps to the same copy everywhere.
  auto* copies = new jrt::IdentityHashMap<UndoableObjects_Storable*, UndoableObjects_Storable*>();
  auto mapObject = [copies](UndoableObjects_Storable* original) -> UndoableObjects_Storable* {
    if (original == nullptr) {
      return nullptr;
    }
    UndoableObjects_Storable* existing = copies->get(original);
    if (existing != nullptr) {
      return existing;
    }
    auto* component = dynamic_cast<Component*>(original);
    UndoableObjects_Storable* copy;
    if (component != nullptr) {
      Component* componentCopy = component->clone();
      componentCopy->restorePose(component);  // keeps the integer location exactly
      copy = componentCopy;
    } else {
      copy = dynamic_cast<UndoableObjects_Storable*>(original->clone());
    }
    copies->put(original, copy);
    return copy;
  };
  result->undoList = this->undoList->copyMapped(mapObject);
  for (Component* component : *this->componentArr) {
    result->componentArr->add(component == nullptr ? nullptr
                                                   : dynamic_cast<Component*>(mapObject(component)));
  }
  result->flipStyleRotateFirst = this->flipStyleRotateFirst;
  return result;
}

Components::Components()
    : undoList(new UndoableObjects()), componentArr(new jrt::ArrayList<Component*>()) {}

Component* Components::addPrecise(const std::string& name, FloatPoint* exactLocation,
                                  double rotationInDegree, bool onFront, Package* packageFront,
                                  Package* packageBack, bool positionFixed,
                                  const std::optional<std::string>& partNumber) {
  auto* newComponent = new Component(name, exactLocation, rotationInDegree, onFront, packageFront,
                                     packageBack, componentArr->size() + 1, positionFixed,
                                     partNumber);
  componentArr->add(newComponent);
  undoList->insert(newComponent);
  return newComponent;
}

Component* Components::add(const std::string& name, Point* location, double rotationInDegree,
                           bool onFront, Package* packageFront, Package* packageBack,
                           bool positionFixed, const std::optional<std::string>& partNumber) {
  return addPrecise(name, location != nullptr ? location->toFloat() : nullptr, rotationInDegree,
                    onFront, packageFront, packageBack, positionFixed, partNumber);
}

Component* Components::add(Point* location, double rotation, bool onFront,
                           Package* componentPackage) {
  std::string componentName = "Component#" + jrt::str(componentArr->size() + 1);
  return add(componentName, location, rotation, onFront, componentPackage, componentPackage,
             false, std::nullopt);
}

Component* Components::copy(Component* original, Vector* translation, Package* componentPackage) {
  FloatPoint* offset = translation->toFloat();
  FloatPoint* location = original->getExactLocation();
  auto* translated = new FloatPoint(location->x + offset->x, location->y + offset->y);
  return addPrecise("Component#" + jrt::str(componentArr->size() + 1), translated,
                    original->getRotationInDegree(), original->placedOnFront(), componentPackage,
                    componentPackage, false, original->getPartNumber());
}

Component* Components::get(const std::string& name) {
  for (Component* current : *componentArr) {
    if (current->name == name) {
      return current;
    }
  }
  return nullptr;
}

Component* Components::get(int32_t componentId) {
  Component* result = componentArr->get(componentId - 1);
  if (result != nullptr && result->id != componentId) {
    FRLogger::warn("Components.get: inconsistent component ID");
  }
  return result;
}

int32_t Components::count() { return componentArr->size(); }

jrt::Collection<Component*>* Components::getAll() { return componentArr; }

void Components::generateSnapshot() { this->undoList->generateSnapshot(); }

bool Components::popSnapshot() { return this->undoList->popSnapshot(); }

bool Components::undo(BoardObservers* observers) {
  if (!this->undoList->undo(nullptr, nullptr)) {
    return false;
  }
  restoreComponentArrFromUndoList(observers);
  return true;
}

bool Components::redo(BoardObservers* observers) {
  if (!this->undoList->redo(nullptr, nullptr)) {
    return false;
  }
  restoreComponentArrFromUndoList(observers);
  return true;
}

void Components::restoreComponentArrFromUndoList(BoardObservers* observers) {
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = this->undoList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* storable = this->undoList->readObject(it);
    if (storable == nullptr) {
      break;
    }
    auto* currentComponent = dynamic_cast<Component*>(storable);
    if (currentComponent == nullptr) {
      throw jrt::ClassCastException("Components: undo list object is not a Component");
    }
    this->componentArr->set(currentComponent->id - 1, currentComponent);

    if (observers != nullptr) {
      observers->notifyMoved(currentComponent);
    }
  }
}

void Components::move(int32_t componentId, Vector* vector) {
  Component* currentComponent = this->get(componentId);
  this->undoList->saveForUndo(currentComponent);
  currentComponent->translateBy(vector);
}

void Components::turn90Degree(int32_t componentId, int32_t factor, IntPoint* pole) {
  Component* currentComponent = this->get(componentId);
  this->undoList->saveForUndo(currentComponent);
  currentComponent->turn90Degree(factor, pole, flipStyleRotateFirst);
}

void Components::rotate(int32_t componentId, double rotationInDegree, IntPoint* pole) {
  Component* currentComponent = this->get(componentId);
  this->undoList->saveForUndo(currentComponent);
  currentComponent->rotate(rotationInDegree, pole, flipStyleRotateFirst);
}

void Components::changeSide(int32_t componentId, IntPoint* pole) {
  Component* currentComponent = this->get(componentId);
  this->undoList->saveForUndo(currentComponent);
  currentComponent->changeSide(pole, flipStyleRotateFirst);
}

}  // namespace fr
