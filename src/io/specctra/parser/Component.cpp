#include "io/specctra/parser/Component.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "core/library/Package.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/FloatPoint.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/ComponentPlacement.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr::dsn {

ComponentPlacement* Component::readScope(IJFlexScanner* scanner) {
  jrt::Object* nextToken = scanner->nextToken();
  auto* name = dynamic_cast<JString*>(nextToken);
  if (name == nullptr) {
    FRLogger::warn("Component.read_scope: component name expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  ComponentPlacement* componentPlacement = new ComponentPlacement(name->value);
  jrt::Object* prevToken = nextToken;
  nextToken = scanner->nextToken();
  while (nextToken != CLOSED_BRACKET() && nextToken != nullptr) {
    if (prevToken == OPEN_BRACKET() && nextToken == PLACE()) {
      ComponentPlacement_ComponentLocation* nextLocation = readPlaceScope(scanner);
      if (nextLocation != nullptr) {
        componentPlacement->locations->add(nextLocation);
      } else {
        return nullptr;
      }
    }
    prevToken = nextToken;
    nextToken = scanner->nextToken();
  }
  return componentPlacement;
}

void Component::writeScope(WriteScopeParameter* scopeParameter, fr::Component* component) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("place ");
  scopeParameter->file->newLine();
  scopeParameter->identifierType->write(component->name, scopeParameter->file);
  if (component->isPlaced()) {
    jrt::Array<double>* coor =
        scopeParameter->coordinateTransform->boardToDsn(component->getExactLocation());
    for (int32_t i = 0; i < coor->length; i++) {
      scopeParameter->file->write(" ");
      scopeParameter->file->write(jrt::str((*coor)[i]));
    }
    if (component->placedOnFront()) {
      scopeParameter->file->write(" front ");
    } else {
      scopeParameter->file->write(" back ");
    }
    double rotation = component->getRotationInDegree();
    scopeParameter->file->write(jrt::str(rotation));
  }
  if (component->positionFixed) {
    scopeParameter->file->newLine();
    scopeParameter->file->write(" (lock_type position)");
  }
  int32_t pinCount = component->getPackage()->pinCount();
  for (int32_t i = 0; i < pinCount; i++) {
    writePinInfo(scopeParameter, component, i);
  }
  writeKeepoutInfos(scopeParameter, component);
  scopeParameter->file->endScope();
}

void Component::writePinInfo(WriteScopeParameter* scopeParameter, fr::Component* component,
                             int32_t pinIndex) {
  if (!component->isPlaced()) {
    return;
  }
  fr::Package_Pin* packagePin = component->getPackage()->getPin(pinIndex);
  if (packagePin == nullptr) {
    FRLogger::warn("Component.write_pin_info: package pin not found at '" + component->name +
                   "'");
    return;
  }
  fr::Pin* componentPin = scopeParameter->board->getPin(component->id, pinIndex);
  if (componentPin == nullptr) {
    FRLogger::warn("Component.write_pin_info: component pin not found at '" + component->name +
                   "'");
    return;
  }
  // PORT NOTE: ClearanceMatrix::getName returns "" where Java returns null.
  std::string clClassName =
      scopeParameter->board->rules->clearanceMatrix->getName(componentPin->clearanceClassIndex());
  if (clClassName.empty()) {
    FRLogger::warn("Component.write_pin_info: clearance class  name not found at '" +
                   component->name + "'");
    return;
  }
  scopeParameter->file->newLine();
  scopeParameter->file->write("(pin ");
  scopeParameter->identifierType->write(packagePin->name, scopeParameter->file);
  scopeParameter->file->write(" (clearance_class ");
  scopeParameter->identifierType->write(clClassName, scopeParameter->file);
  scopeParameter->file->write("))");
}

void Component::writeKeepoutInfos(WriteScopeParameter* scopeParameter, fr::Component* component) {
  if (!component->isPlaced()) {
    return;
  }
  jrt::Array<fr::Package_Keepout*>* currentKeepoutArr;
  std::string keepoutType;
  for (int32_t j = 0; j < 3; j++) {
    if (j == 0) {
      currentKeepoutArr = component->getPackage()->keepouts;
      keepoutType = "(keepout ";
    } else if (j == 1) {
      currentKeepoutArr = component->getPackage()->viaKeepouts;
      keepoutType = "(via_keepout ";
    } else {
      currentKeepoutArr = component->getPackage()->placeKeepoutArr;
      keepoutType = "(place_keepout ";
    }
    for (int32_t i = 0; i < currentKeepoutArr->length; i++) {
      fr::Package_Keepout* currentKeepout = (*currentKeepoutArr)[i];
      fr::ObstacleArea* currentObstacleArea =
          getKeepout(scopeParameter->board, component->id, currentKeepout->name);
      if (currentObstacleArea == nullptr || currentObstacleArea->clearanceClassIndex() == 0) {
        continue;
      }
      // PORT NOTE: ClearanceMatrix::getName returns "" where Java returns null.
      std::string clClassName = scopeParameter->board->rules->clearanceMatrix->getName(
          currentObstacleArea->clearanceClassIndex());
      if (clClassName.empty()) {
        FRLogger::warn("Component.write_keepout_infos: clearance class name not found at '" +
                       component->name + "'");
        return;
      }
      scopeParameter->file->newLine();
      scopeParameter->file->write(keepoutType);
      scopeParameter->identifierType->write(currentKeepout->name, scopeParameter->file);
      scopeParameter->file->write(" (clearance_class ");
      scopeParameter->identifierType->write(clClassName, scopeParameter->file);
      scopeParameter->file->write("))");
    }
  }
}

fr::ObstacleArea* Component::getKeepout(fr::BasicBoard* board, int32_t componentId,
                                        const std::string& name) {
  jrt::Iterator<fr::UndoableObjects_UndoableObjectNode*>* it = board->itemList->startReadObject();
  for (;;) {
    fr::Item* currentItem = dynamic_cast<fr::Item*>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (currentItem->getComponentId() == componentId) {
      if (auto* currentArea = dynamic_cast<fr::ObstacleArea*>(currentItem)) {
        if (currentArea->name.has_value() && *currentArea->name == name) {
          return currentArea;
        }
      }
    }
  }
  return nullptr;
}

ComponentPlacement_ComponentLocation* Component::readPlaceScope(IJFlexScanner* scanner) {
  try {
    jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* pinInfos =
        new jrt::TreeMap<std::string, ComponentPlacement_ItemClearanceInfo*>();
    jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* keepoutInfos =
        new jrt::TreeMap<std::string, ComponentPlacement_ItemClearanceInfo*>();
    jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* viaKeepoutInfos =
        new jrt::TreeMap<std::string, ComponentPlacement_ItemClearanceInfo*>();
    jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* placeKeepoutInfos =
        new jrt::TreeMap<std::string, ComponentPlacement_ItemClearanceInfo*>();

    std::string name = scanner->nextString(true);

    jrt::Object* nextToken;
    jrt::Array<double>* location = new jrt::Array<double>(2);
    for (int32_t i = 0; i < 2; i++) {
      nextToken = scanner->nextToken();
      if (auto* double1 = dynamic_cast<JDouble*>(nextToken)) {
        (*location)[i] = double1->value;
      } else if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
        (*location)[i] = integer->value;
      } else if (nextToken == CLOSED_BRACKET()) {
        // component is not yet placed
        return new ComponentPlacement_ComponentLocation(name, nullptr, true, 0, false, pinInfos,
                                                        keepoutInfos, viaKeepoutInfos,
                                                        placeKeepoutInfos, std::nullopt);
      } else {
        FRLogger::warn(
            "Component.read_place_scope: Double was expected as the second and third "
            "parameter of the component/place command at '" +
            scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }

    nextToken = scanner->nextToken();
    bool isFront = true;
    if (nextToken == BACK()) {
      isFront = false;
    } else if (nextToken != FRONT()) {
      FRLogger::warn("Component.read_place_scope: Keyword.FRONT expected at '" +
                     scanner->getScopeIdentifier() + "'");
    }
    double rotation;
    nextToken = scanner->nextToken();
    if (auto* double1 = dynamic_cast<JDouble*>(nextToken)) {
      rotation = double1->value;
    } else if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
      rotation = integer->value;
    } else {
      FRLogger::warn("Component.read_place_scope: number expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    bool positionFixed = false;
    std::optional<std::string> partNumber;
    nextToken = scanner->nextToken();
    while (nextToken == OPEN_BRACKET()) {
      nextToken = scanner->nextToken();
      auto* stringToken = dynamic_cast<JString*>(nextToken);
      if (nextToken == LOCK_TYPE()) {
        positionFixed = readLockType(scanner);
      } else if (nextToken == PIN()) {
        ComponentPlacement_ItemClearanceInfo* currentPinInfo = readItemClearanceInfo(scanner);
        if (currentPinInfo == nullptr) {
          return nullptr;
        }
        pinInfos->put(currentPinInfo->name, currentPinInfo);
      } else if (nextToken == KEEPOUT()) {
        ComponentPlacement_ItemClearanceInfo* currentKeepoutInfo = readItemClearanceInfo(scanner);
        if (currentKeepoutInfo == nullptr) {
          return nullptr;
        }
        keepoutInfos->put(currentKeepoutInfo->name, currentKeepoutInfo);
      } else if (nextToken == VIA_KEEPOUT()) {
        ComponentPlacement_ItemClearanceInfo* currentKeepoutInfo = readItemClearanceInfo(scanner);
        if (currentKeepoutInfo == nullptr) {
          return nullptr;
        }
        viaKeepoutInfos->put(currentKeepoutInfo->name, currentKeepoutInfo);
      } else if (nextToken == PLACE_KEEPOUT()) {
        ComponentPlacement_ItemClearanceInfo* currentKeepoutInfo = readItemClearanceInfo(scanner);
        if (currentKeepoutInfo == nullptr) {
          return nullptr;
        }
        placeKeepoutInfos->put(currentKeepoutInfo->name, currentKeepoutInfo);
      } else if (nextToken == Keyword::PN() ||
                 (stringToken != nullptr && jrt::equalsIgnoreCase("PN", stringToken->value))) {
        partNumber = DsnFile::readStringScope(scanner);
      } else {
        skipScope(scanner);
      }
      nextToken = scanner->nextToken();
    }
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Component.read_place_scope: ) expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    return new ComponentPlacement_ComponentLocation(name, location, isFront, rotation,
                                                    positionFixed, pinInfos, keepoutInfos,
                                                    viaKeepoutInfos, placeKeepoutInfos,
                                                    partNumber);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Component.read_scope: IO error scanning file", &e);
    return nullptr;
  }
}

ComponentPlacement_ItemClearanceInfo* Component::readItemClearanceInfo(IJFlexScanner* scanner) {
  scanner->yybegin(SpecctraDsnStreamReader::NAME);
  jrt::Object* nextToken = scanner->nextToken();
  auto* name = dynamic_cast<JString*>(nextToken);
  if (name == nullptr) {
    FRLogger::warn("Component.read_item_clearance_info: String expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  std::optional<std::string> clClassName;
  nextToken = scanner->nextToken();
  while (nextToken == OPEN_BRACKET()) {
    nextToken = scanner->nextToken();
    auto* s = dynamic_cast<JString*>(nextToken);
    if (nextToken == CLEARANCE_CLASS() ||
        (s != nullptr && (jrt::equalsIgnoreCase("clearance_class", s->value) ||
                          jrt::equalsIgnoreCase("clearanceClass", s->value)))) {
      clClassName = DsnFile::readStringScope(scanner);
    } else {
      skipScope(scanner);
    }
    nextToken = scanner->nextToken();
  }
  if (nextToken != CLOSED_BRACKET()) {
    FRLogger::warn("Component.read_item_clearance_info: ) expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  if (!clClassName.has_value()) {
    FRLogger::warn("Component.read_item_clearance_info: clearance class name not found at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  return new ComponentPlacement_ItemClearanceInfo(name->value, clClassName);
}

bool Component::readLockType(IJFlexScanner* scanner) {
  bool result = false;
  for (;;) {
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == nullptr || nextToken == CLOSED_BRACKET()) {
      break;
    }
    if (nextToken == POSITION()) {
      result = true;
    }
  }
  return result;
}

bool Component::readScope(ReadScopeParameter* scopeParameter) {
  try {
    ComponentPlacement* componentPlacement = readScope(scopeParameter->scanner);
    if (componentPlacement == nullptr) {
      return false;
    }
    scopeParameter->placementList->add(componentPlacement);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Component.read_scope: IO error scanning file", &e);
    return false;
  }
  return true;
}

}  // namespace fr::dsn
