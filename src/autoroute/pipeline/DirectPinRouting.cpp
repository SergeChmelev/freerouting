#include "autoroute/pipeline/DirectPinRouting.h"

#include "autoroute/AutorouteAttemptResult.h"
#include "autoroute/AutorouteAttemptState.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/pipeline/BatchFanout.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/StoppableThread.h"
#include "core/scoring/BoardStatistics.h"
#include "datastructures/TimeLimit.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

// ---- DirectPinRouting_FixedCopper (record) ----

bool DirectPinRouting_FixedCopper::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<DirectPinRouting_FixedCopper>(o);
  if (other == nullptr) return false;
  return id_ == other->id_ && state_ == other->state_;
}

int32_t DirectPinRouting_FixedCopper::hashCode() {
  int32_t h = 0;
  h = 31 * h + id_;
  h = 31 * h + static_cast<int32_t>(state_);
  return h;
}

std::string DirectPinRouting_FixedCopper::toString() {
  return "FixedCopper[id=" + jrt::str(id_) + ", state=" + FixedState_toString(state_) + "]";
}

// ---- DirectPinRouting ----

int32_t DirectPinRouting::fanoutItemLimit(RouterSettings* settings) {
  std::optional<int32_t> configured =
      settings->fanout != nullptr ? settings->fanout->maxItems : std::optional<int32_t>();
  return configured.has_value() && *configured > 0 ? *configured : jrt::Integer::MAX_VALUE;
}

BatchFanout_FanoutRunSummary* DirectPinRouting::run(RoutingBoard* board, RouterSettings* settings,
                                                    StoppableThread* thread) {
  if (!settings->getRunRouter()) {
    throw jrt::IllegalArgumentException("direct_first requires the autorouter stage");
  }
  int64_t started = jrt::System::currentTimeMillis();
  jrt::List<Pin*>* pins = new (jrt::kOp) jrt::ArrayList<Pin*>();
  jrt::DisposeOnExit loopSource1(board->getSmdPins());
  for (Pin* pin : *loopSource1) {
    if (pin->netCount() == 1 && !pin->hasIgnoredNets()) {
      pins->add(pin);
    }
  }
  pins->sort(jrt::thenComparing<Pin*>(
      jrt::comparingInt<Pin*>([](Pin* p) { return p->getComponentId(); }),
      jrt::comparingInt<Pin*>([](Pin* pin) { return pin->pinIndex; })));
  jrt::List<DirectPinRouting_FixedCopper*>* fixed = new (jrt::kOp) jrt::ArrayList<DirectPinRouting_FixedCopper*>();
  jrt::DisposeOnExit loopSource2(board->getItems());
  for (Item* item : *loopSource2) {
    if ((jrt::fast_cast<Trace>(item) != nullptr || jrt::fast_cast<Via>(item) != nullptr) &&
        !FixedState_isFix(item->getFixedState())) {
      fixed->add(new (jrt::kOp) DirectPinRouting_FixedCopper(item->getId(), item->getFixedState()));
      item->setFixedState(FixedState::SYSTEM_FIXED);
    }
  }
  int32_t attempts = 0;
  int32_t routed = 0;
  int32_t deferred = 0;
  int32_t maxItems = DirectPinRouting::fanoutItemLimit(settings);
  bool fanoutDisabled = settings->fanout != nullptr && settings->fanout->maxPasses.has_value() &&
                        *settings->fanout->maxPasses <= 0;
  // Java finally block
  auto finallyBlock = [&]() {
    board->clearTransientAutorouteState();
    for (DirectPinRouting_FixedCopper* copper : *fixed) {
      jrt::DisposeOnExit loopSource3(board->getItems());
      for (Item* item : *loopSource3) {
        if (item->getId() == copper->id_) {
          item->setFixedState(copper->state_);
          break;
        }
      }
    }
  };
  try {
    for (Pin* pin : *pins) {
      if (stopped(thread) || attempts >= maxItems || fanoutDisabled) {
        break;
      }
      int32_t net = pin->getNetNumber(0);
      int32_t layer = pin->firstLayer();
      jrt::DisposeOnExit<jrt::Set<Item*>> sourceOwner(pin->getConnectedSet(net));
      jrt::Set<Item*>* source = sourceOwner.get();
      jrt::Set<Item*>* destinations = pin->getUnconnectedSet(net);
      if (destinations->isEmpty() || touchesPlane(source) || !onLayer(destinations, layer)) {
        continue;
      }
      attempts++;
      AutorouteAttemptResult* result = routeOnLayer(board, settings, thread, pin, net, layer, false);
      if (result->state != AutorouteAttemptState::ROUTED && settings->getNeckWidthUm() > 0 && !stopped(thread)) {
        result = routeOnLayer(board, settings, thread, pin, net, layer, true);
      }
      if (result->state == AutorouteAttemptState::ROUTED) {
        routed++;
      } else {
        deferred++;
      }
    }
  } catch (...) {
    finallyBlock();
    throw;
  }
  finallyBlock();
  BoardStatistics* stats = new (jrt::kOp) BoardStatistics(board);
  FRLogger::info("Direct pin routing: " + jrt::str(routed) + " complete surface connections, " +
                 jrt::str(deferred) + " deferred.");
  return new (jrt::kOp) BatchFanout_FanoutRunSummary(1, jrt::System::currentTimeMillis() - started,
                                          BatchFanout_EscapeStatistics::fromBoardStatistics(stats), false);
}

AutorouteAttemptResult* DirectPinRouting::routeOnLayer(RoutingBoard* board, RouterSettings* settings,
                                                       StoppableThread* thread, Pin* pin, int32_t net,
                                                       int32_t layer, bool neck) {
  AutorouteControl* control = new (jrt::kOp) AutorouteControl(board, net, settings);
  control->viasAllowed = false;
  control->ripupAllowed = false;
  control->removeUnconnectedVias = false;
  for (int32_t i = 0; i < control->layerCount; i++) {
    (*control->layerActive)[i] = (*control->layerActive)[i] && i == layer;
  }
  if (!(*control->layerActive)[layer]) {
    return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::FAILED, "Pin layer is inactive.");
  }
  if (neck) {
    int32_t neckHalfWidth = DirectPinRouting::neckHalfWidth(board, settings);
    if (neckHalfWidth >= (*control->traceHalfWidth)[layer]) {
      return new (jrt::kOp) AutorouteAttemptResult(AutorouteAttemptState::FAILED,
                                        "Neck width is not narrower than the class width.");
    }
    int32_t compensation = (*control->compensatedTraceHalfWidth)[layer] - (*control->traceHalfWidth)[layer];
    (*control->traceHalfWidth)[layer] = neckHalfWidth;
    (*control->compensatedTraceHalfWidth)[layer] = neckHalfWidth + compensation;
  }
  int64_t perPinMs = settings->fanout != nullptr && settings->fanout->maxMillisecondsPerPin.has_value()
                         ? *settings->fanout->maxMillisecondsPerPin
                         : 1000LL;
  AutorouteEngine* engine = board->initAutoroute(
      net, control->traceClearanceClassIndex, thread,
      new (jrt::kOp) TimeLimit(jrt::l2i(jrt::Math::min(perPinMs, static_cast<int64_t>(jrt::Integer::MAX_VALUE)))), false);
  return engine->autorouteConnection(pin->getConnectedSet(net), pin->getUnconnectedSet(net), control,
                                     new (jrt::kOp) jrt::TreeSet<Item*>(), nullptr);
}

int32_t DirectPinRouting::neckHalfWidth(RoutingBoard* board, RouterSettings* settings) {
  int32_t resolution = jrt::Math::max(1, board->communication->resolution);
  int32_t neckWidth = jrt::l2i(jrt::Math::round(
      Unit_scale(settings->getNeckWidthUm() * resolution, Unit::UM, board->communication->unit)));
  return jrt::Math::max(jrt::Math::max(1, neckWidth / 2), board->rules->getMinAllowedTraceHalfWidth());
}

bool DirectPinRouting::touchesPlane(jrt::Set<Item*>* source) {
  for (Item* item : *source) {
    if (jrt::fast_cast<ConductionArea>(item) != nullptr) {
      return true;
    }
  }
  return false;
}

bool DirectPinRouting::onLayer(jrt::Set<Item*>* destinations, int32_t layer) {
  for (Item* item : *destinations) {
    if (item->firstLayer() <= layer && item->lastLayer() >= layer) {
      return true;
    }
  }
  return false;
}

bool DirectPinRouting::stopped(StoppableThread* thread) {
  return thread != nullptr && (thread->isStopRequested() || thread->isStopAutoRouterRequested());
}

}  // namespace fr
