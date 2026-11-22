#include "io/specctra/parser/Package.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Package.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Shape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Component.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/Shape.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

Package* Package::readScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    bool isFront = true;
    jrt::Collection<Shape*>* outline = new jrt::LinkedList<Shape*>();
    jrt::Collection<Shape_ReadAreaScopeResult*>* keepouts =
        new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
    jrt::Collection<Shape_ReadAreaScopeResult*>* viaKeepouts =
        new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
    jrt::Collection<Shape_ReadAreaScopeResult*>* placeKeepouts =
        new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
    jrt::Object* nextToken = scanner->nextToken();
    auto* packageNameToken = dynamic_cast<JString*>(nextToken);
    if (packageNameToken == nullptr) {
      FRLogger::warn("Package.read_scope: String expected at '" + scanner->getScopeIdentifier() +
                     "'");
      return nullptr;
    }
    std::string packageName = packageNameToken->value;
    scanner->setScopeIdentifier(packageName);
    jrt::Collection<Package_PinInfo*>* pinInfoList = new jrt::LinkedList<Package_PinInfo*>();
    for (;;) {
      jrt::Object* prevToken = nextToken;
      nextToken = scanner->nextToken();

      if (nextToken == nullptr) {
        FRLogger::warn("Package.read_scope: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        // end of scope
        break;
      }
      if (prevToken == Keyword::OPEN_BRACKET()) {
        if (nextToken == Keyword::PIN()) {
          Package_PinInfo* nextPin = readPinInfo(scanner);
          if (nextPin == nullptr) {
            return nullptr;
          }
          pinInfoList->add(nextPin);
        } else if (nextToken == Keyword::SIDE()) {
          isFront = readPlacementSide(scanner);
        } else if (nextToken == Keyword::OUTLINE()) {
          Shape* currentShape = Shape::readScope(scanner, layerStructure);
          if (currentShape != nullptr) {
            outline->add(currentShape);
          }
          // overread closing bracket
          nextToken = scanner->nextToken();
          if (nextToken != Keyword::CLOSED_BRACKET()) {
            FRLogger::warn("Package.read_scope: closed bracket expected at '" +
                           scanner->getScopeIdentifier() + "'");
            return nullptr;
          }
        } else if (nextToken == Keyword::KEEPOUT()) {
          Shape_ReadAreaScopeResult* keepoutArea =
              Shape::readAreaScope(scanner, layerStructure, false);
          if (keepoutArea != nullptr) {
            keepouts->add(keepoutArea);
          } else {
            FRLogger::error("Package.read_scope: could not read keepout area of package '" +
                                packageName + "'",
                            static_cast<const std::exception*>(nullptr));
          }
        } else if (nextToken == Keyword::VIA_KEEPOUT()) {
          Shape_ReadAreaScopeResult* keepoutArea =
              Shape::readAreaScope(scanner, layerStructure, false);
          if (keepoutArea != nullptr) {
            viaKeepouts->add(keepoutArea);
          }
        } else if (nextToken == Keyword::PLACE_KEEPOUT()) {
          Shape_ReadAreaScopeResult* keepoutArea =
              Shape::readAreaScope(scanner, layerStructure, false);
          if (keepoutArea != nullptr) {
            placeKeepouts->add(keepoutArea);
          }
        } else {
          ScopeKeyword::skipScope(scanner);
        }
      }
    }
    auto* pinInfoArr = new jrt::Array<Package_PinInfo*>(pinInfoList->size());
    jrt::Iterator<Package_PinInfo*>* it = pinInfoList->iterator();
    for (int32_t i = 0; i < pinInfoArr->length; i++) {
      (*pinInfoArr)[i] = it->next();
    }
    return new Package(packageName, pinInfoArr, outline, keepouts, viaKeepouts, placeKeepouts,
                       isFront);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Package.read_scope: IO error scanning file", &e);
    return nullptr;
  }
}

void Package::writeScope(WriteScopeParameter* scopeParameter, fr::Package* boardPackage) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("image ");
  scopeParameter->identifierType->write(boardPackage->name, scopeParameter->file);
  // write the placement side of the package
  scopeParameter->file->newLine();
  scopeParameter->file->write("(side ");
  if (boardPackage->isFront) {
    scopeParameter->file->write("front)");
  } else {
    scopeParameter->file->write("back)");
  }
  // write the pins of the package
  for (int32_t i = 0; i < boardPackage->pinCount(); i++) {
    fr::Package_Pin* currentPin = boardPackage->getPin(i);
    scopeParameter->file->newLine();
    scopeParameter->file->write("(pin ");
    fr::Padstack* currentPadstack =
        scopeParameter->board->library->padstacks->get(currentPin->padstackId);
    scopeParameter->identifierType->write(currentPadstack->name, scopeParameter->file);
    scopeParameter->file->write(" ");
    scopeParameter->identifierType->write(currentPin->name, scopeParameter->file);
    jrt::Array<double>* relCoor =
        scopeParameter->coordinateTransform->boardToDsnRel(currentPin->getExactRelativeLocation());
    for (int32_t j = 0; j < relCoor->length; j++) {
      scopeParameter->file->write(" ");
      scopeParameter->file->write(jrt::str((*relCoor)[j]));
    }
    double rotation = currentPin->rotationInDegree;
    if (rotation != 0) {
      scopeParameter->file->write("(rotate ");
      scopeParameter->file->write(jrt::str(rotation));
      scopeParameter->file->write(")");
    }
    scopeParameter->file->write(")");
  }
  // write the keepouts belonging to  the package.
  for (int32_t i = 0; i < boardPackage->keepouts->length; i++) {
    writePackageKeepout((*boardPackage->keepouts)[i], scopeParameter, false);
  }
  for (int32_t i = 0; i < boardPackage->viaKeepouts->length; i++) {
    writePackageKeepout((*boardPackage->viaKeepouts)[i], scopeParameter, true);
  }
  // write the package outline.
  if (boardPackage->outline != nullptr) {
    for (int32_t i = 0; i < boardPackage->outline->length; i++) {
      scopeParameter->file->startScope();
      scopeParameter->file->write("outline");
      Shape* currentOutline = scopeParameter->coordinateTransform->boardToDsnRel(
          (*boardPackage->outline)[i], Layer::SIGNAL());
      currentOutline->writeScope(scopeParameter->file, scopeParameter->identifierType);
      scopeParameter->file->endScope();
    }
  }
  scopeParameter->file->endScope();
}

void Package::writePackageKeepout(fr::Package_Keepout* keepout,
                                  WriteScopeParameter* scopeParameter, bool isViaKeepout) {
  Layer* keepoutLayer;
  if (keepout->layer >= 0) {
    fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[keepout->layer];
    keepoutLayer = new Layer(boardLayer->name, keepout->layer, boardLayer->isSignal);
  } else {
    keepoutLayer = Layer::SIGNAL();
  }
  fr::Shape* boundaryShape;
  jrt::Array<fr::Shape*>* holes;
  if (auto* shape = dynamic_cast<fr::Shape*>(keepout->area)) {
    boundaryShape = shape;
    holes = new jrt::Array<fr::Shape*>(0);
  } else {
    boundaryShape = keepout->area->getBorder();
    holes = keepout->area->getHoles();
  }
  scopeParameter->file->startScope();
  if (isViaKeepout) {
    scopeParameter->file->write("via_keepout");
  } else {
    scopeParameter->file->write("keepout");
  }
  Shape* dsnShape = scopeParameter->coordinateTransform->boardToDsn(boundaryShape, keepoutLayer);
  if (dsnShape != nullptr) {
    dsnShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
  }
  for (int32_t j = 0; j < holes->length; j++) {
    Shape* dsnHole = scopeParameter->coordinateTransform->boardToDsn((*holes)[j], keepoutLayer);
    dsnHole->writeHoleScope(scopeParameter->file, scopeParameter->identifierType);
  }
  scopeParameter->file->endScope();
}

Package_PinInfo* Package::readPinInfo(IJFlexScanner* scanner) {
  try {
    // Read the padstack name.
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    jrt::Object* nextToken = scanner->nextToken();
    if (dynamic_cast<JString*>(nextToken) == nullptr &&
        dynamic_cast<JInteger*>(nextToken) == nullptr) {
      FRLogger::warn("Package.read_pin_info: String or Integer expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    const std::string padstackName = nextToken->toString();
    double rotation = 0;

    scanner->yybegin(
        SpecctraDsnStreamReader::NAME);  // to be able to handle pin names starting with a digit.
    nextToken = scanner->nextToken();
    if (nextToken == Keyword::OPEN_BRACKET()) {
      // read the padstack rotation
      nextToken = scanner->nextToken();
      if (nextToken == Keyword::ROTATE()) {
        rotation = readRotation(scanner);
      } else {
        ScopeKeyword::skipScope(scanner);
      }
      scanner->yybegin(SpecctraDsnStreamReader::NAME);
      nextToken = scanner->nextToken();
    }
    // Read the pin name.
    if (dynamic_cast<JString*>(nextToken) == nullptr &&
        dynamic_cast<JInteger*>(nextToken) == nullptr) {
      FRLogger::warn("Package.read_pin_info: String or Integer expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    std::string pinName = nextToken->toString();

    auto* pinCoor = new jrt::Array<double>(2);
    for (int32_t i = 0; i < 2; i++) {
      nextToken = scanner->nextToken();
      if (auto* double1 = dynamic_cast<JDouble*>(nextToken)) {
        (*pinCoor)[i] = double1->value;
      } else if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
        (*pinCoor)[i] = integer->value;
      } else {
        FRLogger::warn("Package.read_pin_info: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    // Handle scopes at the end of the pin scope.
    for (;;) {
      jrt::Object* prevToken = nextToken;
      nextToken = scanner->nextToken();

      if (nextToken == nullptr) {
        FRLogger::warn("Package.read_pin_info: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        // end of scope
        break;
      }
      if (prevToken == Keyword::OPEN_BRACKET()) {
        if (nextToken == Keyword::ROTATE()) {
          rotation = readRotation(scanner);
        } else {
          ScopeKeyword::skipScope(scanner);
        }
      }
    }
    return new Package_PinInfo(padstackName, pinName, pinCoor, rotation);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Package.read_pin_info: IO error while scanning file", &e);
    return nullptr;
  }
}

double Package::readRotation(IJFlexScanner* scanner) {
  double result = 0;

  try {
    std::string nextString = scanner->nextString();
    result = jrt::Double::parseDouble(nextString);

    // Overread The closing bracket.
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("Package.read_rotation: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("Package.read_rotation: IO error while scanning file", &e);
  }

  return result;
}

void Package::writePlacementScope(WriteScopeParameter* scopeParameter,
                                  fr::Package* boardPackage) {
  jrt::Collection<fr::Item*>* boardItems = scopeParameter->board->getItems();
  bool componentFound = false;
  for (int32_t i = 1; i <= scopeParameter->board->components->count(); i++) {
    fr::Component* currentComponent = scopeParameter->board->components->get(i);
    if (currentComponent->getPackage() == boardPackage) {
      // check, if not all items of the component are deleted
      bool undeletedItemFound = false;
      for (fr::Item* currentItem : *boardItems) {
        if (currentItem->getComponentId() == currentComponent->id) {
          undeletedItemFound = true;
          break;
        }
      }
      if (undeletedItemFound || !currentComponent->isPlaced()) {
        if (!componentFound) {
          // write the scope header
          scopeParameter->file->startScope();
          scopeParameter->file->write("component ");
          scopeParameter->identifierType->write(boardPackage->name, scopeParameter->file);
          componentFound = true;
        }
        Component::writeScope(scopeParameter, currentComponent);
      }
    }
  }
  if (componentFound) {
    scopeParameter->file->endScope();
  }
}

bool Package::readPlacementSide(IJFlexScanner* scanner) {
  jrt::Object* nextToken = scanner->nextToken();
  bool result = nextToken != Keyword::BACK();

  nextToken = scanner->nextToken();
  if (nextToken != Keyword::CLOSED_BRACKET()) {
    FRLogger::warn("Package.read_placement_side: closing bracket expected at '" +
                   scanner->getScopeIdentifier() + "'");
  }
  return result;
}

}  // namespace fr::dsn
