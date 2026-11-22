#include "autoroute/pipeline/AutorouteConnectionRouter.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/pipeline/AutorouteAirlineCalculator.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/RipupMemory.h"
#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/StoppableThread.h"
#include "datastructures/IdGenerator.h"
#include "datastructures/TimeLimit.h"
#include "drc/ClearanceBaseline.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

AutorouteConnectionRouter::AutorouteConnectionRouter(BatchAutorouter* router) : router(router) {}

void AutorouteConnectionRouter::addSharablePads(Item* item, int32_t netNo, jrt::Set<Item*>* destinations) {
  Pin* pin = jrt::fast_cast<Pin>(item);
  if (pin == nullptr || pin->firstLayer() != pin->lastLayer() || router->settings->autorouter == nullptr ||
      !(router->settings->autorouter->planeAsPriority == std::optional<bool>(true))) {
    return;
  }
  std::optional<double> gapUm = router->settings->autorouter->planeShareGapUm;
  double shareGapUm = gapUm.has_value() ? *gapUm : 500.0;
  if (shareGapUm <= 0) {
    return;
  }
  int32_t resolution = jrt::Math::max(1, router->board->communication->resolution);
  double maxGap = Unit_scale(shareGapUm * resolution, Unit::UM, router->board->communication->unit);
  IntBox* box = pin->boundingBox();
  jrt::Set<Item*>* inPlane = new jrt::TreeSet<Item*>();
  for (Item* plane : *(new jrt::ArrayList<Item*>(destinations))) {
    inPlane->addAll(plane->getConnectedSet(netNo));
  }
  for (Item* other : *inPlane) {
    Pin* otherPin = jrt::fast_cast<Pin>(other);
    if (otherPin != nullptr && otherPin != pin && otherPin->isOnLayer(pin->firstLayer()) &&
        boxGap(box, otherPin->boundingBox()) <= maxGap) {
      destinations->add(otherPin);
    }
  }
}

jrt::Set<Item*>* AutorouteConnectionRouter::withoutFracturedAreas(jrt::Set<Item*>* items) {
  jrt::Set<Item*>* filtered = new jrt::TreeSet<Item*>();
  for (Item* it : *items) {
    ConductionArea* ca = jrt::fast_cast<ConductionArea>(it);
    if (!(ca != nullptr && ca->isFractured())) {
      filtered->add(it);
    }
  }
  return filtered->isEmpty() ? items : filtered;
}

jrt::Set<Item*>* AutorouteConnectionRouter::planeEscapeObstacles(Item* item, int32_t netNo,
                                                                 jrt::Set<Item*>* connectedSet,
                                                                 jrt::Set<Item*>* destinations) {
  Pin* pin = jrt::fast_cast<Pin>(item);
  if (pin == nullptr || router->settings->autorouter == nullptr ||
      !(router->settings->autorouter->planeAsPriority == std::optional<bool>(true))) {
    return jrt::Collections::emptySet<Item*>();
  }
  return router->board->planeEscapeObstacles(pin, netNo, connectedSet, destinations);
}

double AutorouteConnectionRouter::boxGap(IntBox* a, IntBox* b) {
  double dx = jrt::Math::max(0, jrt::Math::max(b->ll->x - a->ur->x, a->ll->x - b->ur->x));
  double dy = jrt::Math::max(0, jrt::Math::max(b->ll->y - a->ur->y, a->ll->y - b->ur->y));
  return jrt::Math::hypot(dx, dy);
}

AutorouteAttemptResult* AutorouteConnectionRouter::route(Item* item, int32_t routeNetNo,
                                                         jrt::TreeSet<Item*>* rippedItemList,
                                                         jrt::Map<Item*, int32_t>* ripupCosts, int32_t ripupPassNo) {
  AutorouteAttemptResult* result = route(item, routeNetNo, rippedItemList, ripupCosts, ripupPassNo, true);
  if (aimedAtPlaneOnly &&
      (result->state == AutorouteAttemptState::FAILED || result->state == AutorouteAttemptState::INSERT_ERROR)) {
    result = route(item, routeNetNo, rippedItemList, ripupCosts, ripupPassNo, false);
  }
  return result;
}

AutorouteAttemptResult* AutorouteConnectionRouter::route(Item* item, int32_t routeNetNo,
                                                         jrt::TreeSet<Item*>* rippedItemList,
                                                         jrt::Map<Item*, int32_t>* ripupCosts, int32_t ripupPassNo,
                                                         bool planeOnly) {
  aimedAtPlaneOnly = false;
  try {
    Net* routeNet = router->board->rules->nets->get(routeNetNo);
    bool containsPlane = routeNet != nullptr && routeNet->containsPlane();
    bool routedFirst = routeNet != nullptr && routeNet->isRoutedFirst();
    int32_t signalViaCosts = routedFirst ? router->settings->getPriorityViaCosts() : router->settings->getViaCosts();
    int32_t currentViaCosts =
        containsPlane ? jrt::Math::min(router->settings->getPlaneViaCosts(), signalViaCosts) : signalViaCosts;

    AutorouteControl* autorouteControl =
        new AutorouteControl(router->board, routeNetNo, router->settings, currentViaCosts, router->getTraceCosts());
    autorouteControl->ripupAllowed = true;
    autorouteControl->ripupCosts = router->getStartRipupCosts() * ripupPassNo;
    autorouteControl->netsNotToRip =
        routedFirst ? router->ripupMemory->netsNotToRip(routeNetNo) : router->netsNotToRipFor(routeNetNo);
    autorouteControl->removeUnconnectedVias = router->isRemoveUnconnectedVias();

    jrt::Set<Item*>* unconnectedSet = item->getUnconnectedSet(routeNetNo);
    if (unconnectedSet->isEmpty()) {
      return new AutorouteAttemptResult(AutorouteAttemptState::NO_UNCONNECTED_NETS);
    }

    jrt::Set<Item*>* connectedSet = item->getConnectedSet(routeNetNo);
    jrt::Set<Item*>* routeStartSet;
    jrt::Set<Item*>* routeDestSet;
    jrt::Set<Item*>* planeEscapeObstacles = jrt::Collections::emptySet<Item*>();
    if (containsPlane) {
      if (ConductionArea::coversAllPlanesOfNet(connectedSet, unconnectedSet, routeNetNo)) {
        return new AutorouteAttemptResult(AutorouteAttemptState::CONNECTED_TO_PLANE);
      }
      routeStartSet = connectedSet;
      routeDestSet = unconnectedSet;
      if (planeOnly) {
        jrt::Set<Item*>* planes = router->board->powerLayerPlanes(unconnectedSet);
        planes->removeIf([](Item* p) {
          ConductionArea* ca = jrt::fast_cast<ConductionArea>(p);
          return ca != nullptr && ca->isFractured();
        });
        if (!planes->isEmpty()) {
          addSharablePads(item, routeNetNo, planes);
          routeDestSet = planes;
          aimedAtPlaneOnly = true;
          planeEscapeObstacles = this->planeEscapeObstacles(item, routeNetNo, connectedSet, planes);
        }
      }
    } else {
      routeStartSet = unconnectedSet;
      routeDestSet = connectedSet;
    }
    // A fractured area belongs to the connected set through one island only, but its shape still
    // spans the others. As a start or destination it would let the search end on an island the
    // net does not reach, without a connection being made.
    routeStartSet = withoutFracturedAreas(routeStartSet);
    routeDestSet = withoutFracturedAreas(routeDestSet);
    router->setAirLine(AutorouteAirlineCalculator::calculateAirline(routeStartSet, routeDestSet));

    double maxMilliseconds = 100000 * jrt::Math::pow(2, ripupPassNo - 1);
    maxMilliseconds = jrt::Math::min(maxMilliseconds, static_cast<double>(jrt::Integer::MAX_VALUE));
    TimeLimit* timeLimit = new TimeLimit(jrt::d2i(maxMilliseconds));

    bool strictDrc = router->settings->isStrictDrc();
    // The obstacles of a plane escape are set around the class-width search below only. A necked
    // search builds its own engine without them, so a plane escape is never necked.
    bool escaping = !planeEscapeObstacles->isEmpty();

    // start_necked: a net that needed the neck before routes at the neck width, ripping like any
    // search, and that result stands.
    if (router->startNecked && !escaping && router->settings->getNeckWidthUm() > 0 &&
        router->neckedRouteNets->contains(routeNetNo)) {
      int32_t beforeStart = router->board->communication->idGenerator->maxGeneratedId();
      BasicBoard* startSnapshot = strictDrc ? router->board->serialize(false) : nullptr;
      AutorouteAttemptResult* started =
          retryConnectionNecked(routeNetNo, autorouteControl, currentViaCosts, routeStartSet, routeDestSet,
                                rippedItemList, ripupCosts, ripupPassNo, timeLimit,
                                /* planned= */ true,
                                /* quiet= */ true);
      if (started != nullptr) {
        if (started->state != AutorouteAttemptState::ROUTED) {
          router->startNeckedFailed++;
          return started;
        }
        AutorouteAttemptResult* rejection =
            applyStrictDrcAfterRoute(routeNetNo, beforeStart, startSnapshot, ripupPassNo);
        if (rejection != nullptr) {
          return rejection;
        }
        router->startNeckedRouted++;
        return started;
      }
    }

    // The obstacles below hold for this search only: rooms and drill pages built with them must
    // not be retained for the next connection of the net.
    AutorouteEngine* autorouteEngine =
        router->board->initAutoroute(routeNetNo, autorouteControl->traceClearanceClassIndex, router->thread,
                                     timeLimit, router->isRetainAutorouteDatabase() && !escaping);
    int32_t maxItemIdBeforeRoute = router->board->communication->idGenerator->maxGeneratedId();
    // Strict DRC needs this snapshot to undo a rejected route together with the ripups it made.
    BasicBoard* strictDrcBoardSnapshot = strictDrc ? router->board->serialize(false) : nullptr;

    int64_t mazeSearchStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    AutorouteAttemptResult* searchResult;
    for (Item* obstacle : *planeEscapeObstacles) obstacle->setPlaneEscapeObstacle(true);
    // Java finally block
    auto finallyBlock = [&]() {
      for (Item* obstacle : *planeEscapeObstacles) obstacle->setPlaneEscapeObstacle(false);
      if (escaping) {
        router->board->finishAutoroute();
      }
    };
    try {
      searchResult =
          autorouteEngine->autorouteConnection(routeStartSet, routeDestSet, autorouteControl, rippedItemList, ripupCosts);
    } catch (...) {
      finallyBlock();
      throw;
    }
    finallyBlock();
    AutorouteAttemptResult* autorouteResult = searchResult;
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->addProfileMazeSearchNanos(jrt::System::nanoTime() - mazeSearchStart);
    }

    if (autorouteResult->state == AutorouteAttemptState::ROUTED) {
      int32_t maxItemIdBeforeOpt = router->board->communication->idGenerator->maxGeneratedId();
      FRLogger::trace("compare_trace_opt_changed_area_before net=" + jrt::str(routeNetNo) +
                      ", maxItemId=" + jrt::str(maxItemIdBeforeOpt));
      int64_t pullTightStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
      router->board->optChangedArea(new jrt::Array<int32_t>(0), nullptr, router->getTracePullTightAccuracy(),
                                    autorouteControl->traceCosts, router->thread);
      if (BatchAutorouter::isBenchmarkProfileEnabled()) {
        router->addProfileOptChangedAreaNanos(jrt::System::nanoTime() - pullTightStart);
      }
      int32_t maxItemIdAfterOpt = router->board->communication->idGenerator->maxGeneratedId();
      FRLogger::trace("compare_trace_opt_changed_area_after net=" + jrt::str(routeNetNo) +
                      ", maxItemId=" + jrt::str(maxItemIdAfterOpt) +
                      ", delta=" + jrt::str(maxItemIdAfterOpt - maxItemIdBeforeOpt));
    }

    if ((autorouteResult->state == AutorouteAttemptState::FAILED ||
         autorouteResult->state == AutorouteAttemptState::INSERT_ERROR) &&
        !escaping && router->settings->getNeckWidthUm() > 0) {
      AutorouteAttemptResult* neckedResult =
          retryConnectionNecked(routeNetNo, autorouteControl, currentViaCosts, routeStartSet, routeDestSet,
                                rippedItemList, ripupCosts, ripupPassNo, timeLimit,
                                /* planned= */ false,
                                /* quiet= */ false);
      if (neckedResult != nullptr) {
        AutorouteAttemptResult* strictResult =
            applyStrictDrcAfterRoute(routeNetNo, maxItemIdBeforeRoute, strictDrcBoardSnapshot, ripupPassNo);
        if (strictResult != nullptr) {
          return strictResult;
        }
        return neckedResult;
      }
    }

    if (autorouteResult->state == AutorouteAttemptState::ROUTED) {
      AutorouteAttemptResult* strictResult =
          applyStrictDrcAfterRoute(routeNetNo, maxItemIdBeforeRoute, strictDrcBoardSnapshot, ripupPassNo);
      if (strictResult != nullptr) {
        return strictResult;
      }
    }

    return autorouteResult;
  } catch (const jrt::Exception& e) {
    FRLogger::error("Error during routing passes", &e);
    return new AutorouteAttemptResult(AutorouteAttemptState::FAILED);
  }
}

AutorouteAttemptResult* AutorouteConnectionRouter::retryConnectionNecked(
    int32_t routeNetNo, AutorouteControl* originalControl, int32_t viaCosts, jrt::Set<Item*>* routeStartSet,
    jrt::Set<Item*>* routeDestSet, jrt::TreeSet<Item*>* rippedItemList, jrt::Map<Item*, int32_t>* ripupCosts,
    int32_t ripupPassNo, TimeLimit* timeLimit, bool planned, bool quiet) {
  int32_t neckHalfWidth = AutorouteControl::neckHalfWidth(router->board, router->settings);
  bool narrowerSomewhere = false;
  for (int32_t i = 0; i < originalControl->layerCount; i++) {
    if ((*originalControl->layerActive)[i] && (*originalControl->traceHalfWidth)[i] > neckHalfWidth) {
      narrowerSomewhere = true;
      break;
    }
  }
  if (!narrowerSomewhere) {
    return nullptr;
  }

  AutorouteControl* neckControl =
      new AutorouteControl(router->board, routeNetNo, router->settings, viaCosts, router->getTraceCosts());
  neckControl->ripupAllowed = true;
  neckControl->ripupCosts = router->getStartRipupCosts() * ripupPassNo;
  neckControl->netsNotToRip = originalControl->netsNotToRip;
  neckControl->removeUnconnectedVias = router->isRemoveUnconnectedVias();
  for (int32_t i = 0; i < neckControl->layerCount; i++) {
    int32_t compensation = (*neckControl->compensatedTraceHalfWidth)[i] - (*neckControl->traceHalfWidth)[i];
    (*neckControl->traceHalfWidth)[i] = jrt::Math::min((*neckControl->traceHalfWidth)[i], neckHalfWidth);
    (*neckControl->compensatedTraceHalfWidth)[i] = (*neckControl->traceHalfWidth)[i] + compensation;
    neckControl->updateTraceClearanceOffset(i, router->board);
  }

  AutorouteEngine* neckEngine =
      router->board->initAutoroute(routeNetNo, neckControl->traceClearanceClassIndex, router->thread, timeLimit,
                                   router->isRetainAutorouteDatabase());
  int64_t neckMazeSearchStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
  AutorouteAttemptResult* neckResult =
      neckEngine->autorouteConnection(routeStartSet, routeDestSet, neckControl, rippedItemList, ripupCosts);
  if (BatchAutorouter::isBenchmarkProfileEnabled()) {
    router->addProfileMazeSearchNanos(jrt::System::nanoTime() - neckMazeSearchStart);
  }
  if (neckResult->state != AutorouteAttemptState::ROUTED) {
    return planned ? neckResult : nullptr;
  }

  router->neckedRouteNets->add(routeNetNo);

  int64_t neckPullTightStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
  router->board->optChangedArea(new jrt::Array<int32_t>(0), nullptr, router->getTracePullTightAccuracy(),
                                neckControl->traceCosts, router->thread);
  if (BatchAutorouter::isBenchmarkProfileEnabled()) {
    router->addProfileOptChangedAreaNanos(jrt::System::nanoTime() - neckPullTightStart);
  }
  if (!quiet) {
    Net* routeNet = router->board->rules->nets->get(routeNetNo);
    FRLogger::info(std::string(planned ? "Retry plan routed net '" : "Necked retry routed net '") +
                   (routeNet != nullptr ? routeNet->name : "#" + jrt::str(routeNetNo)) + "' at " +
                   jrt::str(router->settings->getNeckWidthUm()) + " um trace width.");
  }
  return neckResult;
}

AutorouteAttemptResult* AutorouteConnectionRouter::applyStrictDrcAfterRoute(int32_t routeNetNo,
                                                                            int32_t maxItemIdBefore,
                                                                            BasicBoard* boardSnapshotBeforeRoute,
                                                                            int32_t ripupPassNo) {
  if (boardSnapshotBeforeRoute == nullptr) {
    return nullptr;
  }
  AutorouteAttemptResult* rejection = BatchAutorouter::enforceStrictDrc(router->board, routeNetNo, maxItemIdBefore);
  if (rejection != nullptr) {
    FRLogger::trace("AutorouteConnectionRouter.apply_strict_drc", "strict_drc_rejection",
                    "pass=" + jrt::str(ripupPassNo) + ", net=" + jrt::str(routeNetNo) +
                        ", reason=" + rejection->details,
                    "", nullptr);
    ClearanceBaseline* baseline = router->board->clearanceBaseline;
    router->board = jrt::fast_cast<RoutingBoard>(BasicBoard::deserialize(boardSnapshotBeforeRoute));
    router->board->clearanceBaseline = baseline;
  }
  return rejection;
}

}  // namespace fr
