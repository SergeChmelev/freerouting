#include "io/specctra/parser/Wiring.h"

#include <regex>

#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polygon.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "io/specctra/parser/Circle.h"
#include "io/specctra/parser/Polygon.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Net.h"
#include "io/specctra/parser/Path.h"
#include "io/specctra/parser/PolygonPath.h"
#include "io/specctra/parser/PolylinePath.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/Rectangle.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/Shape.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"

namespace fr::dsn {

void Wiring::writeScope(WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("wiring");
  // write the wires
  jrt::Collection<fr::Trace*>* boardWires = scopeParameter->board->getTraces();
  for (fr::Trace* currentBoardWire : *boardWires) {
    writeWireScope(scopeParameter, currentBoardWire);
  }
  jrt::Collection<fr::Via*>* boardVias = scopeParameter->board->getVias();
  for (fr::Via* currentVia : *boardVias) {
    writeViaScope(scopeParameter, currentVia);
  }
  // write the conduction areas
  jrt::Iterator<fr::UndoableObjects_UndoableObjectNode*>* it2 =
      scopeParameter->board->itemList->startReadObject();
  for (;;) {
    fr::UndoableObjects_Storable* currentObject = scopeParameter->board->itemList->readObject(it2);
    if (currentObject == nullptr) {
      break;
    }
    auto* currentArea = jrt::fast_cast<fr::ConductionArea>(currentObject);
    if (currentArea == nullptr) {
      continue;
    }
    if (!(*scopeParameter->board->layerStructure->layers)[currentArea->getLayer()]->isSignal) {
      // This conduction areas arw written in the structure scope.
      continue;
    }
    writeConductionAreaScope(scopeParameter, currentArea);
  }
  scopeParameter->file->endScope();
}

void Wiring::writeViaScope(WriteScopeParameter* scopeParameter, fr::Via* via) {
  fr::Padstack* viaPadstack = via->getPadstack();
  fr::FloatPoint* viaLocation = via->getCenter()->toFloat();
  jrt::Array<double>* viaCoor = scopeParameter->coordinateTransform->boardToDsn(viaLocation);
  int32_t netNumber;
  fr::Net* viaNet;
  if (via->netCount() > 0) {
    netNumber = via->getNetNumber(0);
    viaNet = scopeParameter->board->rules->nets->get(netNumber);
  } else {
    netNumber = 0;
    viaNet = nullptr;
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("via ");
  scopeParameter->identifierType->write(viaPadstack->name, scopeParameter->file);
  for (int32_t i = 0; i < viaCoor->length; i++) {
    scopeParameter->file->write(" ");
    scopeParameter->file->write(jrt::str((*viaCoor)[i]));
  }
  if (viaNet != nullptr) {
    writeNet(viaNet, scopeParameter->file, scopeParameter->identifierType);
  }
  Rule::writeItemClearanceClass(
      scopeParameter->board->rules->clearanceMatrix->getName(via->clearanceClassIndex()),
      scopeParameter->file, scopeParameter->identifierType);
  writeFixedState(scopeParameter->file, via->getFixedState());
  scopeParameter->file->endScope();
}

void Wiring::writeWireScope(WriteScopeParameter* scopeParameter, fr::Trace* wire) {
  auto* currentWire = jrt::fast_cast<fr::PolylineTrace>(wire);
  if (currentWire == nullptr) {
    FRLogger::warn("Wiring.write_wire_scope: trace type not yet implemented");
    return;
  }
  int32_t layerIndex = currentWire->getLayer();
  fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];
  Layer* currentLayer = new Layer(boardLayer->name, layerIndex, boardLayer->isSignal);
  const double wireWidth =
      scopeParameter->coordinateTransform->boardToDsn(2 * currentWire->getHalfWidth());
  fr::Net* wireNet = nullptr;
  if (currentWire->netCount() > 0) {
    wireNet = scopeParameter->board->rules->nets->get(currentWire->getNetNumber(0));
  }
  if (wireNet == nullptr) {
    FRLogger::warn("Wiring.write_wire_scope: net not found");
    return;
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("wire");

  if (scopeParameter->compatMode) {
    jrt::Array<fr::Point*>* corners = currentWire->polyline()->corners();
    auto* floatCornerArr = new jrt::Array<fr::FloatPoint*>(corners->length);
    for (int32_t i = 0; i < corners->length; i++) {
      (*floatCornerArr)[i] = (*corners)[i]->toFloat();
    }
    jrt::Array<double>* coors = scopeParameter->coordinateTransform->boardToDsn(floatCornerArr);
    auto* currentPath = new PolygonPath(currentLayer, wireWidth, coors);
    currentPath->writeScope(scopeParameter->file, scopeParameter->identifierType);
  } else {
    jrt::Array<double>* coors =
        scopeParameter->coordinateTransform->boardToDsn(currentWire->polyline()->lines);
    auto* currentPath = new PolylinePath(currentLayer, wireWidth, coors);
    currentPath->writeScope(scopeParameter->file, scopeParameter->identifierType);
  }
  writeNet(wireNet, scopeParameter->file, scopeParameter->identifierType);
  Rule::writeItemClearanceClass(
      scopeParameter->board->rules->clearanceMatrix->getName(wire->clearanceClassIndex()),
      scopeParameter->file, scopeParameter->identifierType);
  writeFixedState(scopeParameter->file, currentWire->getFixedState());
  scopeParameter->file->endScope();
}

void Wiring::writeConductionAreaScope(WriteScopeParameter* scopeParameter,
                                      fr::ConductionArea* conductionArea) {
  int32_t netCount = conductionArea->netCount();
  if (netCount != 1) {
    FRLogger::warn("Plane.write_scope: unexpected net count");
    return;
  }
  fr::Net* currentNet = scopeParameter->board->rules->nets->get(conductionArea->getNetNumber(0));
  fr::Area* currentArea = conductionArea->getArea();
  int32_t layerIndex = conductionArea->getLayer();
  fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];
  Layer* conductionLayer = new Layer(boardLayer->name, layerIndex, boardLayer->isSignal);
  fr::Shape* boundaryShape;
  jrt::Array<fr::Shape*>* holes;
  if (auto* shape = jrt::fast_cast<fr::Shape>(currentArea)) {
    boundaryShape = shape;
    holes = new jrt::Array<fr::Shape*>(0);
  } else {
    boundaryShape = currentArea->getBorder();
    holes = currentArea->getHoles();
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("wire ");
  Shape* dsnShape = scopeParameter->coordinateTransform->boardToDsn(boundaryShape, conductionLayer);
  if (dsnShape != nullptr) {
    dsnShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
  }
  for (int32_t i = 0; i < holes->length; i++) {
    Shape* dsnHole = scopeParameter->coordinateTransform->boardToDsn((*holes)[i], conductionLayer);
    dsnHole->writeHoleScope(scopeParameter->file, scopeParameter->identifierType);
  }
  writeNet(currentNet, scopeParameter->file, scopeParameter->identifierType);
  Rule::writeItemClearanceClass(
      scopeParameter->board->rules->clearanceMatrix->getName(conductionArea->clearanceClassIndex()),
      scopeParameter->file, scopeParameter->identifierType);
  scopeParameter->file->endScope();
}

void Wiring::writeNet(fr::Net* net, fr::IndentFileWriter* file,
                      fr::IdentifierType* identifierType) {
  file->newLine();
  file->write("(");
  Net::writeNetId(net, file, identifierType);
  file->write(")");
}

void Wiring::writeFixedState(fr::IndentFileWriter* file, fr::FixedState fixedState) {
  if (fixedState == fr::FixedState::UNFIXED) {
    return;
  }
  file->newLine();
  file->write("(type ");
  if (fixedState == fr::FixedState::SHOVE_FIXED) {
    file->write("shoveFixed)");
  } else if (fr::FixedState_isFix(fixedState)) {
    file->write("fix)");
  } else {
    file->write("protect)");
  }
}

jrt::Collection<fr::Net*>* Wiring::getSubnets(Net_Id* netId, fr::BoardRules* rules) {
  jrt::Collection<fr::Net*>* foundNets = new jrt::LinkedList<fr::Net*>();
  if (netId != nullptr) {
    if (netId->subnetNumber > 0) {
      fr::Net* foundNet = rules->nets->get(netId->name, netId->subnetNumber);
      if (foundNet != nullptr) {
        foundNets->add(foundNet);
      }
    } else {
      foundNets = rules->nets->get(netId->name);
    }
  }
  return foundNets;
}

bool Wiring::viaExists(fr::IntPoint* location, fr::Padstack* padstack,
                       jrt::Array<int32_t>* netNumbers, fr::BasicBoard* board) {
  auto* filter = new fr::ItemSelectionFilter(fr::ItemSelectionFilter_SelectableChoices::VIAS);
  int32_t fromLayer = padstack->fromLayer();
  int32_t toLayer = padstack->toLayer();
  jrt::Collection<fr::Item*>* pickedItems = board->pickItems(location, padstack->fromLayer(), filter);
  for (fr::Item* currentItem : *pickedItems) {
    auto* currentVia = jrt::fast_cast<fr::Via>(currentItem);
    if (currentVia->netsEqual(netNumbers) && currentVia->getCenter()->equals(location) &&
        currentVia->firstLayer() == fromLayer && currentVia->lastLayer() == toLayer) {
      return true;
    }
  }
  return false;
}

fr::FixedState Wiring::calcFixed(IJFlexScanner* scanner) {
  try {
    fr::FixedState result = fr::FixedState::UNFIXED;
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == SHOVE_FIXED()) {
      result = fr::FixedState::SHOVE_FIXED;
    } else if (nextToken == FIX()) {
      // The input's lock, told apart from the temporary lock of a pipeline stage
      // (FixedState.INPUT_FIXED).
      result = fr::FixedState::INPUT_FIXED;
    } else if (nextToken != NORMAL()) {
      result = fr::FixedState::USER_FIXED;
    }
    nextToken = scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Wiring.is_fixed: ) expected at '" + scanner->getScopeIdentifier() + "'");
      return fr::FixedState::UNFIXED;
    }
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Wiring.is_fixed: IO error scanning file", &e);
    return fr::FixedState::UNFIXED;
  }
}

Net_Id* Wiring::readNetId(IJFlexScanner* scanner) {
  try {
    int32_t subnetNumber = 0;

    const std::string netName = scanner->nextString();
    scanner->setScopeIdentifier(netName);

    jrt::Object* nextToken = scanner->nextToken();
    if (auto* integer = jrt::fast_cast<JInteger>(nextToken)) {
      subnetNumber = integer->value;
      nextToken = scanner->nextToken();
    }
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Wiring.read_net_id: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
    }
    return new Net_Id(netName, subnetNumber);
  } catch (const jrt::IOException& e) {
    FRLogger::error("DsnFile.read_string_scope: IO error scanning file", &e);
    return nullptr;
  }
}

bool Wiring::readScope(ReadScopeParameter* scopeParameter) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException&) {
      FRLogger::warn("Wiring.read_scope: IO error scanning file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Wiring.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    bool readOk = true;
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == WIRE()) {
        readWireScope(scopeParameter);
      } else if (nextToken == VIA()) {
        readOk = readViaScope(scopeParameter);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
    if (!readOk) {
      return false;
    }
  }
  fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();
  connectWireEndsInsidePads(board);
  try {
    board->normalizeAllTraces();
  } catch (const jrt::Exception& e) {
    std::string msg = "Wiring: normalization of traces failed";
    FRLogger::debug(msg);
    scopeParameter->warnings->add(msg);
  }
  return true;
}

void Wiring::connectWireEndsInsidePads(fr::BasicBoard* board) {
  auto* wires = new jrt::LinkedList<fr::Trace*>();
  wires->addAll(board->getTraces());
  for (fr::Trace* wire : *wires) {
    connectEndToEnclosingPad(board, wire, wire->firstCorner());
    connectEndToEnclosingPad(board, wire, wire->lastCorner());
  }
}

void Wiring::connectEndToEnclosingPad(fr::BasicBoard* board, fr::Trace* wire, fr::Point* end) {
  fr::DrillItem* pad = enclosingPad(board, wire, end);
  if (pad == nullptr || pad->getCenter()->equals(end) || isConnected(wire, pad)) {
    return;
  }
  auto* stub = new fr::Polyline(end, pad->getCenter());
  int32_t layer = wire->getLayer();
  int32_t halfWidth = wire->getHalfWidth();
  int32_t clearanceClass = wire->clearanceClassIndex();
  if (!board->checkPolylineTrace(stub, layer, halfWidth, wire->netNumbers, clearanceClass)) {
    return;
  }
  board->insertTraceWithoutCleaning(stub, layer, halfWidth, wire->netNumbers, clearanceClass,
                                    wire->getFixedState());
}

bool Wiring::isConnected(fr::Trace* wire, fr::DrillItem* pad) {
  if (wire->netNumbers != nullptr) {
    for (int32_t netNo : *wire->netNumbers) {
      if (pad->containsNet(netNo) && wire->getConnectedSet(netNo)->contains(pad)) {
        return true;
      }
    }
  }
  return false;
}

fr::DrillItem* Wiring::enclosingPad(fr::BasicBoard* board, fr::Trace* wire, fr::Point* end) {
  for (fr::Item* item : *board->pickItems(end, wire->getLayer(), nullptr)) {
    auto* pad = jrt::fast_cast<fr::DrillItem>(item);
    if (pad != nullptr && pad->sharesNet(wire) && pad->containsCopper(end, wire->getLayer())) {
      return pad;
    }
  }
  return nullptr;
}

fr::Item* Wiring::readWireScope(ReadScopeParameter* scopeParameter) {
  Net_Id* netId = nullptr;
  std::optional<std::string> clearanceClassName;
  fr::FixedState fixed = fr::FixedState::UNFIXED;
  Path* path = nullptr;         // Used, if a trace is read.
  Shape* borderShape = nullptr;  // Used, if a conduction area is read.
  jrt::Collection<Shape*>* holeList = new jrt::LinkedList<Shape*>();
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Wiring.read_wire_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Wiring.read_wire_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == POLYGON_PATH()) {
        path = Shape::readPolygonPathScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == POLYLINE_PATH()) {
        path = Shape::readPolylinePathScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == RECTANGLE()) {

        borderShape =
            Shape::readRectangleScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == POLYGON()) {

        borderShape =
            Shape::readPolygonScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == CIRCLE()) {

        borderShape =
            Shape::readCircleScope(scopeParameter->scanner, scopeParameter->layerStructure);
      } else if (nextToken == WINDOW()) {
        Shape* holeShape = Shape::readScope(scopeParameter->scanner, scopeParameter->layerStructure);
        holeList->add(holeShape);
        // overread the closing bracket
        try {
          nextToken = scopeParameter->scanner->nextToken();
        } catch (const jrt::IOException& e) {
          FRLogger::error("Wiring.read_wire_scope: IO error scanning file", &e);
          return nullptr;
        }
        if (nextToken != CLOSED_BRACKET()) {
          FRLogger::warn("Wiring.read_wire_scope: closing bracket expected at '" +
                         scopeParameter->scanner->getScopeIdentifier() + "'");
          return nullptr;
        }
      } else if (nextToken == NET()) {
        netId = readNetId(scopeParameter->scanner);
      } else if (nextToken == CLEARANCE_CLASS()) {
        clearanceClassName = DsnFile::readStringScope(scopeParameter->scanner);
      } else if (nextToken == TYPE()) {
        fixed = calcFixed(scopeParameter->scanner);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
  }
  if (path == nullptr && borderShape == nullptr) {
    std::string msg =
        "Wiring: wire has no shape at '" + scopeParameter->scanner->getScopeIdentifier() + "'";
    FRLogger::warn(msg);
    scopeParameter->warnings->add(msg);
    return nullptr;
  }
  fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();

  fr::NetClass* netClass = board->rules->getDefaultNetClass();
  jrt::Collection<fr::Net*>* foundNets = getSubnets(netId, board->rules);
  auto* netNumbers = new jrt::Array<int32_t>(foundNets->size());
  int32_t currentIndex = 0;
  for (fr::Net* currentNet : *foundNets) {
    (*netNumbers)[currentIndex] = currentNet->netNumber;
    netClass = currentNet->getNetClass();
    ++currentIndex;
  }
  int32_t clearanceClassIndex = -1;
  if (clearanceClassName.has_value()) {
    clearanceClassIndex = board->rules->clearanceMatrix->getNo(*clearanceClassName);
  }
  int32_t layerIndex;
  int32_t halfWidth;
  if (path != nullptr) {
    layerIndex = path->layer->no;
    halfWidth = static_cast<int32_t>(
        jrt::Math::round(scopeParameter->coordinateTransform->dsnToBoard(path->width / 2)));
  } else {
    layerIndex = borderShape->layer->no;
    halfWidth = 0;
  }
  if (layerIndex < 0 || layerIndex >= board->getLayerCount()) {
    std::string layerName = path != nullptr ? path->layer->name : borderShape->layer->name;
    std::string msg = "Wiring: wire ignored — unknown layer '" + layerName + "' at '" +
                      scopeParameter->scanner->getScopeIdentifier() + "'";
    FRLogger::warn(msg);
    scopeParameter->warnings->add(msg);
    return nullptr;
  }

  fr::IntBox* boundingBox = board->getBoundingBox();

  fr::Item* result = nullptr;
  if (borderShape != nullptr) {
    if (clearanceClassIndex < 0) {
      clearanceClassIndex = netClass->defaultItemClearanceClasses->get(
          fr::DefaultItemClearanceClasses_ItemClass::AREA);
    }
    jrt::Collection<Shape*>* area = new jrt::LinkedList<Shape*>();
    area->add(borderShape);
    area->addAll(holeList);
    fr::Area* conductionArea =
        Shape::transformAreaToBoard(area, scopeParameter->coordinateTransform);
    result = board->insertConductionArea(conductionArea, layerIndex, netNumbers,
                                         clearanceClassIndex, false, fixed);
  } else if (jrt::fast_cast<PolygonPath>(path) != nullptr) {
    if (clearanceClassIndex < 0) {
      clearanceClassIndex = netClass->defaultItemClearanceClasses->get(
          fr::DefaultItemClearanceClasses_ItemClass::TRACE);
    }
    auto* corners = new jrt::Array<fr::Point*>(path->coordinateArr->length / 2);
    auto* currentPoint = new jrt::Array<double>(2);
    for (int32_t i = 0; i < corners->length; i++) {
      (*currentPoint)[0] = (*path->coordinateArr)[2 * i];
      (*currentPoint)[1] = (*path->coordinateArr)[2 * i + 1];
      fr::FloatPoint* currentCorner = scopeParameter->coordinateTransform->dsnToBoard(currentPoint);
      if (!boundingBox->contains(currentCorner)) {
        std::string msg = "Wiring: wire corner (" + jrt::str(jrt::d2i((*currentPoint)[0])) + "," +
                          jrt::str(jrt::d2i((*currentPoint)[1])) +
                          ") is outside board bounds at '" +
                          scopeParameter->scanner->getScopeIdentifier() + "'";
        FRLogger::warn(msg);
        scopeParameter->warnings->add(msg);
        return nullptr;
      }
      (*corners)[i] = currentCorner->round();
    }

    auto* polygon = new fr::Polygon(corners);

    // if it doesn't have two different points, it's not a valid polygon, so we must skip it
    jrt::Array<fr::Point*>* polygonCorners = polygon->cornerArray();
    // A wire is degenerate if it has fewer than 2 corners, or if all corners map to the same
    // point (zero-length trace). This covers both the 2-point identical case and the N-point
    // all-equal case (e.g. KiCad 4.0.7 exports 3 identical vertices for some degenerate shapes).
    // Such traces cause infinite normalization cycles and must be skipped.
    bool hasDistinctCorner = false;
    for (int32_t i = 1; i < polygonCorners->length; i++) {
      if (!(*polygonCorners)[i]->equals((*polygonCorners)[0])) {
        hasDistinctCorner = true;
        break;
      }
    }
    bool isDegenerate = polygonCorners->length < 2 || !hasDistinctCorner;
    if (!isDegenerate) {
      auto* tracePolyline = new fr::Polyline(polygon);
      // Traces are not yet normalized here because cycles may be removed premature.
      result = board->insertTraceWithoutCleaning(tracePolyline, layerIndex, halfWidth, netNumbers,
                                                 clearanceClassIndex, fixed);
    } else {
      std::string msg = "Wiring: degenerate wire trace skipped (all " +
                        jrt::str(polygonCorners->length) +
                        " corners are identical — zero-length trace) on layer '" +
                        path->layer->name + "'. This is likely a DSN export issue in your EDA tool.";
      FRLogger::debug(msg);
      scopeParameter->warnings->add(msg);
    }
  } else if (jrt::fast_cast<PolylinePath>(path) != nullptr) {
    if (clearanceClassIndex < 0) {
      clearanceClassIndex = netClass->defaultItemClearanceClasses->get(
          fr::DefaultItemClearanceClasses_ItemClass::TRACE);
    }
    auto* lines = new jrt::Array<fr::Line*>(path->coordinateArr->length / 4);
    auto* currentPoint = new jrt::Array<double>(2);
    for (int32_t i = 0; i < lines->length; i++) {
      (*currentPoint)[0] = (*path->coordinateArr)[4 * i];
      (*currentPoint)[1] = (*path->coordinateArr)[4 * i + 1];
      fr::FloatPoint* currentA = scopeParameter->coordinateTransform->dsnToBoard(currentPoint);
      (*currentPoint)[0] = (*path->coordinateArr)[4 * i + 2];
      (*currentPoint)[1] = (*path->coordinateArr)[4 * i + 3];
      fr::FloatPoint* currentB = scopeParameter->coordinateTransform->dsnToBoard(currentPoint);
      (*lines)[i] = new fr::Line(currentA->round(), currentB->round());
    }
    auto* tracePolyline = new fr::Polyline(lines);
    result = board->insertTraceWithoutCleaning(tracePolyline, layerIndex, halfWidth, netNumbers,
                                               clearanceClassIndex, fixed);
  } else {
    FRLogger::warn("Wiring.read_wire_scope: unexpected Path subclass at '" +
                   scopeParameter->scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  if (result != nullptr && result->netCount() == 0) {
    tryCorrectNet(result);
  }
  return result;
}

void Wiring::tryCorrectNet(fr::Item* item) {
  auto* currentTrace = jrt::fast_cast<fr::Trace>(item);
  if (currentTrace == nullptr) {
    return;
  }
  jrt::Set<fr::Item*>* contacts =
      currentTrace->getNormalContacts(currentTrace->firstCorner(), true);
  contacts->addAll(currentTrace->getNormalContacts(currentTrace->lastCorner(), true));
  int32_t correctedNetNo = 0;
  for (fr::Item* currentContact : *contacts) {
    if (currentContact->netCount() == 1) {
      correctedNetNo = currentContact->getNetNumber(0);
      break;
    }
  }
  if (correctedNetNo != 0) {
    item->assignNetNo(correctedNetNo);
  }
}

bool Wiring::readViaScope(ReadScopeParameter* scopeParameter) {
  try {
    fr::FixedState fixed = fr::FixedState::UNFIXED;
    // read the padstack name
    jrt::Object* nextToken = scopeParameter->scanner->nextToken();
    auto* padstackNameToken = jrt::fast_cast<JString>(nextToken);
    if (padstackNameToken == nullptr) {
      FRLogger::warn("Wiring.read_via_scope: padstack name expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    std::string padstackName = padstackNameToken->value;
    scopeParameter->scanner->setScopeIdentifier(padstackName);
    // read the location
    auto* location = new jrt::Array<double>(2);
    for (int32_t i = 0; i < 2; i++) {
      nextToken = scopeParameter->scanner->nextToken();
      if (auto* double1 = jrt::fast_cast<JDouble>(nextToken)) {
        (*location)[i] = double1->value;
      } else if (auto* integer = jrt::fast_cast<JInteger>(nextToken)) {
        (*location)[i] = integer->value;
      } else {
        FRLogger::warn("Wiring.read_via_scope: number expected at '" +
                       scopeParameter->scanner->getScopeIdentifier() + "'");
        return false;
      }
    }
    Net_Id* netId = nullptr;
    std::optional<std::string> clearanceClassName;
    for (;;) {
      jrt::Object* prevToken = nextToken;
      nextToken = scopeParameter->scanner->nextToken();
      if (nextToken == nullptr) {
        FRLogger::warn("Wiring.read_via_scope: unexpected end of file at '" +
                       scopeParameter->scanner->getScopeIdentifier() + "'");
        return false;
      }
      if (nextToken == CLOSED_BRACKET()) {
        // end of scope
        break;
      }
      if (prevToken == OPEN_BRACKET()) {
        if (nextToken == NET()) {
          netId = readNetId(scopeParameter->scanner);
        } else if (nextToken == CLEARANCE_CLASS()) {
          clearanceClassName = DsnFile::readStringScope(scopeParameter->scanner);
        } else if (nextToken == TYPE()) {
          fixed = calcFixed(scopeParameter->scanner);
        } else {
          skipScope(scopeParameter->scanner);
        }
      }
    }
    fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();
    fr::Padstack* currentPadstack = board->library->padstacks->get(padstackName);
    if (currentPadstack == nullptr) {
      // PORT NOTE: padstackName is never null in C++. String.replaceAll("\\.\\d+", "").
      static const std::regex digitSuffix("\\.[0-9]+");
      currentPadstack =
          board->library->padstacks->get(std::regex_replace(padstackName, digitSuffix, ""));
    }
    if (currentPadstack == nullptr) {
      std::string msg = "Wiring: via padstack '" + padstackName + "' not found at '" +
                        scopeParameter->scanner->getScopeIdentifier() + "'";
      FRLogger::warn(msg);
      scopeParameter->warnings->add(msg);
      return false;
    }
    fr::NetClass* netClass = board->rules->getDefaultNetClass();
    jrt::Collection<fr::Net*>* foundNets = getSubnets(netId, board->rules);
    if (netId != nullptr && foundNets->isEmpty()) {
      std::string msg = "Wiring: via net '" + netId->name + "' not found at '" +
                        scopeParameter->scanner->getScopeIdentifier() + "'";
      FRLogger::warn(msg);
      scopeParameter->warnings->add(msg);
    }
    auto* netNumbers = new jrt::Array<int32_t>(foundNets->size());
    int32_t currentIndex = 0;
    for (fr::Net* currentNet : *foundNets) {
      (*netNumbers)[currentIndex++] = currentNet->netNumber;
      netClass = currentNet->getNetClass();
    }
    int32_t clearanceClassIndex = -1;
    if (clearanceClassName.has_value()) {
      clearanceClassIndex = board->rules->clearanceMatrix->getNo(*clearanceClassName);
    }
    if (clearanceClassIndex < 0) {
      clearanceClassIndex = netClass->defaultItemClearanceClasses->get(
          fr::DefaultItemClearanceClasses_ItemClass::VIA);
    }
    fr::IntPoint* boardLocation = scopeParameter->coordinateTransform->dsnToBoard(location)->round();
    if (viaExists(boardLocation, currentPadstack, netNumbers, board)) {
      std::string msg = "Wiring: duplicate via skipped at (" + jrt::str(boardLocation->x) + ", " +
                        jrt::str(boardLocation->y) + ")";
      FRLogger::warn(msg);
      scopeParameter->warnings->add(msg);
    } else {
      bool attachAllowed = scopeParameter->viaAtSmdAllowed && currentPadstack->attachAllowed;
      board->insertVia(currentPadstack, boardLocation, netNumbers, clearanceClassIndex, fixed,
                       attachAllowed);
      if (currentPadstack->hasAuxiliaryShapes()) {
        std::string msg =
            "Wiring: Via at (" + jrt::str(boardLocation->x) + ", " + jrt::str(boardLocation->y) +
            ") uses non-convex padstack '" + currentPadstack->name +
            "'. Non-convex geometry on vias is not routable and auxiliary companion tiles" +
            " are omitted.";
        FRLogger::warn(msg);
        scopeParameter->warnings->add(msg);
      }
    }
    return true;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Wiring.read_via_scope: IO error scanning file", &e);
    return false;
  }
}

}  // namespace fr::dsn
