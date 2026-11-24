#include "board/optimize/StepSlider.h"

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
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "rules/BoardRules.h"

namespace fr {

namespace {

/** PORT NOTE: Java arrays are covariant (IntPoint[] is a Point[]); the C++ port copies. */
jrt::Array<Point*>* asPointArray(jrt::Array<IntPoint*>* corners) {
  auto* result = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*result)[i] = (*corners)[i];
  }
  return result;
}

/** board.getStatistics().clearanceViolations.totalCount (Java Integer auto-unboxing). */
int32_t clearanceViolationCount(RoutingBoard* board) {
  return board->getStatistics()->clearanceViolations->totalCount.value();
}

}  // namespace

StepSlider::StepSlider(RoutingBoard* board, int32_t pullTightAccuracy, Stoppable* stoppable)
    : board(board),
      pullTightAccuracy(pullTightAccuracy),
      stoppable(stoppable),
      zones(new (jrt::kOp) jrt::ArrayList<ConductionArea*>(board->getConductionAreas())) {}

StepSlider_Result* StepSlider::run(RoutingBoard* board, int32_t pullTightAccuracy,
                                   Stoppable* stoppable) {
  return (new (jrt::kOp) StepSlider(board, pullTightAccuracy, stoppable))->run();
}

StepSlider_Result* StepSlider::run() {
  StepSlider_Cost* const start = cost();
  const int32_t incompleteAtStart = board->routingLedger()->incompleteCount();
  const int32_t violationsAllowed = clearanceViolationCount(board);
  int32_t tried = 0;
  int32_t accepted = 0;
  int32_t passes = 0;
  // Shoving next to copper that already violates a clearance is not reliable: leave the board.
  while (violationsAllowed == 0 && passes < MAX_PASSES && !isStopRequested()) {
    ++passes;
    int32_t acceptedInThisPass = 0;
    jrt::List<int32_t>* traceIds = new (jrt::kOp) jrt::ArrayList<int32_t>();
    jrt::DisposeOnExit loopSource1(board->getTraces());
    for (Trace* trace : *loopSource1) {
      if (jrt::fast_cast<PolylineTrace>(trace) != nullptr) {
        traceIds->add(trace->getId());
      }
    }
    jrt::Collections::sort(traceIds);
    for (int32_t traceId : *traceIds) {
      // PORT NOTE (stage E): the geometry of trying the moves of one trace (candidate polylines,
      // shoving) lives in a local region; board items copy out what they keep.
      jrt::LocalGeomRegion traceRegion;
      if (isStopRequested()) {
        break;
      }
      PolylineTrace* trace = traceById(traceId);
      if (trace == nullptr || trace->isShoveFixed() || trace->isUserFixed()) {
        continue;
      }
      jrt::List<jrt::Array<Point*>*>* moves = this->moves(trace);
      if (moves->isEmpty()) {
        continue;
      }
      int32_t halfPitch = this->halfPitch(trace);
      StepSlider_Cost* before = cost();
      jrt::Map<int32_t, int32_t>* tracesBefore = traceSignatures();
      int32_t incompleteBefore = board->routingLedger()->incompleteCount();
      double bendWeight = BEND_WEIGHT_IN_HALF_PITCHES * halfPitch;
      double tolerance = 1e-6 * jrt::Math::max(1.0, before->length());
      jrt::Array<Point*>* bestMove = nullptr;
      double bestScore = before->length() + bendWeight * before->bends();
      for (jrt::Array<Point*>* move : *moves) {
        ++tried;
        board->generateSnapshot();
        if (insert(traceId, move) && !changedTraceEntersZone(tracesBefore)) {
          StepSlider_Cost* after = cost();
          bool notWorse = after->vias() <= before->vias() && after->bends() <= before->bends() &&
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
      }
      if (bestMove == nullptr) {
        continue;
      }
      board->generateSnapshot();
      if (insert(traceId, bestMove) && !changedTraceEntersZone(tracesBefore) &&
          board->routingLedger()->incompleteCount() <= incompleteBefore &&
          clearanceViolationCount(board) <= violationsAllowed) {
        board->popSnapshot();
        board->clearTransientAutorouteState();
        ++accepted;
        ++acceptedInThisPass;
      } else {
        board->undo(nullptr);
        board->clearTransientAutorouteState();
      }
    }
    board->releaseGraveyard();  // PORT NOTE (stage E): pass boundary (traces are kept by id)
    if (acceptedInThisPass == 0) {
      break;
    }
  }
  StepSlider_Cost* end = cost();
  return new (jrt::kOp) StepSlider_Result(passes, tried, accepted, start->length(), end->length(),
                               start->bends(), end->bends(), start->vias(), end->vias(),
                               incompleteAtStart, board->routingLedger()->incompleteCount(),
                               violationsAllowed, clearanceViolationCount(board));
}

jrt::List<jrt::Array<Point*>*>* StepSlider::moves(PolylineTrace* trace) {
  jrt::List<jrt::Array<Point*>*>* result = new (jrt::kOp) jrt::ArrayList<jrt::Array<Point*>*>();
  int32_t cornerCount = trace->cornerCount();
  if (cornerCount < 4) {
    return result;
  }
  jrt::Array<IntPoint*>* corners = new (jrt::kGeom) jrt::Array<IntPoint*>(cornerCount);
  for (int32_t i = 0; i < cornerCount; i++) {
    auto* corner = jrt::fast_cast<IntPoint>(trace->polyline()->corner(i));
    if (corner == nullptr) {
      return result;
    }
    (*corners)[i] = corner;
  }
  jrt::Array<Point*>* cornerPoints = asPointArray(corners);
  double maxStepLength = 2.0 * MAX_STEP_LENGTH_IN_PITCHES * halfPitch(trace);
  int32_t steps = stepCount(cornerPoints);
  for (int32_t k = 1; k + 2 < cornerCount; k++) {
    if (!isStep(cornerPoints, k) ||
        (*corners)[k]->toFloat()->distance((*corners)[k + 1]->toFloat()) > maxStepLength) {
      continue;
    }
    Vector* step = (*corners)[k + 1]->differenceBy((*corners)[k]);
    for (int32_t m = 1; m <= MAX_SLIDE_SEGMENTS; m++) {
      if (k + m + 1 < cornerCount) {
        addIfPromising(result, slide(corners, k, m + 1, step), cornerCount, steps);
      }
      if (k - m >= 0) {
        addIfPromising(result, slide(corners, k, -m, step), cornerCount, steps);
      }
    }
  }
  return result;
}

void StepSlider::addIfPromising(jrt::List<jrt::Array<Point*>*>* result,
                                jrt::Array<Point*>* moved, int32_t cornerCount,
                                int32_t steps) {
  if (moved->length < 2 || moved->length > cornerCount) {
    return;
  }
  if (moved->length == cornerCount && stepCount(moved) >= steps) {
    return;
  }
  result->add(moved);
}

jrt::Array<Point*>* StepSlider::slide(jrt::Array<IntPoint*>* corners, int32_t k, int32_t shift,
                                      Vector* step) {
  jrt::List<Point*>* moved = new (jrt::kOp) jrt::ArrayList<Point*>(corners->length + 1);
  int32_t keptBefore = shift > 0 ? k : k + shift;
  int32_t from = shift > 0 ? k + 2 : k + shift;
  int32_t to = shift > 0 ? k + shift : k - 1;
  int32_t keptAfter = shift > 0 ? k + shift : k + 1;
  Vector* translation = shift > 0 ? step->negate() : step;
  for (int32_t i = 0; i <= keptBefore; i++) {
    moved->add((*corners)[i]);
  }
  for (int32_t i = from; i <= to; i++) {
    moved->add((*corners)[i]->translateBy(translation));
  }
  for (int32_t i = keptAfter; i < corners->length; i++) {
    moved->add((*corners)[i]);
  }
  return withoutCollinearCorners(moved);
}

jrt::Array<Point*>* StepSlider::withoutCollinearCorners(jrt::List<Point*>* corners) {
  jrt::ArrayList<Point*>* result = new (jrt::kOp) jrt::ArrayList<Point*>(corners->size());
  for (Point* corner : *corners) {
    if (!result->isEmpty() && result->getLast()->equals(corner)) {
      continue;
    }
    while (result->size() >= 2 &&
           corner->sideOf(result->get(result->size() - 2), result->getLast()) ==
               Side::COLLINEAR()) {
      result->removeLast();
    }
    result->add(corner);
  }
  return result->toArray();
}

int32_t StepSlider::stepCount(jrt::Array<Point*>* corners) {
  int32_t count = 0;
  for (int32_t k = 1; k + 2 < corners->length; k++) {
    if (isStep(corners, k)) {
      count++;
    }
  }
  return count;
}

bool StepSlider::isStep(jrt::Array<Point*>* corners, int32_t k) {
  auto* a = jrt::fast_cast<IntPoint>((*corners)[k - 1]);
  auto* b = a != nullptr ? jrt::fast_cast<IntPoint>((*corners)[k]) : nullptr;
  auto* c = b != nullptr ? jrt::fast_cast<IntPoint>((*corners)[k + 1]) : nullptr;
  auto* d = c != nullptr ? jrt::fast_cast<IntPoint>((*corners)[k + 2]) : nullptr;
  if (!(a != nullptr && b != nullptr && c != nullptr && d != nullptr)) {
    return false;
  }
  int64_t ax = static_cast<int64_t>(b->x) - a->x;
  int64_t ay = static_cast<int64_t>(b->y) - a->y;
  int64_t sx = static_cast<int64_t>(c->x) - b->x;
  int64_t sy = static_cast<int64_t>(c->y) - b->y;
  int64_t bx = static_cast<int64_t>(d->x) - c->x;
  int64_t by = static_cast<int64_t>(d->y) - c->y;
  return ax * by - ay * bx == 0 && ax * bx + ay * by > 0 && ax * sy - ay * sx != 0;
}

bool StepSlider::insert(int32_t traceId, jrt::Array<Point*>* corners) {
  PolylineTrace* trace = traceById(traceId);
  if (trace == nullptr) {
    return false;
  }
  try {
    Polyline* polyline = new (jrt::kGeom) Polyline(corners);
    if (polyline->cornerCount() < 2) {
      return false;
    }
    int32_t halfWidth = trace->getHalfWidth();
    int32_t layer = trace->getLayer();
    int32_t clearanceClass = trace->clearanceClassIndex();
    jrt::Array<int32_t>* netNumbers = trace->netNumbers;
    if (!board->removeItems(jrt::ArrayList<Item*>::of({static_cast<Item*>(trace)}))) {
      return false;
    }
    Point* end = board->insertForcedTracePolyline(
        polyline, halfWidth, layer, netNumbers, clearanceClass, MAX_SHOVE_RECURSION_DEPTH, 0,
        MAX_SHOVE_RECURSION_DEPTH, jrt::Integer::MAX_VALUE, this->pullTightAccuracy, true,
        nullptr);
    return end != nullptr && end->equals(polyline->lastCorner());
  } catch (const jrt::RuntimeException& e) {
    return false;
  }
}

bool StepSlider::isStopRequested() {
  return this->stoppable != nullptr && this->stoppable->isStopRequested();
}

PolylineTrace* StepSlider::traceById(int32_t id) {
  jrt::DisposeOnExit loopSource2(board->getTraces());
  for (Trace* trace : *loopSource2) {
    if (trace->getId() == id) {
      if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
        return polylineTrace;
      }
    }
  }
  return nullptr;
}

bool StepSlider::traceEntersZone(PolylineTrace* trace) {
  int32_t layer = trace->getLayer();
  int32_t halfWidth = trace->getHalfWidth();
  int32_t clearanceClass = trace->clearanceClassIndex();
  IntBox* traceBox = trace->boundingBox();
  for (ConductionArea* zone : *zones) {
    if (zone->getLayer() != layer) {
      continue;
    }
    int32_t reach =
        halfWidth + board->clearanceValue(clearanceClass, zone->clearanceClassIndex(), layer);
    IntBox* zoneBox = zone->boundingBox();
    if (traceBox->ur->x + reach < zoneBox->ll->x || traceBox->ll->x - reach > zoneBox->ur->x ||
        traceBox->ur->y + reach < zoneBox->ll->y || traceBox->ll->y - reach > zoneBox->ur->y) {
      continue;
    }
    jrt::Array<TileShape*>* traceShapes = trace->polyline()->offsetShapes(reach);
    int32_t zoneTileCount = zone->tileShapeCount();
    for (TileShape* traceShape : *traceShapes) {
      for (int32_t j = 0; j < zoneTileCount; ++j) {
        if (traceShape->intersects(static_cast<Shape*>(zone->getTileShape(j)))) {
          return true;
        }
      }
    }
  }
  return false;
}

jrt::Map<int32_t, int32_t>* StepSlider::traceSignatures() {
  jrt::Map<int32_t, int32_t>* result = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();
  if (zones->isEmpty()) {
    return result;
  }
  jrt::DisposeOnExit loopSource3(board->getItems());
  for (Item* item : *loopSource3) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
      result->put(trace->getId(), signature(trace));
    }
  }
  return result;
}

int32_t StepSlider::signature(PolylineTrace* trace) {
  int32_t result = trace->getLayer();
  for (int32_t i = 0; i < trace->cornerCount(); ++i) {
    FloatPoint* corner = trace->polyline()->corner(i)->toFloat();
    result = 31 * result + jrt::Double::hashCode(corner->x);
    result = 31 * result + jrt::Double::hashCode(corner->y);
  }
  return result;
}

bool StepSlider::changedTraceEntersZone(jrt::Map<int32_t, int32_t>* tracesBefore) {
  if (zones->isEmpty()) {
    return false;
  }
  jrt::DisposeOnExit loopSource4(board->getItems());
  for (Item* item : *loopSource4) {
    auto* trace = jrt::fast_cast<PolylineTrace>(item);
    if (trace == nullptr) {
      continue;
    }
    std::optional<int32_t> before = tracesBefore->getOpt(trace->getId());
    if (before.has_value() && *before == signature(trace)) {
      continue;
    }
    if (traceEntersZone(trace)) {
      return true;
    }
  }
  return false;
}

int32_t StepSlider::halfPitch(PolylineTrace* trace) {
  int32_t pitch = 2 * trace->getHalfWidth() +
                  board->clearanceValue(trace->clearanceClassIndex(),
                                        BoardRules::defaultClearanceClass(), trace->getLayer());
  return jrt::Math::max(1, pitch / 2);
}

StepSlider_Cost* StepSlider::cost() {
  double length = 0;
  int32_t bends = 0;
  int32_t vias = 0;
  jrt::DisposeOnExit loopSource5(board->getItems());
  for (Item* item : *loopSource5) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
      length += trace->getLength();
      bends += jrt::Math::max(0, trace->cornerCount() - 2);
    } else if (jrt::fast_cast<Via>(item) != nullptr) {
      ++vias;
    }
  }
  return new (jrt::kOp) StepSlider_Cost(length, bends, vias);
}

}  // namespace fr
