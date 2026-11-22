// Port of app.freerouting.core.scoring.BoardStatisticsBoundsCalculator.
#include "core/scoring/BoardStatisticsBoundsCalculator.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/library/Padstack.h"
#include "core/scoring/BoardStatisticsBounds.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"

namespace fr {

// ---- records -------------------------------------------------------------------------------

bool BoardStatisticsBoundsCalculator_CachedBounds::equals(jrt::Object* o) {
  auto* other = dynamic_cast<BoardStatisticsBoundsCalculator_CachedBounds*>(o);
  if (other == nullptr) return false;
  return angleRestriction_ == other->angleRestriction_ &&
         jrt::equalsOf<BoardStatisticsBounds*>(bounds_, other->bounds_);
}

int32_t BoardStatisticsBoundsCalculator_CachedBounds::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, static_cast<int32_t>(angleRestriction_));
  h = jrt::hashCombine(h, jrt::hashOf<BoardStatisticsBounds*>(bounds_));
  return h;
}

std::string BoardStatisticsBoundsCalculator_CachedBounds::toString() {
  return "CachedBounds[angleRestriction=" + AngleRestriction_toString(angleRestriction_) +
         ", bounds=" + jrt::str(static_cast<jrt::Object*>(bounds_)) + "]";
}

bool BoardStatisticsBoundsCalculator_Terminal::equals(jrt::Object* o) {
  auto* other = dynamic_cast<BoardStatisticsBoundsCalculator_Terminal*>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(x_, other->x_) && jrt::equalsOf(y_, other->y_) &&
         jrt::equalsOf<jrt::Set<int32_t>*>(signalLayers_, other->signalLayers_) &&
         jrt::equalsOf<Area*>(area_, other->area_);
}

int32_t BoardStatisticsBoundsCalculator_Terminal::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(x_));
  h = jrt::hashCombine(h, jrt::hashOf(y_));
  h = jrt::hashCombine(h, jrt::hashOf<jrt::Set<int32_t>*>(signalLayers_));
  h = jrt::hashCombine(h, jrt::hashOf<Area*>(area_));
  return h;
}

std::string BoardStatisticsBoundsCalculator_Terminal::toString() {
  return "Terminal[x=" + jrt::str(x_) + ", y=" + jrt::str(y_) +
         ", signalLayers=" + jrt::str(static_cast<jrt::Object*>(signalLayers_)) +
         ", area=" + jrt::str(static_cast<jrt::Object*>(area_)) + "]";
}

// ---- LayerGroups ---------------------------------------------------------------------------

void BoardStatisticsBoundsCalculator_LayerGroups::addLayers(jrt::Collection<int32_t>* layers) {
  std::optional<int32_t> firstLayer;
  for (int32_t layer : *layers) {
    parent->putIfAbsent(layer, layer);
    if (!firstLayer.has_value()) {
      firstLayer = layer;
    } else {
      union_(*firstLayer, layer);
    }
  }
}

int32_t BoardStatisticsBoundsCalculator_LayerGroups::count() { return roots()->size(); }

jrt::Set<int32_t>* BoardStatisticsBoundsCalculator_LayerGroups::roots() {
  jrt::Set<int32_t>* roots = new jrt::HashSet<int32_t>();
  for (int32_t layer : *parent->keySet()) {
    roots->add(find(layer));
  }
  return roots;
}

jrt::Set<int32_t>* BoardStatisticsBoundsCalculator_LayerGroups::groupsCoveredBy(
    BoardStatisticsBoundsCalculator_ViaSpan* span) {
  jrt::Set<int32_t>* result = new jrt::HashSet<int32_t>();
  for (int32_t layer : *parent->keySet()) {
    if (layer >= span->firstLayer() && layer <= span->lastLayer()) {
      result->add(find(layer));
    }
  }
  return result;
}

int32_t BoardStatisticsBoundsCalculator_LayerGroups::find(int32_t layer) {
  int32_t currentParent = parent->get(layer);
  if (currentParent != layer) {
    currentParent = find(currentParent);
    parent->put(layer, currentParent);
  }
  return currentParent;
}

void BoardStatisticsBoundsCalculator_LayerGroups::union_(int32_t firstLayer, int32_t secondLayer) {
  int32_t firstRoot = find(firstLayer);
  int32_t secondRoot = find(secondLayer);
  if (firstRoot != secondRoot) {
    parent->put(secondRoot, firstRoot);
  }
}

// ---- BoardStatisticsBoundsCalculator -------------------------------------------------------

jrt::Map<BasicBoard*, BoardStatisticsBoundsCalculator_CachedBounds*>*
BoardStatisticsBoundsCalculator::CACHE() {
  static jrt::Map<BasicBoard*, BoardStatisticsBoundsCalculator_CachedBounds*>* v =
      new jrt::HashMap<BasicBoard*, BoardStatisticsBoundsCalculator_CachedBounds*>();
  return v;
}

BoardStatisticsBounds* BoardStatisticsBoundsCalculator::calculate(BasicBoard* board) {
  AngleRestriction angleRestriction = board->rules->getTraceAngleRestriction();
  BoardStatisticsBoundsCalculator_CachedBounds* cached = CACHE()->get(board);
  if (cached != nullptr && cached->angleRestriction() == angleRestriction) {
    return cached->bounds();
  }
  BoardStatisticsBounds* result = calculateUncached(board, angleRestriction);
  CACHE()->put(board, new BoardStatisticsBoundsCalculator_CachedBounds(angleRestriction, result));
  return result;
}

BoardStatisticsBounds* BoardStatisticsBoundsCalculator::calculateUncached(
    BasicBoard* board, AngleRestriction angleRestriction) {
  BoardStatisticsBounds* result = new BoardStatisticsBounds();
  double minTraceLength = 0.0;
  int32_t minViaCount = 0;
  int32_t minBendCount = 0;
  double boardUnitToMmFactor =
      Unit_scale(1.0, board->communication->unit, Unit::MM) /
      (board->communication->resolution > 0 ? board->communication->resolution : 1);

  for (int32_t netNumber = 1; netNumber <= board->rules->nets->maxNetNumber(); netNumber++) {
    Net* net = board->rules->nets->get(netNumber);
    if (net == nullptr) {
      continue;
    }

    jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* terminals = getTerminals(board, net);
    if (terminals->size() < 2) {
      continue;
    }

    BoardStatisticsBoundsCalculator_MstResult* mst = calculateMst(terminals, angleRestriction);
    minTraceLength += mst->length() * boardUnitToMmFactor;
    minBendCount += mst->bendCount();
    minViaCount += calculateMinimumViaCount(net, terminals);
  }

  result->minTraceLengthMm = static_cast<float>(minTraceLength);
  result->minViaCount = minViaCount;
  result->minBendCount = minBendCount;
  return result;
}

jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* BoardStatisticsBoundsCalculator::getTerminals(
    BasicBoard* board, Net* net) {
  jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* result =
      new jrt::ArrayList<BoardStatisticsBoundsCalculator_Terminal*>();
  for (Item* item : *net->getTerminalItems()) {
    BoardStatisticsBoundsCalculator_Terminal* terminal = toTerminal(board, item);
    if (terminal != nullptr && !terminal->signalLayers()->isEmpty()) {
      result->add(terminal);
    }
  }
  return result;
}

BoardStatisticsBoundsCalculator_Terminal* BoardStatisticsBoundsCalculator::toTerminal(
    BasicBoard* board, Item* item) {
  double x;
  double y;
  jrt::Set<int32_t>* signalLayers = new jrt::HashSet<int32_t>();

  if (auto* pin = dynamic_cast<Pin*>(item)) {
    FloatPoint* center = pin->getCenter()->toFloat();
    x = center->x;
    y = center->y;
    for (int32_t layer = pin->firstLayer(); layer <= pin->lastLayer(); layer++) {
      if (pin->getShape(layer - pin->firstLayer()) != nullptr) {
        addSignalLayer(board, layer, signalLayers);
      }
    }
  } else if (auto* area = dynamic_cast<ConductionArea*>(item)) {
    FloatPoint* center = area->getArea()->getBorder()->centreOfGravity();
    x = center->x;
    y = center->y;
    addSignalLayer(board, area->getLayer(), signalLayers);
    return new BoardStatisticsBoundsCalculator_Terminal(x, y, signalLayers, area->getArea());
  } else {
    return nullptr;
  }

  return new BoardStatisticsBoundsCalculator_Terminal(x, y, signalLayers, nullptr);
}

void BoardStatisticsBoundsCalculator::addSignalLayer(BasicBoard* board, int32_t layer,
                                                     jrt::Set<int32_t>* signalLayers) {
  if (layer >= 0 && layer < board->layerStructure->layers->length &&
      (*board->layerStructure->layers)[layer]->isSignal) {
    signalLayers->add(layer);
  }
}

BoardStatisticsBoundsCalculator_MstResult* BoardStatisticsBoundsCalculator::calculateMst(
    jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* terminals,
    AngleRestriction angleRestriction) {
  jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* pins =
      new jrt::ArrayList<BoardStatisticsBoundsCalculator_Terminal*>();
  jrt::List<Area*>* areas = new jrt::ArrayList<Area*>();
  for (BoardStatisticsBoundsCalculator_Terminal* terminal : *terminals) {
    if (terminal->area() == nullptr) {
      pins->add(terminal);
    } else {
      areas->add(terminal->area());
    }
  }
  // Node indexes: 0 .. pins.size() - 1 are the pins, pins.size() is the plane, if any.
  int32_t nodeCount = pins->size() + (areas->isEmpty() ? 0 : 1);
  jrt::Array<bool>* used = new jrt::Array<bool>(nodeCount);
  jrt::Array<double>* distances = new jrt::Array<double>(nodeCount);
  jrt::Array<int32_t>* parents = new jrt::Array<int32_t>(nodeCount);
  jrt::Arrays::fill(distances, jrt::Double::POSITIVE_INFINITY);
  jrt::Arrays::fill(parents, -1);
  (*distances)[0] = 0.0;

  double length = 0.0;
  int32_t bendCount = 0;
  for (int32_t edge = 0; edge < nodeCount; edge++) {
    int32_t current = -1;
    for (int32_t index = 0; index < nodeCount; index++) {
      if (!(*used)[index] && (current < 0 || (*distances)[index] < (*distances)[current])) {
        current = index;
      }
    }
    if (current < 0) {
      break;
    }

    (*used)[current] = true;
    if ((*parents)[current] >= 0) {
      length += (*distances)[current];
      if (current < pins->size() && (*parents)[current] < pins->size()) {
        BoardStatisticsBoundsCalculator_Terminal* from = pins->get((*parents)[current]);
        BoardStatisticsBoundsCalculator_Terminal* to = pins->get(current);
        if (from->x() != to->x() && from->y() != to->y()) {
          bendCount++;
        }
      }
    }

    for (int32_t index = 0; index < nodeCount; index++) {
      if (!(*used)[index]) {
        double distance;
        if (current < pins->size() && index < pins->size()) {
          distance = BoardStatisticsBoundsCalculator::distance(pins->get(current), pins->get(index),
                                                               angleRestriction);
        } else {
          distance = distanceToAreas(pins->get(jrt::Math::min(current, index)), areas);
        }
        if (distance < (*distances)[index]) {
          (*distances)[index] = distance;
          (*parents)[index] = current;
        }
      }
    }
  }
  return new BoardStatisticsBoundsCalculator_MstResult(length, bendCount);
}

double BoardStatisticsBoundsCalculator::distance(BoardStatisticsBoundsCalculator_Terminal* first,
                                                 BoardStatisticsBoundsCalculator_Terminal* second,
                                                 AngleRestriction angleRestriction) {
  double dx = jrt::Math::abs(first->x() - second->x());
  double dy = jrt::Math::abs(first->y() - second->y());
  switch (angleRestriction) {
    case AngleRestriction::NINETY_DEGREE:
      return dx + dy;
    case AngleRestriction::FORTYFIVE_DEGREE:
      return jrt::Math::max(dx, dy) + (jrt::Math::sqrt(2) - 1) * jrt::Math::min(dx, dy);
    default:
      return jrt::Math::hypot(dx, dy);
  }
}

double BoardStatisticsBoundsCalculator::distanceToAreas(
    BoardStatisticsBoundsCalculator_Terminal* pin, jrt::List<Area*>* areas) {
  FloatPoint* point = new FloatPoint(pin->x(), pin->y());
  double result = jrt::Double::POSITIVE_INFINITY;
  for (Area* area : *areas) {
    if (area->contains(point)) {
      return 0.0;
    }
    if (auto* circle = dynamic_cast<Circle*>(area)) {
      result = jrt::Math::min(
          result, jrt::Math::max(0.0, point->distance(circle->center->toFloat()) - circle->radius));
    } else {
      FloatPoint* nearestPoint = area->nearestPointApprox(point);
      if (nearestPoint == nullptr) {
        return 0.0;
      }
      result = jrt::Math::min(result, point->distance(nearestPoint));
    }
  }
  return result;
}

int32_t BoardStatisticsBoundsCalculator::calculateMinimumViaCount(
    Net* net, jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* terminals) {
  BoardStatisticsBoundsCalculator_LayerGroups* groups =
      new BoardStatisticsBoundsCalculator_LayerGroups();
  for (BoardStatisticsBoundsCalculator_Terminal* terminal : *terminals) {
    groups->addLayers(terminal->signalLayers());
  }
  if (groups->count() <= 1) {
    return 0;
  }

  ViaRule* viaRule = net->getNetClass() != nullptr ? net->getNetClass()->getViaRule() : nullptr;
  if (viaRule == nullptr) {
    return 0;
  }

  jrt::Set<int32_t>* remainingGroups = groups->roots();
  jrt::List<BoardStatisticsBoundsCalculator_ViaSpan*>* spans =
      new jrt::ArrayList<BoardStatisticsBoundsCalculator_ViaSpan*>();
  for (int32_t index = 0; index < viaRule->viaCount(); index++) {
    ViaInfo* viaInfo = viaRule->getVia(index);
    int32_t firstLayer = viaInfo->getPadstack()->fromLayer();
    int32_t lastLayer = viaInfo->getPadstack()->toLayer();
    spans->add(new BoardStatisticsBoundsCalculator_ViaSpan(jrt::Math::min(firstLayer, lastLayer),
                                                           jrt::Math::max(firstLayer, lastLayer)));
  }

  int32_t viaCount = 0;
  while (!remainingGroups->isEmpty()) {
    BoardStatisticsBoundsCalculator_ViaSpan* bestSpan = nullptr;
    // PORT NOTE: Java Set.of() (immutable empty set) -> an empty HashSet; it is never modified.
    jrt::Set<int32_t>* bestCoveredGroups = new jrt::HashSet<int32_t>();
    for (BoardStatisticsBoundsCalculator_ViaSpan* span : *spans) {
      jrt::Set<int32_t>* coveredGroups = groups->groupsCoveredBy(span);
      coveredGroups->retainAll(remainingGroups);
      if (coveredGroups->size() > bestCoveredGroups->size()) {
        bestSpan = span;
        bestCoveredGroups = coveredGroups;
      }
    }
    if (bestSpan == nullptr || bestCoveredGroups->isEmpty()) {
      break;
    }
    remainingGroups->removeAll(bestCoveredGroups);
    viaCount++;
  }
  return viaCount;
}

}  // namespace fr
