#include "io/specctra/SesWriter.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Package.h"
#include "core/library/Packages.h"
#include "core/library/Padstack.h"
#include "datastructures/BigIntAux.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/SesNumbers.h"
#include "io/specctra/SesTracePath.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Parser.h"
#include "io/specctra/parser/Resolution.h"
#include "io/specctra/parser/Shape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr::dsn {

void SesWriter::write(fr::BasicBoard* board, std::ostream& out, const std::string& designName) {
  auto* outputFile = new fr::IndentFileWriter(out);
  std::string sessionName = jrt::replace(designName, ".dsn", ".ses");
  jrt::Array<std::string>* reservedChars =
      jrt::Array<std::string>::of({"(", ")", " ", ";", "-", "_", "/", "~", "{", "}"});
  auto* identifierType =
      new fr::IdentifierType(reservedChars, board->communication->specctraParserInfo->stringQuote);
  writeSessionScope(board, identifierType, outputFile, sessionName, designName);
  outputFile->flush();
  if (out.bad()) {
    throw jrt::IOException("SesWriter: error writing the output stream");
  }
}

void SesWriter::writeSessionScope(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                                  fr::IndentFileWriter* file, const std::string& sessionName,
                                  const std::string& designName) {
  double scaleFactor = board->communication->coordinateTransform->dsnToBoard(1.0) /
                       board->communication->resolution;
  auto* coordinateTransform = new fr::io::CoordinateTransform(scaleFactor, 0, 0);
  file->startScope(false);
  file->write("session ");
  identifierType->write(sessionName, file);
  file->newLine();
  file->write("(base_design ");
  identifierType->write(designName, file);
  file->write(")");
  writePlacement(board, identifierType, coordinateTransform, file);
  writeWasIs(board, identifierType, file);
  writeRoutes(board, identifierType, coordinateTransform, file);
  file->endScope();
}

void SesWriter::writePlacement(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                               fr::io::CoordinateTransform* coordinateTransform,
                               fr::IndentFileWriter* file) {
  file->startScope();
  file->write("placement");
  Resolution::writeScope(file, board->communication);

  if (board->library->packages != nullptr) {
    for (int32_t i = 1; i <= board->library->packages->count(); i++) {
      writeComponents(board, identifierType, coordinateTransform, file,
                      board->library->packages->get(i));
    }
  }

  file->endScope();
}

void SesWriter::writeComponents(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                                fr::io::CoordinateTransform* coordinateTransform,
                                fr::IndentFileWriter* file, fr::Package* pkg) {
  jrt::Collection<fr::Item*>* boardItems = board->getItems();
  bool componentFound = false;
  for (int32_t i = 1; i <= board->components->count(); i++) {
    fr::Component* currentComponent = board->components->get(i);
    if (currentComponent->getPackage() == pkg) {
      // check that not all items of the component are deleted
      bool undeletedItemFound = false;
      for (fr::Item* currentItem : *boardItems) {
        if (currentItem->getComponentId() == currentComponent->id) {
          undeletedItemFound = true;
          break;
        }
      }
      if (undeletedItemFound) {
        if (!componentFound) {
          file->startScope();
          file->write("component ");
          identifierType->write(pkg->name, file);
          componentFound = true;
        }
        writeComponent(board, identifierType, coordinateTransform, file, currentComponent);
      }
    }
  }
  if (componentFound) {
    file->endScope();
  }
}

void SesWriter::writeComponent(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                               fr::io::CoordinateTransform* coordinateTransform,
                               fr::IndentFileWriter* file, fr::Component* component) {
  file->newLine();
  file->write("(place ");
  identifierType->write(component->name, file);
  fr::FloatPoint* origin = component->getExactLocation();
  const jrt::BigFraction scale = SesNumbers::sessionScale(board, board->communication->unit,
                                                          board->communication->resolution);
  file->write(" " + SesNumbers::format(SesNumbers::fromDouble(origin->x).divide(scale)));
  file->write(" " + SesNumbers::format(SesNumbers::fromDouble(origin->y).divide(scale)));
  if (component->placedOnFront()) {
    file->write(" front ");
  } else {
    file->write(" back ");
  }
  file->write(formatPlacementRotation(component->getRotationInDegree()));
  if (component->positionFixed && !board->communication->hostCadIsKiCad()) {
    file->newLine();
    file->write(" (lock_type position)");
  }
  file->write(")");
}

void SesWriter::writeWasIs(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                           fr::IndentFileWriter* file) {
  file->startScope();
  file->write("was_is");
  jrt::Collection<fr::Pin*>* boardPins = board->getPins();
  for (fr::Pin* currentPin : *boardPins) {
    fr::Pin* swappedWith = currentPin->getChangedTo();
    if (currentPin->getChangedTo() != currentPin) {
      file->newLine();
      file->write("(pins ");
      fr::Component* currentCmp = board->components->get(currentPin->getComponentId());
      if (currentCmp != nullptr) {
        identifierType->write(currentCmp->name, file);
        file->write("-");
        fr::Package_Pin* packagePin = currentCmp->getPackage()->getPin(currentPin->getPinIndex());
        identifierType->write(packagePin->name, file);
      } else {
        FRLogger::warn("SesWriter.writeWasIs: component not found");
      }
      file->write(" ");
      fr::Component* swapCmp = board->components->get(swappedWith->getComponentId());
      if (swapCmp != nullptr) {
        identifierType->write(swapCmp->name, file);
        file->write("-");
        fr::Package_Pin* packagePin = swapCmp->getPackage()->getPin(swappedWith->getPinIndex());
        identifierType->write(packagePin->name, file);
      } else {
        FRLogger::warn("SesWriter.writeWasIs: component not found");
      }
      file->write(")");
    }
  }
  file->endScope();
}

void SesWriter::writeRoutes(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                            fr::io::CoordinateTransform* coordinateTransform,
                            fr::IndentFileWriter* file) {
  file->startScope();
  file->write("routes ");
  Resolution::writeScope(file, board->communication);
  Parser::writeScope(file, board->communication->specctraParserInfo, identifierType, true);
  writeLibrary(board, identifierType, coordinateTransform, file);
  writeNetwork(board, identifierType, coordinateTransform, file);
  file->endScope();
}

void SesWriter::writeLibrary(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                             fr::io::CoordinateTransform* coordinateTransform,
                             fr::IndentFileWriter* file) {
  file->startScope();
  file->write("library_out ");
  auto* writtenPadstackNames = new jrt::LinkedHashSet<std::string>();
  for (int32_t i = 0; i < board->library->viaPadstackCount(); i++) {
    fr::Padstack* viaPadstack = board->library->getViaPadstack(i);
    if (viaPadstack == nullptr || !writtenPadstackNames->add(viaPadstack->name)) {
      continue;
    }
    writePadstack(viaPadstack, board, identifierType, coordinateTransform, file);
  }
  file->endScope();
}

std::string SesWriter::formatPlacementRotation(double degrees) {
  return jrt::BigDecimal::valueOf(degrees).stripTrailingZeros().toPlainString();
}

void SesWriter::writePadstack(fr::Padstack* padstack, fr::BasicBoard* board,
                              fr::IdentifierType* identifierType,
                              fr::io::CoordinateTransform* coordinateTransform,
                              fr::IndentFileWriter* file) {
  // determine the layer range covered by this padstack
  int32_t firstLayerNo = 0;
  while (firstLayerNo < board->getLayerCount() && padstack->getShape(firstLayerNo) == nullptr) {
    ++firstLayerNo;
  }
  int32_t lastLayerNo = board->getLayerCount() - 1;
  while (lastLayerNo >= 0 && padstack->getShape(lastLayerNo) == nullptr) {
    --lastLayerNo;
  }
  if (firstLayerNo >= board->getLayerCount() || lastLayerNo < 0) {
    FRLogger::warn("SesWriter.writePadstack: padstack shape not found");
    return;
  }

  file->startScope();
  file->write("padstack ");
  identifierType->write(padstack->name, file);
  for (int32_t i = firstLayerNo; i <= lastLayerNo; i++) {
    fr::Shape* currentBoardShape = padstack->getShape(i);
    if (currentBoardShape == nullptr) {
      continue;
    }
    fr::Layer* boardLayer = (*board->layerStructure->layers)[i];
    auto* currentLayer = new Layer(boardLayer->name, i, boardLayer->isSignal);
    Shape* currentShape = coordinateTransform->boardToDsnRel(currentBoardShape, currentLayer);
    file->startScope();
    file->write("shape");
    currentShape->writeScopeInt(file, identifierType);
    file->endScope();
  }
  if (!padstack->attachAllowed) {
    file->newLine();
    file->write("(attach off)");
  }
  file->endScope();
}

void SesWriter::writeNetwork(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                             fr::io::CoordinateTransform* coordinateTransform,
                             fr::IndentFileWriter* file) {
  file->startScope();
  file->write("network_out ");
  for (int32_t i = 1; i <= board->rules->nets->maxNetNumber(); i++) {
    writeNet(i, board, identifierType, coordinateTransform, file);
  }
  file->endScope();
}

void SesWriter::writeNet(int32_t netNumber, fr::BasicBoard* board,
                         fr::IdentifierType* identifierType,
                         fr::io::CoordinateTransform* coordinateTransform,
                         fr::IndentFileWriter* file) {
  jrt::Collection<fr::Item*>* netItems = board->getConnectableItems(netNumber);
  bool headerWritten = false;
  for (fr::Item* currentItem : *netItems) {
    if (fr::FixedState_isFix(currentItem->getFixedState())) {
      continue;
    }
    bool isWire = jrt::fast_cast<fr::PolylineTrace>(currentItem) != nullptr;
    bool isVia = jrt::fast_cast<fr::Via>(currentItem) != nullptr;
    bool isConductionArea =
        jrt::fast_cast<fr::ConductionArea>(currentItem) != nullptr &&
        (*board->layerStructure->layers)[currentItem->firstLayer()]->isSignal;
    if (!headerWritten && (isWire || isVia || isConductionArea)) {
      file->startScope();
      file->write("net ");
      fr::Net* currentNet = board->rules->nets->get(netNumber);
      if (currentNet == nullptr) {
        FRLogger::warn("SesWriter.writeNet: net not found");
      } else {
        identifierType->write(currentNet->name, file);
      }
      headerWritten = true;
    }
    if (isWire) {
      writeWire(jrt::fast_cast<fr::PolylineTrace>(currentItem), board, identifierType,
                coordinateTransform, file);
    } else if (isVia) {
      writeVia(jrt::fast_cast<fr::Via>(currentItem), board, identifierType, coordinateTransform,
               file);
    } else if (isConductionArea) {
      writeConductionArea(jrt::fast_cast<fr::ConductionArea>(currentItem), board, identifierType,
                          coordinateTransform, file);
    }
  }
  if (headerWritten) {
    file->endScope();
  }
}

void SesWriter::writeWire(fr::PolylineTrace* wire, fr::BasicBoard* board,
                          fr::IdentifierType* identifierType,
                          fr::io::CoordinateTransform* coordinateTransform,
                          fr::IndentFileWriter* file) {
  std::string layer = (*board->layerStructure->layers)[wire->getLayer()]->name;
  const jrt::BigFraction scale = SesNumbers::sessionScale(board, board->communication->unit,
                                                          board->communication->resolution);
  jrt::BigFraction width =
      jrt::BigFraction::of(2LL * static_cast<int64_t>(wire->getHalfWidth())).divide(scale);
  SesTracePath* path = SesTracePath::from(wire, coordinateTransform, scale);
  file->startScope();
  file->write("wire");
  path->write(layer, width, identifierType, file);
  writeFixedState(file, wire->getFixedState());
  file->endScope();
}

void SesWriter::writeVia(fr::Via* via, fr::BasicBoard* board, fr::IdentifierType* identifierType,
                         fr::io::CoordinateTransform* coordinateTransform,
                         fr::IndentFileWriter* file) {
  fr::Padstack* viaPadstack = via->getPadstack();
  fr::BigIntAux_RationalCoordinates* center = via->getCenter()->exactCoordinates();
  const jrt::BigFraction scale = SesNumbers::sessionScale(board, board->communication->unit,
                                                          board->communication->resolution);
  file->startScope();
  file->write("via ");
  identifierType->write(viaPadstack->name, file);
  file->write(" ");
  file->write(SesNumbers::format(jrt::BigFraction::of(center->x(), center->z()).divide(scale)));
  file->write(" ");
  file->write(SesNumbers::format(jrt::BigFraction::of(center->y(), center->z()).divide(scale)));
  writeFixedState(file, via->getFixedState());
  file->endScope();
}

void SesWriter::writeFixedState(fr::IndentFileWriter* file, fr::FixedState fixedState) {
  if (static_cast<int32_t>(fixedState) <= static_cast<int32_t>(fr::FixedState::SHOVE_FIXED)) {
    return;
  }
  file->newLine();
  file->write("(type ");
  if (fr::FixedState_isFix(fixedState)) {
    file->write("fix)");
  } else {
    file->write("protect)");
  }
}

void SesWriter::writeConductionArea(fr::ConductionArea* conductionArea, fr::BasicBoard* board,
                                    fr::IdentifierType* identifierType,
                                    fr::io::CoordinateTransform* coordinateTransform,
                                    fr::IndentFileWriter* file) {
  int32_t netCount = conductionArea->netCount();
  if (netCount != 1) {
    FRLogger::warn("SesWriter.writeConductionArea: unexpected net count");
    return;
  }
  fr::Area* currentArea = conductionArea->getArea();
  int32_t layerIndex = conductionArea->getLayer();
  fr::Layer* boardLayer = (*board->layerStructure->layers)[layerIndex];
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
  file->startScope();
  file->write("wire ");
  Shape* dsnShape = coordinateTransform->boardToDsn(boundaryShape, conductionLayer);
  if (dsnShape != nullptr) {
    dsnShape->writeScopeInt(file, identifierType);
  }
  for (int32_t i = 0; i < holes->length; i++) {
    Shape* dsnHole = coordinateTransform->boardToDsn((*holes)[i], conductionLayer);
    dsnHole->writeHoleScope(file, identifierType);
  }
  file->endScope();
}

}  // namespace fr::dsn
