#include "io/specctra/parser/DsnFile.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr::dsn {

int32_t DsnFile::convertPlatedSlotKeepouts(fr::BasicBoard* routingBoard) {
  if (routingBoard == nullptr) {
    return 0;
  }
  jrt::List<fr::ObstacleArea*>* slotKeepouts = new jrt::ArrayList<fr::ObstacleArea*>();
  for (fr::Item* item : *routingBoard->getItems()) {
    if (typeid(*item) == typeid(fr::ObstacleArea) && item->getComponentId() <= 0 &&
        isInsideThroughHolePad(static_cast<fr::ObstacleArea*>(item), routingBoard)) {
      slotKeepouts->add(static_cast<fr::ObstacleArea*>(item));
    }
  }
  for (fr::ObstacleArea* keepout : *slotKeepouts) {
    fr::FixedState fixedState = keepout->getFixedState();
    // Keepouts read from the DSN are user fixed, and removeItem skips fixed items.
    keepout->setFixedState(fr::FixedState::UNFIXED);
    routingBoard->removeItem(keepout);
    routingBoard->insertViaObstacle(keepout->getArea(), keepout->getLayer(),
                                    keepout->clearanceClassIndex(), fixedState);
  }
  if (!slotKeepouts->isEmpty()) {
    FRLogger::info("Converted " + jrt::str(slotKeepouts->size()) +
                   " keepout(s) inside through-hole pads (plated slots) into via keepouts.");
  }
  return slotKeepouts->size();
}

int32_t DsnFile::removePinEnclosingKeepouts(fr::BasicBoard* routingBoard) {
  if (routingBoard == nullptr) {
    return 0;
  }
  jrt::List<fr::ObstacleArea*>* pinBlockingKeepouts = new jrt::ArrayList<fr::ObstacleArea*>();
  for (fr::Item* item : *routingBoard->getItems()) {
    if (typeid(*item) == typeid(fr::ObstacleArea) && item->getComponentId() <= 0 &&
        (!static_cast<fr::ObstacleArea*>(item)->name.has_value() ||
         static_cast<fr::ObstacleArea*>(item)->name->empty()) &&
        enclosesRoutableSmdPinCenter(static_cast<fr::ObstacleArea*>(item), routingBoard)) {
      pinBlockingKeepouts->add(static_cast<fr::ObstacleArea*>(item));
    }
  }
  for (fr::ObstacleArea* keepout : *pinBlockingKeepouts) {
    keepout->setFixedState(fr::FixedState::UNFIXED);
    routingBoard->removeItem(keepout);
  }
  if (!pinBlockingKeepouts->isEmpty()) {
    FRLogger::warn("Removed " + jrt::str(pinBlockingKeepouts->size()) +
                   " board keepout(s) enclosing routable SMD pin center(s)" +
                   " (likely KiCad copper-pour-only keepout exported as generic keepout).");
  }
  return pinBlockingKeepouts->size();
}

bool DsnFile::enclosesRoutableSmdPinCenter(fr::ObstacleArea* keepout,
                                           fr::BasicBoard* routingBoard) {
  int32_t layer = keepout->getLayer();
  fr::IntBox* keepoutBox = keepout->boundingBox();
  for (fr::Pin* pin : *routingBoard->getPins()) {
    if (pin->firstLayer() != layer || pin->lastLayer() != layer || pin->netCount() == 0 ||
        pin->hasIgnoredNets()) {
      continue;
    }
    fr::Point* center = pin->getCenter();
    if (center != nullptr && keepoutBox->contains(center) &&
        keepout->getArea()->contains(center)) {
      return true;
    }
  }
  return false;
}

bool DsnFile::contains(fr::IntBox* outer, fr::IntBox* inner) {
  return outer->ll->x <= inner->ll->x && outer->ll->y <= inner->ll->y &&
         outer->ur->x >= inner->ur->x && outer->ur->y >= inner->ur->y;
}

bool DsnFile::isInsideThroughHolePad(fr::ObstacleArea* keepout, fr::BasicBoard* routingBoard) {
  int32_t layer = keepout->getLayer();
  jrt::Array<fr::FloatPoint*>* corners = keepout->getArea()->cornerApproxArr();
  if (corners == nullptr || corners->length == 0) {
    return false;
  }
  for (fr::Pin* pin : *routingBoard->getPins()) {
    if (pin->firstLayer() == pin->lastLayer() || !pin->isOnLayer(layer)) {
      continue;
    }
    fr::Shape* padShape = pin->getShape(layer - pin->firstLayer());
    if (padShape == nullptr || !contains(padShape->boundingBox(), keepout->boundingBox())) {
      continue;
    }
    bool inside = true;
    for (fr::FloatPoint* corner : *corners) {
      if (!padShape->contains(corner)) {
        inside = false;
        break;
      }
    }
    if (inside) {
      return true;
    }
  }
  return false;
}

bool DsnFile::adjustPlaneAutorouteSettings(fr::BasicBoard* routingBoard) {
  if (routingBoard == nullptr) {
    return false;
  }
  fr::LayerStructure* boardLayerStructure = routingBoard->layerStructure;
  if (boardLayerStructure->layers->length < 1) {
    return false;
  }
  for (fr::Layer* currentLayer : *boardLayerStructure->layers) {
    if (!currentLayer->isSignal) {
      return false;
    }
  }
  auto* changedLayerArr = new jrt::Array<bool>(boardLayerStructure->layers->length);
  jrt::Collection<fr::ConductionArea*>* conductionAreaList = routingBoard->getConductionAreas();
  bool nothingChanged = true;

  fr::BoardOutline* boardOutline = routingBoard->getOutline();
  double boardArea = 0;
  if (boardOutline != nullptr) {
    for (int32_t i = 0; i < boardOutline->shapeCount(); i++) {
      jrt::Array<fr::TileShape*>* currentPieceArr = boardOutline->getShape(i)->splitToConvex();
      if (currentPieceArr != nullptr) {
        for (fr::TileShape* currentPiece : *currentPieceArr) {
          boardArea += currentPiece->area();
        }
      }
    }
  }
  if (boardArea <= 0) {
    return false;
  }

  for (fr::ConductionArea* currentConductionArea : *conductionAreaList) {
    int32_t layerIndex = currentConductionArea->getLayer();
    if (layerIndex < 0 || layerIndex >= boardLayerStructure->layers->length) {
      continue;
    }
    fr::Layer* currentLayer = (*routingBoard->layerStructure->layers)[layerIndex];
    if (!currentLayer->isSignal) {
      continue;
    }
    jrt::Array<fr::TileShape*>* convexPieces = currentConductionArea->getArea()->splitToConvex();
    if (convexPieces == nullptr) {
      continue;
    }
    double currentArea = 0;
    for (fr::TileShape* currentPiece : *convexPieces) {
      currentArea += currentPiece->area();
    }
    // Relaxed area threshold: 30% of board area (supports outer-layer pours on 2-layer boards)
    if (currentArea < 0.3 * boardArea) {
      continue;
    }
    for (int32_t i = 0; i < currentConductionArea->netCount(); i++) {
      fr::Net* currentNet =
          routingBoard->rules->nets->get(currentConductionArea->getNetNumber(i));
      if (currentNet != nullptr) {
        currentNet->setContainsPlane(true);
        nothingChanged = false;
      }
    }
    (*changedLayerArr)[layerIndex] = true;
    if (static_cast<int32_t>(currentConductionArea->getFixedState()) <
        static_cast<int32_t>(fr::FixedState::USER_FIXED)) {
      currentConductionArea->setFixedState(fr::FixedState::USER_FIXED);
    }
  }
  for (int32_t i = 0; i < changedLayerArr->length; i++) {
    if ((*changedLayerArr)[i]) {
      FRLogger::info(
          "Layer '" + (*routingBoard->layerStructure->layers)[i]->name +
          "' contains a power plane / large copper pour covering >=30% of the board; " +
          "associated net(s) configured for plane routing.");
    }
  }
  return !nothingChanged;
}

bool DsnFile::readOnOffScope(IJFlexScanner* scanner) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    bool result = false;
    if (nextToken == Keyword::ON()) {
      result = true;
    } else if (nextToken != Keyword::OFF()) {
      FRLogger::warn("DsnFile.read_boolean: Keyword.OFF expected at '" +
                     scanner->getScopeIdentifier() + "'");
    }
    ScopeKeyword::skipScope(scanner);
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("DsnFile.read_boolean: IO error scanning file", &e);
    return false;
  }
}

int32_t DsnFile::readIntegerScope(IJFlexScanner* scanner) {
  try {
    int32_t value;
    jrt::Object* nextToken = scanner->nextToken();
    if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
      value = integer->value;
    } else {
      FRLogger::warn("DsnFile.read_integer_scope: number expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return 0;
    }
    nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("DsnFile.read_integer_scope: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return 0;
    }
    return value;
  } catch (const jrt::IOException& e) {
    FRLogger::error("DsnFile.read_integer_scope: IO error scanning file", &e);
    return 0;
  }
}

double DsnFile::readFloatScope(IJFlexScanner* scanner) {
  try {
    double value;
    jrt::Object* nextToken = scanner->nextToken();
    if (auto* double1 = dynamic_cast<JDouble*>(nextToken)) {
      value = double1->value;
    } else if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
      value = integer->value;
    } else {
      FRLogger::warn("DsnFile.read_float_scope: number expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return 0;
    }
    nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("DsnFile.read_float_scope: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return 0;
    }
    return value;
  } catch (const jrt::IOException& e) {
    FRLogger::error("DsnFile.read_float_scope: IO error scanning file", &e);
    return 0;
  }
}

std::optional<std::string> DsnFile::readStringScope(IJFlexScanner* scanner) {
  try {
    // PORT NOTE: nextString never returns null in the C++ port (Java's never does either).
    std::string result = scanner->nextString(true);
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("DsnFile.read_string_scope: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "', got: " + jrt::str(nextToken));
      while (nextToken != nullptr && nextToken != Keyword::CLOSED_BRACKET()) {
        nextToken = scanner->nextToken();
      }
    }
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("DsnFile.read_string_scope: IO error scanning file", &e);
    return std::nullopt;
  }
}

jrt::Array<std::string>* DsnFile::readStringListScope(IJFlexScanner* scanner) {
  jrt::Array<std::string>* result = scanner->nextStringList();
  if (!scanner->nextClosingBracket()) {
    return nullptr;
  }
  return result;
}

}  // namespace fr::dsn
