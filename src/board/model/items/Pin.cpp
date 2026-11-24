// Port of app.freerouting.board.model.items.Pin.
#include "board/model/items/Pin.h"

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/PadPlaneConnection.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "core/library/BoardLibrary.h"
#include "core/library/LogicalPart.h"
#include "core/library/Package.h"
#include "core/library/PadOval.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/PlacementTransform.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "util/TextManager.h"

namespace fr {

Pin::Pin(int32_t componentId, int32_t pinIndex, jrt::Array<int32_t>* netNumbers,
         int32_t clearanceClassIndex, int32_t id, FixedState fixedState, BasicBoard* board)
    : DrillItem(nullptr, netNumbers, clearanceClassIndex, id, componentId, fixedState, board),
      pinIndex(pinIndex) {}

Vector* Pin::relativeLocation() {
  Component* component = board->components->get(this->getComponentId());
  Package* libPackage = component->getPackage();
  Package_Pin* packagePin = libPackage->getPin(this->pinIndex);
  return component->placementTransform(board->components->getFlipStyleRotateFirst())
      ->point(packagePin->getExactRelativeLocation())
      ->round()
      ->differenceBy(component->getLocation());
}

Point* Pin::getCenter() {
  Point* pinCenter = DrillItem::getCenter();
  if (pinCenter == nullptr) {
    // Calculate the pin center.
    Component* component = board->components->get(this->getComponentId());
    pinCenter = component->getLocation()->translateBy(this->relativeLocation());

    // check that the pin center is inside the pin shape and correct it eventually

    Padstack* padstack = getPadstack();
    int32_t fromLayer = padstack->fromLayer();
    int32_t toLayer = padstack->toLayer();
    Shape* currentShape = nullptr;
    for (int32_t i = 0; i < toLayer - fromLayer + 1; i++) {
      currentShape = this->getShape(i);
      if (currentShape != nullptr) {
        break;
      }
    }
    if (currentShape == nullptr) {
      FRLogger::warn("Pin: At least 1 shape != null expected");
    } else if (!currentShape->containsInside(pinCenter)) {
      pinCenter = currentShape->centreOfGravity()->round();
    }
    this->setCenter(pinCenter);
  }
  return pinCenter;
}

Padstack* Pin::getPadstack() {
  Component* component = board->components->get(getComponentId());
  if (component == nullptr) {
    FRLogger::warn("Pin.get_padstack; component not found");
    return nullptr;
  }
  int32_t padstackId = component->getPackage()->getPin(pinIndex)->padstackId;
  return board->library->padstacks->get(padstackId);
}

Item* Pin::copy(int32_t id) {
  auto* currentNetNumbers = new (jrt::kOp) jrt::Array<int32_t>(this->netCount());
  for (int32_t i = 0; i < currentNetNumbers->length; i++) {
    (*currentNetNumbers)[i] = getNetNumber(i);
  }
  Pin* copiedPin = new (jrt::kBoardData) Pin(getComponentId(), this->pinIndex, currentNetNumbers,
                           clearanceClassIndex(), id, getFixedState(), board);
  copiedPin->netTieClusterId = this->netTieClusterId;
  copiedPin->planeConnection = this->planeConnection;
  return copiedPin;
}

void Pin::assignComponentId(int32_t id) {
  DrillItem::assignComponentId(id);
  setCenter(nullptr);
}

std::optional<std::string> Pin::name() {
  Component* component = board->components->get(this->getComponentId());
  if (component == nullptr) {
    FRLogger::warn("Pin.name: component not found");
    return std::nullopt;
  }
  return component->getPackage()->getPin(pinIndex)->name;
}

std::optional<std::string> Pin::componentName() {
  Component* comp = board != nullptr && board->components != nullptr
                        ? board->components->get(getComponentId())
                        : nullptr;
  return comp != nullptr ? std::optional<std::string>(comp->name) : std::nullopt;
}

Shape* Pin::getShape(int32_t index) {
  Padstack* padstack = getPadstack();
  if (this->precalculatedShapes == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // all shapes have to be calculated  at once, because otherwise calculation
    // of fromLayer and toLayer may not be correct
    int32_t shapeCount = jrt::Math::max(0, padstack->toLayer() - padstack->fromLayer() + 1);
    this->precalculatedShapes = new (jrt::kGeom) jrt::Array<Shape*>(shapeCount);

    Component* component = board->components->get(this->getComponentId());
    if (component == nullptr) {
      FRLogger::warn("Pin.get_shape: component not found");
      return nullptr;
    }
    Package* libPackage = component->getPackage();
    if (libPackage == nullptr) {
      FRLogger::warn("Pin.get_shape: package not found");
      return nullptr;
    }
    Package_Pin* packagePin = libPackage->getPin(this->getPinIndex());
    if (packagePin == nullptr) {
      FRLogger::warn("Pin.get_shape: pinNo out of range");
      return nullptr;
    }
    for (int32_t shapeIndex = 0; shapeIndex < this->precalculatedShapes->length; shapeIndex++) {
      int32_t padstackLayer = getPadstackLayer(shapeIndex);

      ConvexShape* currentShape = padstack->getShape(padstackLayer);
      if (currentShape == nullptr) {
        continue;
      }
      Shape* transformed = transformToBoard(currentShape);
      // Java: (ConvexShape) cast -- ClassCastException if the transformed shape is not convex.
      if (transformed != nullptr && jrt::fast_cast<ConvexShape>(transformed) == nullptr) {
        throw jrt::ClassCastException("Pin.getShape: transformed shape is not a ConvexShape");
      }
      (*this->precalculatedShapes)[shapeIndex] = transformed;
    }
  }
  return (*this->precalculatedShapes)[index];
}

Shape* Pin::transformToBoard(Shape* padstackShape) {
  if (padstackShape == nullptr) {
    return nullptr;
  }
  Component* component = board->components->get(this->getComponentId());
  if (component == nullptr) {
    FRLogger::warn("Pin.transformToBoard: component not found");
    return nullptr;
  }
  Package* libPackage = component->getPackage();
  if (libPackage == nullptr) {
    FRLogger::warn("Pin.transformToBoard: package not found");
    return nullptr;
  }
  Package_Pin* packagePin = libPackage->getPin(this->getPinIndex());
  if (packagePin == nullptr) {
    FRLogger::warn("Pin.transformToBoard: pinNo out of range");
    return nullptr;
  }
  return component->placementTransform(board->components->getFlipStyleRotateFirst())
      ->shape(padstackShape, packagePin->getExactRelativeLocation(), packagePin->rotationInDegree);
}

PadOval* Pin::getPlacedOval(int32_t index) {
  Padstack* padstack = getPadstack();
  if (padstack == nullptr || index < 0 ||
      index >= padstack->toLayer() - padstack->fromLayer() + 1) {
    return nullptr;
  }
  PadOval* oval = padstack->getOval(getPadstackLayer(index));
  if (oval == nullptr) {
    return nullptr;
  }
  Component* component = board->components->get(this->getComponentId());
  Package* libPackage = component != nullptr ? component->getPackage() : nullptr;
  Package_Pin* packagePin =
      libPackage != nullptr ? libPackage->getPin(this->getPinIndex()) : nullptr;
  if (packagePin == nullptr) {
    return nullptr;
  }
  PlacementTransform* placement =
      component->placementTransform(board->components->getFlipStyleRotateFirst());
  FloatPoint* offset = packagePin->getExactRelativeLocation();
  return new (jrt::kOp) PadOval(placement->padPoint(oval->start(), offset, packagePin->rotationInDegree),
                     placement->padPoint(oval->end(), offset, packagePin->rotationInDegree),
                     oval->radius());
}

int32_t Pin::getPadstackLayer(int32_t index) {
  Padstack* padstack = getPadstack();
  Component* component = board->components->get(this->getComponentId());
  int32_t padstackLayer;
  if (component->placedOnFront() || padstack->placedAbsolute) {
    padstackLayer = index + this->firstLayer();
  } else {
    padstackLayer = padstack->boardLayerCount() - index - this->firstLayer() - 1;
  }
  return padstackLayer;
}

jrt::Collection<Pin_TraceExitRestriction*>* Pin::getTraceExitRestrictions(int32_t layer) {
  jrt::Collection<Pin_TraceExitRestriction*>* result =
      new (jrt::kOp) jrt::LinkedList<Pin_TraceExitRestriction*>();
  int32_t padstackLayer = this->getPadstackLayer(layer - this->firstLayer());
  double padXyFactor = 1.5;
  // setting 1.5 to a higher factor may hinder the shove algorithm of the autorouter between
  // the pins of SMD components, because the channels can get blocked by the shoveFixed stubs.

  Component* component = board->components->get(this->getComponentId());
  if (component != nullptr) {
    if (component->getPackage()->pinCount() <= 3) {
      padXyFactor *= 2;  // allow connection to the longer side also for shorter pads.
    }
  }

  jrt::Collection<Direction*>* padstackExitDirections =
      this->getPadstack()->getTraceExitDirections(padstackLayer, padXyFactor);
  if (padstackExitDirections->isEmpty()) {
    return result;
  }

  if (component == nullptr) {
    return result;
  }
  Shape* currentShape = this->getShape(layer - this->firstLayer());
  auto* padShape = jrt::fast_cast<TileShape>(currentShape);
  if (padShape == nullptr) {
    return result;
  }
  Point* pinCenter = this->getCenter();
  FloatPoint* centerApprox = pinCenter->toFloat();

  for (Direction* currentPadstackExitDirection : *padstackExitDirections) {
    Package* libPackage = component->getPackage();
    if (libPackage == nullptr) {
      continue;
    }
    Package_Pin* packagePin = libPackage->getPin(this->pinIndex);
    if (packagePin == nullptr) {
      continue;
    }
    FloatPoint* localDirection = PlacementTransform::rotate(
        currentPadstackExitDirection->getVector()->toFloat(), packagePin->rotationInDegree);
    FloatPoint* worldDirection =
        component->placementTransform(board->components->getFlipStyleRotateFirst())
            ->direction(localDirection);
    Direction* currentExitDirection =
        Direction::getInstanceApprox(jrt::Math::atan2(worldDirection->y, worldDirection->x));
    // calculate the minimum line length from the pin center into currentExitDirection
    int32_t intersectingBorderLineNo =
        padShape->intersectingBorderLineNo(pinCenter, currentExitDirection);
    if (intersectingBorderLineNo < 0) {
      FRLogger::warn("Pin.get_trace_exit_restrictions: border line not found");
      continue;
    }
    Line* currentExitLine = new (jrt::kGeom) Line(pinCenter, currentExitDirection);
    FloatPoint* nearestBorderPoint =
        currentExitLine->intersectionApprox(padShape->borderLine(intersectingBorderLineNo));
    auto* currentExitRestriction = new (jrt::kOp) Pin_TraceExitRestriction(
        currentExitDirection, centerApprox->distance(nearestBorderPoint));
    result->add(currentExitRestriction);
  }
  return result;
}

bool Pin::hasTraceExitRestrictions() {
  for (int32_t i = this->firstLayer(); i <= this->lastLayer(); i++) {
    jrt::Collection<Pin_TraceExitRestriction*>* currentExitRestrictions =
        getTraceExitRestrictions(i);
    if (!currentExitRestrictions->isEmpty()) {
      return true;
    }
  }
  return false;
}

bool Pin::isObstacle(Item* other) {
  if (other == this) {
    return false;
  }
  // Nonblocking pours are filled around foreign copper by the EDA tool. Unlike auxiliary
  // non-convex pad copper, their full area must not obstruct a foreign-net pin.
  if (auto* conductionArea = jrt::fast_cast<ConductionArea>(other)) {
    if (!conductionArea->getIsObstacle()) {
      return false;
    }
  }
  if (auto* obstacleArea = jrt::fast_cast<ObstacleArea>(other)) {
    if (this->sharesNet(obstacleArea)) {
      return false;
    }
    return obstacleArea->netCount() > 0;
  }
  if (!other->sharesNet(this)) {
    if (auto* otherPin = jrt::fast_cast<Pin>(other)) {
      if (this->netTieClusterId > 0 && this->netTieClusterId == otherPin->netTieClusterId) {
        // Pins belonging to the same net-tie cluster on the same component are intentionally
        // stacked or bridged across nets and do not violate clearance against each other.
        return false;
      }
      if (this->getComponentId() > 0 && this->getComponentId() == otherPin->getComponentId() &&
          this->netCount() == 0 && otherPin->netCount() == 0 && isSameLogicalPad(this, otherPin)) {
        // Netless sub-pads of the same logical pad (e.g. composite pads with @1, @2)
        // do not violate clearance against each other.
        return false;
      }
    }
    return true;
  }
  if (jrt::fast_cast<Trace>(other) != nullptr || jrt::fast_cast<Pin>(other) != nullptr) {
    // Items sharing the same electrical net (e.g. traces, composite pads) are electrically
    // connected and do not violate clearance.
    return false;
  }
  // Same-net vias must be allowed to contact SMD pins during fanout.
  return !this->drillAllowed() || jrt::fast_cast<Via>(other) == nullptr;
}

bool Pin::isSameLogicalPad(Pin* pin1, Pin* pin2) {
  if (pin1->board == nullptr || pin1->board->components == nullptr) {
    return false;
  }
  Component* comp = pin1->board->components->get(pin1->getComponentId());
  if (comp == nullptr || comp->getPackage() == nullptr) {
    return false;
  }
  Package* pkg = comp->getPackage();
  if (pin1->pinIndex >= pkg->pinCount() || pin2->pinIndex >= pkg->pinCount()) {
    return false;
  }
  // PORT NOTE: Package_Pin::name is a non-null std::string, Java's null checks are always false.
  std::string name1 = pkg->getPin(pin1->pinIndex)->name;
  std::string name2 = pkg->getPin(pin2->pinIndex)->name;
  std::string base1 = getBasePinName(name1);
  std::string base2 = getBasePinName(name2);
  return !base1.empty() && base1 == base2;
}

std::string Pin::getBasePinName(const std::string& pinName) {
  int32_t atIdx = jrt::indexOf(pinName, "@");
  if (atIdx > 0) {
    return jrt::substring(pinName, 0, atIdx);
  }
  if (atIdx == 0) {
    return "@";
  }
  int32_t hashIdx = jrt::indexOf(pinName, "#");
  if (hashIdx > 0) {
    return jrt::substring(pinName, 0, hashIdx);
  }
  if (hashIdx == 0) {
    return "#";
  }
  // Handle composite sub-pad suffixes with numeric segment, e.g. "pad_1_1", "1_1", "1-1"
  int32_t lastUnderscore = jrt::lastIndexOf(pinName, "_");
  if (lastUnderscore > 0 && lastUnderscore < static_cast<int32_t>(pinName.length()) - 1) {
    std::string suffix = jrt::substring(pinName, lastUnderscore + 1);
    if (isAllDigits(suffix)) {
      return jrt::substring(pinName, 0, lastUnderscore);
    }
  }
  int32_t lastHyphen = jrt::lastIndexOf(pinName, "-");
  if (lastHyphen > 0 && lastHyphen < static_cast<int32_t>(pinName.length()) - 1) {
    std::string suffix = jrt::substring(pinName, lastHyphen + 1);
    if (isAllDigits(suffix)) {
      return jrt::substring(pinName, 0, lastHyphen);
    }
  }
  return pinName;
}

bool Pin::isAllDigits(const std::string& str) {
  // PORT NOTE: Java Character.isDigit also accepts non-ASCII Unicode digits; pin names are ASCII.
  for (size_t i = 0; i < str.length(); i++) {
    if (!jrt::Character::isDigit(static_cast<unsigned char>(str[i]))) {
      return false;
    }
  }
  return true;
}

void Pin::turn90Degree(int32_t factor, IntPoint* pole) {
  this->setCenter(nullptr);
  clearDerivedData();
}

void Pin::rotateApprox(double angleInDegree, FloatPoint* pole) {
  this->setCenter(nullptr);
  this->clearDerivedData();
}

void Pin::changePlacementSide(IntPoint* pole) {
  this->setCenter(nullptr);
  this->clearDerivedData();
}

void Pin::clearDerivedData() {
  DrillItem::clearDerivedData();
  this->precalculatedShapes = nullptr;
}

jrt::Set<Pin*>* Pin::getSwappablePins() {
  jrt::Set<Pin*>* result = new (jrt::kOp) jrt::TreeSet<Pin*>();
  Component* component = this->board->components->get(this->getComponentId());
  if (component == nullptr) {
    return result;
  }
  LogicalPart* logicalPart = component->getLogicalPart();
  if (logicalPart == nullptr) {
    return result;
  }
  LogicalPart_PartPin* thisPartPin = logicalPart->getPin(this->pinIndex);
  if (thisPartPin == nullptr) {
    return result;
  }
  if (thisPartPin->gatePinSwapCode <= 0) {
    return result;
  }
  // look up all part pins with the same gateName and the same gatePinSwapCode
  for (int32_t i = 0; i < logicalPart->pinCount(); i++) {
    if (i == this->pinIndex) {
      continue;
    }
    LogicalPart_PartPin* currentPartPin = logicalPart->getPin(i);
    if (currentPartPin != nullptr &&
        currentPartPin->gatePinSwapCode == thisPartPin->gatePinSwapCode &&
        currentPartPin->gateName == thisPartPin->gateName) {
      Pin* currentSwappeblePin =
          this->board->getPin(this->getComponentId(), currentPartPin->pinIndex);
      if (currentSwappeblePin != nullptr) {
        result->add(currentSwappeblePin);
      } else {
        FRLogger::warn("Pin.get_swappable_pins: swappable pin not found");
      }
    }
  }
  return result;
}

bool Pin::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::PINS);
}

bool Pin::swap(Pin* other) {
  if (this->netCount() > 1 || other->netCount() > 1) {
    FRLogger::warn("Pin.swap not yet implemented for pins belonging to more than 1 net ");
    return false;
  }
  int32_t thisNetNo;
  if (this->netCount() > 0) {
    thisNetNo = this->getNetNumber(0);
  } else {
    thisNetNo = 0;
  }
  int32_t otherNetNo;
  if (other->netCount() > 0) {
    otherNetNo = other->getNetNumber(0);
  } else {
    otherNetNo = 0;
  }
  this->assignNetNo(otherNetNo);
  other->assignNetNo(thisNetNo);
  Pin* tmp = this->changedTo;
  this->changedTo = other->changedTo;
  other->changedTo = tmp;
  return true;
}

bool Pin::isPlacedOnFront() {
  bool result = true;
  Component* component = board->components->get(this->getComponentId());
  if (component != nullptr) {
    result = component->placedOnFront();
  }
  return result;
}

double Pin::getMinWidth(int32_t layer) {
  int32_t padstackLayer = getPadstackLayer(layer - this->firstLayer());
  Shape* padstackShape = this->getPadstack()->getShape(padstackLayer);
  if (padstackShape == nullptr) {
    FRLogger::warn("Pin.get_min_width: padstackShape is null");
    return 0;
  }
  IntBox* padstackBoundingBox = padstackShape->boundingBox();
  if (padstackBoundingBox == nullptr) {
    FRLogger::warn("Pin.get_min_width: padstackBoundingBox is null");
    return 0;
  }
  return padstackBoundingBox->minWidth();
}

int32_t Pin::getTraceNeckdownHalfwidth(int32_t layer) {
  double result = jrt::Math::max(0.5 * this->getMinWidth(layer) - 1, 1.0);
  int32_t minAllowed = (this->board != nullptr && this->board->rules != nullptr)
                           ? this->board->rules->getMinAllowedTraceHalfWidth()
                           : 0;
  return jrt::Math::max(jrt::d2i(result), minAllowed);
}

double Pin::getMaxWidth(int32_t layer) {
  int32_t padstackLayer = getPadstackLayer(layer - this->firstLayer());
  Shape* padstackShape = this->getPadstack()->getShape(padstackLayer);
  if (padstackShape == nullptr) {
    FRLogger::warn("Pin.get_max_width: padstackShape is null");
    return 0;
  }
  IntBox* padstackBoundingBox = padstackShape->boundingBox();
  if (padstackBoundingBox == nullptr) {
    FRLogger::warn("Pin.get_max_width: padstackBoundingBox is null");
    return 0;
  }
  return padstackBoundingBox->maxWidth();
}

void Pin::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("pin") + ": ");
  printer->append(tm->getText("component_2") + " ");
  Component* component = board->components->get(this->getComponentId());
  printer->append(component->name, tm->getText("component_info"), component);
  printer->append(", " + tm->getText("pin_2") + " ");
  printer->append(component->getPackage()->getPin(this->pinIndex)->name);
  printer->append(", " + tm->getText("padstack") + " ");
  Padstack* padstack = this->getPadstack();
  printer->append(padstack->name, tm->getText("padstack_info"), padstack);
  printer->append(" " + tm->getText("at") + " ");
  printer->append(this->getCenter()->toFloat());
  this->printConnectableItemInfo(printer, locale);
  printer->newline();
}

std::string Pin::getHoverInfo(const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  Component* component = board->components->get(this->getComponentId());
  Padstack* padstack = this->getPadstack();
  std::string componentName = component->name;
  std::string pinName = component->getPackage()->getPin(this->pinIndex)->name;
  std::string padstackName = padstack->name;
  std::string connInfo = this->getConnectableItemHoverInfo(locale);

  return tm->getText("pin_hover_info", {componentName, pinName, padstackName, connInfo});
}

Direction* Pin::calcNearestExitRestrictionDirection(Polyline* tracePolyline,
                                                    int32_t traceHalfWidth, int32_t layer) {
  jrt::Collection<Pin_TraceExitRestriction*>* traceExitRestrictions =
      this->getTraceExitRestrictions(layer);
  if (traceExitRestrictions->isEmpty()) {
    return nullptr;
  }
  Shape* pinShape = this->getShape(layer - this->firstLayer());
  if (jrt::fast_cast<TileShape>(pinShape) == nullptr) {
    return nullptr;
  }
  const double edgeToTurnDist = this->board->rules->getPinEdgeToTurnDist();
  if (edgeToTurnDist < 0) {
    return nullptr;
  }
  TileShape* offsetPinShape = jrt::fast_cast<TileShape>(
      jrt::fast_cast<TileShape>(pinShape)->offset(edgeToTurnDist + traceHalfWidth));
  jrt::Array<jrt::Array<int32_t>*>* entries = offsetPinShape->entrancePoints(tracePolyline);
  if (entries->length == 0) {
    return nullptr;
  }
  jrt::Array<int32_t>* latestEntryTuple = (*entries)[entries->length - 1];
  FloatPoint* traceEntryLocationApprox =
      (*tracePolyline->lines)[(*latestEntryTuple)[0]]->intersectionApprox(
          offsetPinShape->borderLine((*latestEntryTuple)[1]));
  // calculate the nearest legal pin exit point to traceEntryLocationApprox
  double minExitCornerDistance = jrt::Double::MAX_VALUE;
  FloatPoint* nearestExitCorner = nullptr;
  Direction* pinExitDirection = nullptr;
  const double tolerance = 1;
  Point* pinCenter = this->getCenter();
  for (Pin_TraceExitRestriction* currentExitRestriction : *traceExitRestrictions) {
    int32_t currentIntersectingBorderLineNo =
        offsetPinShape->intersectingBorderLineNo(pinCenter, currentExitRestriction->direction);
    Line* currentPinExitRay = new (jrt::kGeom) Line(pinCenter, currentExitRestriction->direction);
    FloatPoint* currentExitCorner = currentPinExitRay->intersectionApprox(
        offsetPinShape->borderLine(currentIntersectingBorderLineNo));
    double currentExitCornerDistance = currentExitCorner->distanceSquare(traceEntryLocationApprox);
    bool newNearestCornerFound = false;
    if (currentExitCornerDistance + tolerance < minExitCornerDistance) {
      newNearestCornerFound = true;
    } else if (currentExitCornerDistance < minExitCornerDistance + tolerance) {
      // the distances are near equal, compare to the previous corners of tracePolyline
      for (int32_t i = 1; i < tracePolyline->cornerCount(); i++) {
        FloatPoint* currentTraceCorner = tracePolyline->cornerApprox(i);
        double currentTraceCornerDistance = currentTraceCorner->distanceSquare(currentExitCorner);
        double oldTraceCornerDistance = currentTraceCorner->distanceSquare(nearestExitCorner);
        if (currentTraceCornerDistance + tolerance < oldTraceCornerDistance) {
          newNearestCornerFound = true;
          break;
        } else if (currentTraceCornerDistance > oldTraceCornerDistance + tolerance) {
          break;
        }
      }
    }
    if (newNearestCornerFound) {
      minExitCornerDistance = currentExitCornerDistance;
      pinExitDirection = currentExitRestriction->direction;
      nearestExitCorner = currentExitCorner;
    }
  }
  return pinExitDirection;
}

FloatPoint* Pin::nearestTraceExitCorner(FloatPoint* fromPoint, int32_t traceHalfWidth,
                                        int32_t layer) {
  jrt::Collection<Pin_TraceExitRestriction*>* traceExitRestrictions =
      this->getTraceExitRestrictions(layer);
  if (traceExitRestrictions->isEmpty()) {
    return nullptr;
  }
  Shape* pinShape = this->getShape(layer - this->firstLayer());
  Point* pinCenter = this->getCenter();
  if (jrt::fast_cast<TileShape>(pinShape) == nullptr) {
    return nullptr;
  }
  const double edgeToTurnDist = this->board->rules->getPinEdgeToTurnDist();
  if (edgeToTurnDist < 0) {
    return nullptr;
  }
  TileShape* offsetPinShape = jrt::fast_cast<TileShape>(
      jrt::fast_cast<TileShape>(pinShape)->offset(edgeToTurnDist + traceHalfWidth));

  // calculate the nearest legal pin exit point to traceEntryLocationApprox
  double minExitCornerDistance = jrt::Double::MAX_VALUE;
  FloatPoint* nearestExitCorner = nullptr;
  for (Pin_TraceExitRestriction* currentExitRestriction : *traceExitRestrictions) {
    int32_t currentIntersectingBorderLineNo =
        offsetPinShape->intersectingBorderLineNo(pinCenter, currentExitRestriction->direction);
    Line* currentPinExitRay = new (jrt::kGeom) Line(pinCenter, currentExitRestriction->direction);
    FloatPoint* currentExitCorner = currentPinExitRay->intersectionApprox(
        offsetPinShape->borderLine(currentIntersectingBorderLineNo));
    double currentExitCornerDistance = currentExitCorner->distanceSquare(fromPoint);
    if (currentExitCornerDistance < minExitCornerDistance) {
      minExitCornerDistance = currentExitCornerDistance;
      nearestExitCorner = currentExitCorner;
    }
  }
  return nearestExitCorner;
}

std::string Pin::toString() {
  std::string simpleName;

  simpleName += jrt::toLowerCase(jrt::className(this));

  if (pinIndex > 0) {
    simpleName += " #";
    simpleName += jrt::str(pinIndex);
  }

  if (componentId > 0) {
    simpleName += " of component #";
    simpleName += jrt::str(componentId);
  }

  return simpleName;
}

jrt::Set<Item*>* Pin::getNormalContacts() {
  jrt::Set<Item*>* result = DrillItem::getNormalContacts();
  if (board == nullptr || getComponentId() == 0) {
    return result;
  }
  jrt::DisposeOnExit pins(copperOverlappingPins([this](Pin* o) { return o->sharesNet(this); }));
  for (Pin* other : *pins) {
    result->add(other);
  }
  return result;
}

jrt::List<Pin*>* Pin::copperOverlappingPins(std::function<bool(Pin*)> accept) {
  jrt::List<Pin*>* result = new (jrt::kOp) jrt::LinkedList<Pin*>();
  if (board == nullptr) {
    return result;
  }
  IntBox* box = this->boundingBox();
  // The pin tests come before the exact overlap test of the search tree, which every item under
  // the pin, planes and traces included, would otherwise reach.
  jrt::DisposeOnExit candidates(
      board->searchTreeManager->getDefaultTree()->overlappingObjects(
          box, -1, [this, accept, box](SearchTreeObject* obj) {
            auto* other = jrt::fast_cast<Pin>(obj);
            return other != nullptr && other != this && accept(other) && other->sharesLayer(this) &&
                   box->intersects(other->boundingBox());
          }));

  for (SearchTreeObject* obj : *candidates) {
    Pin* other = jrt::fast_cast<Pin>(obj);
    bool overlaps = false;
    for (int32_t i = 0; i < tileShapeCount() && !overlaps; i++) {
      Shape* mine = getShape(i);
      for (int32_t j = 0; j < other->tileShapeCount() && !overlaps; j++) {
        Shape* theirs = other->getShape(j);
        overlaps = mine != nullptr && theirs != nullptr && shapeLayer(i) == other->shapeLayer(j) &&
                   mine->intersects(theirs);
      }
    }
    if (overlaps) {
      result->add(other);
    }
  }
  return result;
}

jrt::List<Pin*>* Pin::overlappingNettedPins() {
  // PORT NOTE: Collections.emptyList() is immutable in Java; the port returns a new empty list.
  return (board == nullptr || netCount() != 0)
             ? static_cast<jrt::List<Pin*>*>(jrt::Collections::emptyList<Pin*>())
             : copperOverlappingPins([](Pin* other) { return other->netCount() != 0; });
}

}  // namespace fr
