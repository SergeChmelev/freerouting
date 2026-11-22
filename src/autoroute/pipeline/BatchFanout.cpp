#include "autoroute/pipeline/BatchFanout.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/fanout/BgaEscapeCommitter.h"
#include "autoroute/fanout/BgaEscapeDescriptor.h"
#include "autoroute/fanout/BgaEscapeSolver.h"
#include "autoroute/fanout/BgaFootprintDetector.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/DirectPinRouting.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/ProgressThrottler.h"
#include "core/StoppableThread.h"
#include "core/library/Padstack.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsTraces.h"
#include "core/scoring/BoardStatisticsVias.h"
#include "datastructures/IdGenerator.h"
#include "datastructures/TimeLimit.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"
#include "settings/AutorouterSettings.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"
#include "util/TextManager.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------------------------

BatchFanout_EscapeStatistics* BatchFanout_EscapeStatistics::fromBoardStatistics(BoardStatistics* stats) {
  double percentage =
      stats->fanout->totalSmdPins > 0 ? (stats->fanout->escapedCount * 100.0) / stats->fanout->totalSmdPins : 0.0;
  return new BatchFanout_EscapeStatistics(stats->fanout->totalSmdPins, stats->fanout->escapedCount, percentage);
}

bool BatchFanout_FanoutPassStatus::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = dynamic_cast<BatchFanout_FanoutPassStatus*>(o);
  if (other == nullptr) return false;
  return passNo_ == other->passNo_ && ripupCosts_ == other->ripupCosts_ && totalPins_ == other->totalPins_ &&
         pinsToGo_ == other->pinsToGo_ && routedCount_ == other->routedCount_ &&
         notRoutedCount_ == other->notRoutedCount_ && insertErrorCount_ == other->insertErrorCount_ &&
         extraViasThisPass_ == other->extraViasThisPass_ && extraViasTotal_ == other->extraViasTotal_ &&
         passDurationMillis_ == other->passDurationMillis_ &&
         jrt::equalsOf(boardStatistics_, other->boardStatistics_) && passCompleted_ == other->passCompleted_ &&
         jrt::equalsOf(escapeStatistics_, other->escapeStatistics_);
}

int32_t BatchFanout_FanoutPassStatus::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(passNo_));
  h = jrt::hashCombine(h, jrt::hashOf(ripupCosts_));
  h = jrt::hashCombine(h, jrt::hashOf(totalPins_));
  h = jrt::hashCombine(h, jrt::hashOf(pinsToGo_));
  h = jrt::hashCombine(h, jrt::hashOf(routedCount_));
  h = jrt::hashCombine(h, jrt::hashOf(notRoutedCount_));
  h = jrt::hashCombine(h, jrt::hashOf(insertErrorCount_));
  h = jrt::hashCombine(h, jrt::hashOf(extraViasThisPass_));
  h = jrt::hashCombine(h, jrt::hashOf(extraViasTotal_));
  h = jrt::hashCombine(h, jrt::hashOf(passDurationMillis_));
  h = jrt::hashCombine(h, jrt::hashOf(boardStatistics_));
  h = jrt::hashCombine(h, jrt::hashOf(passCompleted_));
  h = jrt::hashCombine(h, jrt::hashOf(escapeStatistics_));
  return h;
}

std::string BatchFanout_FanoutPassStatus::toString() {
  return "FanoutPassStatus[passNo=" + jrt::str(passNo_) + ", ripupCosts=" + jrt::str(ripupCosts_) +
         ", totalPins=" + jrt::str(totalPins_) + ", pinsToGo=" + jrt::str(pinsToGo_) +
         ", routedCount=" + jrt::str(routedCount_) + ", notRoutedCount=" + jrt::str(notRoutedCount_) +
         ", insertErrorCount=" + jrt::str(insertErrorCount_) + ", extraViasThisPass=" + jrt::str(extraViasThisPass_) +
         ", extraViasTotal=" + jrt::str(extraViasTotal_) + ", passDurationMillis=" + jrt::str(passDurationMillis_) +
         ", boardStatistics=" + jrt::str(boardStatistics_) + ", passCompleted=" + jrt::str(passCompleted_) +
         ", escapeStatistics=" + jrt::str(escapeStatistics_) + "]";
}

bool BatchFanout_FanoutRunSummary::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = dynamic_cast<BatchFanout_FanoutRunSummary*>(o);
  if (other == nullptr) return false;
  return completedPassCount_ == other->completedPassCount_ && totalDurationMillis_ == other->totalDurationMillis_ &&
         jrt::equalsOf(escapeStatistics_, other->escapeStatistics_) && isTimedOut_ == other->isTimedOut_;
}

int32_t BatchFanout_FanoutRunSummary::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(completedPassCount_));
  h = jrt::hashCombine(h, jrt::hashOf(totalDurationMillis_));
  h = jrt::hashCombine(h, jrt::hashOf(escapeStatistics_));
  h = jrt::hashCombine(h, jrt::hashOf(isTimedOut_));
  return h;
}

std::string BatchFanout_FanoutRunSummary::toString() {
  return "FanoutRunSummary[completedPassCount=" + jrt::str(completedPassCount_) +
         ", totalDurationMillis=" + jrt::str(totalDurationMillis_) +
         ", escapeStatistics=" + jrt::str(escapeStatistics_) + ", isTimedOut=" + jrt::str(isTimedOut_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BatchFanout_Component / BatchFanout_Component_Pin
// ---------------------------------------------------------------------------------------------

BatchFanout_Component::BatchFanout_Component(Component* boardComponent, jrt::Collection<Pin*>* boardSmdPinList,
                                             const std::string& pinSortingOrder, RoutingBoard* routingBoard)
    : boardComponent(boardComponent), pinSortingOrder(pinSortingOrder) {
  // calculate the center of gravity of all SMD pins of this component.
  jrt::Collection<Pin*>* currentPinList = new jrt::LinkedList<Pin*>();
  int32_t componentId = boardComponent->id;
  for (Pin* currentBoardPin : *boardSmdPinList) {
    if (currentBoardPin->getComponentId() == componentId) {
      currentPinList->add(currentBoardPin);
    }
  }
  double x = 0;
  double y = 0;
  for (Pin* currentPin : *currentPinList) {
    FloatPoint* currentPoint = currentPin->getCenter()->toFloat();
    x += currentPoint->x;
    y += currentPoint->y;
  }
  this->smdPinCount = currentPinList->size();
  if (this->smdPinCount > 0) {
    x /= this->smdPinCount;
    y /= this->smdPinCount;
  }
  this->gravityCenterOfSmdPins = new FloatPoint(x, y);

  // calculate the sorted SMD pins of this component
  this->smdPins = new jrt::TreeSet<BatchFanout_Component_Pin*>();

  for (Pin* currentBoardPin : *currentPinList) {
    this->smdPins->add(new BatchFanout_Component_Pin(this, currentBoardPin, boardSmdPinList, routingBoard));
  }
}

int32_t BatchFanout_Component::compareTo(BatchFanout_Component* other) {
  int32_t compareValue = this->smdPinCount - other->smdPinCount;
  int32_t result;
  if (compareValue > 0) {
    result = -1;
  } else if (compareValue < 0) {
    result = 1;
  } else {
    result = this->boardComponent->id - other->boardComponent->id;
  }
  return result;
}

BatchFanout_Component_Pin::BatchFanout_Component_Pin(BatchFanout_Component* outer, Pin* boardPin,
                                                     jrt::Collection<Pin*>* boardSmdPinList,
                                                     RoutingBoard* routingBoard)
    : outer(outer), boardPin(boardPin) {
  FloatPoint* pinLocation = boardPin->getCenter()->toFloat();
  this->distanceToComponentCenter = pinLocation->distance(outer->gravityCenterOfSmdPins);

  // distanceToClosestOnNet calculation
  double minDistance = jrt::Double::MAX_VALUE;
  int32_t netNumber = boardPin->netCount() > 0 ? boardPin->getNetNumber(0) : 0;
  if (netNumber > 0) {
    for (Pin* otherPin : *routingBoard->getPins()) {
      if (otherPin != boardPin && otherPin->containsNet(netNumber)) {
        double dist = pinLocation->distance(otherPin->getCenter()->toFloat());
        if (dist < minDistance) {
          minDistance = dist;
        }
      }
    }
  }
  this->distanceToClosestOnNet = minDistance;

  // surroundingsDensity calculation
  double resolution = routingBoard->communication->getResolution(Unit::UM);
  double maxDist = 20000.0 * resolution;  // 20.0 mm in coordinate units
  int32_t density = 0;
  for (Pin* otherPin : *boardSmdPinList) {
    if (otherPin != boardPin) {
      double dist = pinLocation->distance(otherPin->getCenter()->toFloat());
      if (dist <= maxDist) {
        density++;
      }
    }
  }
  this->surroundingsDensity = density;
}

int32_t BatchFanout_Component_Pin::compareTo(BatchFanout_Component_Pin* other) {
  int32_t result = 0;
  const std::string& pinSortingOrder = outer->pinSortingOrder;
  if ("inner_first" == pinSortingOrder) {
    double deltaDist = this->distanceToComponentCenter - other->distanceToComponentCenter;
    if (deltaDist > 0) {
      result = 1;
    } else if (deltaDist < 0) {
      result = -1;
    }
  } else if ("outer_first" == pinSortingOrder) {
    double deltaDist = this->distanceToComponentCenter - other->distanceToComponentCenter;
    if (deltaDist > 0) {
      result = -1;
    } else if (deltaDist < 0) {
      result = 1;
    }
  } else if ("distanceToClosestOnNet" == pinSortingOrder) {
    double delta = this->distanceToClosestOnNet - other->distanceToClosestOnNet;
    if (delta > 0) {
      result = 1;
    } else if (delta < 0) {
      result = -1;
    }
  } else if ("surroundingsDensity" == pinSortingOrder) {
    int32_t delta = other->surroundingsDensity - this->surroundingsDensity;  // densest first
    if (delta > 0) {
      result = 1;
    } else if (delta < 0) {
      result = -1;
    }
  }
  if (result == 0) {
    result = this->boardPin->pinIndex - other->boardPin->pinIndex;
  }
  return result;
}

// ---------------------------------------------------------------------------------------------
// BatchFanout
// ---------------------------------------------------------------------------------------------

BatchFanout::BatchFanout(RoutingBoard* board, RouterSettings* settings, StoppableThread* thread)
    : BatchFanout(board, settings, thread, new jrt::HashSet<Pin*>()) {}

BatchFanout::BatchFanout(RoutingBoard* board, RouterSettings* settings, StoppableThread* thread,
                         jrt::Set<Pin*>* bgaCommittedPins)
    : progressThrottler(new ProgressThrottler(1000)) {
  this->thread = thread;
  this->routingBoard = board;
  this->settings = settings;
  std::string sortingOrder = settings->fanout != nullptr && settings->fanout->pinSortingOrder.has_value()
                                 ? *settings->fanout->pinSortingOrder
                                 : "outer_first";
  jrt::Collection<Pin*>* boardSmdPinList = routingBoard->getSmdPins();
  // Filter out SMD pins that belong to no net or an ignored net class, or were already
  // escaped by the BGA escape committer.
  jrt::Collection<Pin*>* boardSmdPinListWithNets = new jrt::LinkedList<Pin*>();
  for (Pin* pin : *boardSmdPinList) {
    if (pin->netCount() > 0 && !pin->hasIgnoredNets() &&
        (bgaCommittedPins == nullptr || !bgaCommittedPins->contains(pin)) &&
        !leftToPlaneRouting(routingBoard, settings, pin)) {
      boardSmdPinListWithNets->add(pin);
    }
  }
  this->sortedComponents = new jrt::TreeSet<BatchFanout_Component*>();
  for (int32_t i = 1; i <= routingBoard->components->count(); ++i) {
    Component* currentBoardComponent = routingBoard->components->get(i);
    BatchFanout_Component* currentComponent =
        new BatchFanout_Component(currentBoardComponent, boardSmdPinListWithNets, sortingOrder, routingBoard);
    if (currentComponent->smdPinCount > 0) {
      sortedComponents->add(currentComponent);
    }
  }
  int32_t pinCount = 0;
  int32_t alreadyConnected = 0;
  for (BatchFanout_Component* component : *sortedComponents) {
    pinCount += component->smdPinCount;
    for (BatchFanout_Component_Pin* pin : *component->smdPins) {
      // A pin is already connected if all items in its connected set are on the pin's layer
      // and its unconnected set is empty - same logic as RoutingBoard.fanout().
      Pin* boardPin = pin->boardPin;
      int32_t netNumber = boardPin->getNetNumber(0);
      if (boardPin->getUnconnectedSet(netNumber)->isEmpty()) {
        alreadyConnected++;
      }
    }
  }
  this->totalSmdPinCount = pinCount;
  this->alreadyConnectedPinCount = alreadyConnected;
}

bool BatchFanout::leftToPlaneRouting(RoutingBoard* board, RouterSettings* settings, Pin* pin) {
  if (settings->autorouter == nullptr || !(settings->autorouter->planeAsPriority == true) || pin->netCount() != 1) {
    return false;
  }
  int32_t netNo = pin->getNetNumber(0);
  bool powerPlane = false;
  for (ConductionArea* area : *board->getConductionAreas()) {
    if (area->containsNet(netNo) && !(*board->layerStructure->layers)[area->firstLayer()]->isSignal) {
      powerPlane = true;
      break;
    }
  }
  return powerPlane && !isFinePitch(board, pin, netNo);
}

bool BatchFanout::isFinePitch(RoutingBoard* board, Pin* pin, int32_t netNo) {
  Net* net = board->rules->nets->get(netNo);
  if (net == nullptr || net->getNetClass() == nullptr) {
    return true;
  }
  ViaRule* viaRule = net->getNetClass()->getViaRule();
  if (viaRule == nullptr || viaRule->viaCount() == 0) {
    return true;
  }
  ConvexShape* viaShape = viaRule->getVia(0)->getPadstack()->getShape(pin->firstLayer());
  if (viaShape == nullptr) {
    return true;
  }
  int32_t layer = pin->firstLayer();
  double clearance = board->clearanceValue(pin->clearanceClassIndex(), pin->clearanceClassIndex(), layer);
  double limit = viaShape->maxWidth() + 2 * clearance;
  FloatPoint* center = pin->getCenter()->toFloat();
  for (Pin* other : *board->getPins()) {
    if (other != pin && other->getComponentId() == pin->getComponentId() &&
        other->getCenter()->toFloat()->distance(center) < limit) {
      return true;
    }
  }
  return false;
}

BatchFanout_FanoutRunSummary* BatchFanout::fanoutBoard(RoutingBoard* board, RouterSettings* settings,
                                                       StoppableThread* thread) {
  return fanoutBoard(board, settings, thread, nullptr, nullptr);
}

BatchFanout_FanoutRunSummary* BatchFanout::fanoutBoard(RoutingBoard* board, RouterSettings* settings,
                                                       StoppableThread* thread,
                                                       BatchFanout_FanoutProgressListener* progressListener) {
  return fanoutBoard(board, settings, thread, progressListener, nullptr);
}

BatchFanout_FanoutRunSummary* BatchFanout::fanoutBoard(RoutingBoard* board, RouterSettings* settings,
                                                       StoppableThread* thread,
                                                       BatchFanout_FanoutProgressListener* progressListener,
                                                       std::function<void(const std::string&)> infoLogger) {
  if (settings->usesDirectPinRouting()) {
    return DirectPinRouting::run(board, settings, thread);
  }

  // Pre-pass: deterministic CPU multi-layer BGA escape routing
  bool fanoutEnabled = settings->getRunFanout();
  bool bgaEscapeEnabled = fanoutEnabled && (settings->fanout == nullptr ||
                                            !settings->fanout->bgaEscapeEnabled.has_value() ||
                                            settings->fanout->bgaEscapeEnabled == true);

  jrt::Set<Pin*>* bgaCommittedPins = new jrt::HashSet<Pin*>();
  if (bgaEscapeEnabled && board != nullptr && board->components != nullptr && board->components->count() > 0) {
    try {
      jrt::List<BgaEscapeDescriptor*>* bgas = BgaFootprintDetector::detect(board, settings, infoLogger);
      for (BgaEscapeDescriptor* bga : *bgas) {
        // PORT NOTE: Java `thread.isInterrupted()` (java.lang.Thread) is always false in the
        // single-threaded port.
        if (thread != nullptr && false) {
          break;
        }
        jrt::List<BgaEscapeSolver_PinEscapeProposal*>* proposals = BgaEscapeSolver::solve(board, bga, infoLogger);
        if (!proposals->isEmpty()) {
          BgaEscapeCommitter_CommitResult* commitResult = BgaEscapeCommitter::commit(board, bga, proposals);
          if (commitResult->committedPins() != nullptr) {
            bgaCommittedPins->addAll(commitResult->committedPins());
          }
          std::string committedMsg =
              "BGA escape committer: committed " + jrt::str(commitResult->pinsCommitted()) + "/" +
              jrt::str(proposals->size()) + " escape routes for " + bga->component()->name +
              (commitResult->drcRejections() > 0 ? " (" + jrt::str(commitResult->drcRejections()) + " rejected by DRC)"
                                                 : std::string("")) +
              ".";
          FRLogger::info(committedMsg);
          if (infoLogger) {
            infoLogger(committedMsg);
          }
          if (progressListener != nullptr) {
            BoardStatistics* stats = new BoardStatistics(board, std::nullopt, false);
            BatchFanout_EscapeStatistics* escapeStats = BatchFanout_EscapeStatistics::fromBoardStatistics(stats);
            progressListener->onProgress(new BatchFanout_FanoutPassStatus(
                0, 0, bga->pins()->size(), 0, commitResult->pinsCommitted(), 0, commitResult->drcRejections(),
                commitResult->viasInserted(), commitResult->viasInserted(), 0LL, stats, true, escapeStats));
          }
        }
      }
    } catch (const jrt::Exception& ex) {
      // PORT NOTE: Thread.currentThread().isInterrupted() / InterruptedException cannot occur in the
      // single-threaded port; only the "skipped due to error" branch remains reachable.
      std::string errMsg = "BGA escape fanout pre-pass skipped due to error: " + ex.getMessage();
      FRLogger::warn(errMsg);
      if (infoLogger) {
        infoLogger(errMsg);
      }
    }
  }

  if (!bgaCommittedPins->isEmpty()) {
    FRLogger::info("CPU fanout: bypassing " + jrt::str(bgaCommittedPins->size()) +
                   " already-escaped BGA pin(s) to prevent redundant passes and stubs.");
  }

  BatchFanout* fanoutInstance = new BatchFanout(board, settings, thread, bgaCommittedPins);
  int64_t fanoutStart = jrt::System::currentTimeMillis();
  if (settings->fanout != nullptr && settings->fanout->timeoutString.has_value()) {
    std::optional<int64_t> timeoutSeconds = TextManager::parseTimespanString(settings->fanout->timeoutString);
    if (timeoutSeconds.has_value()) {
      fanoutInstance->deadlineMs = fanoutStart + *timeoutSeconds * 1000;
    }
  }
  int32_t maxPasses =
      settings->fanout != nullptr && settings->fanout->maxPasses.has_value() ? *settings->fanout->maxPasses : 20;
  const int32_t stagnationPassLimit = 3;
  int32_t completedPasses = 0;
  int64_t previousBoardState = jrt::Long::MIN_VALUE;
  int32_t identicalPasses = 0;
  std::string lastBoardHash = board->getHash();
  for (int32_t i = 0; i < maxPasses; ++i) {
    if (fanoutInstance->deadlineMs.has_value() && jrt::System::currentTimeMillis() >= *fanoutInstance->deadlineMs) {
      fanoutInstance->isTimedOut = true;
      FRLogger::info("Fanout stage timed out before starting pass #" + jrt::str(i + 1));
      break;
    }
    if (settings->fanout != nullptr && settings->fanout->maxItems.has_value() && *settings->fanout->maxItems > 0 &&
        fanoutInstance->totalItemsFanouted >= *settings->fanout->maxItems) {
      break;
    }
    int32_t routedCount = fanoutInstance->fanoutPass(i, progressListener);
    completedPasses++;
    if (routedCount == 0) {
      break;
    }
    // Oscillation detector, complementing the single-pass board-hash check below.
    int64_t boardState = jrt::shl(static_cast<int64_t>(routedCount), 32) ^ static_cast<int64_t>(board->getViaCount());
    if (boardState == previousBoardState) {
      identicalPasses++;
      if (identicalPasses >= stagnationPassLimit) {
        FRLogger::info("Fanout stopped after " + jrt::str(completedPasses) + " passes: no progress for " +
                       jrt::str(stagnationPassLimit) + " consecutive passes.");
        break;
      }
    } else {
      identicalPasses = 0;
      previousBoardState = boardState;
    }
    if (fanoutInstance->isTimedOut) {
      break;
    }
    std::string currentBoardHash = board->getHash();
    if (currentBoardHash == lastBoardHash) {
      break;
    }
    lastBoardHash = currentBoardHash;
  }
  BoardStatistics* stats = new BoardStatistics(board, std::nullopt, false);
  BatchFanout_EscapeStatistics* finalEscape = BatchFanout_EscapeStatistics::fromBoardStatistics(stats);
  int64_t totalDurationMillis = jrt::Math::max(static_cast<int64_t>(0), jrt::System::currentTimeMillis() - fanoutStart);
  return new BatchFanout_FanoutRunSummary(completedPasses, totalDurationMillis, finalEscape,
                                          fanoutInstance->isTimedOut);
}

int32_t BatchFanout::fanoutPass(int32_t passNo, BatchFanout_FanoutProgressListener* progressListener) {
  int64_t passStart = jrt::System::currentTimeMillis();
  int32_t pinsToGo = this->totalSmdPinCount;
  int32_t routedCount = 0;
  int32_t notRoutedCount = 0;
  int32_t insertErrorCount = 0;
  const int32_t viasBeforePass = this->routingBoard->getViaCount();
  int32_t ripupCosts = this->settings->getStartRipupCosts() * (passNo + 1);

  int64_t baseMillisPerPin =
      this->settings->fanout != nullptr && this->settings->fanout->maxMillisecondsPerPin.has_value()
          ? *this->settings->fanout->maxMillisecondsPerPin
          : 10000LL;
  bool ripupAllowed = (this->settings->fanout == nullptr || !this->settings->fanout->ripupAllowed.has_value()) ||
                      this->settings->fanout->ripupAllowed == true;
  // Negative ripup costs signal "no ripup" to RoutingBoard.fanout()
  int32_t effectiveRipupCosts = ripupAllowed ? ripupCosts : -1;

  FRLogger::trace("BatchFanout.fanout_pass", "pass_start",
                  "pass=" + jrt::str(passNo + 1) + ", totalPins=" + jrt::str(this->totalSmdPinCount) +
                      ", alreadyConnected=" + jrt::str(this->alreadyConnectedPinCount) +
                      ", pinsToFanout=" + jrt::str(this->totalSmdPinCount - this->alreadyConnectedPinCount) +
                      ", ripupCosts=" + jrt::str(effectiveRipupCosts) +
                      ", baseMillisPerPin=" + jrt::str(baseMillisPerPin),
                  "", new jrt::Array<Point*>(0));

  this->progressThrottler->reset();
  BoardStatistics* progressStats = new BoardStatistics(this->routingBoard, std::nullopt, false);
  publishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount, insertErrorCount, 0,
                  new BatchFanout_EscapeStatistics(this->totalSmdPinCount, 0, 0.0), false, passStart, progressStats);
  bool maxLimitReached = false;
  int32_t alreadyConnectedCount = 0;
  for (BatchFanout_Component* currentComponent : *this->sortedComponents) {
    for (BatchFanout_Component_Pin* currentPin : *currentComponent->smdPins) {
      if (this->settings->fanout != nullptr && this->settings->fanout->maxItems.has_value() &&
          *this->settings->fanout->maxItems > 0 &&
          this->totalItemsFanouted >= *this->settings->fanout->maxItems) {
        FRLogger::info("Max items limit reached (" + jrt::str(*this->settings->fanout->maxItems) +
                       "). Stopping fanout.");
        maxLimitReached = true;
        break;
      }
      double maxMilliseconds = static_cast<double>(baseMillisPerPin * (passNo + 1));
      TimeLimit* const timeLimit = new TimeLimit(jrt::d2i(maxMilliseconds));
      if (currentPin->boardPin->hasIgnoredNets()) {
        --pinsToGo;
        continue;
      }
      std::string fullPinName = currentComponent->boardComponent->name + "-" + jrt::str(currentPin->boardPin->name());
      int32_t netNumber = currentPin->boardPin->getNetNumber(0);
      int32_t targetCount = currentPin->boardPin->getUnconnectedSet(netNumber)->size();

      Net* net = this->routingBoard->rules->nets->get(netNumber);
      if (net != nullptr) {
        NetClass* netClass = net->getNetClass();
        ViaRule* viaRule = netClass != nullptr ? netClass->getViaRule() : nullptr;
        bool hasBoardVias = !this->routingBoard->rules->viaRules->isEmpty() &&
                            this->routingBoard->rules->viaRules->get(0)->viaCount() > 0;
        bool fallbackAllowed =
            this->settings->fanout != nullptr && this->settings->fanout->fallbackToBoardVias == true && hasBoardVias;
        bool canUseVias = (viaRule != nullptr && viaRule->viaCount() > 0) || fallbackAllowed;
        if (!canUseVias) {
          FRLogger::debug("BatchFanout: skipping pin " + fullPinName +
                          " because its net class has no vias defined and fallback is " + "disabled/unavailable.");
          --pinsToGo;
          continue;
        }
      }

      if (!retryFanout(failedPinGeneration->getOpt(currentPin->boardPin->getId()),
                       componentGeneration->getOrDefault(currentComponent->boardComponent->id, 0))) {
        --pinsToGo;
        continue;
      }

      FRLogger::trace("BatchFanout.fanout_pass", "pin_start",
                      "pin=" + fullPinName + ", net=" + jrt::str(netNumber) + ", targetCount=" + jrt::str(targetCount) +
                          ", center=" + jrt::str(currentPin->boardPin->getCenter()) +
                          ", layer=" + jrt::str(currentPin->boardPin->firstLayer()) + ", pass=" + jrt::str(passNo + 1),
                      fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));

      int32_t maxItemIdBeforeFanout = this->routingBoard->communication->idGenerator->maxGeneratedId();
      this->routingBoard->startMarkingChangedArea();
      int64_t pinStartNanos = jrt::System::nanoTime();
      AutorouteAttemptResult* currentResult = this->routingBoard->fanout(currentPin->boardPin, this->settings,
                                                                         effectiveRipupCosts, this->thread, timeLimit);
      int64_t pinDurationMs = (jrt::System::nanoTime() - pinStartNanos) / 1000000LL;

      if (currentResult->state == AutorouteAttemptState::ROUTED) {
        AutorouteAttemptResult* fanoutDrcRejection =
            BatchAutorouter::enforceStrictDrc(this->routingBoard, netNumber, maxItemIdBeforeFanout);
        if (fanoutDrcRejection != nullptr) {
          FRLogger::trace("BatchFanout.fanout_pass", "fanout_via_reverted",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) +
                              ", reason=" + fanoutDrcRejection->details,
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          currentResult = fanoutDrcRejection;
        }
      }

      switch (currentResult->state) {
        case AutorouteAttemptState::ROUTED: {
          ++routedCount;
          this->totalItemsFanouted++;
          componentGeneration->merge(currentComponent->boardComponent->id, 1,
                                     [](int32_t a, int32_t b) { return a + b; });
          FRLogger::trace("BatchFanout.fanout_pass", "pin_routed",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) +
                              ", durationMs=" + jrt::str(pinDurationMs) + ", targetCount=" + jrt::str(targetCount),
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
        case AutorouteAttemptState::ALREADY_CONNECTED: {
          ++alreadyConnectedCount;
          FRLogger::trace("BatchFanout.fanout_pass", "pin_already_connected",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) +
                              ", targetCount=" + jrt::str(targetCount) + ", detail=" + currentResult->details,
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
        case AutorouteAttemptState::FAILED: {
          ++notRoutedCount;
          this->totalItemsFanouted++;
          failedPinGeneration->put(currentPin->boardPin->getId(),
                                   componentGeneration->getOrDefault(currentComponent->boardComponent->id, 0));
          FRLogger::trace("BatchFanout.fanout_pass", "pin_failed",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) +
                              ", targetCount=" + jrt::str(targetCount) + ", durationMs=" + jrt::str(pinDurationMs) +
                              ", detail=" +
                              (currentResult->details.empty() ? std::string("no detail") : currentResult->details),
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
        case AutorouteAttemptState::INSERT_ERROR: {
          ++insertErrorCount;
          this->totalItemsFanouted++;
          failedPinGeneration->put(currentPin->boardPin->getId(),
                                   componentGeneration->getOrDefault(currentComponent->boardComponent->id, 0));
          FRLogger::trace("BatchFanout.fanout_pass", "pin_insert_error",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) + ", detail=" +
                              (currentResult->details.empty() ? std::string("no detail") : currentResult->details),
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
        case AutorouteAttemptState::NO_UNCONNECTED_NETS: {
          FRLogger::trace("BatchFanout.fanout_pass", "pin_no_unconnected_nets",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) + ", detail=" + currentResult->details,
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
        default: {
          FRLogger::trace("BatchFanout.fanout_pass", "pin_other_state",
                          "pin=" + fullPinName + ", net=" + jrt::str(netNumber) +
                              ", state=" + AutorouteAttemptState_toString(currentResult->state) +
                              ", detail=" + currentResult->details,
                          fullPinName, jrt::Array<Point*>::of({currentPin->boardPin->getCenter()}));
          break;
        }
      }
      --pinsToGo;
      int32_t extraViasThisPass = jrt::Math::max(0, this->routingBoard->getViaCount() - viasBeforePass);
      maybePublishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount,
                           insertErrorCount, extraViasThisPass, false, passStart, progressStats);
      if (this->deadlineMs.has_value() && jrt::System::currentTimeMillis() >= *this->deadlineMs) {
        FRLogger::info("Fanout stage timed out.");
        this->isTimedOut = true;
        BoardStatistics* passStats = new BoardStatistics(this->routingBoard, std::nullopt, false);
        BatchFanout_EscapeStatistics* escapeStats = BatchFanout_EscapeStatistics::fromBoardStatistics(passStats);
        publishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount, insertErrorCount,
                        extraViasThisPass, escapeStats, true, passStart, passStats);
        return routedCount;
      }
      if (this->thread != nullptr && this->thread->isStopAutoRouterRequested()) {
        BoardStatistics* passStats = new BoardStatistics(this->routingBoard, std::nullopt, false);
        BatchFanout_EscapeStatistics* escapeStats = BatchFanout_EscapeStatistics::fromBoardStatistics(passStats);
        publishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount, insertErrorCount,
                        extraViasThisPass, escapeStats, true, passStart, passStats);
        return routedCount;
      }
    }
    if (maxLimitReached) {
      break;
    }
  }
  int32_t extraViasThisPass = jrt::Math::max(0, this->routingBoard->getViaCount() - viasBeforePass);
  this->extraViasTotal += extraViasThisPass;
  BoardStatistics* passStats = new BoardStatistics(this->routingBoard, std::nullopt, false);
  BatchFanout_EscapeStatistics* escapeStats = BatchFanout_EscapeStatistics::fromBoardStatistics(passStats);

  int64_t passDurationMs = jrt::System::currentTimeMillis() - passStart;
  FRLogger::trace("BatchFanout.fanout_pass", "pass_end",
                  "pass=" + jrt::str(passNo + 1) + ", durationMs=" + jrt::str(passDurationMs) +
                      ", routed=" + jrt::str(routedCount) + ", notRouted=" + jrt::str(notRoutedCount) +
                      ", insertErrors=" + jrt::str(insertErrorCount) +
                      ", alreadyConnected=" + jrt::str(alreadyConnectedCount) +
                      ", escaped=" + jrt::str(escapeStats->escapedCount()) + "/" +
                      jrt::str(escapeStats->totalSmdPins()) + " (" +
                      jrt::format("%.1f", escapeStats->escapedPercentage()) + "%)",
                  "", new jrt::Array<Point*>(0));

  if (progressListener == nullptr) {
    FRLogger::info("fanout pass: " + jrt::str(passNo + 1) + ", routed: " + jrt::str(routedCount) +
                   ", not routed: " + jrt::str(notRoutedCount) + ", errors: " + jrt::str(insertErrorCount) +
                   ", extra vias: +" + jrt::str(extraViasThisPass) +
                   ", escaped SMD pins: " + jrt::str(escapeStats->escapedCount()) + "/" +
                   jrt::str(escapeStats->totalSmdPins()) + " (" +
                   jrt::format("%.1f", escapeStats->escapedPercentage()) + "%)");
  }
  this->lastNotRoutedCount = notRoutedCount;
  publishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount, insertErrorCount,
                  extraViasThisPass, escapeStats, true, passStart, passStats);

  return routedCount;
}

bool BatchFanout::retryFanout(std::optional<int32_t> failedGeneration, int32_t currentGeneration) {
  return !failedGeneration.has_value() || *failedGeneration != currentGeneration;
}

void BatchFanout::maybePublishProgress(BatchFanout_FanoutProgressListener* progressListener, int32_t passNo,
                                       int32_t ripupCosts, int32_t pinsToGo, int32_t routedCount,
                                       int32_t notRoutedCount, int32_t insertErrorCount, int32_t extraViasThisPass,
                                       bool passCompleted, int64_t passStart, BoardStatistics* progressStats) {
  if (passCompleted || progressThrottler->shouldUpdate()) {
    // Mid-pass interim updates use a lightweight empty escape statistics placeholder
    // to avoid the cost of a full escape scan on every tick.
    BatchFanout_EscapeStatistics* interimEscape = new BatchFanout_EscapeStatistics(this->totalSmdPinCount, 0, 0.0);
    if (progressStats != nullptr) {
      progressStats->vias->totalCount = this->routingBoard->getViaCount();
      progressStats->traces->totalCount = this->routingBoard->getTraces()->size();
    }
    publishProgress(progressListener, passNo, ripupCosts, pinsToGo, routedCount, notRoutedCount, insertErrorCount,
                    extraViasThisPass, interimEscape, passCompleted, passStart, progressStats);
  }
}

void BatchFanout::publishProgress(BatchFanout_FanoutProgressListener* progressListener, int32_t passNo,
                                  int32_t ripupCosts, int32_t pinsToGo, int32_t routedCount, int32_t notRoutedCount,
                                  int32_t insertErrorCount, int32_t extraViasThisPass,
                                  BatchFanout_EscapeStatistics* escapeStatistics, bool passCompleted,
                                  int64_t passStart, BoardStatistics* boardStatistics) {
  if (progressListener == nullptr) {
    return;
  }
  int64_t duration = jrt::Math::max(static_cast<int64_t>(0), jrt::System::currentTimeMillis() - passStart);
  progressListener->onProgress(new BatchFanout_FanoutPassStatus(
      passNo + 1, ripupCosts, this->totalSmdPinCount, pinsToGo, routedCount, notRoutedCount, insertErrorCount,
      extraViasThisPass, this->extraViasTotal + extraViasThisPass, duration, boardStatistics, passCompleted,
      escapeStatistics));
}

}  // namespace fr
