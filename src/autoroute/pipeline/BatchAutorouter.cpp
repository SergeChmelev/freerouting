#include "autoroute/pipeline/BatchAutorouter.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/BoardHistory.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/pipeline/AutorouteBatchLoop.h"
#include "autoroute/pipeline/AutorouteConnectionRouter.h"
#include "autoroute/pipeline/AutoroutePassRunner.h"
#include "autoroute/pipeline/AutorouteRuntimeMetrics.h"
#include "autoroute/pipeline/AutorouteUnroutedReport.h"
#include "autoroute/pipeline/BallEscapeOrder.h"
#include "autoroute/pipeline/NamedAlgorithmType.h"
#include "autoroute/pipeline/RipupMemory.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "core/RoutingJob.h"
#include "core/StoppableThread.h"
#include "datastructures/UndoableObjects.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

static_assert(BatchAutorouter::BOARD_RANK_LIMIT == BoardHistory::MAX_HISTORY_SIZE,
              "BOARD_RANK_LIMIT must equal BoardHistory.MAX_HISTORY_SIZE");

// ---------------------------------------------------------------------------------------------
// BatchAutorouter_NetPlanes (record)
// ---------------------------------------------------------------------------------------------

bool BatchAutorouter_NetPlanes::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<BatchAutorouter_NetPlanes>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(all_, other->all_) && jrt::equalsOf(withPins_, other->withPins_) &&
         jrt::equalsOf(fracturedIslandPins_, other->fracturedIslandPins_);
}

int32_t BatchAutorouter_NetPlanes::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(all_));
  h = jrt::hashCombine(h, jrt::hashOf(withPins_));
  h = jrt::hashCombine(h, jrt::hashOf(fracturedIslandPins_));
  return h;
}

std::string BatchAutorouter_NetPlanes::toString() {
  return "NetPlanes[all=" + jrt::str(all_) + ", withPins=" + jrt::str(withPins_) +
         ", fracturedIslandPins=" + jrt::str(fracturedIslandPins_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BatchAutorouter
// ---------------------------------------------------------------------------------------------

bool BatchAutorouter::BENCHMARK_PROFILE_ENABLED() {
  static bool v = jrt::Boolean::parseBoolean(
      jrt::System::getProperty("freerouting.benchmark.profile").value_or(""));
  return v;
}

bool BatchAutorouter::BENCHMARK_RETAIN_AUTOROUTE_DATABASE() {
  static bool v = jrt::Boolean::parseBoolean(
      jrt::System::getProperty("freerouting.benchmark.retain_autoroute_database").value_or(""));
  return v;
}

jrt::Set<int32_t>* BatchAutorouter::netsNotToRipFor(int32_t netNo) {
  if (routedFirstNets == nullptr) {
    routedFirstNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
    for (int32_t i = 1; i <= board->rules->nets->maxNetNumber(); i++) {
      Net* net = board->rules->nets->get(i);
      if (net != nullptr && net->isRoutedFirst()) {
        routedFirstNets->add(i);
      }
    }
  }
  jrt::Set<int32_t>* remembered = ripupMemory->netsNotToRip(netNo);
  if (routedFirstNets->isEmpty()) {
    return remembered;
  }
  jrt::TreeSet<int32_t>* result = new (jrt::kOp) jrt::TreeSet<int32_t>(remembered);
  result->addAll(routedFirstNets);
  return result;
}

BallEscapeOrder* BatchAutorouter::getBallEscapeOrder() {
  if (!this->ballEscapeOrderFound) {
    this->ballEscapeOrder = BallEscapeOrder::of(
        this->board, this->settings->autorouter != nullptr ? this->settings->autorouter->ballEscapeOrder : std::nullopt);
    this->ballEscapeOrderFound = true;
  }
  return this->ballEscapeOrder;
}

BatchAutorouter* BatchAutorouter::createAttempt(RoutingJob* job, RoutingBoard* board, RouterSettings* settings,
                                                bool removeUnconnectedVias) {
  int32_t accuracy = settings->tracePullTightAccuracy.has_value() ? *settings->tracePullTightAccuracy : 500;
  BatchAutorouter* router = new (jrt::kOp) BatchAutorouter(job->thread, board, settings, removeUnconnectedVias, true,
                                                settings->getStartRipupCosts(), accuracy);
  router->job = job;
  return router;
}

bool BatchAutorouter::repeatsEarlierPassLoop(const std::optional<std::string>& hash) {
  return hash.has_value() && this->repeatsPassLoop != nullptr && this->repeatsPassLoop(*hash);
}

BatchAutorouter::BatchAutorouter(RoutingJob* job)
    : BatchAutorouter(job->thread, job->board, job->routerSettings, !job->routerSettings->isFanoutEnabled(), true,
                      job->routerSettings->getStartRipupCosts(),
                      job->routerSettings->tracePullTightAccuracy.has_value()
                          ? *job->routerSettings->tracePullTightAccuracy
                          : 500) {
  this->job = job;
}

BatchAutorouter::BatchAutorouter(StoppableThread* thread, RoutingBoard* board, RouterSettings* settings,
                                 bool removeUnconnectedVias, bool withPreferredDirections, int32_t startRipupCosts,
                                 int32_t pullTightAccuracy)
    : NamedAlgorithm(thread, board, settings) {
  // Java field initializers
  this->reusableAutorouteItemList = new (jrt::kOp) jrt::ArrayList<Item*>();
  this->reusablePlaneItemList = new (jrt::kOp) jrt::ArrayList<Item*>();
  this->reusableSignalItemList = new (jrt::kOp) jrt::ArrayList<Item*>();
  this->reusableHandledItems = new (jrt::kOp) jrt::TreeSet<Item*>();
  this->ripupMemory = new (jrt::kOp) RipupMemory();

  this->random = new (jrt::kOp) jrt::Random(0);

  this->removeUnconnectedVias = removeUnconnectedVias;
  this->preferredDirections = withPreferredDirections;
  if (withPreferredDirections) {
    this->traceCosts = this->settings->getTraceCosts();
  } else {
    // remove preferred direction
    this->traceCosts = new (jrt::kOp) jrt::Array<AutorouteControl_ExpansionCostFactor*>(this->board->getLayerCount());
    for (int32_t i = 0; i < this->traceCosts->length; i++) {
      double currentMinCost = this->settings->getPreferredDirectionTraceCosts(i);
      (*this->traceCosts)[i] = new (jrt::kOp) AutorouteControl_ExpansionCostFactor(currentMinCost, currentMinCost);
    }
  }

  this->startRipupCosts = startRipupCosts;
  this->tracePullTightAccuracy = pullTightAccuracy;
  this->retainAutorouteDatabase = BENCHMARK_RETAIN_AUTOROUTE_DATABASE();
  this->startNecked = settings->autorouter == nullptr || !settings->autorouter->startNecked.has_value() ||
                      *settings->autorouter->startNecked;
  if (this->startNecked && board != nullptr) {
    for (Trace* trace : *board->getTraces()) {
      if (trace->netCount() == 1 && board->rules->nets->get(trace->getNetNumber(0)) != nullptr &&
          trace->getHalfWidth() < board->rules->getTraceHalfWidth(trace->getNetNumber(0), trace->getLayer())) {
        neckedRouteNets->add(trace->getNetNumber(0));
      }
    }
  }
  this->connectionRouter = new (jrt::kOp) AutorouteConnectionRouter(this);
  this->passRunner = new (jrt::kOp) AutoroutePassRunner(this);
  this->batchLoop = new (jrt::kOp) AutorouteBatchLoop(this);
  BallEscapeOrder::checkMode(this->settings->autorouter != nullptr ? this->settings->autorouter->ballEscapeOrder
                                                                   : std::nullopt);
}

void BatchAutorouter::resetPassProfile() {
  this->profileItemSelectionNanos = 0;
  this->profileIntermediateStatisticsNanos = 0;
  this->profileBoardStatisticsNanos = 0;
  this->profileIncompleteDrcNanos = 0;
  this->profileAutorouteItemNanos = 0;
  this->profileMazeSearchNanos = 0;
  this->profileOptChangedAreaNanos = 0;
  this->profileTailRemovalNanos = 0;
  this->profileRouteItemCount = 0;
  this->profilePlaneItemCount = 0;
}

void BatchAutorouter::logBenchmarkProfile(int32_t passNo) {
  if (!BENCHMARK_PROFILE_ENABLED()) {
    return;
  }
  FRLogger::info(
      "BENCHMARK_PROFILE pass=" + jrt::str(passNo) + ", items=" + jrt::str(this->profileRouteItemCount) +
      ", plane_items=" + jrt::str(this->profilePlaneItemCount) +
      ", selection_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileItemSelectionNanos)) +
      ", autoroute_item_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileAutorouteItemNanos)) +
      ", maze_search_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileMazeSearchNanos)) +
      ", opt_changed_area_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileOptChangedAreaNanos)) +
      ", tail_removal_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileTailRemovalNanos)) +
      ", statistics_ms=" +
      jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileIntermediateStatisticsNanos)) +
      ", retain_database=" + jrt::str(this->retainAutorouteDatabase) +
      ", board_statistics_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileBoardStatisticsNanos)) +
      ", incomplete_drc_ms=" + jrt::str(AutorouteRuntimeMetrics::nanosToMillis(this->profileIncompleteDrcNanos)));
}

int32_t BatchAutorouter::autoroutePassesForOptimizingItem(RoutingJob* job, int32_t maxPassCount, int32_t ripupCosts,
                                                          int32_t tracePullTightAccuracy,
                                                          bool withPreferredDirections,
                                                          RoutingBoard* updatedRoutingBoard,
                                                          RouterSettings* routerSettings,
                                                          jrt::Set<int32_t>* netsLeftUnrouted) {
  BatchAutorouter* routerInstance = new (jrt::kOp) BatchAutorouter(job->thread, updatedRoutingBoard, routerSettings, true,
                                                        withPreferredDirections, ripupCosts, tracePullTightAccuracy);
  routerInstance->job = job;
  routerInstance->isOptimizerAutorouter = true;
  routerInstance->startNecked = false;
  routerInstance->netsLeftUnrouted = netsLeftUnrouted;

  bool stillUnroutedItems = true;
  int32_t currentPassNo = 1;
  while (stillUnroutedItems && !job->thread->isStopAutoRouterRequested() && currentPassNo <= maxPassCount) {
    stillUnroutedItems = routerInstance->autoroutePass(currentPassNo);
    if (stillUnroutedItems && !job->thread->isStopAutoRouterRequested() && updatedRoutingBoard == nullptr) {
    }
    ++currentPassNo;
  }
  routerInstance->removeTails(Item_StopConnectionOption::NONE);
  if (!stillUnroutedItems) {
    --currentPassNo;
  }
  return currentPassNo;
}

jrt::Array<Point*>* BatchAutorouter::getImpactedPoints(Item* item) {
  if (auto* trace = jrt::fast_cast<Trace>(item)) {
    return jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()});
  }
  if (auto* via = jrt::fast_cast<Via>(item)) {
    return jrt::Array<Point*>::of({via->getCenter()});
  }
  if (auto* pin = jrt::fast_cast<Pin>(item)) {
    return jrt::Array<Point*>::of({pin->getCenter()});
  }
  if (auto* drillItem = jrt::fast_cast<DrillItem>(item)) {
    return jrt::Array<Point*>::of({drillItem->getCenter()});
  }
  return new (jrt::kGeom) jrt::Array<Point*>(0);
}

AutorouteAttemptResult* BatchAutorouter::enforceStrictDrc(RoutingBoard* board, int32_t routeNetNo,
                                                          int32_t maxItemIdBefore) {
  jrt::List<Item*>* newItems = new (jrt::kOp) jrt::ArrayList<Item*>();
  bool hasViolation = false;
  for (Item* currentItem : *board->getConnectableItems(routeNetNo)) {
    if (currentItem->getId() <= maxItemIdBefore ||
        !(jrt::fast_cast<Trace>(currentItem) != nullptr || jrt::fast_cast<Via>(currentItem) != nullptr)) {
      continue;
    }
    newItems->add(currentItem);
    if (!hasViolation && !currentItem->clearanceViolations()->isEmpty()) {
      hasViolation = true;
    }
  }
  if (!hasViolation) {
    return nullptr;
  }
  board->removeItems(newItems);
  return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::FAILED,
                                    "strict_drc: connection ripped because " + jrt::str(newItems->size()) +
                                        " new item(s) included clearance violations");
}

bool BatchAutorouter::isFanoutTimedOut() { return this->fanoutTimedOut; }

bool BatchAutorouter::shouldFireBoardUpdate() {
  int64_t currentTime = jrt::System::currentTimeMillis();
  if (currentTime - lastBoardUpdateTimestamp > 250) {  // Limit updates to 4 times per second (250ms)
    lastBoardUpdateTimestamp = currentTime;
    return true;
  }
  return false;
}

jrt::List<Item*>* BatchAutorouter::getAutorouteItems(RoutingBoard* board) {
  // Reuse instance collections to reduce memory allocation
  reusableAutorouteItemList->clear();
  reusablePlaneItemList->clear();
  reusableSignalItemList->clear();
  reusableHandledItems->clear();
  jrt::List<Item*>* autorouteItemList = reusableAutorouteItemList;
  jrt::Set<Item*>* handledItems = reusableHandledItems;
  jrt::Map<int32_t, BatchAutorouter_NetPlanes*>* planesByNet = new (jrt::kOp) jrt::HashMap<int32_t, BatchAutorouter_NetPlanes*>();
  auto itScope = board->itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    UndoableObjects_Storable* currentObject = board->itemList->readObject(it);
    if (currentObject == nullptr) {
      break;
    }
    auto* currentItem = jrt::fast_cast<Item>(currentObject);
    if (jrt::fast_cast<Connectable>(currentObject) != nullptr && currentItem != nullptr) {
      // This is a connectable item, like PolylineTrace or Pin
      if (!currentItem->isRoutable()) {
        if (!handledItems->contains(currentItem)) {
          bool needsRouting = false;
          bool hasPlaneNet = false;
          std::optional<std::string> queuedNetName;
          int32_t queuedConnected = 0;
          int32_t queuedTotal = 0;

          // Let's go through all nets of this item
          for (int32_t i = 0; i < currentItem->netCount(); i++) {
            int32_t currentNetNumber = currentItem->getNetNumber(i);
            if (netsLeftUnrouted->contains(currentNetNumber)) {
              continue;
            }
            jrt::Set<Item*>* connectedSet = currentItem->getConnectedSet(currentNetNumber);
            for (Item* currentConnectedItem : *connectedSet) {
              if (currentConnectedItem->netCount() <= 1) {
                handledItems->add(currentConnectedItem);
              }
            }
            int32_t netItemCount = board->connectableItemCount(currentNetNumber);

            // If the item is not connected to all other items of the net, we add it to the
            // auto-router's to-do list
            if ((connectedSet->size() < netItemCount) && (!currentItem->hasIgnoredNets())) {
              Net* net = board->rules->nets->get(currentNetNumber);
              bool isPlane = net != nullptr && net->containsPlane();
              if (isPlane) {
                if (coversAllPlanesOfNet(connectedSet, planesByNet, board, currentNetNumber)) {
                  continue;
                }
                hasPlaneNet = true;
              }
              needsRouting = true;
              if (!queuedNetName.has_value()) {
                queuedNetName = net != nullptr ? net->name : "net#" + jrt::str(currentNetNumber);
                queuedConnected = connectedSet->size();
                queuedTotal = netItemCount;
              }
            }
          }

          if (needsRouting) {
            if (hasPlaneNet) {
              reusablePlaneItemList->add(currentItem);
            } else {
              reusableSignalItemList->add(currentItem);
            }
            FRLogger::debug("Queuing item for routing: " + jrt::className(currentItem) + " on net '" +
                            jrt::str(queuedNetName) + "' (connected: " + jrt::str(queuedConnected) + "/" +
                            jrt::str(queuedTotal) + ", plane: " + jrt::str(hasPlaneNet) + ")");
          }
        }
      }
    }
  }
  // Route power plane-nets first: placing short stubs and vias early leaves escape
  // corridors open around pads and prevents signal traces from blocking via placement.
  autorouteItemList->addAll(reusablePlaneItemList);
  autorouteItemList->addAll(reusableSignalItemList);
  return autorouteItemList;
}

bool BatchAutorouter::coversAllPlanesOfNet(jrt::Set<Item*>* connectedSet,
                                           jrt::Map<int32_t, BatchAutorouter_NetPlanes*>* planesByNet,
                                           RoutingBoard* board, int32_t netNo) {
  BatchAutorouter_NetPlanes* planes =
      planesByNet->computeIfAbsent(netNo, [board](const int32_t& n) -> BatchAutorouter_NetPlanes* {
        jrt::List<ConductionArea*>* all = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
        jrt::List<ConductionArea*>* withPins = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
        jrt::List<Pin*>* fracturedIslandPins = new (jrt::kOp) jrt::ArrayList<Pin*>();
        for (ConductionArea* ca : *board->getConductionAreas()) {
          if (ca->containsNet(n)) {
            if (!ca->isFractured()) {
              all->add(ca);
              if (ca->reachesPinOfNet(n)) {
                withPins->add(ca);
              }
            } else {
              ca->collectPinsOnFracturedIslands(fracturedIslandPins);
            }
          }
        }
        return new (jrt::kOp) BatchAutorouter_NetPlanes(all, withPins, fracturedIslandPins);
      });
  bool onPlane = false;
  for (ConductionArea* plane : *planes->all()) {
    if (connectedSet->contains(plane)) {
      onPlane = true;
      break;
    }
  }
  if (!onPlane) {
    return false;
  }
  int32_t reached = 0;
  for (ConductionArea* plane : *planes->withPins()) {
    if (connectedSet->contains(plane)) {
      reached++;
    }
  }
  if (reached == 0) {
    // the item is on floating unfractured fragments only, which nothing needs
    return true;
  }
  if (reached < planes->withPins()->size()) {
    return false;
  }
  for (Pin* pin : *planes->fracturedIslandPins()) {
    if (!connectedSet->contains(pin)) {
      return false;
    }
  }
  return true;
}

bool BatchAutorouter::isPlaneItem(Item* item, RoutingBoard* board) {
  if (item == nullptr || board == nullptr) {
    return false;
  }
  for (int32_t i = 0; i < item->netCount(); i++) {
    Net* net = board->rules->nets->get(item->getNetNumber(i));
    if (net != nullptr && net->containsPlane()) {
      return true;
    }
  }
  return false;
}

bool BatchAutorouter::autoroutePass(int32_t passNo) { return passRunner->runSingleThread(passNo); }

void BatchAutorouter::resetAntiOscillationState() {
  if (this->passRunner != nullptr) {
    this->passRunner->resetAntiOscillationState();
  }
}

std::string BatchAutorouter::getId() { return "freerouting-router"; }

std::string BatchAutorouter::getName() { return "Freerouting Auto-router"; }

std::string BatchAutorouter::getVersion() { return "1.0"; }

std::string BatchAutorouter::getDescription() { return "Freerouting Auto-router v1.0"; }

NamedAlgorithmType BatchAutorouter::getType() { return NamedAlgorithmType::ROUTER; }

int32_t BatchAutorouter::getInitialUnroutedCount() { return this->initialUnroutedCount; }

int64_t BatchAutorouter::getSessionStartTime() { return this->sessionStartTime; }

bool BatchAutorouter::runBatchLoop() {
  this->board->awaitPostLoad();
  return batchLoop->run();
}

std::string BatchAutorouter::buildUnroutedConnectionsReport() { return AutorouteUnroutedReport::build(this->board); }

void BatchAutorouter::removeTails(Item_StopConnectionOption stopConnectionOption) {
  const int64_t tailRemovalStart = BENCHMARK_PROFILE_ENABLED() ? jrt::System::nanoTime() : 0;
  board->startMarkingChangedArea();
  board->removeTraceTails(-1, stopConnectionOption);
  int64_t pullTightStart = BENCHMARK_PROFILE_ENABLED() ? jrt::System::nanoTime() : 0;
  board->optChangedArea(new (jrt::kOp) jrt::Array<int32_t>(0), nullptr, this->tracePullTightAccuracy, this->traceCosts,
                        this->thread);
  if (BENCHMARK_PROFILE_ENABLED()) {
    this->profileTailRemovalNanos += jrt::System::nanoTime() - tailRemovalStart;
    this->profileOptChangedAreaNanos += jrt::System::nanoTime() - pullTightStart;
  }
}

// Tries to route an item on a specific net. Returns true, if the item is routed.
AutorouteAttemptResult* BatchAutorouter::autorouteItem(Item* item, int32_t routeNetNo,
                                                       jrt::TreeSet<Item*>* rippedItemList,
                                                       jrt::Map<Item*, int32_t>* ripupCosts, int32_t ripupPassNo) {
  return connectionRouter->route(item, routeNetNo, rippedItemList, ripupCosts, ripupPassNo);
}

FloatLine* BatchAutorouter::getAirLine() {
  if (this->airLine == nullptr) {
    return nullptr;
  }
  if (this->airLine->a == nullptr || this->airLine->b == nullptr) {
    return nullptr;
  }
  return this->airLine;
}

int32_t BatchAutorouter::calculateIncompleteCount(RoutingBoard* board) { return calculateIncompleteCount(board, nullptr); }

int32_t BatchAutorouter::calculateIncompleteCount(RoutingBoard* board, jrt::Set<int32_t>* incompleteNets) {
  int64_t drcStart = BENCHMARK_PROFILE_ENABLED() ? jrt::System::nanoTime() : 0;
  int32_t incompleteCount = board->routingLedger()->incompleteCount();
  if (BENCHMARK_PROFILE_ENABLED()) {
    this->profileIncompleteDrcNanos += jrt::System::nanoTime() - drcStart;
  }
  if (incompleteNets != nullptr) {
    incompleteNets->addAll(board->routingLedger()->incompleteNetNumbers());
  }
  return incompleteCount;
}

bool BatchAutorouter::isStartNecked() { return startNecked; }

void BatchAutorouter::setStartNecked(bool startNecked) { this->startNecked = startNecked; }

jrt::Set<int32_t>* BatchAutorouter::getNeckedRouteNets() { return neckedRouteNets; }

int32_t BatchAutorouter::getStartNeckedRouted() { return startNeckedRouted; }

int32_t BatchAutorouter::getStartNeckedFailed() { return startNeckedFailed; }

}  // namespace fr
