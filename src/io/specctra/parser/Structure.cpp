#include "io/specctra/parser/Structure.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/items/WireObstacleArea.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/state/Communication.h"
#include "core/RoutingJob.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/AutorouteSettings.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/Net.h"
#include "io/specctra/parser/NetList.h"
#include "io/specctra/parser/PlaceControl.h"
#include "io/specctra/parser/Plane.h"
#include "io/specctra/parser/PolygonPath.h"
#include "io/specctra/parser/PolylinePath.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/Rectangle.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/Shape.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/MeanderPatternType.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/NetMeanderConstraint.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "settings/RouterSettings.h"

namespace fr::dsn {

namespace {

/** Java Collection.addAll(null) throws NullPointerException; keep that instead of crashing. */
template <class T>
void addAllNonNull(jrt::Collection<T>* target, jrt::Collection<T>* source) {
  if (source == nullptr) {
    throw jrt::NullPointerException("Collection.addAll(null)");
  }
  target->addAll(source);
}

/**
 * Java String.split(sep, 2) for a separator without regex metacharacters: at most 2 parts, the
 * second part keeps everything after the first occurrence of sep.
 */
jrt::Array<std::string>* splitLimit2(const std::string& s, const std::string& sep) {
  std::size_t pos = sep.empty() ? std::string::npos : s.find(sep);
  if (pos == std::string::npos) {
    return jrt::Array<std::string>::of({s});
  }
  return jrt::Array<std::string>::of({s.substr(0, pos), s.substr(pos + sep.size())});
}

/** Java s.replaceAll("[\"]", ""). */
std::string removeQuotes(const std::string& s) {
  std::string result;
  for (char c : s) {
    if (c != '"') {
      result += c;
    }
  }
  return result;
}

}  // namespace

// -------------------------------------------------------------------------------------------------
// Nested classes
// -------------------------------------------------------------------------------------------------

Structure_BoardConstructionInfo::Structure_BoardConstructionInfo()
    : layerInfo(new jrt::LinkedList<Layer*>()),
      outlineShapes(new jrt::LinkedList<Shape*>()),
      defaultRules(new jrt::LinkedList<Rule*>()),
      layerDependentRules(new jrt::LinkedList<Structure_LayerRule*>()) {}

Structure_OutlineShape::Structure_OutlineShape(fr::PolylineShape* shape) : shape(shape) {
  boundingBox = shape->boundingBox();
  convexShapes = shape->splitToConvex();
  isHole = false;
}

bool Structure_OutlineShape::containsAllCorners(Structure_OutlineShape* otherShape) {
  if (this->convexShapes == nullptr) {
    // calculation of the convex shapes failed
    return false;
  }
  int32_t cornerCount = otherShape->shape->borderLineCount();
  for (int32_t i = 0; i < cornerCount; i++) {
    fr::Point* currentCorner = otherShape->shape->corner(i);
    bool isContained = false;
    for (int32_t j = 0; j < this->convexShapes->length; j++) {
      if ((*this->convexShapes)[j]->contains(currentCorner)) {
        isContained = true;
        break;
      }
    }
    if (!isContained) {
      return false;
    }
  }
  return true;
}

// -------------------------------------------------------------------------------------------------
// Structure
// -------------------------------------------------------------------------------------------------

void Structure::writeScope(WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("structure");

  // write the layer structure
  writeLayers(scopeParameter);

  // write the boundaries
  writeBoundaries(scopeParameter);

  // write the routing vias
  writeViaPadstacks(scopeParameter->board->library, scopeParameter->file,
                    scopeParameter->identifierType);

  // write the rules
  writeDefaultRules(scopeParameter);

  // write the snap angles
  writeSnapAngle(scopeParameter->file, scopeParameter->board->rules->getTraceAngleRestriction());

  // write the control scope
  writeControlScope(scopeParameter->board->rules, scopeParameter->file);

  if (scopeParameter->autorouteSettings != nullptr) {
    // write the auto-route settings
    AutorouteSettings::writeScope(scopeParameter->file, scopeParameter->autorouteSettings,
                                  scopeParameter->board->layerStructure,
                                  scopeParameter->identifierType);
  }

  // write the conduction areas
  writeConductionAreas(scopeParameter);

  // write the keepouts
  writeKeepouts(scopeParameter);

  scopeParameter->file->endScope();
}

void Structure::writeConductionAreas(WriteScopeParameter* scopeParameter) {
  fr::UndoableObjects_Storable* currentObject;
  jrt::Iterator<fr::UndoableObjects_UndoableObjectNode*>* it =
      scopeParameter->board->itemList->startReadObject();
  for (;;) {
    currentObject = scopeParameter->board->itemList->readObject(it);
    if (currentObject == nullptr) {
      break;
    }
    auto* currentArea = jrt::fast_cast<fr::ConductionArea>(currentObject);
    if (currentArea == nullptr) {
      continue;
    }
    if ((*scopeParameter->board->layerStructure->layers)[currentArea->getLayer()]->isSignal) {
      // These conduction areas are written in the wiring scope.
      continue;
    }
    Plane::writeScope(scopeParameter, currentArea);
  }
}

void Structure::writeKeepouts(WriteScopeParameter* scopeParameter) {
  fr::UndoableObjects_Storable* currentObject;
  jrt::Iterator<fr::UndoableObjects_UndoableObjectNode*>* it =
      scopeParameter->board->itemList->startReadObject();
  for (;;) {
    currentObject = scopeParameter->board->itemList->readObject(it);
    if (currentObject == nullptr) {
      break;
    }
    auto* currentKeepout = jrt::fast_cast<fr::ObstacleArea>(currentObject);
    if (currentKeepout == nullptr) {
      continue;
    }
    if (currentKeepout->getComponentId() != 0) {
      // keepouts belonging to a component are not written individually.
      continue;
    }
    if (currentKeepout->name.has_value() && jrt::endsWith(*currentKeepout->name, "_aux")) {
      // auxiliary companion tiles generated for custom padstacks are not standalone keepouts
      continue;
    }
    if (jrt::fast_cast<fr::ConductionArea>(currentKeepout) != nullptr) {
      // conduction area will be written later.
      continue;
    }
    writeKeepoutScope(scopeParameter, currentKeepout);
  }
}

void Structure::writeBoundaries(WriteScopeParameter* scopeParameter) {
  // write the bounding box
  scopeParameter->file->startScope();
  scopeParameter->file->write("boundary");
  fr::IntBox* bounds = scopeParameter->board->getBoundingBox();
  jrt::Array<double>* rectCoor = scopeParameter->coordinateTransform->boardToDsn(bounds);
  Rectangle* boundingRectangle = new Rectangle(Layer::PCB(), rectCoor);
  boundingRectangle->writeScope(scopeParameter->file, scopeParameter->identifierType);
  scopeParameter->file->endScope();
  // lookup the outline in the board
  fr::UndoableObjects_Storable* currentObject;
  jrt::Iterator<fr::UndoableObjects_UndoableObjectNode*>* it =
      scopeParameter->board->itemList->startReadObject();
  for (;;) {
    currentObject = scopeParameter->board->itemList->readObject(it);
    if (currentObject == nullptr) {
      break;
    }
    if (jrt::fast_cast<fr::BoardOutline>(currentObject) != nullptr) {
      break;
    }
  }
  if (currentObject == nullptr) {
    FRLogger::warn("Structure.write_scope: board outline not found");
    return;
  }
  fr::BoardOutline* outline = jrt::fast_cast<fr::BoardOutline>(currentObject);

  // write the outline
  for (int32_t i = 0; i < outline->shapeCount(); i++) {
    Shape* outlineShape =
        scopeParameter->coordinateTransform->boardToDsn(outline->getShape(i), Layer::SIGNAL());
    scopeParameter->file->startScope();
    scopeParameter->file->write("boundary");
    outlineShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
    scopeParameter->file->endScope();
  }
}

void Structure::writeLayers(WriteScopeParameter* scopeParameter) {
  for (int32_t i = 0; i < scopeParameter->board->layerStructure->layers->length; i++) {
    bool writeLayerRule =
        scopeParameter->board->rules->getDefaultNetClass()->getTraceHalfWidth(i) !=
            scopeParameter->board->rules->getDefaultNetClass()->getTraceHalfWidth(0) ||
        !clearanceEquals(scopeParameter->board->rules->clearanceMatrix, i, 0);
    Layer::writeScope(scopeParameter, i, writeLayerRule);
  }
}

void Structure::writeDefaultRules(WriteScopeParameter* scopeParameter) {
  // write the default rule using 0 as default layer.
  Rule::writeDefaultRule(scopeParameter, 0);
}

void Structure::writeViaPadstacks(fr::BoardLibrary* library, fr::IndentFileWriter* file,
                                  fr::IdentifierType* identifierType) {
  file->newLine();
  file->write("(via");
  for (int32_t i = 0; i < library->viaPadstackCount(); i++) {
    fr::Padstack* currentPadstack = library->getViaPadstack(i);
    if (currentPadstack != nullptr) {
      file->write(" ");
      identifierType->write(currentPadstack->name, file);
    } else {
      FRLogger::warn("Structure.write_via_padstacks: padstack is null");
    }
  }
  file->write(")");
}

void Structure::writeControlScope(fr::BoardRules* rules, fr::IndentFileWriter* file) {
  file->startScope();
  file->write("control");
  file->newLine();
  file->write("(via_at_smd ");
  bool viaAtSmdAllowed = false;
  for (int32_t i = 0; i < rules->viaInfos->count(); i++) {
    if (rules->viaInfos->get(i)->attachSmdAllowed()) {
      viaAtSmdAllowed = true;
      break;
    }
  }
  if (viaAtSmdAllowed) {
    file->write("on)");
  } else {
    file->write("off)");
  }
  file->endScope();
}

void Structure::writeKeepoutScope(WriteScopeParameter* scopeParameter, fr::ObstacleArea* keepout) {
  fr::Area* keepoutArea = keepout->getArea();
  int32_t layerIndex = keepout->getLayer();
  fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];
  Layer* const keepoutLayer = new Layer(boardLayer->name, layerIndex, boardLayer->isSignal);
  fr::Shape* boundaryShape;
  jrt::Array<fr::Shape*>* holes;
  if (auto* shape = jrt::fast_cast<fr::Shape>(keepoutArea)) {
    boundaryShape = shape;
    holes = new jrt::Array<fr::Shape*>(0);
  } else {
    boundaryShape = keepoutArea->getBorder();
    holes = keepoutArea->getHoles();
  }
  scopeParameter->file->startScope();
  if (jrt::fast_cast<fr::ViaObstacleArea>(keepout) != nullptr) {
    scopeParameter->file->write("via_keepout");
  } else if (jrt::fast_cast<fr::WireObstacleArea>(keepout) != nullptr) {
    scopeParameter->file->write("wire_keepout");
  } else {
    scopeParameter->file->write("keepout");
  }
  Shape* dsnShape = scopeParameter->coordinateTransform->boardToDsn(boundaryShape, keepoutLayer);
  if (dsnShape != nullptr) {
    dsnShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
  }
  for (int32_t i = 0; i < holes->length; i++) {
    Shape* dsnHole = scopeParameter->coordinateTransform->boardToDsn((*holes)[i], keepoutLayer);
    dsnHole->writeHoleScope(scopeParameter->file, scopeParameter->identifierType);
  }
  // write clearance class if it's defined for this keepout area.
  if (keepout->clearanceClassIndex() > 0) {
    // skip it if it's the default clearance class.
    std::string clearanceName =
        scopeParameter->board->rules->clearanceMatrix->getName(keepout->clearanceClassIndex());

    if (!("default" == clearanceName)) {
      Rule::writeItemClearanceClass(clearanceName, scopeParameter->file,
                                    scopeParameter->identifierType);
    }
  }
  scopeParameter->file->endScope();
}

bool Structure::readBoundaryScope(IJFlexScanner* scanner,
                                  Structure_BoardConstructionInfo* boardConstructionInfo) {
  Shape* currentShape = Shape::readScope(scanner, nullptr);
  try {
    jrt::Object* prevToken = nullptr;
    for (;;) {
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      if (prevToken == Keyword::OPEN_BRACKET()) {
        if (nextToken == Keyword::CLEARANCE_CLASS()) {
          boardConstructionInfo->outlineClearanceClassName = DsnFile::readStringScope(scanner);
        } else {
          Shape* additionalShape = Shape::readScopeFromKeyword(scanner, nextToken, nullptr);
          addBoundaryShape(boardConstructionInfo, additionalShape);
        }
      }
      prevToken = nextToken;
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("Structure.read_boundary_scope: IO error scanning file", &e);
    return false;
  }
  if (currentShape == nullptr) {
    FRLogger::warn("Structure.read_boundary_scope: shape is null at '" +
                   scanner->getScopeIdentifier() + "'");
    return true;
  }
  addBoundaryShape(boardConstructionInfo, currentShape);
  return true;
}

void Structure::addBoundaryShape(Structure_BoardConstructionInfo* boardConstructionInfo,
                                 Shape* shape) {
  if (shape == nullptr) {
    return;
  }
  if (jrt::fast_cast<PolylinePath>(shape) != nullptr ||
      jrt::fast_cast<PolygonPath>(shape) != nullptr) {
    boardConstructionInfo->outlineShapes->add(shape);
    return;
  }
  if (shape->layer == Layer::PCB()) {
    if (boardConstructionInfo->boundingShape == nullptr) {
      boardConstructionInfo->boundingShape = shape;
    } else {
      boardConstructionInfo->outlineShapes->add(shape);
    }
  } else if (shape->layer == Layer::SIGNAL()) {
    boardConstructionInfo->outlineShapes->add(shape);
  } else {
    FRLogger::warn("Structure.add_boundary_shape: unexpected layer at boundary");
  }
}

bool Structure::readLayerScope(IJFlexScanner* scanner,
                               Structure_BoardConstructionInfo* boardConstructionInfo,
                               const std::string& stringQuote) {
  try {
    bool layerOk = true;
    bool isSignal = true;

    std::string layerString = scanner->nextString();

    jrt::Collection<std::string>* netNames = new jrt::LinkedList<std::string>();
    jrt::Object* nextToken = scanner->nextToken();
    while (nextToken != Keyword::CLOSED_BRACKET()) {
      if (nextToken != Keyword::OPEN_BRACKET()) {
        FRLogger::warn("Structure.read_layer_scope: ( expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      nextToken = scanner->nextToken();
      if (nextToken == Keyword::TYPE()) {
        nextToken = scanner->nextToken();
        if (nextToken == Keyword::POWER()) {
          isSignal = false;
        } else if (nextToken != Keyword::SIGNAL()) {
          // Java: !Objects.equals(nextToken.toString(), Keyword.JUMPER.getName())
          if (nextToken == nullptr) {
            throw jrt::NullPointerException("nextToken.toString()");
          }
          if (!(nextToken->toString() == Keyword::JUMPER()->getName())) {
            if (jrt::fast_cast<JString>(nextToken) != nullptr) {
              FRLogger::error("Structure.read_layer_scope: the layer '" + layerString +
                                  "' has an unknown layer type '" + jrt::str(nextToken) + "'",
                              static_cast<const std::exception*>(nullptr));
            } else {
              FRLogger::warn("Structure.read_layer_scope: the layer '" + layerString +
                             "' has an unknown layer type at '" + scanner->getScopeIdentifier() +
                             "'");
            }
            layerOk = false;
          }
        }
        nextToken = scanner->nextToken();
        if (nextToken != Keyword::CLOSED_BRACKET()) {
          FRLogger::warn("Structure.read_layer_scope: ) expected at '" +
                         scanner->getScopeIdentifier() + "'");
          return false;
        }
      } else if (nextToken == Keyword::RULE()) {
        jrt::Collection<Rule*>* currentRules = Rule::readScope(scanner);
        boardConstructionInfo->layerDependentRules->add(
            new Structure_LayerRule(layerString, currentRules));
      } else if (nextToken == Keyword::USE_NET()) {
        for (;;) {
          scanner->yybegin(SpecctraDsnStreamReader::NAME);
          nextToken = scanner->nextToken();
          if (nextToken == Keyword::CLOSED_BRACKET()) {
            break;
          }
          if (auto* string = jrt::fast_cast<JString>(nextToken)) {
            netNames->add(string->value);
          } else {
            FRLogger::warn("Structure.read_layer_scope: string expected at '" +
                           scanner->getScopeIdentifier() + "'");
          }
        }
      } else {
        skipScope(scanner);
      }
      nextToken = scanner->nextToken();
    }
    if (layerOk) {
      Layer* const currentLayer =
          new Layer(layerString, boardConstructionInfo->foundLayerCount, isSignal, netNames);
      boardConstructionInfo->layerInfo->add(currentLayer);
      ++boardConstructionInfo->foundLayerCount;
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("Layer.read_scope: IO error scanning file", &e);
    return false;
  }
  return true;
}

jrt::Collection<std::string>* Structure::readViaPadstacks(IJFlexScanner* scanner) {
  try {
    jrt::Collection<std::string>* normalVias = new jrt::LinkedList<std::string>();
    jrt::Collection<std::string>* spareVias = new jrt::LinkedList<std::string>();
    for (;;) {
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      if (nextToken == Keyword::OPEN_BRACKET()) {
        nextToken = scanner->nextToken();
        if (nextToken == Keyword::SPARE()) {
          spareVias = readViaPadstacks(scanner);
        } else {
          skipScope(scanner);
        }
      } else if (auto* string = jrt::fast_cast<JString>(nextToken)) {
        normalVias->add(string->value);
      } else {
        FRLogger::warn("Structure.read_via_padstack: String expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    // add the spare vias to the end of the list
    addAllNonNull(normalVias, spareVias);
    return normalVias;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Structure.read_via_padstack: IO error scanning file", &e);
    return nullptr;
  }
}

bool Structure::readControlScope(ReadScopeParameter* scopeParameter) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Structure.read_control_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Structure.read_control_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == Keyword::VIA_AT_SMD()) {
        scopeParameter->viaAtSmdAllowed = DsnFile::readOnOffScope(scopeParameter->scanner);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
  }
  return true;
}

std::optional<fr::AngleRestriction> Structure::readSnapAngle(IJFlexScanner* scanner) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    fr::AngleRestriction snapAngle;
    if (nextToken == Keyword::NINETY_DEGREE()) {
      snapAngle = fr::AngleRestriction::NINETY_DEGREE;
    } else if (nextToken == Keyword::FORTYFIVE_DEGREE()) {
      snapAngle = fr::AngleRestriction::FORTYFIVE_DEGREE;
    } else if (nextToken == Keyword::NONE()) {
      snapAngle = fr::AngleRestriction::NONE;
    } else {
      FRLogger::warn("Structure.read_snap_angle_scope: unexpected token at '" +
                     scanner->getScopeIdentifier() + "'");
      return std::nullopt;
    }
    nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("Structure.read_selection_layer_scop: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return std::nullopt;
    }
    return snapAngle;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Structure.read_snap_angle: IO error scanning file", &e);
    return std::nullopt;
  }
}

void Structure::writeSnapAngle(fr::IndentFileWriter* file, fr::AngleRestriction angleRestriction) {
  file->startScope();
  file->write("snapAngle ");
  file->newLine();

  if (angleRestriction == fr::AngleRestriction::NINETY_DEGREE) {
    file->write("ninety_degree");
  } else if (angleRestriction == fr::AngleRestriction::FORTYFIVE_DEGREE) {
    file->write("fortyfive_degree");
  } else {
    file->write("none");
  }
  file->endScope();
}

void Structure::insertMissingPowerPlanes(jrt::Collection<Layer*>* layerInfo, NetList* netlist,
                                         fr::BasicBoard* board,
                                         jrt::Set<std::string>* layersWithWireCopper) {
  jrt::Collection<fr::ConductionArea*>* conductionAreas = board->getConductionAreas();
  for (Layer* currentLayer : *layerInfo) {
    if (currentLayer->isSignal || layersWithWireCopper->contains(currentLayer->name)) {
      // a layer whose copper comes from the wiring scope gets its plane there
      continue;
    }
    bool conductionAreaFound = false;
    for (fr::ConductionArea* currentConductionArea : *conductionAreas) {
      if (currentConductionArea->getLayer() == currentLayer->no) {
        conductionAreaFound = true;
        break;
      }
    }
    if (!conductionAreaFound && !currentLayer->netNames->isEmpty()) {
      std::string currentNetName = currentLayer->netNames->iterator()->next();
      Net_Id* currentNetId = new Net_Id(currentNetName, 1);
      if (!netlist->contains(currentNetId)) {
        Net* newNet = netlist->addNet(currentNetId);
        if (newNet != nullptr) {
          board->rules->nets->add(newNet->id->name, newNet->id->subnetNumber, true);
        }
      }
      fr::Net* const currentNet =
          board->rules->nets->get(currentNetId->name, currentNetId->subnetNumber);
      {
        if (currentNet == nullptr) {
          FRLogger::warn("Structure.insert_missing_power_planes: net not found at '" +
                         currentNetId->name + "'");
          continue;
        }
      }
      jrt::Array<int32_t>* netNumbers = new jrt::Array<int32_t>(1);
      (*netNumbers)[0] = currentNet->netNumber;
      board->insertConductionArea(board->boundingBox, currentLayer->no, netNumbers,
                                  fr::BoardRules::clearanceClassNone(), false,
                                  fr::FixedState::SYSTEM_FIXED);
    }
  }
}

jrt::Collection<fr::PolylineShape*>* Structure::separateHoles(
    jrt::Collection<fr::PolylineShape*>* outlineShapes) {
  jrt::Array<Structure_OutlineShape*>* shapes =
      new jrt::Array<Structure_OutlineShape*>(outlineShapes->size());
  jrt::Iterator<fr::PolylineShape*>* it = outlineShapes->iterator();
  for (int32_t i = 0; i < shapes->length; i++) {
    (*shapes)[i] = new Structure_OutlineShape(it->next());
  }
  for (int32_t i = 0; i < shapes->length; i++) {
    Structure_OutlineShape* currentShape = (*shapes)[i];
    for (int32_t j = 0; j < shapes->length; j++) {
      // check if shapes[j] may be contained in shapes[i]
      Structure_OutlineShape* otherShape = (*shapes)[j];
      if (i == j || otherShape->isHole) {
        continue;
      }
      if (!otherShape->boundingBox->contains(currentShape->boundingBox)) {
        continue;
      }
      currentShape->isHole = otherShape->containsAllCorners(currentShape);
    }
  }
  jrt::Collection<fr::PolylineShape*>* holeList = new jrt::LinkedList<fr::PolylineShape*>();
  for (int32_t i = 0; i < shapes->length; i++) {
    if ((*shapes)[i]->isHole) {
      outlineShapes->remove((*shapes)[i]->shape);
      holeList->add((*shapes)[i]->shape);
    }
  }
  return holeList;
}

void Structure::updateBoardRules(ReadScopeParameter* scopeParameter,
                                 Structure_BoardConstructionInfo* boardConstructionInfo,
                                 fr::BoardRules* boardRules) {
  bool smdToTurnGapFound = false;
  // update the clearance matrix
  for (Rule* currentObject : *boardConstructionInfo->defaultRules) {
    if (auto* currentRule = jrt::fast_cast<Rule_ClearanceRule>(currentObject)) {
      if (setClearanceRule(currentRule, -1, scopeParameter->coordinateTransform, boardRules,
                           scopeParameter->stringQuote)) {
        smdToTurnGapFound = true;
      }
    }
  }
  // update width rules
  for (Rule* currentObject : *boardConstructionInfo->defaultRules) {
    if (auto* rule = jrt::fast_cast<Rule_WidthRule>(currentObject)) {
      const double wireWidth = rule->value;
      int32_t traceHalfwidth = jrt::l2i(
          jrt::Math::round(scopeParameter->coordinateTransform->dsnToBoard(wireWidth) / 2));
      FRLogger::debug("Set default trace width (all layers): DSN=" + jrt::str(wireWidth) +
                      " → board=" + jrt::str(traceHalfwidth * 2) + " (" +
                      jrt::str(traceHalfwidth * 2 / 40000.0) + " mm)");
      boardRules->setDefaultTraceHalfWidths(traceHalfwidth);
    } else if (auto* rule = jrt::fast_cast<Rule_MeanderRule>(currentObject)) {
      fr::NetMeanderConstraint* currentDefault =
          boardRules->getDefaultMeanderConstraint(fr::MeanderPatternType::SINGLE_TRACK);
      fr::NetMeanderConstraint* updated =
          rule->applyTo(currentDefault, scopeParameter->coordinateTransform);
      boardRules->setDefaultMeanderConstraint(fr::MeanderPatternType::SINGLE_TRACK, updated);
    }
  }
  for (Structure_LayerRule* layerRule : *boardConstructionInfo->layerDependentRules) {
    int32_t layerIndex = scopeParameter->layerStructure->getNo(layerRule->layerName);
    if (layerIndex < 0) {
      continue;
    }
    // PORT NOTE: Java iterating a null rule collection throws NullPointerException.
    if (layerRule->rule == nullptr) {
      throw jrt::NullPointerException("layerRule.rule");
    }
    for (Rule* currentObject : *layerRule->rule) {
      if (auto* rule = jrt::fast_cast<Rule_WidthRule>(currentObject)) {
        const double wireWidth = rule->value;
        int32_t traceHalfwidth = jrt::l2i(
            jrt::Math::round(scopeParameter->coordinateTransform->dsnToBoard(wireWidth) / 2));
        boardRules->setDefaultTraceHalfWidth(layerIndex, traceHalfwidth);
      } else if (auto* currentRule = jrt::fast_cast<Rule_ClearanceRule>(currentObject)) {
        setClearanceRule(currentRule, layerIndex, scopeParameter->coordinateTransform, boardRules,
                         scopeParameter->stringQuote);
      }
    }
  }
  if (!smdToTurnGapFound) {
    boardRules->setPinEdgeToTurnDist(boardRules->getMinTraceHalfWidth());
  }
}

bool Structure::setClearanceRule(Rule_ClearanceRule* rule, int32_t layerIndex,
                                 fr::io::CoordinateTransform* coordinateTransform,
                                 fr::BoardRules* boardRules, const std::string& stringQuote) {
  bool result = false;
  int32_t currentClearance = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(rule->value)));
  if (rule->clearanceClassPairs->isEmpty()) {
    if (layerIndex < 0) {
      boardRules->clearanceMatrix->setDefaultValue(currentClearance);
      FRLogger::debug("Set DEFAULT clearance (all layers): " + jrt::str(currentClearance) + " (" +
                      jrt::str(currentClearance / 40000.0) + " mm) from DSN value " +
                      jrt::str(rule->value));
    } else {
      boardRules->clearanceMatrix->setDefaultValue(layerIndex, currentClearance);
      FRLogger::debug("Set DEFAULT clearance (layer " + jrt::str(layerIndex) + "): " +
                      jrt::str(currentClearance) + " (" + jrt::str(currentClearance / 40000.0) +
                      " mm) from DSN value " + jrt::str(rule->value));
    }
    return result;
  }
  if (containsWireClearancePair(rule->clearanceClassPairs)) {
    createDefaultClearanceClasses(boardRules);
  }

  for (std::string currentString : *rule->clearanceClassPairs) {
    if (jrt::equalsIgnoreCase("smd_to_turn_gap", currentString)) {
      boardRules->setPinEdgeToTurnDist(currentClearance);
      result = true;
      continue;
    }
    jrt::Array<std::string>* currentPair = new jrt::Array<std::string>(2);
    if (rule->clearanceClassPairs->size() == 2) {
      jrt::Iterator<std::string>* iterator = rule->clearanceClassPairs->iterator();
      (*currentPair)[0] = iterator->next();
      (*currentPair)[1] = iterator->next();
      for (int32_t i = 0; i < currentPair->length; i++) {
        (*currentPair)[i] = removeQuotes((*currentPair)[i]);
        if (jrt::startsWith((*currentPair)[1], "_")) {
          (*currentPair)[1] = jrt::substring((*currentPair)[1], 1);
        }
      }
    } else if (jrt::startsWith(currentString, stringQuote)) {
      // split at the second occurrence of stringQuote
      // PORT NOTE: Java String.split(stringQuote, 2) uses stringQuote as a regex; the quote
      // characters used in DSN files have no regex meaning, so a literal split is used.
      currentString = jrt::substring(currentString, static_cast<int32_t>(stringQuote.length()));
      currentPair = splitLimit2(currentString, stringQuote);
      if (currentPair->length != 2 || !jrt::startsWith((*currentPair)[1], "_")) {
        FRLogger::warn("Structure.set_clearance_rule: '_' expected at '" + currentString + "'");
        FRLogger::warn(
            "You probably get this error because your clearance rule name has spaces or "
            "special characters in its name. Please change them first, and try again.");
        continue;
      }
      (*currentPair)[1] = jrt::substring((*currentPair)[1], 1);
    } else {
      currentPair = splitLimit2(currentString, "_");
      if (currentPair->length != 2) {
        // pairs with more than 1 underline like smd_via_same_net are not implemented
        continue;
      }
    }

    int32_t firstClassNo;
    if ("wire" == (*currentPair)[0]) {
      firstClassNo = 1;  // default class
    } else {
      firstClassNo = boardRules->clearanceMatrix->getNo((*currentPair)[0]);
    }
    if (firstClassNo < 0) {
      firstClassNo = appendClearanceClass(boardRules, (*currentPair)[0]);
    }
    int32_t secondClassNo;
    if ("wire" == (*currentPair)[1]) {
      secondClassNo = 1;  // default class
    } else {
      secondClassNo = boardRules->clearanceMatrix->getNo((*currentPair)[1]);
    }
    if (secondClassNo < 0) {
      secondClassNo = appendClearanceClass(boardRules, (*currentPair)[1]);
    }
    if (layerIndex < 0) {
      boardRules->clearanceMatrix->setValue(firstClassNo, secondClassNo, currentClearance);
      boardRules->clearanceMatrix->setValue(secondClassNo, firstClassNo, currentClearance);
      FRLogger::debug("Set clearance (all layers): " + (*currentPair)[0] + "_" +
                      (*currentPair)[1] + " = " + jrt::str(currentClearance) + " (" +
                      jrt::str(currentClearance / 40000.0) + " mm), classes [" +
                      jrt::str(firstClassNo) + "," + jrt::str(secondClassNo) + "]");
    } else {
      boardRules->clearanceMatrix->setValue(firstClassNo, secondClassNo, layerIndex,
                                            currentClearance);
      boardRules->clearanceMatrix->setValue(secondClassNo, firstClassNo, layerIndex,
                                            currentClearance);
      FRLogger::debug("Set clearance (layer " + jrt::str(layerIndex) + "): " +
                      (*currentPair)[0] + "_" + (*currentPair)[1] + " = " +
                      jrt::str(currentClearance) + " (" + jrt::str(currentClearance / 40000.0) +
                      " mm), classes [" + jrt::str(firstClassNo) + "," + jrt::str(secondClassNo) +
                      "]");
    }
  }
  return result;
}

bool Structure::containsWireClearancePair(jrt::Collection<std::string>* clearancePairs) {
  for (const std::string& currentPair : *clearancePairs) {
    if (jrt::startsWith(currentPair, "wire_") || jrt::endsWith(currentPair, "_wire")) {
      return true;
    }
  }
  return false;
}

void Structure::createDefaultClearanceClasses(fr::BoardRules* boardRules) {
  appendClearanceClass(boardRules, "via");
  appendClearanceClass(boardRules, "smd");
  appendClearanceClass(boardRules, "pin");
  appendClearanceClass(boardRules, "area");
}

int32_t Structure::appendClearanceClass(fr::BoardRules* boardRules, const std::string& name) {
  boardRules->clearanceMatrix->appendClass(name);
  int32_t result = boardRules->clearanceMatrix->getNo(name);
  fr::NetClass* defaultNetClass = boardRules->getDefaultNetClass();
  if (name == "via") {
    defaultNetClass->defaultItemClearanceClasses->set(fr::DefaultItemClearanceClasses_ItemClass::VIA,
                                                      result);
  } else if (name == "pin") {
    defaultNetClass->defaultItemClearanceClasses->set(fr::DefaultItemClearanceClasses_ItemClass::PIN,
                                                      result);
  } else if (name == "smd") {
    defaultNetClass->defaultItemClearanceClasses->set(fr::DefaultItemClearanceClasses_ItemClass::SMD,
                                                      result);
  } else if (name == "area") {
    defaultNetClass->defaultItemClearanceClasses->set(
        fr::DefaultItemClearanceClasses_ItemClass::AREA, result);
  } else {
    // Ignore unsupported item classes.
  }
  return result;
}

bool Structure::clearanceEquals(fr::ClearanceMatrix* clMatrix, int32_t layer1, int32_t layer2) {
  if (layer1 == layer2) {
    return true;
  }
  for (int32_t i = 1; i < clMatrix->getClassCount(); i++) {
    for (int32_t j = i; j < clMatrix->getClassCount(); j++) {
      if (clMatrix->getValue(i, j, layer1, false) != clMatrix->getValue(i, j, layer2, false)) {
        return false;
      }
    }
  }
  return true;
}

bool Structure::insertKeepout(Shape_ReadAreaScopeResult* area, ReadScopeParameter* scopeParameter,
                              Structure_KeepoutType keepoutType, fr::FixedState fixedState) {
  if (area == nullptr || area->shapeList == nullptr || area->shapeList->isEmpty()) {
    FRLogger::warn("Structure.insert_keepout: area shapeList is empty or null");
    return true;
  }
  fr::Area* keepoutArea =
      Shape::transformAreaToBoard(area->shapeList, scopeParameter->coordinateTransform);
  if (keepoutArea == nullptr || keepoutArea->dimension() < 2) {
    // A degenerate keepout (e.g. all polygon vertices identical, exported incorrectly by the EDA
    // tool) cannot be enforced as a routing constraint. The board remains valid — the keepout
    // restriction is simply not applied, making routing more permissive in that area.
    // This is a known export defect in some EDA tools (e.g. KiCad 4.0.7).
    FRLogger::warn("Keepout zone '" + jrt::str(area->areaName) +
                   "' was skipped because its geometry is degenerate "
                   "(e.g. zero-area polygon). This is likely a DSN export issue in your EDA tool. "
                   "The board will be routed without this keepout constraint.");
    return true;
  }
  fr::BasicBoard* board = scopeParameter->boardHandling->getRoutingBoard();
  if (board == nullptr) {
    FRLogger::warn("Structure.insert_keepout: board not initialized");
    return false;
  }
  Layer* const currentLayer = (area->shapeList->iterator()->next())->layer;
  if (currentLayer == Layer::SIGNAL()) {
    for (int32_t i = 0; i < board->getLayerCount(); i++) {
      if ((*scopeParameter->layerStructure->layers)[i]->isSignal) {
        insertKeepout(board, keepoutArea, i, area->clearanceClassName, keepoutType, fixedState,
                      area->areaName);
      }
    }
  } else if (currentLayer->no >= 0) {
    insertKeepout(board, keepoutArea, currentLayer->no, area->clearanceClassName, keepoutType,
                  fixedState, area->areaName);
  } else {
    FRLogger::warn("Structure.insert_keepout: unknown layer name at '" +
                   scopeParameter->scanner->getScopeIdentifier() + "'");
    return false;
  }

  return true;
}

void Structure::insertKeepout(fr::BasicBoard* board, fr::Area* area, int32_t layer,
                              const std::optional<std::string>& clearanceClassName,
                              Structure_KeepoutType keepoutType, fr::FixedState fixedState,
                              const std::optional<std::string>& name) {
  int32_t clearanceClassIndex;
  if (!clearanceClassName.has_value()) {
    clearanceClassIndex = board->rules->getDefaultNetClass()->defaultItemClearanceClasses->get(
        fr::DefaultItemClearanceClasses_ItemClass::AREA);
  } else {
    clearanceClassIndex = board->rules->clearanceMatrix->getNo(*clearanceClassName);
    if (clearanceClassIndex < 0) {
      FRLogger::warn("Keepout.insert_keepout: clearance class not found at '" +
                     *clearanceClassName + "'");
      clearanceClassIndex = fr::BoardRules::clearanceClassNone();
    }
  }
  if (keepoutType == Structure_KeepoutType::via_keepout && name.has_value() && !name->empty()) {
    board->insertViaObstacle(area, layer, fr::Vector::ZERO(), 0, false, clearanceClassIndex, 0,
                             name, fixedState);
  } else if (keepoutType == Structure_KeepoutType::via_keepout) {
    board->insertViaObstacle(area, layer, clearanceClassIndex, fixedState);
  } else if (keepoutType == Structure_KeepoutType::wire_keepout) {
    board->insertWireObstacle(area, layer, clearanceClassIndex, fixedState);
  } else if (keepoutType == Structure_KeepoutType::place_keepout) {
    board->insertComponentObstacle(area, layer, clearanceClassIndex, fixedState);
  } else if (name.has_value() && !name->empty()) {
    board->insertObstacle(area, layer, fr::Vector::ZERO(), 0, false, clearanceClassIndex, 0, name,
                          fixedState);
  } else {
    board->insertObstacle(area, layer, clearanceClassIndex, fixedState);
  }
}

bool Structure::readScope(ReadScopeParameter* scopeParameter) {
  Structure_BoardConstructionInfo* boardConstructionInfo = new Structure_BoardConstructionInfo();

  // If true, components on the back side are rotated before mirroring
  // The correct location is the scope PlaceControl, but Electra writes it here.
  bool flipStyleRotateFirst = false;

  jrt::Collection<Shape_ReadAreaScopeResult*>* keepoutList =
      new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
  jrt::Collection<Shape_ReadAreaScopeResult*>* viaKeepoutList =
      new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
  jrt::Collection<Shape_ReadAreaScopeResult*>* wireKeepoutList =
      new jrt::LinkedList<Shape_ReadAreaScopeResult*>();
  jrt::Collection<Shape_ReadAreaScopeResult*>* placeKeepoutList =
      new jrt::LinkedList<Shape_ReadAreaScopeResult*>();

  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Structure.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Structure.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    bool readOk = true;
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == Keyword::BOUNDARY()) {
        readBoundaryScope(scopeParameter->scanner, boardConstructionInfo);
      } else if (nextToken == Keyword::LAYER()) {
        readOk = readLayerScope(scopeParameter->scanner, boardConstructionInfo,
                                scopeParameter->stringQuote);
        if (scopeParameter->layerStructure != nullptr) {
          // correct the layerStructure because another layer isr read
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
      } else if (nextToken == Keyword::VIA()) {
        scopeParameter->viaPadstackNames = readViaPadstacks(scopeParameter->scanner);
      } else if (nextToken == Keyword::RULE()) {
        addAllNonNull(boardConstructionInfo->defaultRules, Rule::readScope(scopeParameter->scanner));
      } else if (nextToken == Keyword::KEEPOUT()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        keepoutList->add(
            Shape::readAreaScope(scopeParameter->scanner, scopeParameter->layerStructure, false));
      } else if (nextToken == Keyword::VIA_KEEPOUT()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        viaKeepoutList->add(
            Shape::readAreaScope(scopeParameter->scanner, scopeParameter->layerStructure, false));
      } else if (nextToken == Keyword::WIRE_KEEPOUT()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        wireKeepoutList->add(
            Shape::readAreaScope(scopeParameter->scanner, scopeParameter->layerStructure, false));
      } else if (nextToken == Keyword::PLACE_KEEPOUT()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        placeKeepoutList->add(
            Shape::readAreaScope(scopeParameter->scanner, scopeParameter->layerStructure, false));
      } else if (nextToken == Keyword::PLANE_SCOPE()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        Keyword::PLANE_SCOPE()->readScope(scopeParameter);
      } else if (nextToken == Keyword::AUTOROUTE_SETTINGS()) {
        if (scopeParameter->layerStructure == nullptr) {
          scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);
        }
        scopeParameter->autorouteSettings =
            AutorouteSettings::readScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == Keyword::CONTROL()) {
        readOk = readControlScope(scopeParameter);
      } else if (nextToken == Keyword::FLIP_STYLE()) {
        flipStyleRotateFirst = PlaceControl::readFlipStyleRotateFirst(scopeParameter->scanner);
      } else if (nextToken == Keyword::SNAP_ANGLE()) {
        std::optional<fr::AngleRestriction> snapAngle = readSnapAngle(scopeParameter->scanner);
        if (snapAngle.has_value()) {
          scopeParameter->snapAngle = *snapAngle;
        }
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
    if (!readOk) {
      return false;
    }
  }

  // let's create a board based on the data we read (TODO: move this method
  // somewhere outside of the designforms.specctra package)
  bool result = true;
  if (scopeParameter->boardHandling->getRoutingBoard() == nullptr) {
    result = createBoard(scopeParameter, boardConstructionInfo);
  }
  fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();
  if (board == nullptr) {
    return false;
  }
  if (flipStyleRotateFirst) {
    board->components->setFlipStyleRotateFirst(true);
  }

  // insert the keepouts
  for (Shape_ReadAreaScopeResult* currentArea : *keepoutList) {
    if (!insertKeepout(currentArea, scopeParameter, Structure_KeepoutType::keepout,
                       fr::FixedState::SYSTEM_FIXED)) {
      return false;
    }
  }

  for (Shape_ReadAreaScopeResult* currentArea : *viaKeepoutList) {
    if (!insertKeepout(currentArea, scopeParameter, Structure_KeepoutType::via_keepout,
                       fr::FixedState::SYSTEM_FIXED)) {
      return false;
    }
  }

  for (Shape_ReadAreaScopeResult* currentArea : *wireKeepoutList) {
    if (!insertKeepout(currentArea, scopeParameter, Structure_KeepoutType::wire_keepout,
                       fr::FixedState::SYSTEM_FIXED)) {
      return false;
    }
  }

  for (Shape_ReadAreaScopeResult* currentArea : *placeKeepoutList) {
    if (!insertKeepout(currentArea, scopeParameter, Structure_KeepoutType::place_keepout,
                       fr::FixedState::SYSTEM_FIXED)) {
      return false;
    }
  }

  // insert the planes.
  for (ReadScopeParameter_PlaneInfo* planeInfo : *scopeParameter->planeList) {
    Net_Id* netId = new Net_Id(planeInfo->netName, 1);
    if (!scopeParameter->netlist->contains(netId)) {
      Net* newNet = scopeParameter->netlist->addNet(netId);
      if (newNet != nullptr) {
        board->rules->nets->add(newNet->id->name, newNet->id->subnetNumber, true);
      }
    }
    fr::Net* const currentNet = board->rules->nets->get(planeInfo->netName, 1);
    if (currentNet == nullptr) {
      FRLogger::warn("Plane.read_scope: net not found at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      continue;
    }
    fr::Area* planeArea = Shape::transformAreaToBoard(planeInfo->area->shapeList,
                                                      scopeParameter->coordinateTransform);
    Layer* const currentLayer = (planeInfo->area->shapeList->iterator()->next())->layer;
    if (currentLayer->no >= 0) {
      int32_t clearanceClassIndex;
      if (planeInfo->area->clearanceClassName.has_value()) {
        clearanceClassIndex =
            board->rules->clearanceMatrix->getNo(*planeInfo->area->clearanceClassName);
        if (clearanceClassIndex < 0) {
          FRLogger::warn("Structure.read_scope: clearance class not found at '" +
                         scopeParameter->scanner->getScopeIdentifier() + "'");
          clearanceClassIndex = fr::BoardRules::clearanceClassNone();
        }
      } else {
        clearanceClassIndex = currentNet->getNetClass()->defaultItemClearanceClasses->get(
            fr::DefaultItemClearanceClasses_ItemClass::AREA);
      }
      jrt::Array<int32_t>* netNumbers = new jrt::Array<int32_t>(1);
      (*netNumbers)[0] = currentNet->netNumber;
      fr::ConductionArea* insertedPlane =
          board->insertConductionArea(planeArea, currentLayer->no, netNumbers,
                                      clearanceClassIndex, false, fr::FixedState::SYSTEM_FIXED);
      if (insertedPlane != nullptr && !planeInfo->area->clearanceClassName.has_value()) {
        scopeParameter->planesWithInheritedClearance->add(insertedPlane);
      }
    } else {
      FRLogger::warn("Plane.read_scope: unexpected layer name at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
  }
  insertMissingPowerPlanes(boardConstructionInfo->layerInfo, scopeParameter->netlist, board,
                           scopeParameter->polygonWireNetsByLayer->keySet());

  // Apply DSN autoroute settings to the current routing job if they were parsed
  if (scopeParameter->autorouteSettings != nullptr) {
    // Get the current routing job from the board manager
    fr::RoutingJob* currentJob = scopeParameter->boardHandling->getCurrentRoutingJob();
    if (currentJob != nullptr && currentJob->routerSettings != nullptr) {
      // Apply the DSN file's autoroute settings to the routing job
      currentJob->routerSettings->applyNewValuesFrom(scopeParameter->autorouteSettings);
      FRLogger::info("Applied DSN autoroute settings to routing job");
    }
  }

  return result;
}

void Structure::promotePowerLayersWithoutPlane(
    ReadScopeParameter* scopeParameter, Structure_BoardConstructionInfo* boardConstructionInfo) {
  jrt::Set<int32_t>* layersWithPlane = new jrt::HashSet<int32_t>();
  for (ReadScopeParameter_PlaneInfo* planeInfo : *scopeParameter->planeList) {
    for (Shape* planeShape : *planeInfo->area->shapeList) {
      layersWithPlane->add(planeShape->layer->no);
    }
  }
  jrt::List<Layer*>* updatedLayers =
      new jrt::ArrayList<Layer*>(boardConstructionInfo->layerInfo->size());
  bool changed = false;
  for (Layer* currentLayer : *boardConstructionInfo->layerInfo) {
    if (currentLayer->isSignal || !currentLayer->netNames->isEmpty() ||
        layersWithPlane->contains(currentLayer->no)) {
      updatedLayers->add(currentLayer);
      continue;
    }
    jrt::Set<std::string>* wireNets =
        scopeParameter->polygonWireNetsByLayer->get(currentLayer->name);
    if (wireNets != nullptr && !wireNets->isEmpty()) {
      // The plane copper comes later, as (wire (polygon ...)) in the wiring scope: the layer
      // stays a plane layer, and its nets are plane nets, as with a (plane ...) scope.
      FRLogger::info("Layer '" + currentLayer->name +
                     "' is declared as a power layer and its copper is given as wire polygon(s) of"
                     " net(s) " +
                     jrt::str(wireNets) + " in the wiring. It stays a plane layer.");
      updatedLayers->add(new Layer(currentLayer->name, currentLayer->no, false, wireNets));
      changed = true;
    } else {
      std::string message = "Layer '" + currentLayer->name +
                            "' is declared as a power layer but no plane is defined for it. It "
                            "will be treated as a signal layer.";
      if (scopeParameter->wiringIsRead) {
        FRLogger::warn(message);
      } else {
        FRLogger::debug(message);
      }
      updatedLayers->add(
          new Layer(currentLayer->name, currentLayer->no, true, currentLayer->netNames));
      changed = true;
    }
  }
  if (changed) {
    boardConstructionInfo->layerInfo->clear();
    boardConstructionInfo->layerInfo->addAll(updatedLayers);
  }
}

bool Structure::createBoard(ReadScopeParameter* scopeParameter,
                            Structure_BoardConstructionInfo* boardConstructionInfo) {
  int32_t layerCount = boardConstructionInfo->layerInfo->size();
  if (layerCount == 0) {
    FRLogger::warn("Structure.create_board: layers missing in structure scope at '" +
                   scopeParameter->scanner->getScopeIdentifier() + "'");
    return false;
  }
  promotePowerLayersWithoutPlane(scopeParameter, boardConstructionInfo);
  if (boardConstructionInfo->boundingShape == nullptr) {
    // happens if the boundary shape with layer pcb is missing
    if (boardConstructionInfo->outlineShapes->isEmpty()) {
      FRLogger::warn("Structure.create_board: outline missing at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      scopeParameter->boardOutlineOk = false;
      return false;
    }
    jrt::Iterator<Shape*>* it = boardConstructionInfo->outlineShapes->iterator();

    Rectangle* boundingBox = it->next()->boundingBox();
    while (it->hasNext()) {
      boundingBox = boundingBox->union_(it->next()->boundingBox());
    }
    boardConstructionInfo->boundingShape = boundingBox;
  }
  Rectangle* boundingBox = boardConstructionInfo->boundingShape->boundingBox();
  jrt::Array<fr::Layer*>* boardLayerArr = new jrt::Array<fr::Layer*>(layerCount);
  jrt::Iterator<Layer*>* it = boardConstructionInfo->layerInfo->iterator();
  for (int32_t i = 0; i < layerCount; i++) {
    Layer* const currentLayer = it->next();
    if (currentLayer->no < 0 || currentLayer->no >= layerCount) {
      FRLogger::warn("Structure.create_board: illegal layer number at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    (*boardLayerArr)[i] = new fr::Layer(currentLayer->name, currentLayer->isSignal);
  }
  fr::LayerStructure* const boardLayerStructure = new fr::LayerStructure(boardLayerArr);
  scopeParameter->layerStructure = new LayerStructure(boardConstructionInfo->layerInfo);

  // Calculate an approximate scaling between dsn coordinates and board
  // coordinates.
  // Keep fractional scales: integer division can otherwise collapse the board to zero.
  double scaleFactor = jrt::Math::max(scopeParameter->resolution, 1);

  double maxCoor = 0;
  for (int32_t i = 0; i < 4; i++) {
    maxCoor = jrt::Math::max(maxCoor, jrt::Math::abs((*boundingBox->coor)[i] * scaleFactor));
  }
  if (maxCoor == 0) {
    scopeParameter->boardOutlineOk = false;
    return false;
  }
  // make scalefactor smaller, if there is a danger of integer overflow.
  while (5 * maxCoor >= fr::Limits::CRIT_INT) {
    scaleFactor /= 10.0;
    maxCoor /= 10.0;
  }

  scopeParameter->coordinateTransform = new fr::io::CoordinateTransform(scaleFactor, 0, 0);

  fr::IntBox* bounds =
      jrt::fast_cast<fr::IntBox>(boundingBox->transformToBoard(scopeParameter->coordinateTransform));
  if (bounds == nullptr) {
    throw jrt::ClassCastException("Structure.create_board: IntBox expected");
  }
  bounds = bounds->offset(1000);

  jrt::Collection<fr::PolylineShape*>* boardOutlineShapes =
      new jrt::LinkedList<fr::PolylineShape*>();
  for (Shape* currentShape : *boardConstructionInfo->outlineShapes) {
    if (auto* currentPath = jrt::fast_cast<PolygonPath>(currentShape)) {
      if (currentPath->width != 0) {
        // set the width to 0, because the offset function used in transform_to_board is
        // not
        // implemented
        // for shapes, which are not convex.
        currentShape = new PolygonPath(currentPath->layer, 0, currentPath->coordinateArr);
      }
    }
    fr::Shape* transformed = currentShape->transformToBoard(scopeParameter->coordinateTransform);
    fr::PolylineShape* currentBoardShape = jrt::fast_cast<fr::PolylineShape>(transformed);
    if (currentBoardShape == nullptr) {
      throw jrt::ClassCastException("Structure.create_board: PolylineShape expected");
    }
    if (currentBoardShape->dimension() > 0) {
      boardOutlineShapes->add(currentBoardShape);
    }
  }
  if (boardOutlineShapes->isEmpty()) {
    // construct an outline from the boundingShape, if the outline is missing.
    fr::PolylineShape* currentBoardShape = jrt::fast_cast<fr::PolylineShape>(
        boardConstructionInfo->boundingShape->transformToBoard(
            scopeParameter->coordinateTransform));
    if (currentBoardShape == nullptr) {
      throw jrt::ClassCastException("Structure.create_board: PolylineShape expected");
    }
    boardOutlineShapes->add(currentBoardShape);
  }
  jrt::Collection<fr::PolylineShape*>* const holeShapes = separateHoles(boardOutlineShapes);
  fr::ClearanceMatrix* clearanceMatrix =
      fr::ClearanceMatrix::getDefaultInstance(boardLayerStructure, 0);
  fr::BoardRules* boardRules = new fr::BoardRules(boardLayerStructure, clearanceMatrix);
  fr::Communication_SpecctraParserInfo* specctraParserInfo =
      new fr::Communication_SpecctraParserInfo(
          scopeParameter->stringQuote, scopeParameter->hostCad, scopeParameter->hostVersion,
          scopeParameter->constants, scopeParameter->writeResolution,
          scopeParameter->dsnFileGeneratedByHost);
  fr::Communication* boardCommunication = new fr::Communication(
      scopeParameter->unit, scopeParameter->resolution, specctraParserInfo,
      scopeParameter->coordinateTransform, scopeParameter->idGenerator, scopeParameter->observers);

  if (boardCommunication->hostIsOldKicad()) {
    FRLogger::warn(
        "Structure.create_board: The DSN file was exported from an old KiCad version "
        "that has known compatibility issues. Please update KiCad to version 6 or newer.");
  }

  jrt::Array<fr::PolylineShape*>* outlineShapes =
      new jrt::Array<fr::PolylineShape*>(boardOutlineShapes->size());
  jrt::Iterator<fr::PolylineShape*>* it2 = boardOutlineShapes->iterator();
  for (int32_t i = 0; i < outlineShapes->length; i++) {
    (*outlineShapes)[i] = it2->next();
  }
  updateBoardRules(scopeParameter, boardConstructionInfo, boardRules);
  boardRules->setTraceAngleRestriction(scopeParameter->snapAngle);
  scopeParameter->boardHandling->createBoard(bounds, boardLayerStructure, outlineShapes,
                                             boardConstructionInfo->outlineClearanceClassName,
                                             boardRules, boardCommunication);

  fr::BasicBoard* board = scopeParameter->boardHandling->getRoutingBoard();

  // Insert the holes in the board outline as keepouts.
  for (fr::PolylineShape* currentOutlineHole : *holeShapes) {
    for (int32_t i = 0; i < boardLayerStructure->layers->length; i++) {
      board->insertObstacle(currentOutlineHole, i, 0, fr::FixedState::SYSTEM_FIXED);
    }
  }

  return true;
}

}  // namespace fr::dsn
