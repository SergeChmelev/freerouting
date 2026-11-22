#include "io/specctra/parser/Plane.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/Shape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/Shape.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr::dsn {

void Plane::writeScope(WriteScopeParameter* scopeParameter, fr::ConductionArea* conduction) {
  int32_t netCount = conduction->netCount();
  if (netCount != 1) {
    FRLogger::warn("Plane.write_scope: unexpected net count at '" + jrt::str(conduction->name) +
                   "'");
    return;
  }
  const std::string netName =
      scopeParameter->board->rules->nets->get(conduction->getNetNumber(0))->name;
  fr::Area* currentArea = conduction->getArea();
  int32_t layerIndex = conduction->getLayer();
  fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];
  Layer* planeLayer = new (jrt::kOp) Layer(boardLayer->name, layerIndex, boardLayer->isSignal);
  fr::Shape* boundaryShape;
  jrt::Array<fr::Shape*>* holes;
  if (auto* shape = jrt::fast_cast<fr::Shape>(currentArea)) {
    boundaryShape = shape;
    holes = new (jrt::kOp) jrt::Array<fr::Shape*>(0);
  } else {
    boundaryShape = currentArea->getBorder();
    holes = currentArea->getHoles();
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("plane ");
  scopeParameter->identifierType->write(netName, scopeParameter->file);
  Shape* dsnShape = scopeParameter->coordinateTransform->boardToDsn(boundaryShape, planeLayer);
  if (dsnShape != nullptr) {
    dsnShape->writeScope(scopeParameter->file, scopeParameter->identifierType);
  }
  for (int32_t i = 0; i < holes->length; i++) {
    Shape* dsnHole = scopeParameter->coordinateTransform->boardToDsn((*holes)[i], planeLayer);
    dsnHole->writeHoleScope(scopeParameter->file, scopeParameter->identifierType);
  }
  scopeParameter->file->endScope();
}

bool Plane::readScope(ReadScopeParameter* scopeParameter) {
  // read the net name
  std::string netName;
  bool skipWindowScopes = scopeParameter->hostCad.has_value() &&
                          jrt::equalsIgnoreCase("allegro", *scopeParameter->hostCad);
  // Cadence Allegro cutouts the pins on power planes, which leads to performance problems
  // when dividing a conduction area into convex pieces.
  Shape_ReadAreaScopeResult* conductionArea;
  try {
    jrt::Object* nextToken = scopeParameter->scanner->nextToken();
    auto* nextString = jrt::fast_cast<JString>(nextToken);
    if (nextString == nullptr) {
      FRLogger::warn("Plane.read_scope: String expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    netName = nextString->value;
    scopeParameter->scanner->setScopeIdentifier(netName);
    conductionArea = Shape::readAreaScope(scopeParameter->scanner, scopeParameter->layerStructure,
                                          skipWindowScopes);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Plane.read_scope: IO error scanning file", &e);
    return false;
  }
  auto* planeInfo = new (jrt::kOp) ReadScopeParameter_PlaneInfo(conductionArea, netName);
  scopeParameter->planeList->add(planeInfo);
  return true;
}

}  // namespace fr::dsn
