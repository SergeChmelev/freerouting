#include "autoroute/maze/AutorouteControl.h"

#include "autoroute/maze/AutorouteEngine.h"
#include "board/actions/ForcedViaInserter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/library/Padstack.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

AutorouteControl::AutorouteControl(RoutingBoard* board, int32_t netNumber,
                                   RouterSettings* settings)
    : AutorouteControl(board, settings, settings->getTraceCosts()) {
  initNet(netNumber, board, settings->getViaCosts());
}

AutorouteControl::AutorouteControl(RoutingBoard* board, int32_t netNumber,
                                   RouterSettings* settings, int32_t viaCosts,
                                   jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts)
    : AutorouteControl(board, settings, traceCosts) {
  initNet(netNumber, board, viaCosts);
}

AutorouteControl::AutorouteControl(RoutingBoard* board, RouterSettings* settings,
                                   jrt::Array<AutorouteControl_ExpansionCostFactor*>* traceCosts) {
  this->settings = settings;
  this->viaInPad = settings != nullptr && settings->autorouter != nullptr &&
                   settings->autorouter->viaInPad.has_value() && *settings->autorouter->viaInPad;
  this->viaTouchingPad = settings != nullptr && settings->autorouter != nullptr &&
                         settings->autorouter->viaTouchingPad.has_value() &&
                         *settings->autorouter->viaTouchingPad;
  this->fanoutViaPadGap = (settings != nullptr && settings->autorouter != nullptr &&
                           settings->autorouter->fanoutViaPadGap.has_value())
                              ? jrt::Math::max(0.0, *settings->autorouter->fanoutViaPadGap)
                              : 0;
  this->centreOfGravityViaCandidate = (settings != nullptr && settings->autorouter != nullptr &&
                                       settings->autorouter->viaCentreOfGravity.has_value())
                                          ? *settings->autorouter->viaCentreOfGravity
                                          : !viaInPad;
  layerCount = board->getLayerCount();
  traceHalfWidth = new (jrt::kOp) jrt::Array<int32_t>(layerCount);
  compensatedTraceHalfWidth = new (jrt::kOp) jrt::Array<int32_t>(layerCount);
  traceClearanceOffset = new (jrt::kOp) jrt::Array<int32_t>(layerCount);
  layerActive = new (jrt::kOp) jrt::Array<bool>(layerCount);
  viasAllowed = settings->getViasAllowed();
  viaRadii = new (jrt::kOp) jrt::Array<double>(layerCount);
  addViaCosts = new (jrt::kOp) jrt::Array<AutorouteControl_ViaCost*>(layerCount);
  this->bendCosts = new (jrt::kOp) jrt::Array<double>(layerCount);
  for (int32_t i = 0; i < layerCount; i++) {
    (*this->bendCosts)[i] = settings->getBendCost(i);
  }

  for (int32_t i = 0; i < layerCount; i++) {
    (*addViaCosts)[i] = new (jrt::kOp) AutorouteControl_ViaCost(layerCount);
    bool activeSetting = settings->getLayerActive(i);
    if (!(*board->layerStructure->layers)[i]->isSignal && activeSetting) {
      FRLogger::warn("Layer '" + (*board->layerStructure->layers)[i]->name +
                     "' is a dedicated power plane and cannot be routed. " +
                     "Forcing active state to false.");
      (*layerActive)[i] = false;
    } else {
      (*layerActive)[i] = activeSetting;
    }
  }
  isFanout = false;
  fanoutStartPinName = std::nullopt;
  fanoutStartPinCenter = nullptr;
  fanoutStartPinLayer = -1;
  removeUnconnectedVias = true;
  withNeckdown = settings->getAutomaticNeckdown();
  tidyRegionWidth = jrt::Integer::MAX_VALUE;
  pullTightAccuracy = 500;
  maxShoveTraceRecursionDepth = 20;
  maxShoveViaRecursionDepth = 8;
  maxSpringOverRecursionDepth = 8;
  for (int32_t i = 0; i < layerCount; i++) {
    for (int32_t j = 0; j < layerCount; j++) {
      (*(*addViaCosts)[i]->toLayer)[j] = 0;
    }
  }
  this->traceCosts = traceCosts;
  attachSmdAllowed = false;
  viaLowerBound = 0;
  viaUpperBound = layerCount;

  ripupAllowed = false;
  ripupCosts = 1000;
  ripupPassNo = 1;
}

bool AutorouteControl::viaAttachAllowed(ViaInfo* viaInfo) {
  return viaInfo->attachSmdAllowed() || ((viaInPad || viaTouchingPad) && this->attachSmdAllowed);
}

bool AutorouteControl::viaHoleAllowed(RoutingBoard* board, ViaInfo* viaInfo, Point* location,
                                      jrt::Array<int32_t>* netNumbers) {
  return !viaTouchingPad || viaInPad || viaInfo->attachSmdAllowed() ||
         !ForcedViaInserter::holeInSmdPad(board, viaInfo->getPadstack(), location, netNumbers);
}

bool AutorouteControl::isPureSmdNet(RoutingBoard* board, int32_t netNumber) {
  jrt::Collection<Item*>* netItems = board->getConnectableItems(netNumber);
  if (netItems->isEmpty()) {
    return false;
  }

  for (Item* item : *netItems) {
    auto* pin = jrt::fast_cast<Pin>(item);
    if (pin == nullptr || pin->firstLayer() != pin->lastLayer()) {
      return false;
    }
  }

  return true;
}

bool AutorouteControl::hasSmdPin(RoutingBoard* board, int32_t netNumber) {
  jrt::Collection<Item*>* netItems = board->getConnectableItems(netNumber);
  for (Item* item : *netItems) {
    auto* pin = jrt::fast_cast<Pin>(item);
    if (pin != nullptr && pin->firstLayer() == pin->lastLayer()) {
      return true;
    }
  }
  return false;
}

void AutorouteControl::initNet(int32_t netNumber, RoutingBoard* board, int32_t viaCosts) {
  this->netNumber = netNumber;
  Net* currentNet = board->rules->nets->get(netNumber);
  NetClass* currentNetClass;
  if (currentNet != nullptr) {
    currentNetClass = currentNet->getNetClass();
    traceClearanceClassIndex = currentNetClass->getTraceClearanceClass();
    viaRule = currentNetClass->getViaRule();
  } else {
    traceClearanceClassIndex = 1;
    // Java: Vector.firstElement()
    viaRule = board->rules->viaRules->getFirst();
    currentNetClass = nullptr;
  }
  for (int32_t i = 0; i < layerCount; i++) {
    if (netNumber > 0) {
      (*traceHalfWidth)[i] = board->rules->getTraceHalfWidth(netNumber, i);
    } else {
      (*traceHalfWidth)[i] = board->rules->getTraceHalfWidth(1, i);
    }
    (*compensatedTraceHalfWidth)[i] =
        (*traceHalfWidth)[i] +
        board->rules->clearanceMatrix->clearanceCompensationValue(traceClearanceClassIndex, i);
    int32_t safetyMargin =
        board->rules->clearanceMatrix->getValue(traceClearanceClassIndex,
                                                traceClearanceClassIndex, i, true) -
        board->rules->clearanceMatrix->getValue(traceClearanceClassIndex,
                                                traceClearanceClassIndex, i, false);
    (*traceClearanceOffset)[i] = (*compensatedTraceHalfWidth)[i] +
                                 AutorouteEngine::TRACE_WIDTH_TOLERANCE + safetyMargin;
    if (currentNetClass != nullptr && !currentNetClass->isActiveRoutingLayer(i)) {
      (*layerActive)[i] = false;
    }
  }
  rebuildViaInfo(board, viaCosts, netNumber);
}

int32_t AutorouteControl::getTraceClearanceOffset(int32_t layer) {
  if (layer < 0 || layer >= layerCount) {
    return 0;
  }
  return (*traceClearanceOffset)[layer];
}

void AutorouteControl::updateTraceClearanceOffset(int32_t layer, RoutingBoard* board) {
  if (layer >= 0 && layer < layerCount && board != nullptr && board->rules != nullptr) {
    int32_t safetyMargin =
        board->rules->clearanceMatrix->getValue(traceClearanceClassIndex,
                                                traceClearanceClassIndex, layer, true) -
        board->rules->clearanceMatrix->getValue(traceClearanceClassIndex,
                                                traceClearanceClassIndex, layer, false);
    (*traceClearanceOffset)[layer] = (*compensatedTraceHalfWidth)[layer] +
                                     AutorouteEngine::TRACE_WIDTH_TOLERANCE + safetyMargin;
  }
}

void AutorouteControl::rebuildViaInfo(RoutingBoard* board, int32_t viaCosts, int32_t netNumber) {
  if (viaRule->viaCount() > 0) {
    this->viaClearanceClass = viaRule->getVia(0)->getClearanceClassIndex();
  } else {
    this->viaClearanceClass = 1;
  }
  this->viaInfos = new (jrt::kOp) jrt::Array<AutorouteControl_ViaMask*>(viaRule->viaCount());
  this->attachSmdAllowed = false;
  for (int32_t i = 0; i < viaRule->viaCount(); i++) {
    ViaInfo* currentVia = viaRule->getVia(i);
    if (currentVia->attachSmdAllowed()) {
      this->attachSmdAllowed = true;
    }
    Padstack* currentViaPadstack = currentVia->getPadstack();
    int32_t fromLayer = currentViaPadstack->fromLayer();
    int32_t toLayer = currentViaPadstack->toLayer();
    for (int32_t j = fromLayer; j <= toLayer; j++) {
      ConvexShape* currentShape = currentViaPadstack->getShape(j);
      double currentRadius;
      if (currentShape != nullptr) {
        currentRadius = 0.5 * currentShape->maxWidth();
      } else {
        currentRadius = 0;
      }
      (*this->viaRadii)[j] = jrt::Math::max((*this->viaRadii)[j], currentRadius);
    }
    (*viaInfos)[i] =
        new (jrt::kOp) AutorouteControl_ViaMask(fromLayer, toLayer, currentVia->attachSmdAllowed());
  }

  bool pureSmdNet = isPureSmdNet(board, netNumber);
  bool hasSmd = hasSmdPin(board, netNumber);
  if (!this->attachSmdAllowed && layerCount > 1 && hasSmd) {
    // Nets containing SMD pins on multi-layer boards must be able to escape congested
    // component pads via layer transitions, even if the DSN marks padstacks as attach-off
    // or the net also connects to through-hole pins. This only relaxes the routing gate
    // for same-net fanout/escape; cross-net DRC remains governed by the padstack's attach flag.
    this->attachSmdAllowed = true;
    for (int32_t i = 0; i < this->viaInfos->length; i++) {
      (*this->viaInfos)[i] = new (jrt::kOp) AutorouteControl_ViaMask((*this->viaInfos)[i]->fromLayer,
                                                          (*this->viaInfos)[i]->toLayer, true);
    }
  }

  for (int32_t j = 0; j < this->layerCount; j++) {
    (*this->viaRadii)[j] =
        jrt::Math::max((*this->viaRadii)[j], static_cast<double>((*traceHalfWidth)[j]));
    this->maxViaRadius = jrt::Math::max(this->maxViaRadius, (*this->viaRadii)[j]);
  }
  double viaCostFactor = this->maxViaRadius;
  viaCostFactor = jrt::Math::max(viaCostFactor, 1.0);
  if (pureSmdNet) {
    // Pure SMD boards need a much cheaper via escape to avoid exhausting the local pad channel
    // before the search commits to a layer change.
    double factor = this->settings != nullptr && this->settings->autorouter != nullptr &&
                            this->settings->autorouter->smdNetViaCostFactor.has_value()
                        ? *this->settings->autorouter->smdNetViaCostFactor
                        : 0.1;
    viaCostFactor *= factor;
  }
  minNormalViaCost = viaCosts * viaCostFactor;
  minCheapViaCost = 0.8 * minNormalViaCost;
  applySignalLayerViaSurcharge(board, viaCosts, netNumber, viaCostFactor);
}

void AutorouteControl::applySignalLayerViaSurcharge(RoutingBoard* board, int32_t viaCosts,
                                                    int32_t netNumber, double viaCostFactor) {
  for (int32_t i = 0; i < layerCount; i++) {
    jrt::Array<int32_t>* toLayer = (*addViaCosts)[i]->toLayer;
    for (int32_t k = 0; k < toLayer->length; k++) {
      (*toLayer)[k] = 0;
    }
  }
  if (settings == nullptr || netNumber <= 0) {
    return;
  }
  jrt::Array<bool>* planeLayer = new (jrt::kOp) jrt::Array<bool>(layerCount);
  bool anyPlane = false;
  jrt::DisposeOnExit loopSource1(board->getConductionAreas());
  for (ConductionArea* area : *loopSource1) {
    int32_t layer = area->getLayer();
    if (layer >= 0 && layer < layerCount && !(*board->layerStructure->layers)[layer]->isSignal &&
        area->containsNet(netNumber)) {
      (*planeLayer)[layer] = true;
      anyPlane = true;
    }
  }
  if (!anyPlane) {
    return;
  }
  Net* net = board->rules != nullptr && board->rules->nets != nullptr
                 ? board->rules->nets->get(netNumber)
                 : nullptr;
  bool routedFirst = net != nullptr && net->isRoutedFirst();
  int32_t signalCost = routedFirst ? settings->getPriorityViaCosts() : settings->getViaCosts();
  int32_t planeCost = settings->getPlaneViaCosts();
  int32_t baseCost = jrt::Math::min(signalCost, planeCost);
  minNormalViaCost = baseCost * viaCostFactor;
  minCheapViaCost = 0.8 * minNormalViaCost;

  int32_t signalSurcharge = jrt::l2i(jrt::Math::round((signalCost - baseCost) * viaCostFactor));
  int32_t planeSurcharge = jrt::l2i(jrt::Math::round((planeCost - baseCost) * viaCostFactor));

  for (int32_t i = 0; i < layerCount; i++) {
    for (int32_t j = 0; j < layerCount; j++) {
      if (i != j) {
        bool touchesPlane = (*planeLayer)[i] || (*planeLayer)[j];
        (*(*addViaCosts)[i]->toLayer)[j] = touchesPlane ? planeSurcharge : signalSurcharge;
      }
    }
  }
}

int32_t AutorouteControl::neckHalfWidth(RoutingBoard* board, RouterSettings* settings) {
  double neckWidthUm = settings->getNeckWidthUm();
  if (neckWidthUm <= 0) {
    return board->rules->getMinAllowedTraceHalfWidth();
  }
  int32_t boardResolution = jrt::Math::max(1, board->communication->resolution);
  int32_t neckWidth = jrt::l2i(jrt::Math::round(
      Unit_scale(neckWidthUm * boardResolution, Unit::UM, board->communication->unit)));
  return jrt::Math::max(jrt::Math::max(1, neckWidth / 2),
                        board->rules->getMinAllowedTraceHalfWidth());
}

}  // namespace fr
