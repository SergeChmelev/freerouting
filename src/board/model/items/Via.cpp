// Port of app.freerouting.board.model.items.Via.
#include "board/model/items/Via.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "autoroute/drill/ExpansionDrill.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/WireObstacleArea.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/Communication.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "drc/ClearanceViolation.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/HoleSpacing.h"
#include "util/TextManager.h"

namespace fr {

Via::Via(Padstack* padstack, Point* center, jrt::Array<int32_t>* netNumbers,
         int32_t clearanceClassIndex, int32_t id, int32_t groupId, FixedState fixedState,
         bool attachAllowed, BasicBoard* board)
    : DrillItem(center, netNumbers, clearanceClassIndex, id, groupId, fixedState, board),
      attachAllowed(attachAllowed) {
  this->padstack = padstack;
}

Item* Via::copy(int32_t id) {
  Via* copy = new (jrt::kBoardData) Via(padstack, getCenter(), netNumbers, clearanceClassIndex(), id,
                      getComponentId(), getFixedState(), attachAllowed, board);
  copy->isEscapeVia = this->isEscapeVia;
  copy->escapeViaSmdLayer = this->escapeViaSmdLayer;
  return copy;
}

jrt::Collection<ClearanceViolation*>* Via::clearanceViolations() {
  jrt::Collection<ClearanceViolation*>* rawViolations = DrillItem::clearanceViolations();
  if (!this->isEscapeVia || this->escapeViaSmdLayer < 0) {
    addHoleToHoleViolations(rawViolations);
    return rawViolations;
  }
  jrt::Collection<ClearanceViolation*>* filteredViolations =
      new (jrt::kOp) jrt::LinkedList<ClearanceViolation*>();
  for (ClearanceViolation* violation : *rawViolations) {
    if (violation->layer == this->escapeViaSmdLayer) {
      Item* other = nullptr;
      if (violation->firstItem == this) {
        other = violation->secondItem;
      } else if (violation->secondItem == this) {
        other = violation->firstItem;
      }
      if (other != nullptr && other->sharesNet(this)) {
        continue;
      }
    }
    filteredViolations->add(violation);
  }
  addHoleToHoleViolations(filteredViolations);
  return filteredViolations;
}

void Via::addHoleToHoleViolations(jrt::Collection<ClearanceViolation*>* violations) {
  if (this->board == nullptr || this->board->rules == nullptr) {
    return;
  }
  int32_t minimum = this->board->rules->getMinHoleToHole();
  if (minimum <= 0 || this->padstack == nullptr || !HoleSpacing::hasHole(this->padstack)) {
    return;
  }
  double toleranceUm = this->board->rules->clearanceToleranceUm;
  double tolerance =
      jrt::Double::isFinite(toleranceUm) && toleranceUm > 0 &&
              this->board->communication != nullptr
          ? toleranceUm * jrt::Math::max(1, this->board->communication->resolution) /
                Unit_scale(1.0, this->board->communication->unit, Unit::UM)
          : 0;
  double radius = this->padstack->getDrillRadius();
  FloatPoint* center = getCenter()->toFloat();
  for (HoleSpacing_TooCloseItem* tooClose : *HoleSpacing::tooClose(
           this->board, center, radius, firstLayer(), lastLayer(), minimum - tolerance, this)) {
    DrillItem* other = tooClose->item;
    double gap = tooClose->gap;
    IntPoint* c = center->round();
    int32_t r = jrt::d2i(jrt::Math::ceil(radius));
    violations->add(new (jrt::kOp) ClearanceViolation(
        this, other, new (jrt::kGeom) IntBox(c->x - r, c->y - r, c->x + r, c->y + r),
        jrt::Math::max(firstLayer(), other->firstLayer()), minimum, jrt::Math::max(0.0, gap),
        true));
  }
}

Shape* Via::getShape(int32_t index) {
  if (padstack == nullptr) {
    FRLogger::warn("Via.get_shape: padstack is null");
    return nullptr;
  }
  if (this->precalculatedShapes == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    int32_t shapeCount = jrt::Math::max(0, padstack->toLayer() - padstack->fromLayer() + 1);
    this->precalculatedShapes = new (jrt::kGeom) jrt::Array<Shape*>(shapeCount);
    for (int32_t i = 0; i < this->precalculatedShapes->length; i++) {
      int32_t padstackLayer = i + this->firstLayer();
      Vector* translateVector = getCenter()->differenceBy(Point::ZERO());
      Shape* currentShape = padstack->getShape(padstackLayer);

      if (currentShape == nullptr) {
        (*this->precalculatedShapes)[i] = nullptr;
      } else {
        (*this->precalculatedShapes)[i] = jrt::fast_cast<Shape>(currentShape->translateBy(translateVector));
      }
    }
  }
  return (*this->precalculatedShapes)[index];
}

bool Via::isRoutable() { return !isUserFixed() && (this->netCount() > 0); }

bool Via::isObstacle(Item* other) {
  if (other == this || jrt::fast_cast<ComponentObstacleArea>(other) != nullptr ||
      jrt::fast_cast<WireObstacleArea>(other) != nullptr) {
    return false;
  }
  if (auto* area = jrt::fast_cast<ConductionArea>(other)) {
    if (!area->getIsObstacle()) {
      return false;
    }
  }
  if (!other->sharesNet(this)) {
    return true;
  }
  if (jrt::fast_cast<Trace>(other) != nullptr || jrt::fast_cast<Via>(other) != nullptr) {
    return false;
  }
  if (auto* pin = jrt::fast_cast<Pin>(other)) {
    return !pin->drillAllowed() ||
           (!this->attachAllowed && !pin->viaMayAttach() && !ownPadAllowedByRules(pin));
  }
  return false;
}

bool Via::ownPadAllowedByRules(Pin* pin) {
  if (this->board == nullptr || this->board->rules == nullptr) {
    return false;
  }
  if (this->board->rules->isViaInPad()) {
    return true;
  }
  return this->board->rules->isViaTouchingPad() && !holeInPad(pin);
}

bool Via::holeInPad(Pin* pin) {
  double drillRadius = this->getPadstack()->getDrillRadius();
  auto* center = jrt::fast_cast<IntPoint>(this->getCenter());
  if (drillRadius <= 0 || center == nullptr) {
    return false;
  }
  Circle* hole = new (jrt::kOp) Circle(center, jrt::d2i(jrt::Math::ceil(drillRadius)));
  int32_t fromLayer = jrt::Math::max(this->firstLayer(), pin->firstLayer());
  int32_t toLayer = jrt::Math::min(this->lastLayer(), pin->lastLayer());
  for (int32_t layer = fromLayer; layer <= toLayer; layer++) {
    Shape* pad = pin->getShapeOnLayer(layer);
    if (pad != nullptr && pad->intersects(hole)) {
      return true;
    }
  }
  return false;
}

bool Via::isTail() {
  jrt::DisposeOnExit<jrt::Collection<Item*>> contactList(this->getNormalContacts());
  if (contactList->size() <= 1) {
    return true;
  }
  jrt::ScopedIter<Item*> it(contactList.get());

  Item* currentContactItem = it->next();
  int32_t firstContactFirstLayer = currentContactItem->firstLayer();
  int32_t firstContactLastLayer = currentContactItem->lastLayer();
  while (it->hasNext()) {
    currentContactItem = it->next();
    if (currentContactItem->firstLayer() != firstContactFirstLayer ||
        currentContactItem->lastLayer() != firstContactLastLayer) {
      return false;
    }
  }
  return true;
}

void Via::changePlacementSide(IntPoint* pole) {
  if (this->board == nullptr) {
    return;
  }
  Padstack* newPadstack = this->board->library->getMirroredViaPadstack(this->padstack);
  if (newPadstack == nullptr) {
    return;
  }
  this->padstack = newPadstack;
  DrillItem::changePlacementSide(pole);
  clearDerivedData();
}

ExpansionDrill* Via::getAutorouteDrillInfo(ShapeSearchTree* autorouteTree) {
  if (this->autorouteDrillInfo == nullptr) {
    ItemAutorouteInfo* viaAutorouteInfo = this->getAutorouteInfo();
    TileShape* currentDrillShape = TileShape::getInstance(this->getCenter());
    this->autorouteDrillInfo = new (jrt::kSearch) ExpansionDrill(currentDrillShape, this->getCenter(),
                                                  this->firstLayer(), this->lastLayer());
    int32_t viaLayerCount = this->lastLayer() - this->firstLayer() + 1;
    for (int32_t i = 0; i < viaLayerCount; i++) {
      (*this->autorouteDrillInfo->roomArr)[i] = viaAutorouteInfo->getExpansionRoom(i, autorouteTree);
    }
  }
  return this->autorouteDrillInfo;
}

void Via::clearDerivedData() {
  DrillItem::clearDerivedData();
  this->precalculatedShapes = nullptr;
  this->autorouteDrillInfo = nullptr;
}

void Via::clearAutorouteInfo() {
  DrillItem::clearAutorouteInfo();
  this->autorouteDrillInfo = nullptr;
}

bool Via::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::VIAS);
}

void Via::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("via"));
  printer->append(" " + tm->getText("at") + " ");
  printer->append(this->getCenter()->toFloat());
  printer->append(", " + tm->getText("padstack"));
  printer->append(padstack->name, tm->getText("padstack_info"), padstack);
  this->printConnectableItemInfo(printer, locale);
  printer->newline();
}

std::string Via::getHoverInfo(const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  std::string fromLayer = (*this->board->layerStructure->layers)[this->firstLayer()]->name;
  std::string toLayer = (*this->board->layerStructure->layers)[this->lastLayer()]->name;
  std::string padstackName = padstack->name;
  std::string connInfo = this->getConnectableItemHoverInfo(locale);

  return tm->getText("via_hover_info", {padstackName, fromLayer, toLayer, connInfo});
}

}  // namespace fr
