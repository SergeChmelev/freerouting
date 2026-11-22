#include "board/optimize/ViaNudger.h"

#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/trace/PolylineTrace.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "datastructures/Stoppable.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "rules/BoardRules.h"

namespace fr {

namespace {

/** board.getStatistics().clearanceViolations.totalCount (Java Integer auto-unboxing). */
int32_t clearanceViolationCount(RoutingBoard* board) {
  return board->getStatistics()->clearanceViolations->totalCount.value();
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// ViaNudger.ZoneRegion record

bool ViaNudger_ZoneRegion::equals(jrt::Object* o) {
  auto* other = jrt::fast_cast<ViaNudger_ZoneRegion>(o);
  if (other == nullptr) return false;
  // Java record equals: Objects.equals on reference components (arrays by identity).
  return layer_ == other->layer_ && clearanceClass_ == other->clearanceClass_ &&
         jrt::equalsOf(boundingBox_, other->boundingBox_) && tiles_ == other->tiles_;
}

int32_t ViaNudger_ZoneRegion::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(layer_));
  h = jrt::hashCombine(h, jrt::hashOf(clearanceClass_));
  h = jrt::hashCombine(h, jrt::hashOf(boundingBox_));
  h = jrt::hashCombine(h, jrt::System::identityHashCode(tiles_));
  return h;
}

std::string ViaNudger_ZoneRegion::toString() {
  return "ZoneRegion[layer=" + jrt::str(layer_) + ", clearanceClass=" + jrt::str(clearanceClass_) +
         ", boundingBox=" + jrt::str(boundingBox_) + ", tiles=" + jrt::str(tiles_) + "]";
}

// ---------------------------------------------------------------------------------------------
// ViaNudger

jrt::Array<int32_t>* ViaNudger::STEPS_IN_HALF_PITCHES() {
  static jrt::Array<int32_t>* v = jrt::Array<int32_t>::of({1, 2, 4, 6});
  return v;
}

jrt::Array<jrt::Array<int32_t>*>* ViaNudger::DIRECTIONS() {
  static jrt::Array<jrt::Array<int32_t>*>* v = jrt::Array<jrt::Array<int32_t>*>::of(
      {jrt::Array<int32_t>::of({1, 0}), jrt::Array<int32_t>::of({1, 1}),
       jrt::Array<int32_t>::of({0, 1}), jrt::Array<int32_t>::of({-1, 1}),
       jrt::Array<int32_t>::of({-1, 0}), jrt::Array<int32_t>::of({-1, -1}),
       jrt::Array<int32_t>::of({0, -1}), jrt::Array<int32_t>::of({1, -1})});
  return v;
}

ViaNudger::ViaNudger(RoutingBoard* board, int32_t pullTightAccuracy, Stoppable* stoppable)
    : board(board), pullTightAccuracy(pullTightAccuracy), stoppable(stoppable) {
  jrt::List<ViaNudger_ZoneRegion*>* zoneRegions = new jrt::ArrayList<ViaNudger_ZoneRegion*>();
  for (ConductionArea* zone : *board->getConductionAreas()) {
    Area* area = zone->getArea();
    if (area == nullptr || area->isEmpty()) {
      continue;
    }
    zoneRegions->add(new ViaNudger_ZoneRegion(zone->getLayer(), zone->clearanceClassIndex(),
                                              area->boundingBox(), area->splitToConvex()));
  }
  this->zones = zoneRegions;
}

ViaNudger_Result* ViaNudger::run(RoutingBoard* board, int32_t pullTightAccuracy,
                                 Stoppable* stoppable) {
  return (new ViaNudger(board, pullTightAccuracy, stoppable))->run();
}

ViaNudger_Result* ViaNudger::run() {
  ViaNudger_Cost* const start = cost();
  const int32_t incompleteAtStart = board->routingLedger()->incompleteCount();
  int32_t violationsAllowed = clearanceViolationCount(board);
  if (violationsAllowed > 0) {
    // Shoving next to copper that already violates a clearance is not reliable: leave the board.
    return new ViaNudger_Result(0, 0, 0, start->length(), start->length(), start->bends(),
                                start->bends(), start->vias(), start->vias(), incompleteAtStart,
                                incompleteAtStart, violationsAllowed, violationsAllowed);
  }
  jrt::List<int32_t>* viaIds = new jrt::ArrayList<int32_t>();
  for (Via* via : *board->getVias()) {
    viaIds->add(via->getId());
  }
  jrt::Collections::sort(viaIds);

  int32_t tried = 0;
  int32_t accepted = 0;
  int32_t passes = 0;
  jrt::List<FloatPoint*>* movedInPreviousPass = nullptr;
  while (passes < MAX_PASSES && !isStopRequested()) {
    ++passes;
    jrt::List<FloatPoint*>* movedInThisPass = new jrt::ArrayList<FloatPoint*>();
    for (int32_t viaId : *viaIds) {
      if (isStopRequested()) {
        break;
      }
      Via* via = viaById(viaId);
      if (via == nullptr || via->isShoveFixed() || via->isUserFixed() ||
          touchesConductionArea(via) || hasShoveFixedContact(via)) {
        continue;
      }
      int32_t halfPitch = this->halfPitch(via);
      if (halfPitch <= 0) {
        continue;
      }
      FloatPoint* center = via->getCenter()->toFloat();
      if (isInOrNearZone(via, nullptr)) {
        continue;
      }
      if (movedInPreviousPass != nullptr && !isNear(center, movedInPreviousPass, halfPitch) &&
          !isNear(center, movedInThisPass, halfPitch)) {
        continue;
      }
      ViaNudger_Cost* before = cost();
      jrt::Map<int32_t, int32_t>* tracesBefore = traceSignatures();
      int32_t incompleteBefore = board->routingLedger()->incompleteCount();
      double bendWeight = BEND_WEIGHT_IN_HALF_PITCHES * halfPitch;
      double tolerance = 1e-6 * jrt::Math::max(1.0, before->length());
      IntVector* bestMove = nullptr;
      double bestScore = before->length() + bendWeight * before->bends();
      for (jrt::Array<int32_t>* direction : *DIRECTIONS()) {
        for (int32_t step : *STEPS_IN_HALF_PITCHES()) {
          IntVector* move = new IntVector((*direction)[0] * step * halfPitch,
                                          (*direction)[1] * step * halfPitch);
          if (isInOrNearZone(via, move)) {
            continue;
          }
          ++tried;
          board->generateSnapshot();
          bool moved = this->move(via, move) && !changedTraceEntersZone(tracesBefore);
          if (moved) {
            ViaNudger_Cost* after = cost();
            bool notWorse = after->vias() <= before->vias() &&
                            after->bends() <= before->bends() &&
                            after->length() <= before->length() + tolerance &&
                            board->routingLedger()->incompleteCount() <= incompleteBefore;
            bool better =
                after->bends() < before->bends() || after->length() < before->length() - tolerance;
            double score = after->length() + bendWeight * after->bends();
            if (notWorse && better && score < bestScore - tolerance) {
              bestScore = score;
              bestMove = move;
            }
          }
          board->undo(nullptr);
          board->clearTransientAutorouteState();
          via = viaById(viaId);
          if (via == nullptr) {
            break;
          }
        }
        if (via == nullptr) {
          break;
        }
      }
      if (bestMove == nullptr || via == nullptr) {
        continue;
      }
      board->generateSnapshot();
      if (this->move(via, bestMove) && !changedTraceEntersZone(tracesBefore) &&
          board->routingLedger()->incompleteCount() <= incompleteBefore &&
          clearanceViolationCount(board) <= violationsAllowed) {
        board->clearTransientAutorouteState();
        ++accepted;
        movedInThisPass->add(center);
      } else {
        board->undo(nullptr);
        board->clearTransientAutorouteState();
      }
    }
    if (movedInThisPass->isEmpty()) {
      break;
    }
    movedInPreviousPass = movedInThisPass;
  }
  if (isStopRequested()) {
    while (accepted > 0) {
      board->undo(nullptr);
      --accepted;
    }
  } else {
    for (int32_t i = 0; i < accepted; ++i) {
      board->popSnapshot();
    }
  }
  ViaNudger_Cost* end = cost();
  return new ViaNudger_Result(passes, tried, accepted, start->length(), end->length(),
                              start->bends(), end->bends(), start->vias(), end->vias(),
                              incompleteAtStart, board->routingLedger()->incompleteCount(),
                              violationsAllowed, clearanceViolationCount(board));
}

bool ViaNudger::move(Via* via, IntVector* move) {
  try {
    return board->moveDrillItem(via, move, MAX_SHOVE_RECURSION_DEPTH, 0, jrt::Integer::MAX_VALUE,
                                this->pullTightAccuracy, 0);
  } catch (const jrt::RuntimeException& e) {
    return false;
  }
}

bool ViaNudger::isStopRequested() {
  return this->stoppable != nullptr && this->stoppable->isStopRequested();
}

bool ViaNudger::isNear(FloatPoint* point, jrt::List<FloatPoint*>* others, int32_t halfPitch) {
  double radius = static_cast<double>(RETRY_RADIUS_IN_HALF_PITCHES) * halfPitch;
  for (FloatPoint* other : *others) {
    if (jrt::Math::abs(point->x - other->x) <= radius &&
        jrt::Math::abs(point->y - other->y) <= radius) {
      return true;
    }
  }
  return false;
}

Via* ViaNudger::viaById(int32_t id) {
  for (Via* via : *board->getVias()) {
    if (via->getId() == id) {
      return via;
    }
  }
  return nullptr;
}

bool ViaNudger::touchesConductionArea(Via* via) {
  for (Item* contact : *via->getNormalContacts()) {
    if (jrt::fast_cast<ConductionArea>(contact) != nullptr) {
      return true;
    }
  }
  return false;
}

bool ViaNudger::hasShoveFixedContact(Via* via) {
  for (Item* contact : *via->getNormalContacts()) {
    if (contact->isShoveFixed()) {
      return true;
    }
  }
  return false;
}

bool ViaNudger::isInOrNearZone(Via* via, IntVector* translation) {
  if (zones->isEmpty()) {
    return false;
  }
  for (int32_t layer = via->firstLayer(); layer <= via->lastLayer(); ++layer) {
    Shape* padShape = via->getShape(layer - via->firstLayer());
    if (padShape == nullptr) {
      continue;
    }
    if (translation != nullptr && !translation->equals(Vector::ZERO())) {
      padShape = jrt::fast_cast<Shape>(padShape->translateBy(static_cast<Vector*>(translation)));
    }
    if (shapeEntersZone(padShape, layer, via->clearanceClassIndex())) {
      return true;
    }
  }
  return false;
}

bool ViaNudger::shapeEntersZone(Shape* shape, int32_t layer, int32_t clearanceClass) {
  for (ViaNudger_ZoneRegion* zone : *zones) {
    if (zone->layer() != layer) {
      continue;
    }
    int32_t clearance = board->clearanceValue(clearanceClass, zone->clearanceClass(), layer);
    Shape* expanded = clearance > 0 ? shape->enlarge(clearance) : shape;
    if (!expanded->boundingBox()->intersects(zone->boundingBox())) {
      continue;
    }
    for (TileShape* tile : *zone->tiles()) {
      if (tile->intersects(expanded)) {
        return true;
      }
    }
  }
  return false;
}

jrt::Map<int32_t, int32_t>* ViaNudger::traceSignatures() {
  jrt::Map<int32_t, int32_t>* result = new jrt::HashMap<int32_t, int32_t>();
  if (zones->isEmpty()) {
    return result;
  }
  for (Item* item : *board->getItems()) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
      result->put(trace->getId(), signature(trace));
    }
  }
  return result;
}

int32_t ViaNudger::signature(PolylineTrace* trace) {
  int32_t result = trace->getLayer();
  for (int32_t i = 0; i < trace->cornerCount(); ++i) {
    FloatPoint* corner = trace->polyline()->corner(i)->toFloat();
    result = 31 * result + jrt::Double::hashCode(corner->x);
    result = 31 * result + jrt::Double::hashCode(corner->y);
  }
  return result;
}

bool ViaNudger::changedTraceEntersZone(jrt::Map<int32_t, int32_t>* tracesBefore) {
  if (zones->isEmpty()) {
    return false;
  }
  for (Item* item : *board->getItems()) {
    auto* trace = jrt::fast_cast<PolylineTrace>(item);
    if (trace == nullptr) {
      continue;
    }
    std::optional<int32_t> before = tracesBefore->getOpt(trace->getId());
    if (before.has_value() && *before == signature(trace)) {
      continue;
    }
    int32_t layer = trace->getLayer();
    int32_t clearanceClass = trace->clearanceClassIndex();
    int32_t segmentCount = trace->tileShapeCount();
    for (int32_t i = 0; i < segmentCount; ++i) {
      TileShape* segment = trace->getTileShape(i);
      if (segment != nullptr && shapeEntersZone(segment, layer, clearanceClass)) {
        return true;
      }
    }
  }
  return false;
}

int32_t ViaNudger::halfPitch(Via* via) {
  int32_t result = 0;
  for (Item* contact : *via->getNormalContacts()) {
    auto* trace = jrt::fast_cast<Trace>(contact);
    if (trace == nullptr) {
      continue;
    }
    int32_t pitch = 2 * trace->getHalfWidth() +
                    board->clearanceValue(trace->clearanceClassIndex(),
                                          BoardRules::defaultClearanceClass(), trace->getLayer());
    int32_t halfPitch = pitch / 2;
    if (result == 0 || halfPitch < result) {
      result = halfPitch;
    }
  }
  return result;
}

ViaNudger_Cost* ViaNudger::cost() {
  double length = 0;
  int32_t bends = 0;
  int32_t vias = 0;
  for (Item* item : *board->getItems()) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
      length += trace->getLength();
      bends += jrt::Math::max(0, trace->cornerCount() - 2);
    } else if (jrt::fast_cast<Via>(item) != nullptr) {
      ++vias;
    }
  }
  return new ViaNudger_Cost(length, bends, vias);
}

}  // namespace fr
