#include "autoroute/pipeline/AutoroutePassRunner.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/DesignBlocks.h"
#include "autoroute/PerformanceProfiler.h"
#include "autoroute/RoutingFailureLog.h"
#include "autoroute/pipeline/BallEscapeOrder.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/LastMileBlockerRipup.h"
#include "autoroute/pipeline/RipupMemory.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/state/Communication.h"
#include "core/RouterCounters.h"
#include "core/RoutingJob.h"
#include "core/StoppableThread.h"
#include "core/library/Package.h"
#include "core/scoring/BoardStatistics.h"
#include "datastructures/IdGenerator.h"
#include "drc/AirLine.h"
#include "drc/DesignRulesChecker.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Point.h"
#include "io/CoordinateTransform.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// AutoroutePassRunner_FailedConnectionPair (Java record)
// ---------------------------------------------------------------------------------------------

AutoroutePassRunner_FailedConnectionPair* AutoroutePassRunner_FailedConnectionPair::of(
    Item* item, int32_t netNo) {
  jrt::Set<Item*>* connected = item->getConnectedSet(netNo);
  jrt::Set<Item*>* unconnected = item->getUnconnectedSet(netNo);
  if (connected->isEmpty() || unconnected->isEmpty()) {
    return nullptr;
  }
  jrt::Set<int32_t>* a = new jrt::TreeSet<int32_t>();
  for (Item* it : *connected) {
    a->add(it->getId());
  }
  jrt::Set<int32_t>* b = new jrt::TreeSet<int32_t>();
  for (Item* it : *unconnected) {
    b->add(it->getId());
  }
  int32_t minA = a->iterator()->next();
  int32_t minB = b->iterator()->next();
  return minA <= minB ? new AutoroutePassRunner_FailedConnectionPair(netNo, a, b)
                      : new AutoroutePassRunner_FailedConnectionPair(netNo, b, a);
}

bool AutoroutePassRunner_FailedConnectionPair::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* other = jrt::fast_cast<AutoroutePassRunner_FailedConnectionPair>(o);
  if (other == nullptr) {
    return false;
  }
  if (netNo_ != other->netNo_) {
    return false;
  }
  bool sameA = sideA_ == nullptr ? other->sideA_ == nullptr : sideA_->equals(other->sideA_);
  if (!sameA) {
    return false;
  }
  return sideB_ == nullptr ? other->sideB_ == nullptr : sideB_->equals(other->sideB_);
}

int32_t AutoroutePassRunner_FailedConnectionPair::hashCode() {
  int32_t h = 0;
  h = 31 * h + jrt::Integer::hashCode(netNo_);
  h = 31 * h + (sideA_ != nullptr ? sideA_->hashCode() : 0);
  h = 31 * h + (sideB_ != nullptr ? sideB_->hashCode() : 0);
  return h;
}

std::string AutoroutePassRunner_FailedConnectionPair::toString() {
  return "FailedConnectionPair[netNo=" + jrt::str(netNo_) + ", sideA=" +
         jrt::str(static_cast<jrt::Object*>(sideA_)) +
         ", sideB=" + jrt::str(static_cast<jrt::Object*>(sideB_)) + "]";
}

// ---------------------------------------------------------------------------------------------
// AutoroutePassRunner
// ---------------------------------------------------------------------------------------------

AutoroutePassRunner::AutoroutePassRunner(BatchAutorouter* router)
    : router(router), previousIncompleteNets(jrt::Collections::emptySet<int32_t>()) {}

bool AutoroutePassRunner::runSingleThread(int32_t passNo) {
  int64_t passStartTime = jrt::System::currentTimeMillis();
  if (BatchAutorouter::isBenchmarkProfileEnabled()) {
    router->resetPassProfile();
  }
  try {
    int64_t itemSelectionStart =
        BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    jrt::List<Item*>* autorouteItemList = router->getAutorouteItems(router->board);
    dropDesignBlockedItems(autorouteItemList);
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->profileItemSelectionNanos += jrt::System::nanoTime() - itemSelectionStart;
    }

    if (autorouteItemList->isEmpty()) {
      router->airLine = nullptr;
      return false;
    }

    if (passNo > 1) {
      reorderSingleThreadItems(autorouteItemList, passNo);
    } else if (router->settings->autorouter != nullptr &&
               router->settings->autorouter->planeAsPriority.has_value() &&
               *router->settings->autorouter->planeAsPriority) {
      // plane_as_priority: the plane pads drop into their planes before the signals take the
      // room around them (later passes already route plane items first).
      jrt::List<Item*>* planeItems = new jrt::ArrayList<Item*>();
      BatchAutorouter* r = router;
      autorouteItemList->removeIf(
          [r, planeItems](Item* item) { return r->isPlaneItem(item, r->board) && planeItems->add(item); });
      // Java: autorouteItemList.addAll(0, planeItems)
      int32_t insertAt = 0;
      for (Item* planeItem : *planeItems) {
        autorouteItemList->add(insertAt++, planeItem);
      }
    }
    priorityItemsFirst(autorouteItemList);

    // freerouting.net: the balls of ball grid parts first in the first pass, ring by ring
    // (router.autorouter.ball_escape_order); a retry plan still goes ahead of them.
    if (passNo == 1 && router->getBallEscapeOrder() != nullptr) {
      int32_t moved = router->getBallEscapeOrder()->moveToFront(autorouteItemList);
      if (router->job != nullptr) {
        router->job->logDebug("Pass #1: the ball escape order puts " + jrt::str(moved) +
                              " ball(s) first.");
      }
    }

    int64_t initialProgressStatisticsStart =
        BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    // The optimizer routes each candidate with a listener-less router: no one reads these.
    router->progressStatistics = router->hasBoardStatisticsReaders()
                                     ? new BoardStatistics(router->board, std::nullopt, false)
                                     : nullptr;
    router->progressItemsSinceStatistics = 0;
    BoardStatistics* stats = router->progressStatistics;
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->profileBoardStatisticsNanos += jrt::System::nanoTime() - initialProgressStatisticsStart;
    }

    int32_t itemsToGoCount = autorouteItemList->size();
    RouterCounters* routerCounters = new RouterCounters();
    routerCounters->phase = std::string("autoroute");
    routerCounters->passCount = passNo;
    routerCounters->queuedToBeRoutedCount = itemsToGoCount;
    routerCounters->skippedCount = 0;
    routerCounters->rippedCount = 0;
    routerCounters->failedToBeRoutedCount = 0;
    routerCounters->routedCount = 0;

    int64_t statisticsStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    // The ledger keeps these counts up to date; a full DRC scan would count every net again.
    routerCounters->incompleteCount = router->calculateIncompleteCount(router->board);
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->profileIntermediateStatisticsNanos += jrt::System::nanoTime() - statisticsStart;
    }

    logIncompleteDetails(passNo, itemsToGoCount, routerCounters);
    router->fireBoardUpdatedEvent(stats, routerCounters, router->board);

    int32_t rippedItemCount = 0;
    int32_t notRouted = 0;
    int32_t routed = 0;
    int32_t skipped = 0;
    jrt::Map<int32_t, IntBox*>* netCandidateBounds = buildPaddedNetBounds(autorouteItemList);
    jrt::Map<AutoroutePassRunner_FailedConnectionPair*, IntBox*>* zeroRipupFailedPairs =
        new jrt::LinkedHashMap<AutoroutePassRunner_FailedConnectionPair*, IntBox*>();
    jrt::Set<int32_t>* keptAwayNetsAttemptedInPass = new jrt::TreeSet<int32_t>();
    jrt::Map<int32_t, AutoroutePassRunner_NetPassProfile*>* netProfiles =
        BatchAutorouter::isBenchmarkProfileEnabled()
            ? new jrt::LinkedHashMap<int32_t, AutoroutePassRunner_NetPassProfile*>()
            : nullptr;
    jrt::List<AutoroutePassRunner_ItemPassProfile*>* slowItemProfiles =
        BatchAutorouter::isBenchmarkProfileEnabled()
            ? new jrt::ArrayList<AutoroutePassRunner_ItemPassProfile*>()
            : nullptr;
    for (Item* currentItem : *autorouteItemList) {
      if (router->thread->isStopAutoRouterRequested()) {
        break;
      }

      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        if (router->thread->isStopAutoRouterRequested()) {
          break;
        }

        if (router->settings->autorouter->maxItems.has_value() &&
            *router->settings->autorouter->maxItems > 0 &&
            router->totalItemsRouted >= *router->settings->autorouter->maxItems) {
          router->job->logInfo("Max items limit reached (" +
                               jrt::str(*router->settings->autorouter->maxItems) +
                               "). Stopping auto-router.");
          router->thread->requestStop();
          break;
        }
        router->totalItemsRouted++;
        router->board->startMarkingChangedArea();

        jrt::TreeSet<Item*>* rippedItemList = new jrt::TreeSet<Item*>();
        jrt::Map<Item*, int32_t>* rippedItemCosts = new jrt::LinkedHashMap<Item*, int32_t>();
        const int32_t netItemsBefore =
            router->board->getConnectableItems(currentItem->getNetNumber(i))->size();
        if (BatchAutorouter::isBenchmarkProfileEnabled()) {
          router->profileRouteItemCount++;
          Net* routeNet = router->board->rules->nets->get(currentItem->getNetNumber(i));
          if (routeNet != nullptr && routeNet->containsPlane()) {
            router->profilePlaneItemCount++;
          }
        }

        int64_t routeItemStart = BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
        PerformanceProfiler::start("autoroute_item");
        int32_t currentNetNo = currentItem->getNetNumber(i);
        Net* currentRouteNet = router->board->rules->nets->get(currentNetNo);
        AutoroutePassRunner_FailedConnectionPair* failedPairKey = nullptr;
        if (currentRouteNet != nullptr && !currentRouteNet->containsPlane()) {
          failedPairKey = AutoroutePassRunner_FailedConnectionPair::of(currentItem, currentNetNo);
        }
        AutorouteAttemptResult* autorouterResult;
        if (failedPairKey != nullptr && zeroRipupFailedPairs->containsKey(failedPairKey)) {
          autorouterResult = new AutorouteAttemptResult(
              AutorouteAttemptState::FAILED,
              "Skipped reverse attempt in same pass (pair already failed with 0 ripups).");
        } else {
          autorouterResult = router->autorouteItem(
              currentItem, currentNetNo, rippedItemList, rippedItemCosts, passNo);
        }
        PerformanceProfiler::end("autoroute_item");
        if (!rippedItemList->isEmpty()) {
          invalidateZeroRipupPairs(zeroRipupFailedPairs, rippedItemList);
        } else if (failedPairKey != nullptr &&
                   autorouterResult->state == AutorouteAttemptState::FAILED) {
          zeroRipupFailedPairs->put(failedPairKey, netCandidateBounds->get(currentNetNo));
        }
        if (BatchAutorouter::isBenchmarkProfileEnabled()) {
          int64_t itemElapsedNanos = jrt::System::nanoTime() - routeItemStart;
          router->profileAutorouteItemNanos += itemElapsedNanos;
          int32_t netNo = currentItem->getNetNumber(i);
          Net* routeNet = router->board->rules->nets->get(netNo);
          std::string netName = routeNet != nullptr ? routeNet->name : "net#" + jrt::str(netNo);
          AutoroutePassRunner_NetPassProfile* np = netProfiles->computeIfAbsent(
              netNo, [netNo, netName](int32_t) {
                return new AutoroutePassRunner_NetPassProfile(netNo, netName);
              });
          np->totalNanos += itemElapsedNanos;
          np->attempts++;
          np->ripped += rippedItemList->size();
          if (autorouterResult->state == AutorouteAttemptState::ROUTED) {
            np->routed++;
          } else if (autorouterResult->state == AutorouteAttemptState::FAILED ||
                     autorouterResult->state == AutorouteAttemptState::INSERT_ERROR) {
            np->failed++;
          } else {
            np->skipped++;
          }
          slowItemProfiles->add(new AutoroutePassRunner_ItemPassProfile(
              itemElapsedNanos,
              netNo,
              netName,
              jrt::className(currentItem),
              currentItem->getId(),
              autorouterResult->state,
              rippedItemList->size()));
        }

        if (router->ripupMemory->isKeptAway(currentItem->getNetNumber(i))) {
          keptAwayNetsAttemptedInPass->add(currentItem->getNetNumber(i));
        }
        logRippedItems(currentItem, i, rippedItemList, rippedItemCosts);
        rememberRipups(currentItem->getNetNumber(i), rippedItemList, passNo);
        if (FRLogger::isTraceEnabled()) {
          logTraceRouteComparison(currentItem, i, autorouterResult, rippedItemList, netItemsBefore);
        }
        if (currentItem->getNetNumber(i) == 94) {
          logNet94Items();
        }

        if (autorouterResult->state == AutorouteAttemptState::ROUTED) {
          ++routed;
        } else if ((autorouterResult->state == AutorouteAttemptState::ALREADY_CONNECTED) ||
                   (autorouterResult->state == AutorouteAttemptState::NO_UNCONNECTED_NETS) ||
                   (autorouterResult->state == AutorouteAttemptState::CONNECTED_TO_PLANE)) {
          ++skipped;
        } else {
          Net* net = router->board->rules->nets->get(currentItem->getNetNumber(i));
          std::string netName =
              net != nullptr ? net->name : "net#" + jrt::str(currentItem->getNetNumber(i));
          router->board->failureLog->recordFailure(
              currentItem, passNo, autorouterResult->state, autorouterResult->details);
          router->job->logDebug("Autorouter " + autorouterResult->details);
          int32_t failureCount = router->board->failureLog->getFailureCount(currentItem);
          int32_t netNo = currentItem->getNetNumber(i);
          if (failureCount >= 2 && !designBlocked(router->board, currentItem, netNo)) {
            jrt::List<Item*>* tracesToRip = failedNetRipItems(router->board, currentItem, netNo);
            if (!tracesToRip->isEmpty()) {
              router->board->removeItems(tracesToRip);
              invalidateZeroRipupPairs(zeroRipupFailedPairs, tracesToRip);
            }
          }
          if (itemsToGoCount <= 5 || failureCount >= 3) {
            router->job->logDebug("Pass #" + jrt::str(passNo) + ": Failed to route " +
                                  jrt::className(currentItem) + " on net '" + netName + "' (" +
                                  jrt::str(itemsToGoCount) + " items remaining, " +
                                  jrt::str(failureCount) + " failures). State: " +
                                  AutorouteAttemptState_toString(autorouterResult->state));
          }
          ++notRouted;
        }
        --itemsToGoCount;
        rippedItemCount += rippedItemList->size();
        updateProgress(routerCounters, itemsToGoCount, rippedItemCount, notRouted, routed, skipped);
      }
    }

    logTailRemoval(passNo, router->calculateIncompleteCount(router->board), true);
    if (router->removeUnconnectedVias) {
      router->removeTails(Item_StopConnectionOption::NONE);
    } else {
      router->removeTails(Item_StopConnectionOption::FANOUT_VIA);
    }
    logTailRemoval(passNo, router->calculateIncompleteCount(router->board), false);

    int64_t finalStatisticsStart =
        BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    int64_t finalBoardStatisticsStart =
        BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    BoardStatistics* boardStatistics =
        router->hasBoardStatisticsReaders() ? router->board->getStatistics() : nullptr;
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->profileBoardStatisticsNanos += jrt::System::nanoTime() - finalBoardStatisticsStart;
      router->profileIntermediateStatisticsNanos += jrt::System::nanoTime() - finalStatisticsStart;
    }
    routerCounters->passCount = passNo;
    routerCounters->queuedToBeRoutedCount = itemsToGoCount;
    routerCounters->skippedCount = skipped;
    routerCounters->rippedCount = rippedItemCount;
    routerCounters->failedToBeRoutedCount = notRouted;
    routerCounters->routedCount = routed;
    jrt::Set<int32_t>* currentIncompleteNets = new jrt::TreeSet<int32_t>();
    routerCounters->incompleteCount =
        router->calculateIncompleteCount(router->board, currentIncompleteNets);
    if (!router->thread->isStopAutoRouterRequested()) {
      resolveKeptAwayOutcomes(keptAwayNetsAttemptedInPass, currentIncompleteNets, routerCounters,
                              passNo);
    }
    router->fireBoardUpdatedEvent(boardStatistics, routerCounters, router->board);

    if (this->previousIncompleteCount >= 0 &&
        routerCounters->incompleteCount.value() >= this->previousIncompleteCount) {
      this->stagnationCount++;
    } else {
      this->stagnationCount = 0;
    }
    this->previousIncompleteCount = routerCounters->incompleteCount.value();
    this->previousIncompleteNets = currentIncompleteNets;

    int64_t passDuration = jrt::System::currentTimeMillis() - passStartTime;
    int32_t currentRipupCost = router->startRipupCosts * passNo;
    PerformanceProfiler::recordPass(passNo, routerCounters->incompleteCount.value(), passDuration,
                                    currentRipupCost);
    router->logBenchmarkProfile(passNo);
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      logPassBreakdownProfile(passNo, netProfiles, slowItemProfiles);
    }

    router->airLine = nullptr;
    return routed > 0 || notRouted > 0;
  } catch (const jrt::Exception& e) {
    router->job->logError("Something went wrong during the auto-routing", &e);
    router->airLine = nullptr;
    return false;
  }
}

jrt::Map<int32_t, IntBox*>* AutoroutePassRunner::buildPaddedNetBounds(
    jrt::List<Item*>* autorouteItemList) {
  jrt::Map<int32_t, IntBox*>* bounds = new jrt::LinkedHashMap<int32_t, IntBox*>();
  for (Item* item : *autorouteItemList) {
    IntBox* box = item->boundingBox();
    for (int32_t i = 0; i < item->netCount(); i++) {
      int32_t netNo = item->getNetNumber(i);
      bounds->merge(netNo, box, [](IntBox* a, IntBox* b) { return a->union_(b); });
    }
  }
  for (auto* entry : *bounds->entrySet()) {
    IntBox* box = entry->getValue();
    int32_t pad = jrt::Math::max(100000, jrt::Math::max(box->width(), box->height()) / 2);
    entry->setValue(box->offset(pad));
  }
  return bounds;
}

void AutoroutePassRunner::invalidateZeroRipupPairs(
    jrt::Map<AutoroutePassRunner_FailedConnectionPair*, IntBox*>* zeroRipupFailedPairs,
    jrt::Collection<Item*>* rippedItems) {
  if (zeroRipupFailedPairs->isEmpty() || rippedItems->isEmpty()) {
    return;
  }
  // PORT NOTE: entrySet().removeIf(...) rewritten as explicit removals on the backing map
  // (jrt entrySet() is a snapshot).
  for (auto* entry : *zeroRipupFailedPairs->entrySet()) {
    bool remove = false;
    IntBox* region = entry->getValue();
    if (region == nullptr) {
      remove = true;
    } else {
      for (Item* ripped : *rippedItems) {
        if (ripped->boundingBox()->intersects(region)) {
          remove = true;
          break;
        }
      }
    }
    if (remove) {
      zeroRipupFailedPairs->remove(entry->getKey());
    }
  }
}

int32_t AutoroutePassRunner::componentPinCount(RoutingBoard* board, Item* item) {
  int32_t componentId = item->getComponentId();
  if (componentId <= 0 || board == nullptr || board->components == nullptr) {
    return 0;
  }
  Component* component = board->components->get(componentId);
  if (component == nullptr || component->getPackage() == nullptr) {
    return 0;
  }
  return component->getPackage()->pinCount();
}

jrt::Comparator<Item*> AutoroutePassRunner::deterministicItemComparator(RoutingBoard* board) {
  jrt::Comparator<Item*> byNet = jrt::comparingInt<Item*>(
      [](Item* item) { return item->netCount() > 0 ? item->getNetNumber(0) : 0; });
  jrt::Comparator<Item*> byPinCountDesc = jrt::reverseOrder<Item*>(
      jrt::comparingInt<Item*>([board](Item* item) { return componentPinCount(board, item); }));
  jrt::Comparator<Item*> byId = jrt::comparingInt<Item*>([](Item* item) { return item->getId(); });
  return jrt::thenComparing<Item*>(jrt::thenComparing<Item*>(byNet, byPinCountDesc), byId);
}

void AutoroutePassRunner::reorderSingleThreadItems(jrt::List<Item*>* autorouteItemList,
                                                   int32_t passNo) {
  jrt::List<Item*>* planeItems = new jrt::ArrayList<Item*>();
  jrt::List<Item*>* signalItems = new jrt::ArrayList<Item*>();
  for (Item* item : *autorouteItemList) {
    if (router->isPlaneItem(item, router->board)) {
      planeItems->add(item);
    } else {
      signalItems->add(item);
    }
  }

  if (signalItems->isEmpty()) {
    return;
  }

  jrt::Comparator<Item*> itemComparator = deterministicItemComparator(router->board);

  std::optional<int32_t> orderSeed = router->settings->autorouter != nullptr
                                         ? router->settings->autorouter->itemOrderSeed
                                         : std::nullopt;
  if (this->stagnationCount >= 2 || orderSeed.has_value()) {
    signalItems->sort(itemComparator);
    int64_t seed = static_cast<int64_t>(passNo) * 1000003LL + router->board->rules->nets->maxNetNumber();
    if (orderSeed.has_value()) {
      seed = seed * 31LL + *orderSeed;
    }
    jrt::Collections::shuffle(signalItems, new jrt::Random(seed));
    router->job->logDebug("Pass #" + jrt::str(passNo) + ": applied deterministic permutation to " +
                          jrt::str(signalItems->size()) + " signal items (stagnation count: " +
                          jrt::str(this->stagnationCount) + ").");
  } else if (!this->previousIncompleteNets->isEmpty()) {
    jrt::List<Item*>* persistentItems = new jrt::ArrayList<Item*>();
    jrt::List<Item*>* otherItems = new jrt::ArrayList<Item*>();
    for (Item* item : *signalItems) {
      bool isPersistent = false;
      for (int32_t i = 0; i < item->netCount(); i++) {
        if (this->previousIncompleteNets->contains(item->getNetNumber(i))) {
          isPersistent = true;
          break;
        }
      }
      if (isPersistent) {
        persistentItems->add(item);
      } else {
        otherItems->add(item);
      }
    }
    persistentItems->sort(itemComparator);
    otherItems->sort(itemComparator);
    signalItems->clear();
    signalItems->addAll(persistentItems);
    signalItems->addAll(otherItems);
  } else {
    signalItems->sort(itemComparator);
  }

  keptAwayNetsLast(signalItems);
  autorouteItemList->clear();
  autorouteItemList->addAll(planeItems);
  autorouteItemList->addAll(signalItems);
}

void AutoroutePassRunner::priorityItemsFirst(jrt::List<Item*>* autorouteItemList) {
  jrt::List<Item*>* planeItems = new jrt::ArrayList<Item*>();
  jrt::List<Item*>* priorityItems = new jrt::ArrayList<Item*>();
  jrt::List<Item*>* otherItems = new jrt::ArrayList<Item*>();
  for (Item* item : *autorouteItemList) {
    if (router->isPlaneItem(item, router->board)) {
      planeItems->add(item);
    } else if (isRoutedFirst(item)) {
      priorityItems->add(item);
    } else {
      otherItems->add(item);
    }
  }
  if (priorityItems->isEmpty()) {
    return;
  }
  autorouteItemList->clear();
  autorouteItemList->addAll(planeItems);
  autorouteItemList->addAll(priorityItems);
  autorouteItemList->addAll(otherItems);
}

bool AutoroutePassRunner::isRoutedFirst(Item* item) {
  for (int32_t i = 0; i < item->netCount(); i++) {
    Net* net = router->board->rules->nets->get(item->getNetNumber(i));
    if (net != nullptr && net->isRoutedFirst()) {
      return true;
    }
  }
  return false;
}

void AutoroutePassRunner::keptAwayNetsLast(jrt::List<Item*>* signalItems) {
  if (router->ripupMemory->activeCount() == 0) {
    return;
  }
  jrt::List<Item*>* keptAway = new jrt::ArrayList<Item*>();
  RipupMemory* memory = router->ripupMemory;
  signalItems->removeIf([memory, keptAway](Item* item) {
    for (int32_t i = 0; i < item->netCount(); i++) {
      if (memory->isKeptAway(item->getNetNumber(i))) {
        keptAway->add(item);
        return true;
      }
    }
    return false;
  });
  signalItems->addAll(keptAway);
}

void AutoroutePassRunner::rememberRipups(int32_t netNo, jrt::TreeSet<Item*>* rippedItemList,
                                         int32_t passNo) {
  if (!rippedItemList->isEmpty()) {
    jrt::Set<int32_t>* started = router->ripupMemory->recordRipup(netNo, rippedItemList, passNo);
    if (!started->isEmpty()) {
      logDebug(passNo, "net '" + netName(netNo) + "' moved " + netNames(started) +
                           " aside a second time; they will not rip it back.");
    }
  }
}

void AutoroutePassRunner::resolveKeptAwayOutcomes(jrt::Set<int32_t>* keptAwayNetsAttempted,
                                                  jrt::Set<int32_t>* incompleteNets,
                                                  RouterCounters* routerCounters,
                                                  int32_t passNo) {
  if (keptAwayNetsAttempted->isEmpty()) {
    return;
  }
  int32_t itemsRemovedByFlip = 0;
  for (int32_t netNo : *keptAwayNetsAttempted) {
    jrt::Set<int32_t>* keptAwayFrom = router->ripupMemory->netsNotToRip(netNo);
    if (keptAwayFrom->isEmpty()) {
      continue;
    }
    bool routed = !incompleteNets->contains(netNo);
    for (int32_t keptFromNet : *(new jrt::ArrayList<int32_t>(keptAwayFrom))) {
      RipupMemory_Outcome outcome =
          router->ripupMemory->recordOutcome(netNo, keptFromNet, routed, passNo);
      std::string message;
      switch (outcome) {
        case RipupMemory_Outcome::RESOLVED:
          message = "routed without ripping '" + netName(keptFromNet) + "'.";
          break;
        case RipupMemory_Outcome::FLIPPED: {
          int32_t ripped = ripInCorridors(keptFromNet, netNo);
          itemsRemovedByFlip += ripped;
          message = "failed again without ripping '" + netName(keptFromNet) + "'; " +
                    jrt::str(ripped) + " item(s) of it removed from its way, '" +
                    netName(keptFromNet) + "' is now kept away from it.";
          break;
        }
        case RipupMemory_Outcome::RETIRED:
          message = "failed again while '" + netName(keptFromNet) +
                    "' was kept away; the pair is released.";
          break;
        case RipupMemory_Outcome::NONE:
          message = "failed without ripping '" + netName(keptFromNet) + "'.";
          break;
      }
      logDebug(passNo, "net '" + netName(netNo) + "' " + message);
    }
  }
  if (itemsRemovedByFlip > 0) {
    routerCounters->incompleteCount = router->calculateIncompleteCount(router->board);
  }
}

int32_t AutoroutePassRunner::ripInCorridors(int32_t netNo, int32_t failedNetNo) {
  jrt::Array<AirLine*>* airlines = (new DesignRulesChecker(router->board, nullptr))->getAllAirlines();
  jrt::Set<Item*>* inTheWay = new jrt::TreeSet<Item*>();
  for (int32_t ai = 0; ai < airlines->length; ai++) {
    AirLine* airline = (*airlines)[ai];
    if (airline == nullptr || airline->net == nullptr || airline->net->netNumber != failedNetNo) {
      continue;
    }
    IntBox* corridor = LastMileBlockerRipup::corridor(airline->fromCorner, airline->toCorner);
    for (SearchTreeObject* object : *router->board->overlappingObjects(corridor, -1)) {
      if (auto* item = jrt::fast_cast<Item>(object)) {
        if (item->containsNet(netNo) && LastMileBlockerRipup::isRemovableBlocker(item, failedNetNo)) {
          inTheWay->add(item);
        }
      }
    }
  }
  if (!inTheWay->isEmpty()) {
    router->board->removeItems(new jrt::ArrayList<Item*>(inTheWay));
  }
  return inTheWay->size();
}

void AutoroutePassRunner::logDebug(int32_t passNo, const std::string& message) {
  if (router->job != nullptr) {
    router->job->logDebug("Pass #" + jrt::str(passNo) + ": " + message);
  }
}

std::string AutoroutePassRunner::netName(int32_t netNo) {
  Net* net = router->board->rules->nets->get(netNo);
  return net != nullptr ? net->name : "net#" + jrt::str(netNo);
}

std::string AutoroutePassRunner::netNames(jrt::Set<int32_t>* netNumbers) {
  std::string names;
  for (int32_t netNo : *netNumbers) {
    if (names.length() > 0) {
      names += ", ";
    }
    names += '\'';
    names += netName(netNo);
    names += '\'';
  }
  return names;
}

void AutoroutePassRunner::resetAntiOscillationState() {
  this->previousIncompleteNets = jrt::Collections::emptySet<int32_t>();
  this->previousIncompleteCount = -1;
  this->stagnationCount = 0;
}

void AutoroutePassRunner::logIncompleteDetails(int32_t passNo, int32_t itemsToGoCount,
                                               RouterCounters* counters) {
  if (counters->incompleteCount.value() <= 0) {
    return;
  }
  router->job->logDebug("Pass #" + jrt::str(passNo) + ": " + jrt::str(counters->incompleteCount) +
                        " incompletes across " + jrt::str(itemsToGoCount) + " items to route");
  for (int32_t netNumber = 1; netNumber <= router->board->rules->nets->maxNetNumber(); netNumber++) {
    int32_t netIncompletes = router->board->routingLedger()->incompleteCount(netNumber);
    if (netIncompletes > 0) {
      Net* net = router->board->rules->nets->get(netNumber);
      std::string netName = net != nullptr ? net->name : "net#" + jrt::str(netNumber);
      router->job->logDebug("  Net '" + netName + "' has " + jrt::str(netIncompletes) +
                            " incomplete(s)");
    }
  }
}

void AutoroutePassRunner::logRippedItems(Item* currentItem,
                                         int32_t netIndex,
                                         jrt::TreeSet<Item*>* rippedItemList,
                                         jrt::Map<Item*, int32_t>* rippedItemCosts) {
  for (Item* rippedItem : *rippedItemList) {
    std::string rippedNets;
    for (int32_t netIx = 0; netIx < rippedItem->netCount(); netIx++) {
      if (netIx > 0) {
        rippedNets += '|';
      }
      rippedNets += jrt::str(rippedItem->getNetNumber(netIx));
    }
    int32_t ripupCost = rippedItemCosts->getOrDefault(rippedItem, -1);
    FRLogger::trace(
        "BatchAutorouter.autoroute_pass",
        "compare_trace_ripped_item",
        "source_item=" + jrt::str(currentItem->getId()) +
            ", source_net=" + jrt::str(currentItem->getNetNumber(netIndex)) +
            ", ripped_id=" + jrt::str(rippedItem->getId()) +
            ", ripped_type=" + jrt::className(rippedItem) +
            ", ripped_net_count=" + jrt::str(rippedItem->netCount()) +
            ", ripped_nets=" + rippedNets + ", ripupCost=" + jrt::str(ripupCost),
        "Net #" + jrt::str(currentItem->getNetNumber(netIndex)) + ",Item #" +
            jrt::str(currentItem->getId()),
        BatchAutorouter::getImpactedPoints(rippedItem));
  }
}

void AutoroutePassRunner::logTraceRouteComparison(Item* currentItem,
                                                  int32_t netIndex,
                                                  AutorouteAttemptResult* result,
                                                  jrt::TreeSet<Item*>* rippedItemList,
                                                  int32_t netItemsBefore) {
  DesignRulesChecker* innerDrc = new DesignRulesChecker(router->board, nullptr);
  innerDrc->calculateAllIncompletes();
  int32_t tempIncomp = innerDrc->getIncompleteCount();
  int32_t tempNetIncomp = innerDrc->getIncompleteCount(currentItem->getNetNumber(netIndex));
  int32_t netItemsAfter =
      router->board->getConnectableItems(currentItem->getNetNumber(netIndex))->size();
  int32_t maxItemId = router->board->communication->idGenerator->maxGeneratedId();
  FRLogger::trace(
      "BatchAutorouter.autoroute_pass",
      "compare_trace_route_item",
      "Routing " + jrt::className(currentItem) +
          " -> result=" + AutorouteAttemptState_toString(result->state) +
          ", details=" + result->details + ", incompletes=" + jrt::str(tempIncomp) +
          ", netIncomplete=" + jrt::str(tempNetIncomp) +
          ", ripped=" + jrt::str(rippedItemList->size()) +
          ", netItems=" + jrt::str(netItemsBefore) + "->" + jrt::str(netItemsAfter) +
          ", maxItemId=" + jrt::str(maxItemId),
      "Net #" + jrt::str(currentItem->getNetNumber(netIndex)) + ",Item #" +
          jrt::str(currentItem->getId()) + ",Type=" + jrt::className(currentItem),
      BatchAutorouter::getImpactedPoints(currentItem));
}

void AutoroutePassRunner::logNet94Items() {
  FRLogger::trace("BatchAutorouter.autoroute_pass",
                  "compare_trace_dump_net_items",
                  "Dump net 94 items",
                  "Net #94",
                  new jrt::Array<Point*>(0));
  for (Item* netItem : *router->board->getConnectableItems(94)) {
    if (auto* trace = jrt::fast_cast<Trace>(netItem)) {
      FRLogger::trace(
          "BatchAutorouter.autoroute_pass",
          "compare_trace_dump_net_item",
          "Trace layer=" + jrt::str(trace->getLayer()) + " corners=" +
              jrt::str(trace->firstCorner()) + " to " + jrt::str(trace->lastCorner()),
          "Net #94,Item #" + jrt::str(trace->getId()) + ",Type=Trace",
          jrt::Array<Point*>::of({trace->firstCorner(), trace->lastCorner()}));
    } else if (auto* via = jrt::fast_cast<Via>(netItem)) {
      FRLogger::trace("BatchAutorouter.autoroute_pass",
                      "compare_trace_dump_net_item",
                      "Via center=" + jrt::str(via->getCenter()),
                      "Net #94,Item #" + jrt::str(via->getId()) + ",Type=Via",
                      jrt::Array<Point*>::of({via->getCenter()}));
    } else if (auto* pin = jrt::fast_cast<Pin>(netItem)) {
      FRLogger::trace("BatchAutorouter.autoroute_pass",
                      "compare_trace_dump_net_item",
                      "Pin center=" + jrt::str(pin->getCenter()) + " name=" + jrt::str(pin->name()) +
                          " comp=" + jrt::str(pin->componentName()),
                      "Net #94,Item #" + jrt::str(pin->getId()) + ",Type=Pin",
                      jrt::Array<Point*>::of({pin->getCenter()}));
    } else {
      FRLogger::trace("BatchAutorouter.autoroute_pass",
                      "compare_trace_dump_net_item",
                      "Item " + jrt::className(netItem),
                      "Net #94,Item #" + jrt::str(netItem->getId()) +
                          ",Type=" + jrt::className(netItem),
                      BatchAutorouter::getImpactedPoints(netItem));
    }
  }
}

void AutoroutePassRunner::updateProgress(RouterCounters* counters,
                                         int32_t itemsToGoCount,
                                         int32_t rippedItemCount,
                                         int32_t notRouted,
                                         int32_t routed,
                                         int32_t skipped) {
  router->progressItemsSinceStatistics++;
  if (router->progressItemsSinceStatistics >= BatchAutorouter::PROGRESS_STATISTICS_ITEM_INTERVAL &&
      router->hasBoardStatisticsReaders()) {
    int64_t progressStatisticsStart =
        BatchAutorouter::isBenchmarkProfileEnabled() ? jrt::System::nanoTime() : 0;
    router->progressStatistics = new BoardStatistics(router->board, std::nullopt, false);
    router->progressItemsSinceStatistics = 0;
    if (BatchAutorouter::isBenchmarkProfileEnabled()) {
      router->profileBoardStatisticsNanos += jrt::System::nanoTime() - progressStatisticsStart;
    }
  }

  if (router->shouldFireBoardUpdate()) {
    counters->queuedToBeRoutedCount = itemsToGoCount;
    counters->skippedCount = skipped;
    counters->rippedCount = rippedItemCount;
    counters->failedToBeRoutedCount = notRouted;
    counters->routedCount = routed;
    counters->incompleteCount = router->calculateIncompleteCount(router->board);
    router->fireBoardUpdatedEvent(router->progressStatistics, counters, router->board);
  }
}

void AutoroutePassRunner::dropDesignBlockedItems(jrt::List<Item*>* items) {
  int32_t before = items->size();
  jrt::Set<int32_t>* ruleBlocked = ruleBlockedNets(router->board);
  RoutingBoard* board = router->board;
  items->removeIf([board, ruleBlocked](Item* item) {
    return (board->failureLog->getFailureCount(item) > 0 ||
            (ruleBlocked != nullptr && item->netCount() == 1 &&
             ruleBlocked->contains(item->getNetNumber(0)))) &&
           designBlocked(board, item, item->getNetNumber(0));
  });
  if (items->size() < before && router->job != nullptr) {
    router->job->logDebug("Not retrying " + jrt::str(before - items->size()) +
                          " item(s) whose open connections the design blocks (pins on foreign "
                          "copper).");
  }
}

jrt::Set<int32_t>* AutoroutePassRunner::ruleBlockedNets(RoutingBoard* board) {
  if (board->ruleBlockedPins == nullptr || board->ruleBlockedPins->isEmpty()) {
    return nullptr;
  }
  jrt::Set<int32_t>* nets = new jrt::HashSet<int32_t>();
  for (Pin* pin : *board->getPins()) {
    if (pin->netCount() == 1 && board->ruleBlockedPins->contains(pin->getId())) {
      nets->add(pin->getNetNumber(0));
    }
  }
  return nets->isEmpty() ? nullptr : nets;
}

bool AutoroutePassRunner::designBlocked(RoutingBoard* board, Item* item, int32_t netNumber) {
  return item->netCount() == 1 && DesignBlocks::connectionsBlocked(board, item, netNumber);
}

jrt::List<Item*>* AutoroutePassRunner::failedNetRipItems(RoutingBoard* board, Item* failedItem,
                                                        int32_t netNumber) {
  jrt::Collection<Item*>* items = board->getConnectableItems(netNumber);
  int64_t pinCount = 0;
  for (Item* i : *items) {
    if (jrt::fast_cast<Pin>(i) != nullptr) {
      pinCount++;
    }
  }
  bool branched = pinCount > 2;
  int32_t offset = static_cast<int32_t>(jrt::Math::round(board->communication->coordinateTransform->dsnToBoard(
      Unit_scale(1500, Unit::UM, board->communication->unit))));
  IntBox* region = failedItem->boundingBox()->offset(offset);
  jrt::List<Item*>* result = new jrt::ArrayList<Item*>();
  for (Item* i : *items) {
    if ((jrt::fast_cast<Trace>(i) != nullptr || jrt::fast_cast<Via>(i) != nullptr) &&
        !i->isUserFixed() && i->netCount() == 1 &&
        (!branched || region->intersects(i->boundingBox()))) {
      result->add(i);
    }
  }
  return result;
}

void AutoroutePassRunner::logTailRemoval(int32_t passNo, int32_t incompleteCount, bool before) {
  FRLogger::trace("BatchAutorouter.autoroute_pass",
                  "compare_trace_remove_tails",
                  std::string("Incompletes ") + (before ? "before" : "after") +
                      " remove_tails=" + jrt::str(incompleteCount),
                  "Autorouter pass #" + jrt::str(passNo),
                  new jrt::Array<Point*>(0));
}

void AutoroutePassRunner::logPassBreakdownProfile(
    int32_t passNo,
    jrt::Map<int32_t, AutoroutePassRunner_NetPassProfile*>* netProfiles,
    jrt::List<AutoroutePassRunner_ItemPassProfile*>* slowItemProfiles) {
  if (netProfiles != nullptr && !netProfiles->isEmpty()) {
    jrt::List<AutoroutePassRunner_NetPassProfile*>* sortedNets =
        new jrt::ArrayList<AutoroutePassRunner_NetPassProfile*>(netProfiles->values());
    sortedNets->sort(jrt::reverseOrder<AutoroutePassRunner_NetPassProfile*>(
        jrt::comparingLong<AutoroutePassRunner_NetPassProfile*>(
            [](AutoroutePassRunner_NetPassProfile* n) { return n->totalNanos; })));
    std::string sb;
    sb += "BENCHMARK_PROFILE_NETS pass=";
    sb += jrt::str(passNo);
    int32_t limit = jrt::Math::min(10, sortedNets->size());
    for (int32_t idx = 0; idx < limit; idx++) {
      AutoroutePassRunner_NetPassProfile* np = sortedNets->get(idx);
      sb += jrt::format(" | #%d net=%s(%d) ms=%.1f att=%d ok=%d fail=%d skip=%d rip=%d",
                        idx + 1,
                        np->netName.c_str(),
                        np->netNo,
                        np->totalNanos / 1000000.0,
                        np->attempts,
                        np->routed,
                        np->failed,
                        np->skipped,
                        np->ripped);
    }
    FRLogger::info(sb);
  }
  if (slowItemProfiles != nullptr && !slowItemProfiles->isEmpty()) {
    slowItemProfiles->sort(jrt::reverseOrder<AutoroutePassRunner_ItemPassProfile*>(
        jrt::comparingLong<AutoroutePassRunner_ItemPassProfile*>(
            [](AutoroutePassRunner_ItemPassProfile* p) { return p->elapsedNanos; })));
    std::string sb;
    sb += "BENCHMARK_PROFILE_SLOW_ITEMS pass=";
    sb += jrt::str(passNo);
    int32_t limit = jrt::Math::min(10, slowItemProfiles->size());
    for (int32_t idx = 0; idx < limit; idx++) {
      AutoroutePassRunner_ItemPassProfile* ip = slowItemProfiles->get(idx);
      std::string stateText = AutorouteAttemptState_toString(ip->state);
      sb += jrt::format(" | #%d %s#%d net=%s(%d) ms=%.1f state=%s rip=%d",
                        idx + 1,
                        ip->itemType.c_str(),
                        ip->itemId,
                        ip->netName.c_str(),
                        ip->netNo,
                        ip->elapsedNanos / 1000000.0,
                        stateText.c_str(),
                        ip->ripped);
    }
    FRLogger::info(sb);
  }
}

}  // namespace fr
