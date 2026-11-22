// Port of app.freerouting.board.model.items.Trace.
#include "board/model/items/Trace.h"

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Pin.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "util/TextManager.h"

namespace fr {

Trace::Trace(int32_t layer, int32_t halfWidth, jrt::Array<int32_t>* netNumbers,
             int32_t clearanceClassIndex, int32_t id, int32_t groupId, FixedState fixedState,
             BasicBoard* board)
    : Item(netNumbers, clearanceClassIndex, id, groupId, fixedState, board) {
  this->halfWidth = halfWidth;
  layer = jrt::Math::max(layer, 0);
  if (board != nullptr) {
    layer = jrt::Math::min(layer, board->getLayerCount() - 1);
  }
  this->layer = layer;
}

int32_t Trace::getCompensatedHalfWidth(ShapeSearchTree* searchTree) {
  return this->halfWidth + searchTree->clearanceCompensationValue(clearanceClassIndex(), this->layer);
}

bool Trace::isObstacle(Item* other) {
  if (other == this || jrt::fast_cast<ViaObstacleArea>(other) != nullptr ||
      jrt::fast_cast<ComponentObstacleArea>(other) != nullptr) {
    return false;
  }
  if (auto* area = jrt::fast_cast<ConductionArea>(other)) {
    if (!area->getIsObstacle()) {
      return false;
    }
  }
  return !other->sharesNet(this);
}

bool Trace::isPreservedRoute() {
  // PORT NOTE: getFixedState() is never null in the port.
  return FixedState_isPreservedRoute(getFixedState());
}

bool Trace::isConnectionPoint(Point* point) {
  if (point == nullptr) {
    return false;
  }
  return point->equals(firstCorner()) || point->equals(lastCorner()) || centreLineContains(point);
}

jrt::List<Point*>* Trace::interiorJunctions() {
  jrt::List<Point*>* result = new jrt::ArrayList<Point*>();
  for (Item* contact : *getNormalContacts()) {
    Point* point = normalContactPoint(contact);
    if (point != nullptr && !point->equals(firstCorner()) && !point->equals(lastCorner()) &&
        !result->contains(point)) {
      result->add(point);
    }
  }
  return result;
}

void Trace::splitAt(jrt::List<Point*>* points) {
  jrt::List<Trace*>* pieces = new jrt::ArrayList<Trace*>();
  pieces->add(this);
  for (Point* point : *points) {
    int32_t index = -1;
    for (int32_t i = 0; i < pieces->size(); i++) {
      Trace* piece = pieces->get(i);
      if (piece->isOnTheBoard() && !point->equals(piece->firstCorner()) &&
          !point->equals(piece->lastCorner()) && piece->centreLineContains(point)) {
        index = i;
        break;
      }
    }
    if (index < 0) {
      continue;
    }
    Trace* target = pieces->get(index);
    jrt::Array<Trace*>* split = target->split(point);
    if (split == nullptr || split->length == 0) {
      continue;
    }
    pieces->removeAt(index);
    for (Trace* piece : *split) {
      pieces->add(piece);
    }
  }
}

Point* Trace::normalContactPoint(Item* other) { return other->normalContactPoint(this); }

Point* Trace::normalContactPoint(DrillItem* drillItem) {
  return drillItem->normalContactPoint(this);
}

Point* Trace::normalContactPoint(Trace* other) {
  if (this->layer != other->layer) {
    return nullptr;
  }
  Point* p1 = this->firstCorner();
  Point* p2 = this->lastCorner();
  Point* op1 = other->firstCorner();
  Point* op2 = other->lastCorner();

  bool thisFirstOnOther = p1 != nullptr && other->isConnectionPoint(p1);
  bool thisLastOnOther = p2 != nullptr && other->isConnectionPoint(p2);
  bool otherFirstOnThis = op1 != nullptr && this->isConnectionPoint(op1);
  bool otherLastOnThis = op2 != nullptr && this->isConnectionPoint(op2);

  Point* uniquePoint = nullptr;
  int32_t count = 0;

  if (thisFirstOnOther) {
    uniquePoint = p1;
    count++;
  }
  if (thisLastOnOther) {
    if (uniquePoint == nullptr || !uniquePoint->equals(p2)) {
      uniquePoint = p2;
      count++;
    }
  }
  if (otherFirstOnThis) {
    if (uniquePoint == nullptr || !uniquePoint->equals(op1)) {
      uniquePoint = op1;
      count++;
    }
  }
  if (otherLastOnThis) {
    if (uniquePoint == nullptr || !uniquePoint->equals(op2)) {
      uniquePoint = op2;
      count++;
    }
  }

  if (count == 1) {
    return uniquePoint;
  }
  return nullptr;
}

jrt::Set<Item*>* Trace::getNormalContacts() {
  jrt::Set<Item*>* result = new jrt::TreeSet<Item*>();
  Point* startCorner = this->firstCorner();
  if (startCorner != nullptr) {
    result->addAll(getNormalContacts(startCorner, false));
  }
  Point* endCorner = this->lastCorner();
  if (endCorner != nullptr) {
    result->addAll(getNormalContacts(endCorner, false));
  }

  // Query same-net items in the trace bounds to capture branches meeting
  // this trace at internal bends or along the centerline (e.g. preserved T-junctions)
  if (board != nullptr) {
    TileShape* contactBounds;
    if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(this)) {
      contactBounds = polylineTrace->polyline()
                          ->boundingOctagon(0, polylineTrace->cornerCount() - 1)
                          ->offset(this->getHalfWidth());
    } else {
      contactBounds = this->boundingBox();
    }
    jrt::Set<SearchTreeObject*>* overlaps = board->overlappingObjects(contactBounds, this->layer);
    for (SearchTreeObject* obj : *overlaps) {
      auto* item = jrt::fast_cast<Item>(obj);
      if (item == nullptr || item == this || !item->sharesNet(this) || !item->sharesLayer(this)) {
        continue;
      }
      if (auto* otherTrace = jrt::fast_cast<Trace>(item)) {
        Point* p1 = otherTrace->firstCorner();
        Point* p2 = otherTrace->lastCorner();
        if ((p1 != nullptr && this->centreLineContains(p1)) ||
            (p2 != nullptr && this->centreLineContains(p2))) {
          result->add(item);
        }
      } else if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
        Point* center = drillItem->getCenter();
        if (center != nullptr && this->centreLineContains(center)) {
          result->add(item);
        }
      }
    }
  }
  return result;
}

jrt::Set<Item*>* Trace::getNormalContacts(Point* point, bool ignoreNet) {
  if (point == nullptr ||
      !(point->equals(this->firstCorner()) || point->equals(this->lastCorner()))) {
    return new jrt::TreeSet<Item*>();
  }
  TileShape* searchShape = TileShape::getInstance(point);
  jrt::Set<SearchTreeObject*>* overlaps = board->overlappingObjects(searchShape, this->layer);
  jrt::Set<Item*>* result = new jrt::TreeSet<Item*>();
  for (SearchTreeObject* currentObject : *overlaps) {
    auto* currentItem = jrt::fast_cast<Item>(currentObject);
    if (currentItem == nullptr) {
      continue;
    }
    if (currentItem != this && currentItem->sharesLayer(this) &&
        (ignoreNet || currentItem->sharesNet(this))) {
      if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
        if (currentTrace->isConnectionPoint(point)) {
          result->add(currentItem);
        }
      } else if (auto* currentDrillItem = jrt::fast_cast<DrillItem>(currentItem)) {
        if (point->equals(currentDrillItem->getCenter())) {
          result->add(currentItem);
        }
      } else if (auto* currentArea = jrt::fast_cast<ConductionArea>(currentItem)) {
        if (currentArea->getArea()->contains(point)) {
          result->add(currentItem);
        }
      }
    }
  }
  return result;
}

bool Trace::isRoutable() { return !isUserFixed() && (this->netCount() > 0); }

bool Trace::isTail() { return !hasContactAt(true) || !hasContactAt(false); }

bool Trace::hasContactAt(bool first) {
  for (Item* contact : *(first ? getStartContacts() : getEndContacts())) {
    auto* area = jrt::fast_cast<ConductionArea>(contact);
    if (area == nullptr || area->touchesEndOf(this, first)) {
      return true;
    }
  }
  return false;
}

bool Trace::hasInteriorContacts() {
  for (Item* contact : *getNormalContacts()) {
    if (auto* trace = jrt::fast_cast<Trace>(contact)) {
      if (isInteriorPoint(trace->firstCorner()) || isInteriorPoint(trace->lastCorner())) {
        return true;
      }
    } else if (auto* drill = jrt::fast_cast<DrillItem>(contact)) {
      if (isInteriorPoint(drill->getCenter())) {
        return true;
      }
    }
  }
  return false;
}

bool Trace::isInteriorPoint(Point* point) {
  return point != nullptr && !point->equals(firstCorner()) && !point->equals(lastCorner()) &&
         contains(point);
}

bool Trace::isDrillable(int32_t netNumber) {
  return !isPlaneEscapeObstacle() && this->containsNet(netNumber);
}

bool Trace::isOverlap() {
  jrt::Set<Item*>* startContacts = this->getStartContacts();
  for (Item* contact : *this->getEndContacts()) {
    // a fractured plane under both ends joins them only if they touch a common island
    auto* area = jrt::fast_cast<ConductionArea>(contact);
    if (startContacts->contains(contact) && !(area != nullptr && !area->joinsEndsOf(this))) {
      return true;
    }
  }
  return false;
}

bool Trace::isShoveFixed() {
  if (Item::isShoveFixed()) {
    return true;
  }

  // check, if the trace belongs to a net, which is not shovable.
  Nets* nets = this->board->rules->nets;
  for (int32_t currentNetNumber : *this->netNumbers) {
    if (Nets::isNormalNetNumber(currentNetNumber)) {
      if (nets->get(currentNetNumber)->getNetClass()->isShoveFixed()) {
        return true;
      }
    }
  }
  return false;
}

Point* Trace::nearestEndPoint(Point* fromPoint) {
  Point* p1 = firstCorner();
  Point* p2 = lastCorner();
  FloatPoint* fromPointFloat = fromPoint->toFloat();
  double d1 = fromPointFloat->distance(p1->toFloat());
  double d2 = fromPointFloat->distance(p2->toFloat());
  Point* result;
  if (d1 < d2) {
    result = p1;
  } else {
    result = p2;
  }
  return result;
}

namespace {
std::string joinIds(jrt::Collection<Item*>* items) {
  std::string s;
  bool first = true;
  for (Item* i : *items) {
    if (!first) s += ",";
    s += jrt::str(i->getId()) + "";
    first = false;
  }
  return s;
}
}  // namespace

bool Trace::isCycle() {
  bool debugNet49 = this->netNumbers != nullptr && this->netNumbers->length > 0 &&
                    (*this->netNumbers)[0] == 49;
  if (this->isOverlap()) {
    if (debugNet49) {
      FRLogger::trace("compare_trace_is_cycle_overlap net=49, id=" + jrt::str(this->getId()) +
                      ", first=" + jrt::str(this->firstCorner()) +
                      ", last=" + jrt::str(this->lastCorner()) +
                      ", startContacts=" + joinIds(this->getStartContacts()) +
                      ", endContacts=" + joinIds(this->getEndContacts()));
    }
    return true;
  }
  jrt::Collection<Item*>* startContacts = this->getStartContacts();
  // a cycle exists if through expanding the start contact we reach
  // this trace again via an end contact
  // make sure, that all direct neighbours are
  // expanded from here, to block coming back to
  // this trace via a start contact.
  jrt::Set<Item*>* visitedItems = new jrt::TreeSet<Item*>(startContacts);
  bool ignoreAreas = false;
  if (this->netNumbers->length > 0) {
    Net* currentNet = this->board->rules->nets->get((*this->netNumbers)[0]);
    if (currentNet != nullptr && currentNet->getNetClass() != nullptr) {
      ignoreAreas = currentNet->getNetClass()->getIgnoreCyclesWithAreas();
    }
  }
  for (Item* currentContact : *startContacts) {
    if (currentContact->isCycleRecu(visitedItems, this, this, ignoreAreas)) {
      if (debugNet49) {
        FRLogger::trace("compare_trace_is_cycle_dfs net=49, id=" + jrt::str(this->getId()) +
                        ", first=" + jrt::str(this->firstCorner()) +
                        ", last=" + jrt::str(this->lastCorner()) +
                        ", startContacts=" + joinIds(startContacts) +
                        ", found_via=" + jrt::str(currentContact->getId()));
      }
      return true;
    }
  }
  return false;
}

jrt::Array<Point*>* Trace::getRatsnestCorners() {
  // Use only uncontacted endpoints of the trace.
  // Otherwise, the allocated memory in the calculation of the incompletes might
  // become very big.
  int32_t stubCount = 0;
  bool stubAtStart = false;
  bool stubAtEnd = false;
  if (getStartContacts()->isEmpty()) {
    ++stubCount;
    stubAtStart = true;
  }
  if (getEndContacts()->isEmpty()) {
    ++stubCount;
    stubAtEnd = true;
  }
  auto* result = new jrt::Array<Point*>(stubCount);
  int32_t stubNo = 0;
  if (stubAtStart) {
    (*result)[stubNo] = firstCorner();
    ++stubNo;
  }
  if (stubAtEnd) {
    (*result)[stubNo] = lastCorner();
  }
  for (int32_t i = 0; i < result->length; i++) {
    if ((*result)[i] == nullptr) {
      return new jrt::Array<Point*>(0);  // Trace is inconsistent
    }
  }
  return result;
}

bool Trace::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::TRACES);
}

jrt::Set<Pin*>* Trace::touchingPinsAtEndCorners() {
  jrt::Set<Pin*>* result = new jrt::TreeSet<Pin*>();
  if (this->board == nullptr) {
    return result;
  }
  Point* currentEndPoint = this->firstCorner();
  for (int32_t i = 0; i < 2; i++) {
    IntOctagon* currentOct = currentEndPoint->surroundingOctagon();
    currentOct = currentOct->enlarge(
        getCompensatedHalfWidth(this->board->searchTreeManager->getDefaultTree()));
    jrt::Set<Item*>* currentOverlaps = this->board->overlappingItemsWithClearance(
        currentOct, this->layer, new jrt::Array<int32_t>(0), this->clearanceClassIndex());
    for (Item* currentItem : *currentOverlaps) {
      auto* pin = jrt::fast_cast<Pin>(currentItem);
      if (pin != nullptr && currentItem->sharesNet(this)) {
        result->add(pin);
      }
    }
    currentEndPoint = this->lastCorner();
  }
  return result;
}

void Trace::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("trace"));
  printer->append(" " + tm->getText("from") + " ");
  printer->append(this->firstCorner()->toFloat());
  printer->append(" " + tm->getText("to") + " ");
  printer->append(this->lastCorner()->toFloat());
  printer->append(" " + tm->getText("on_layer") + " ");
  printer->append((*this->board->layerStructure->layers)[this->layer]->name);
  printer->append(", " + tm->getText("width") + " ");
  printer->append(static_cast<double>(2 * this->halfWidth));
  printer->append(", " + tm->getText("length") + " ");
  printer->append(this->getLength());
  this->printConnectableItemInfo(printer, locale);
  printer->newline();
}

std::string Trace::getHoverInfo(const std::string& locale) {
  TextManager* tm = new TextManager(jrt::className(this), locale);

  double mmResolution = this->board->communication->getResolution(Unit::MM);
  double widthInMm = (2 * this->halfWidth) / mmResolution;
  double lengthInMm = this->getLength() / mmResolution;

  std::string layerName = (*this->board->layerStructure->layers)[this->layer]->name;
  // PORT NOTE: String.format(locale, ...) -> root-locale formatting.
  std::string widthStr = jrt::format("%.4f", widthInMm);
  std::string lengthStr = jrt::format("%.4f", lengthInMm);
  std::string connInfo = this->getConnectableItemHoverInfo(locale);

  return tm->getText("trace_hover_info", {layerName, widthStr, lengthStr, connInfo});
}

bool Trace::validate() {
  bool result = Item::validate();

  if (this->firstCorner()->equals(this->lastCorner())) {
    FRLogger::warn("Trace.validate: first and last corner are equal");
    result = false;
  }
  return result;
}

}  // namespace fr
