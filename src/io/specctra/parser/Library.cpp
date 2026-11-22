#include "io/specctra/parser/Library.h"

#include <regex>

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Package.h"
#include "core/library/Packages.h"
#include "core/library/PadOval.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/KiCadRoundRectPad.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/Package.h"
#include "io/specctra/parser/Path.h"
#include "io/specctra/parser/PolygonPath.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/Shape.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

void Library::writeScope(WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("library");

  if (scopeParameter->board->library->packages != nullptr) {
    for (int32_t i = 1; i <= scopeParameter->board->library->packages->count(); i++) {
      Package::writeScope(scopeParameter, scopeParameter->board->library->packages->get(i));
    }
  }

  if (scopeParameter->board->library->padstacks != nullptr) {
    for (int32_t i = 1; i <= scopeParameter->board->library->padstacks->count(); i++) {
      writePadstackScope(scopeParameter, scopeParameter->board->library->padstacks->get(i));
    }
  }

  scopeParameter->file->endScope();
}

void Library::writePadstackScope(WriteScopeParameter* scopeParameter, fr::Padstack* padstack) {
  // search the layer range of the padstack
  int32_t firstLayerNo = 0;
  while (firstLayerNo < scopeParameter->board->getLayerCount() &&
         padstack->getShape(firstLayerNo) == nullptr) {
    ++firstLayerNo;
  }
  int32_t lastLayerNo = scopeParameter->board->getLayerCount() - 1;
  while (lastLayerNo >= 0 && padstack->getShape(lastLayerNo) == nullptr) {
    --lastLayerNo;
  }
  if (firstLayerNo >= scopeParameter->board->getLayerCount() || lastLayerNo < 0) {
    FRLogger::warn("Library.write_padstack_scope: padstack shape not found at '" +
                   padstack->name + "'");
    return;
  }

  scopeParameter->file->startScope();
  scopeParameter->file->write("padstack ");
  scopeParameter->identifierType->write(padstack->name, scopeParameter->file);
  for (int32_t i = firstLayerNo; i <= lastLayerNo; i++) {
    fr::Shape* currentBoardShape = padstack->getShape(i);
    if (currentBoardShape == nullptr) {
      continue;
    }
    fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[i];
    Layer* currentLayer = new Layer(boardLayer->name, i, boardLayer->isSignal);
    Shape* currentShape =
        scopeParameter->coordinateTransform->boardToDsnRel(currentBoardShape, currentLayer);
    scopeParameter->file->startScope();
    scopeParameter->file->write("shape");
    currentShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
    scopeParameter->file->endScope();
    jrt::Array<fr::ConvexShape*>* auxShapes = padstack->getAuxiliaryShapes(i);
    if (auxShapes != nullptr) {
      for (fr::ConvexShape* aux : *auxShapes) {
        if (aux != nullptr) {
          Shape* auxDsnShape = scopeParameter->coordinateTransform->boardToDsnRel(aux, currentLayer);
          scopeParameter->file->startScope();
          scopeParameter->file->write("shape");
          auxDsnShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
          scopeParameter->file->endScope();
        }
      }
    }
  }
  if (!padstack->attachAllowed) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(attach off)");
  }
  if (padstack->placedAbsolute) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(absolute on)");
  }
  if (!scopeParameter->compatMode && padstack->explicitDrillRadius.has_value()) {
    scopeParameter->file->newLine();
    double dsnDia = scopeParameter->coordinateTransform->boardToDsn(2 * *padstack->explicitDrillRadius);
    scopeParameter->file->write(
        "(freerouting_drill_diameter " +
        (dsnDia == static_cast<double>(jrt::d2l(dsnDia)) ? jrt::str(jrt::d2l(dsnDia))
                                                         : jrt::str(dsnDia)) +
        ")");
  }
  scopeParameter->file->endScope();
}

bool Library::readPadstackScope(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                fr::io::CoordinateTransform* coordinateTransform,
                                fr::Padstacks* boardPadstacks) {
  return readPadstackScope(scanner, layerStructure, coordinateTransform, boardPadstacks,
                           std::nullopt);
}

bool Library::readPadstackScope(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                fr::io::CoordinateTransform* coordinateTransform,
                                fr::Padstacks* boardPadstacks,
                                std::optional<fr::Unit> kicadDsnUnit) {
  std::string padstackName;
  bool isDrilllable = true;
  bool placedAbsolute = false;
  std::optional<double> drillDiameter;
  jrt::Collection<Shape*>* shapeList = new jrt::LinkedList<Shape*>();
  try {
    jrt::Object* nextToken = scanner->nextToken();
    if (auto* string = dynamic_cast<JString*>(nextToken)) {
      padstackName = string->value;
      scanner->setScopeIdentifier(padstackName);
    } else {
      FRLogger::warn("Library.read_padstack_scope: unexpected padstack identifier at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }

    while (nextToken != Keyword::CLOSED_BRACKET()) {
      jrt::Object* prevToken = nextToken;
      nextToken = scanner->nextToken();
      if (nextToken == nullptr) {
        FRLogger::warn("Library.read_padstack_scope: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      if (prevToken == Keyword::OPEN_BRACKET()) {
        auto* nextString = dynamic_cast<JString*>(nextToken);
        if (nextToken == Keyword::SHAPE()) {
          Shape* currentShape = Shape::readScope(scanner, layerStructure);
          if (currentShape != nullptr) {
            if (kicadDsnUnit.has_value()) {
              currentShape = KiCadRoundRectPad::correct(padstackName, currentShape, kicadDsnUnit,
                                                        coordinateTransform);
            }
            shapeList->add(currentShape);
          }
          // overread the closing bracket and unknown scopes.
          jrt::Object* currentNextToken = scanner->nextToken();
          while (currentNextToken == Keyword::OPEN_BRACKET()) {
            ScopeKeyword::skipScope(scanner);
            currentNextToken = scanner->nextToken();
          }
          if (currentNextToken != Keyword::CLOSED_BRACKET()) {
            FRLogger::warn("Library.read_padstack_scope: closing bracket expected at '" +
                           scanner->getScopeIdentifier() + "'");
            return false;
          }
        } else if (nextToken == Keyword::ATTACH()) {
          isDrilllable = DsnFile::readOnOffScope(scanner);
        } else if (nextToken == Keyword::ABSOLUTE()) {
          placedAbsolute = DsnFile::readOnOffScope(scanner);
        } else if (nextString != nullptr && nextString->value == "freerouting_drill_diameter") {
          jrt::Object* diaToken = scanner->nextToken();
          double diameter = jrt::Double::NaN;
          if (auto* intVal = dynamic_cast<JInteger*>(diaToken)) {
            diameter = static_cast<double>(intVal->value);
          } else if (auto* doubleVal = dynamic_cast<JDouble*>(diaToken)) {
            diameter = doubleVal->value;
          }
          // PORT NOTE: the Java Float branch is dropped (the scanner never produces Floats).
          if (!jrt::Double::isFinite(diameter) || diameter < 0 ||
              scanner->nextToken() != Keyword::CLOSED_BRACKET()) {
            FRLogger::warn("Invalid native drill diameter in padstack '" + padstackName + "'");
            return false;
          }
          drillDiameter = diameter;
        } else {
          ScopeKeyword::skipScope(scanner);
        }
      }
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("Library.read_padstack_scope: IO error scanning file", &e);
    return false;
  }
  fr::Padstack* existingPadstack = boardPadstacks->get(padstackName);
  if (existingPadstack != nullptr && padstackName == existingPadstack->name) {
    // Padstack exists already (differing only in case is treated as a separate padstack)
    return true;
  }
  if (shapeList->isEmpty()) {
    FRLogger::warn("Library.read_padstack_scope: shape not found for padstack with name '" +
                   padstackName + "'");
    return true;
  }
  auto* isGenericDefault = new jrt::Array<bool>(layerStructure->layers->length);
  jrt::List<jrt::List<fr::ConvexShape*>*>* layerPieces =
      new jrt::ArrayList<jrt::List<fr::ConvexShape*>*>(layerStructure->layers->length);
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    layerPieces->add(new jrt::ArrayList<fr::ConvexShape*>());
  }

  // The exact copper of round and oval pads, by the piece that bounds it.
  jrt::Map<fr::ConvexShape*, fr::PadOval*>* ovalOfPiece =
      new jrt::IdentityHashMap<fr::ConvexShape*, fr::PadOval*>();
  for (Shape* padShape : *shapeList) {
    fr::Shape* currentShape = padShape->transformToBoardRel(coordinateTransform);
    jrt::List<fr::ConvexShape*>* pieces = new jrt::ArrayList<fr::ConvexShape*>();

    if (auto* shape1 = dynamic_cast<fr::ConvexShape*>(currentShape)) {
      pieces->add(shape1);
      if (auto* path = dynamic_cast<PolygonPath*>(padShape)) {
        fr::PadOval* oval = path->padOvalRel(coordinateTransform);
        if (oval != nullptr) {
          ovalOfPiece->put(shape1, oval);
        }
      }
    } else {
      jrt::Array<fr::TileShape*>* convexPieces = nullptr;
      if (auto* shape = dynamic_cast<fr::PolygonShape*>(currentShape)) {
        convexPieces = shape->splitToConvex();
      } else if (currentShape != nullptr) {
        convexPieces = currentShape->splitToConvex();
      }

      if (convexPieces != nullptr && convexPieces->length > 0) {
        for (fr::TileShape* piece : *convexPieces) {
          fr::ConvexShape* convexPiece = piece;
          if (auto* simplex = dynamic_cast<fr::Simplex*>(convexPiece)) {
            convexPiece = simplex->simplify();
          }
          pieces->add(convexPiece);
        }
      } else {
        if (auto* shape = dynamic_cast<fr::PolygonShape*>(currentShape)) {
          currentShape = shape->convexHull();
        }
        jrt::Array<fr::TileShape*>* fallback =
            currentShape != nullptr ? currentShape->splitToConvex() : nullptr;
        if (fallback != nullptr && fallback->length != 1) {
          FRLogger::warn("Library.read_padstack_scope: convex shape expected at '" +
                         scanner->getScopeIdentifier() + "'");
        }
        fr::ConvexShape* fallbackShape =
            (fallback != nullptr && fallback->length > 0) ? (*fallback)[0] : nullptr;
        if (auto* simplex = dynamic_cast<fr::Simplex*>(fallbackShape)) {
          fallbackShape = simplex->simplify();
        }
        if (fallbackShape != nullptr) {
          pieces->add(fallbackShape);
        }
      }
    }

    // Enlarge 1D/0D shapes to at least dimension 2 if needed
    jrt::List<fr::ConvexShape*>* validatedPieces =
        new jrt::ArrayList<fr::ConvexShape*>(pieces->size());
    for (fr::ConvexShape* p : *pieces) {
      if (p != nullptr) {
        if (p->dimension() < 2) {
          FRLogger::warn("Library.read_padstack_scope: the shape of padstack '" + padstackName +
                         "' is not an area. We will enlarge it as a workaround, but it may " +
                         "result in unintended consequences.");
          p = p->offset(1);
        }
        if (p->dimension() >= 2) {
          validatedPieces->add(p);
        }
      }
    }

    if (padShape->layer == Layer::PCB() || padShape->layer == Layer::SIGNAL()) {
      for (int32_t l = 0; l < layerStructure->layers->length; l++) {
        if ((*isGenericDefault)[l]) {
          layerPieces->get(l)->addAll(validatedPieces);
        } else if (layerPieces->get(l)->isEmpty()) {
          layerPieces->get(l)->addAll(validatedPieces);
          (*isGenericDefault)[l] = true;
        } else {
          layerPieces->get(l)->addAll(validatedPieces);
        }
      }
    } else {
      int32_t shapeLayer = layerStructure->getNo(padShape->layer->name);
      if (shapeLayer < 0 || shapeLayer >= layerStructure->layers->length) {
        FRLogger::warn("Library.read_padstack_scope: layer number found at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      if ((*isGenericDefault)[shapeLayer]) {
        layerPieces->get(shapeLayer)->clear();
        (*isGenericDefault)[shapeLayer] = false;
      }
      layerPieces->get(shapeLayer)->addAll(validatedPieces);
    }
  }

  auto* padstackShapes = new jrt::Array<fr::ConvexShape*>(layerStructure->layers->length);
  auto* auxiliaryShapes =
      new jrt::Array<jrt::Array<fr::ConvexShape*>*>(layerStructure->layers->length);
  auto* ovals = new jrt::Array<fr::PadOval*>(layerStructure->layers->length);
  bool hasOvals = false;
  bool hasNonConvex = false;

  for (int32_t l = 0; l < layerStructure->layers->length; l++) {
    jrt::List<fr::ConvexShape*>* pieces = layerPieces->get(l);
    if (pieces->isEmpty()) {
      continue;
    }
    if (pieces->size() == 1) {
      (*padstackShapes)[l] = pieces->get(0);
      (*ovals)[l] = ovalOfPiece->get(pieces->get(0));
      hasOvals |= (*ovals)[l] != nullptr;
    } else {
      hasNonConvex = true;
      FRLogger::warn("Library.read_padstack_scope: Padstack '" + padstackName +
                     "' contains non-convex custom geometry (" + jrt::str(pieces->size()) +
                     " convex tiles on layer " + jrt::str(l) +
                     "). Decomposing into primary core pin and auxiliary copper obstacles.");

      int32_t coreIndex = -1;
      for (int32_t p = 0; p < pieces->size(); p++) {
        if (pieces->get(p)->contains(static_cast<fr::Point*>(fr::Point::ZERO()))) {
          coreIndex = p;
          break;
        }
      }
      if (coreIndex < 0) {
        double maxArea = -1;
        for (int32_t p = 0; p < pieces->size(); p++) {
          double area = pieces->get(p)->area();
          if (area > maxArea) {
            maxArea = area;
            coreIndex = p;
          }
        }
      }
      if (coreIndex < 0) {
        coreIndex = 0;
      }

      (*padstackShapes)[l] = pieces->get(coreIndex);
      auto* aux = new jrt::Array<fr::ConvexShape*>(pieces->size() - 1);
      int32_t auxIdx = 0;
      for (int32_t p = 0; p < pieces->size(); p++) {
        if (p != coreIndex) {
          (*aux)[auxIdx++] = pieces->get(p);
        }
      }
      (*auxiliaryShapes)[l] = aux;
    }
  }

  fr::Padstack* padstack = boardPadstacks->add(padstackName, padstackShapes, auxiliaryShapes,
                                               isDrilllable, placedAbsolute);
  if (drillDiameter.has_value()) {
    padstack->explicitDrillRadius = coordinateTransform->dsnToBoard(*drillDiameter) / 2.0;
  }
  if (hasNonConvex) {
    padstack->hasNonConvexGeometry_ = true;
  }
  if (hasOvals) {
    padstack->setOvals(ovals);
  }
  return true;
}

bool Library::readScope(ReadScopeParameter* scopeParameter) {
  fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();
  board->library->padstacks =
      new fr::Padstacks(scopeParameter->boardHandling->getRoutingBoard()->layerStructure);
  jrt::Collection<Package*>* packageList = new jrt::LinkedList<Package*>();
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Library.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Library.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == Keyword::PADSTACK()) {
        if (!readPadstackScope(scopeParameter->scanner, scopeParameter->layerStructure,
                               scopeParameter->coordinateTransform, board->library->padstacks,
                               isKiCad(scopeParameter) ? std::optional<fr::Unit>(scopeParameter->unit)
                                                       : std::nullopt)) {
          return false;
        }
      } else if (nextToken == Keyword::IMAGE()) {
        Package* currentPackage =
            Package::readScope(scopeParameter->scanner, scopeParameter->layerStructure);
        if (currentPackage == nullptr) {
          return false;
        }
        packageList->add(currentPackage);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
  }

  // Create the library packages on the board
  board->library->packages = new fr::Packages(board->library->padstacks);
  for (Package* currentPackage : *packageList) {
    auto* pins = new jrt::Array<fr::Package_Pin*>(currentPackage->pinInfoArr->length);
    for (int32_t i = 0; i < pins->length; i++) {
      Package_PinInfo* pinInfo = (*currentPackage->pinInfoArr)[i];
      fr::FloatPoint* exactRel = scopeParameter->coordinateTransform->dsnToBoardRel(pinInfo->relCoor);
      fr::Padstack* boardPadstack = board->library->padstacks->get(pinInfo->padstackName);
      if (boardPadstack == nullptr) {
        // PORT NOTE: padstackName is never null in C++. String.replaceAll("\\.\\d+", "").
        static const std::regex digitSuffix("\\.[0-9]+");
        boardPadstack = board->library->padstacks->get(
            std::regex_replace(pinInfo->padstackName, digitSuffix, ""));
      }
      if (boardPadstack == nullptr) {
        FRLogger::warn("Library.read_scope: board padstack '" + pinInfo->padstackName +
                       "' not found at '" + scopeParameter->scanner->getScopeIdentifier() + "'");
        return false;
      }
      (*pins)[i] =
          new fr::Package_Pin(pinInfo->pinName, boardPadstack->id, exactRel, pinInfo->rotation);
    }
    auto* outlines = new jrt::Array<fr::Shape*>(currentPackage->outline->size());
    auto* outlineWidths = new jrt::Array<double>(currentPackage->outline->size());
    auto* outlineIsClosed = new jrt::Array<bool>(currentPackage->outline->size());

    jrt::Iterator<Shape*>* it3 = currentPackage->outline->iterator();
    for (int32_t i = 0; i < outlines->length; i++) {
      Shape* currentShape = it3->next();
      if (currentShape != nullptr) {
        (*outlines)[i] = currentShape->transformToBoardRel(scopeParameter->coordinateTransform);
        if (auto* path = dynamic_cast<Path*>(currentShape)) {
          (*outlineWidths)[i] = path->width;
          jrt::Array<double>* coords = path->coordinateArr;
          if (coords->length >= 4) {
            (*outlineIsClosed)[i] = (*coords)[0] == (*coords)[coords->length - 2] &&
                                    (*coords)[1] == (*coords)[coords->length - 1];
          }
        } else {
          (*outlineWidths)[i] = 0.0;
          (*outlineIsClosed)[i] = true;  // Non-path shapes (polygons/rects) are closed
        }
      } else {
        FRLogger::warn("Library.read_scope: outline shape is null at '" +
                       scopeParameter->scanner->getScopeIdentifier() + "'");
      }
    }
    generateMissingKeepoutNames("keepout_", currentPackage->keepouts);
    generateMissingKeepoutNames("via_keepout_", currentPackage->viaKeepouts);
    generateMissingKeepoutNames("place_keepout_", currentPackage->placeKeepouts);
    // PORT NOTE: after generateMissingKeepoutNames every areaName is set, so value() is safe
    // (Java would pass null otherwise).
    auto* keepouts = new jrt::Array<fr::Package_Keepout*>(currentPackage->keepouts->size());
    jrt::Iterator<Shape_ReadAreaScopeResult*>* it2 = currentPackage->keepouts->iterator();
    for (int32_t i = 0; i < keepouts->length; i++) {
      Shape_ReadAreaScopeResult* currentKeepout = it2->next();
      Layer* currentLayer = currentKeepout->shapeList->iterator()->next()->layer;
      fr::Area* currentArea = Shape::transformAreaToBoardRel(currentKeepout->shapeList,
                                                             scopeParameter->coordinateTransform);
      (*keepouts)[i] =
          new fr::Package_Keepout(currentKeepout->areaName.value(), currentArea, currentLayer->no);
    }
    auto* viaKeepouts = new jrt::Array<fr::Package_Keepout*>(currentPackage->viaKeepouts->size());
    it2 = currentPackage->viaKeepouts->iterator();
    for (int32_t i = 0; i < viaKeepouts->length; i++) {
      Shape_ReadAreaScopeResult* currentKeepout = it2->next();
      Layer* currentLayer = (currentKeepout->shapeList->iterator()->next())->layer;
      fr::Area* currentArea = Shape::transformAreaToBoardRel(currentKeepout->shapeList,
                                                             scopeParameter->coordinateTransform);
      (*viaKeepouts)[i] =
          new fr::Package_Keepout(currentKeepout->areaName.value(), currentArea, currentLayer->no);
    }
    auto* placeKeepoutArr =
        new jrt::Array<fr::Package_Keepout*>(currentPackage->placeKeepouts->size());
    it2 = currentPackage->placeKeepouts->iterator();
    for (int32_t i = 0; i < placeKeepoutArr->length; i++) {
      Shape_ReadAreaScopeResult* currentKeepout = it2->next();
      Layer* currentLayer = (currentKeepout->shapeList->iterator()->next())->layer;
      fr::Area* currentArea = Shape::transformAreaToBoardRel(currentKeepout->shapeList,
                                                             scopeParameter->coordinateTransform);
      (*placeKeepoutArr)[i] =
          new fr::Package_Keepout(currentKeepout->areaName.value(), currentArea, currentLayer->no);
    }
    // PORT NOTE: currentPackage->name is never null in C++ (Java falls back to "Package").
    board->library->packages->add(currentPackage->name, pins, outlines, outlineWidths,
                                  outlineIsClosed, keepouts, viaKeepouts, placeKeepoutArr,
                                  currentPackage->isFront);
  }
  return true;
}

bool Library::isKiCad(ReadScopeParameter* scopeParameter) {
  return scopeParameter->hostCad.has_value() &&
         jrt::contains(jrt::toLowerCase(*scopeParameter->hostCad), "kicad");
}

void Library::generateMissingKeepoutNames(
    const std::string& keepoutType, jrt::Collection<Shape_ReadAreaScopeResult*>* keepoutList) {
  bool allNamesExisting = true;
  for (Shape_ReadAreaScopeResult* currentKeepout : *keepoutList) {
    if (!currentKeepout->areaName.has_value()) {
      allNamesExisting = false;
      break;
    }
  }
  if (allNamesExisting) {
    return;
  }
  // generate names
  int32_t currentNameIndex = 1;
  for (Shape_ReadAreaScopeResult* currentKeepout : *keepoutList) {
    currentKeepout->areaName = keepoutType + jrt::str(currentNameIndex);
    ++currentNameIndex;
  }
}

}  // namespace fr::dsn
