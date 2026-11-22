#include "settings/RouterSettings.h"

#include <thread>
#include <vector>

#include "autoroute/maze/AutorouteControl.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "geometry/planar/IntBox.h"
#include "io/KiCadNetClassNames.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/NetClass.h"
#include "rules/NetClasses.h"
#include "settings/PropertyChangeSupport.h"
#include "settings/SettingsCopy.h"

namespace fr {

namespace {
// PORT NOTE: Runtime.getRuntime().availableProcessors() -> std::thread::hardware_concurrency()
// (at least 1).
int32_t availableProcessors() {
  unsigned int n = std::thread::hardware_concurrency();
  return n == 0 ? 1 : static_cast<int32_t>(n);
}

// Java String.isBlank(): empty or only whitespace.
bool isBlank(const std::string& s) { return jrt::strip(s).empty(); }
}  // namespace

RouterSettings::RouterSettings() {
  this->pcs = new (jrt::kBoard) PropertyChangeSupport(this);
  this->optimizer = new (jrt::kBoard) OptimizerSettings();
  this->scoring = new (jrt::kBoard) RoutingCostSettings();
  this->routerScoring = new (jrt::kBoard) RouterScoreSettings();
  this->optimizerScoring = new (jrt::kBoard) OptimizerScoreSettings();
  this->fanout = new (jrt::kBoard) FanoutSettings();
  this->autorouter = new (jrt::kBoard) AutorouterSettings();
}

RouterSettings::RouterSettings(RoutingBoard* board) : RouterSettings() {
  setLayerCount(board->getLayerCount());
  applyBoardSpecificOptimizations(board);
}

int32_t RouterSettings::defaultMaxThreads() {
  return jrt::Math::max(1, availableProcessors() - 1);
}

int32_t RouterSettings::normalizeMaxThreads(std::optional<int32_t> value) {
  int32_t availableProcessorsCount = availableProcessors();
  if (!value.has_value()) {
    return defaultMaxThreads();
  }
  if (*value < 0) {
    return defaultMaxThreads();
  }
  if (*value == 0) {
    return availableProcessorsCount;
  }
  return jrt::Math::min(*value, availableProcessorsCount);
}

void RouterSettings::addPropertyChangeListener(PropertyChangeListener* listener) {
  if (pcs == nullptr) {
    pcs = new (jrt::kBoard) PropertyChangeSupport(this);
  }
  pcs->addPropertyChangeListener(listener);
}

void RouterSettings::removePropertyChangeListener(PropertyChangeListener* listener) {
  if (pcs != nullptr) {
    pcs->removePropertyChangeListener(listener);
  }
}

AutorouterSettings* RouterSettings::autorouterOrCreate() {
  if (this->autorouter == nullptr) {
    this->autorouter = new (jrt::kBoard) AutorouterSettings();
  }
  return this->autorouter;
}

void RouterSettings::setMaxPasses(std::optional<int32_t> value) {
  std::optional<int32_t> oldValue = autorouterOrCreate()->maxPasses;
  autorouterOrCreate()->maxPasses = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("maxPasses"), PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(value));
  }
}

void RouterSettings::setMaxThreads(std::optional<int32_t> value) {
  std::optional<int32_t> oldValue = this->maxThreads;
  this->maxThreads = normalizeMaxThreads(value);
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("maxThreads"), PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(this->maxThreads));
  }
  // Keep the legacy flat knob, the nested autorouter knob, and the optimizer
  // pool in sync when the GUI / setMaxThreads path is used.
  if (this->autorouter != nullptr) {
    this->autorouter->maxThreads = this->maxThreads;
  }
  if (this->optimizer != nullptr) {
    this->optimizer->maxThreads = this->maxThreads;
  }
}

int32_t RouterSettings::getAutorouterMaxThreads() {
  int32_t globalCap =
      maxThreads.has_value() ? normalizeMaxThreads(maxThreads) : defaultMaxThreads();
  if (autorouter != nullptr && autorouter->maxThreads.has_value()) {
    int32_t stageThreads = normalizeMaxThreads(autorouter->maxThreads);
    return maxThreads.has_value() ? jrt::Math::min(stageThreads, globalCap) : stageThreads;
  }
  return globalCap;
}

int32_t RouterSettings::getOptimizerMaxThreads() {
  int32_t globalCap =
      maxThreads.has_value() ? normalizeMaxThreads(maxThreads) : defaultMaxThreads();
  if (optimizer != nullptr && optimizer->maxThreads.has_value()) {
    int32_t stageThreads = normalizeMaxThreads(optimizer->maxThreads);
    return maxThreads.has_value() ? jrt::Math::min(stageThreads, globalCap) : stageThreads;
  }
  return globalCap;
}

void RouterSettings::setJobTimeoutString(const std::optional<std::string>& value) {
  std::optional<std::string> oldValue = this->jobTimeoutString;
  this->jobTimeoutString = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("jobTimeoutString"), oldValue, value);
  }
}

void RouterSettings::setEnabled(std::optional<bool> value) {
  std::optional<bool> oldValue = autorouterOrCreate()->enabled;
  autorouterOrCreate()->enabled = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("enabled"), PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(value));
  }
}

void RouterSettings::setViasAllowed(std::optional<bool> value) {
  std::optional<bool> oldValue = this->viasAllowed;
  this->viasAllowed = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("viasAllowed"), PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(value));
  }
}

void RouterSettings::setViasAllowed(bool value) { viasAllowed = value; }

void RouterSettings::setAlgorithm(const std::optional<std::string>& value) {
  std::optional<std::string> oldValue = autorouterOrCreate()->algorithm;
  autorouterOrCreate()->algorithm = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("algorithm"), oldValue, value);
  }
}

void RouterSettings::setOptimizerEnabled(std::optional<bool> value) {
  std::optional<bool> oldValue =
      this->optimizer != nullptr ? this->optimizer->enabled : std::nullopt;
  if (this->optimizer != nullptr) {
    this->optimizer->enabled = value;
  }
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("optimizer.enabled"),
                            PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(value));
  }
}

void RouterSettings::applyBoardSpecificOptimizationsIfNeeded(RoutingBoard* board) {
  if (board == nullptr) {
    return;
  }
  int32_t boardLayerCount = board->getLayerCount();
  if (getLayerCount() != boardLayerCount ||
      !(boardSpecificTraceCostsApplied.has_value() && *boardSpecificTraceCostsApplied)) {
    applyBoardSpecificOptimizations(board);
  }
}

bool RouterSettings::areBoardSpecificTraceCostsApplied() {
  return boardSpecificTraceCostsApplied.has_value() && *boardSpecificTraceCostsApplied;
}

void RouterSettings::applyBoardSpecificOptimizations(RoutingBoard* board) {
  double horizontalWidth = board->boundingBox->width();
  double verticalWidth = board->boundingBox->height();

  int32_t layerCount = board->getLayerCount();

  // Track original values to log changes later
  std::vector<std::optional<bool>> originalRoutable(layerCount);
  std::vector<std::optional<double>> originalBendCost(layerCount);
  std::vector<std::optional<bool>> originalPrefHoriz(layerCount);
  if (layers != nullptr) {
    for (int32_t i = 0; i < jrt::Math::min(layers->length, layerCount); i++) {
      if ((*layers)[i] != nullptr) {
        originalRoutable[i] = (*layers)[i]->routable;
        originalBendCost[i] = (*layers)[i]->bendCost;
        originalPrefHoriz[i] = (*layers)[i]->preferredDirectionHorizontal;
      }
    }
  }
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  jrt::Array<double>* originalPrefCost = scoring->preferredDirectionTraceCost != nullptr
                                             ? scoring->preferredDirectionTraceCost->clone()
                                             : nullptr;
  jrt::Array<double>* originalUndesiredCost = scoring->undesiredDirectionTraceCost != nullptr
                                                  ? scoring->undesiredDirectionTraceCost->clone()
                                                  : nullptr;

  // additional costs against preferred direction with 1 digit behind the decimal point.
  const double horizontalAddCostsAgainstPreferredDir =
      0.1 * static_cast<double>(jrt::Math::round(10 * horizontalWidth / verticalWidth));

  const double verticalAddCostsAgainstPreferredDir =
      0.1 * static_cast<double>(jrt::Math::round(10 * verticalWidth / horizontalWidth));

  // initialize the layer specific settings.
  if (layers == nullptr || layers->length != layerCount) {
    boardSpecificTraceCostsApplied = false;
    jrt::Array<LayerSettings*>* oldLayers = layers;
    layers = new (jrt::kBoard) jrt::Array<LayerSettings*>(layerCount);
    for (int32_t i = 0; i < layerCount; i++) {
      if (oldLayers != nullptr && i < oldLayers->length && (*oldLayers)[i] != nullptr) {
        (*layers)[i] = (*oldLayers)[i];
      } else {
        (*layers)[i] = new (jrt::kBoard) LayerSettings();
      }
    }
  } else {
    // Ensure no elements inside the layers array are null
    for (int32_t i = 0; i < layerCount; i++) {
      if ((*layers)[i] == nullptr) {
        (*layers)[i] = new (jrt::kBoard) LayerSettings();
      }
    }
  }
  if (scoring->preferredDirectionTraceCost == nullptr ||
      scoring->preferredDirectionTraceCost->length != layerCount) {
    scoring->preferredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(layerCount);
    boardSpecificTraceCostsApplied = false;
  }
  if (scoring->undesiredDirectionTraceCost == nullptr ||
      scoring->undesiredDirectionTraceCost->length != layerCount) {
    scoring->undesiredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(layerCount);
    boardSpecificTraceCostsApplied = false;
  }

  // Guard against null defaults that can occur when RouterSettings is
  // deserialized without going through DefaultSettings (e.g. from board history).
  if (!scoring->defaultPreferredDirectionTraceCost.has_value()) {
    scoring->defaultPreferredDirectionTraceCost = 1.0;
  }
  if (!scoring->defaultUndesiredDirectionTraceCost.has_value()) {
    scoring->defaultUndesiredDirectionTraceCost = 1.0;
  }

  bool currentPreferredDirectionIsHorizontal = horizontalWidth < verticalWidth;
  bool initializeTraceCosts =
      !(boardSpecificTraceCostsApplied.has_value() && *boardSpecificTraceCostsApplied);

  for (int32_t i = 0; i < layerCount; i++) {
    if ((*board->layerStructure->layers)[i]->isSignal) {
      currentPreferredDirectionIsHorizontal = !currentPreferredDirectionIsHorizontal;
    }
    bool isInnerLayer = i > 0 && i < layerCount - 1;
    LayerSettings* layer = (*layers)[i];
    if (!(*board->layerStructure->layers)[i]->isSignal && isInnerLayer) {
      layer->routable = false;
    } else if (!layer->routable.has_value()) {
      layer->routable = true;
    }
    if (!layer->bendCost.has_value()) {
      layer->bendCost = scoring != nullptr && scoring->defaultBendCost.has_value()
                            ? *scoring->defaultBendCost
                            : 0.0;
    }
    if (!layer->preferredDirectionHorizontal.has_value()) {
      layer->preferredDirectionHorizontal = currentPreferredDirectionIsHorizontal;
    }

    // If this layer has an explicit preferred direction cost, preserve it;
    // otherwise, compute it from the default.
    if (layer->preferredDirectionTraceCost.has_value()) {
      (*scoring->preferredDirectionTraceCost)[i] = *layer->preferredDirectionTraceCost;
    } else if (initializeTraceCosts) {
      (*scoring->preferredDirectionTraceCost)[i] = *scoring->defaultPreferredDirectionTraceCost;
    }

    // If this layer has an explicit against-preferred direction cost, preserve it;
    // otherwise, compute it from default + aspect ratio penalty.
    if (layer->undesiredDirectionTraceCost.has_value()) {
      (*scoring->undesiredDirectionTraceCost)[i] = *layer->undesiredDirectionTraceCost;
    } else if (initializeTraceCosts) {
      (*scoring->undesiredDirectionTraceCost)[i] = *scoring->defaultUndesiredDirectionTraceCost;
      bool isPrefHoriz = layer->preferredDirectionHorizontal.has_value()
                             ? *layer->preferredDirectionHorizontal
                             : currentPreferredDirectionIsHorizontal;
      if (isPrefHoriz) {
        (*scoring->undesiredDirectionTraceCost)[i] += horizontalAddCostsAgainstPreferredDir;
      } else {
        (*scoring->undesiredDirectionTraceCost)[i] += verticalAddCostsAgainstPreferredDir;
      }
    }
  }
  if (initializeTraceCosts) {
    int32_t signalLayerCount = board->layerStructure->signalLayerCount();
    if (signalLayerCount > 2) {
      double outerAddCosts = 0.2 * signalLayerCount;
      // increase costs on the outer layers only if not explicitly configured.
      if (!(*layers)[0]->preferredDirectionTraceCost.has_value()) {
        (*scoring->preferredDirectionTraceCost)[0] += outerAddCosts;
      }
      if (!(*layers)[layerCount - 1]->preferredDirectionTraceCost.has_value()) {
        (*scoring->preferredDirectionTraceCost)[layerCount - 1] += outerAddCosts;
      }
      if (!(*layers)[0]->undesiredDirectionTraceCost.has_value()) {
        (*scoring->undesiredDirectionTraceCost)[0] += outerAddCosts;
      }
      if (!(*layers)[layerCount - 1]->undesiredDirectionTraceCost.has_value()) {
        (*scoring->undesiredDirectionTraceCost)[layerCount - 1] += outerAddCosts;
      }
    }
    boardSpecificTraceCostsApplied = true;
  }

  // Log the changed parameters
  std::string summary = "applyBoardSpecificOptimizations changed parameters:";
  bool changed = false;
  for (int32_t i = 0; i < layerCount; i++) {
    std::string layerChanges;
    LayerSettings* layer = (*layers)[i];

    if (originalRoutable[i] != layer->routable) {
      layerChanges += " routable: " + jrt::str(originalRoutable[i]) + " -> " +
                      jrt::str(layer->routable) + ";";
    }
    if (originalBendCost[i] != layer->bendCost) {
      layerChanges += " bendCost: " + jrt::str(originalBendCost[i]) + " -> " +
                      jrt::str(layer->bendCost) + ";";
    }
    if (originalPrefHoriz[i] != layer->preferredDirectionHorizontal) {
      layerChanges += " preferredDirectionHorizontal: " + jrt::str(originalPrefHoriz[i]) +
                      " -> " + jrt::str(layer->preferredDirectionHorizontal) + ";";
    }

    double oldPrefCost = originalPrefCost != nullptr && i < originalPrefCost->length
                             ? (*originalPrefCost)[i]
                             : 0.0;
    if (oldPrefCost != (*scoring->preferredDirectionTraceCost)[i]) {
      layerChanges += " preferredDirectionTraceCost: " + jrt::str(oldPrefCost) + " -> " +
                      jrt::str((*scoring->preferredDirectionTraceCost)[i]) + ";";
    }

    double oldUndesiredCost =
        originalUndesiredCost != nullptr && i < originalUndesiredCost->length
            ? (*originalUndesiredCost)[i]
            : 0.0;
    if (oldUndesiredCost != (*scoring->undesiredDirectionTraceCost)[i]) {
      layerChanges += " undesiredDirectionTraceCost: " + jrt::str(oldUndesiredCost) + " -> " +
                      jrt::str((*scoring->undesiredDirectionTraceCost)[i]) + ";";
    }

    if (!layerChanges.empty()) {
      summary += "\n  Layer " + jrt::str(i) + ":" + layerChanges;
      changed = true;
    }
  }
  if (changed) {
    FRLogger::debug(summary);
  }
}

void RouterSettings::applyNetClassExclusions(RoutingBoard* board) {
  if (board == nullptr || board->rules == nullptr || board->rules->netClasses == nullptr) {
    return;
  }
  for (int32_t i = 0; i < board->rules->netClasses->count(); i++) {
    board->rules->netClasses->get(i)->isRoutedFirst = false;
  }
  if (this->autorouter == nullptr) {
    return;
  }
  for (NetClass* netClass :
       *resolveNetClasses(board->rules, this->autorouter->ignoreNetClasses, nullptr)) {
    netClass->isIgnoredByAutorouter = true;
  }
  for (NetClass* netClass :
       *resolveNetClasses(board->rules, this->autorouter->priorityNetClasses, nullptr)) {
    netClass->isRoutedFirst = true;
  }
}

int32_t RouterSettings::getPriorityViaCosts() {
  return autorouter != nullptr && autorouter->priorityViaCosts.has_value()
             ? jrt::Math::max(*autorouter->priorityViaCosts, 1)
             : getViaCosts();
}

jrt::List<NetClass*>* RouterSettings::resolveNetClasses(BoardRules* rules,
                                                        jrt::Array<std::string>* listed,
                                                        jrt::List<std::string>* unknown) {
  jrt::List<std::string>* names = new (jrt::kOp) jrt::ArrayList<std::string>();
  if (listed == nullptr) {
    return new (jrt::kOp) jrt::ArrayList<NetClass*>();
  }
  for (const std::string& name : *listed) {
    // PORT NOTE: Java's `name != null` check: a std::string element cannot be null.
    if (!isBlank(name)) {
      names->add(jrt::trim(name));
    }
  }
  jrt::List<NetClass*>* result = new (jrt::kOp) jrt::ArrayList<NetClass*>();
  for (int32_t i = 0; i < names->size(); i++) {
    NetClass* netClass = findNetClass(rules, names->get(i));
    std::string joined = names->get(i);
    for (int32_t j = i + 1; netClass == nullptr && j < names->size(); j++) {
      joined += ',';
      joined += names->get(j);
      netClass = findNetClass(rules, joined);
      if (netClass != nullptr) {
        i = j;
      }
    }
    if (netClass != nullptr) {
      result->add(netClass);
    } else if (unknown != nullptr) {
      unknown->add(names->get(i));
    }
  }
  return result;
}

NetClass* RouterSettings::findNetClass(BoardRules* rules, const std::string& name) {
  if (io::KiCadNetClassNames::isKiCadDefaultNetClassName(name)) {
    return rules->getDefaultNetClass();
  }
  for (int32_t i = 0; i < rules->netClasses->count(); i++) {
    if (jrt::equalsIgnoreCase(rules->netClasses->get(i)->getName(), name)) {
      return rules->netClasses->get(i);
    }
  }
  return nullptr;
}

void RouterSettings::populateEffectiveLayerCosts() {
  if (layers == nullptr || scoring == nullptr) {
    return;
  }
  for (int32_t i = 0; i < layers->length; i++) {
    if ((*layers)[i] != nullptr) {
      (*layers)[i]->preferredDirectionTraceCost = getPreferredDirectionTraceCosts(i);
      (*layers)[i]->undesiredDirectionTraceCost = getAgainstPreferredDirectionTraceCosts(i);
    }
  }
}

int32_t RouterSettings::getLayerCount() {
  if (layers == nullptr) {
    return 0;
  }

  return layers->length;
}

void RouterSettings::setLayerCount(int32_t layerCount) {
  if (layers == nullptr || layers->length != layerCount) {
    boardSpecificTraceCostsApplied = false;
    layers = new (jrt::kBoard) jrt::Array<LayerSettings*>(layerCount);
    for (int32_t i = 0; i < layerCount; i++) {
      (*layers)[i] = new (jrt::kBoard) LayerSettings();
    }
    if (scoring == nullptr) {
      scoring = new (jrt::kBoard) RoutingCostSettings();
    }
    // Initialize per-layer cost arrays with a neutral default so callers can
    // write individual entries without waiting for applyBoardSpecificOptimizations.
    scoring->preferredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(layerCount);
    scoring->undesiredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(layerCount);

    for (int32_t i = 0; i < layerCount; i++) {
      (*layers)[i]->routable = true;
      (*layers)[i]->preferredDirectionHorizontal = std::nullopt;
      (*layers)[i]->bendCost = std::nullopt;
      (*layers)[i]->preferredDirectionTraceCost = std::nullopt;
      (*layers)[i]->undesiredDirectionTraceCost = std::nullopt;
      (*scoring->preferredDirectionTraceCost)[i] = 1.0;
      (*scoring->undesiredDirectionTraceCost)[i] = 1.0;
    }
  }
}

RouterSettings* RouterSettings::clone() {
  auto* result = new (jrt::kBoard) RouterSettings();
  int32_t layerCount = this->getLayerCount();
  if (layerCount > 0) {
    result->setLayerCount(layerCount);
  }
  result->jobTimeoutString = this->jobTimeoutString;
  if (this->layers != nullptr) {
    result->layers = new (jrt::kOp) jrt::Array<LayerSettings*>(this->layers->length);
    for (int32_t i = 0; i < this->layers->length; i++) {
      if ((*this->layers)[i] != nullptr) {
        (*result->layers)[i] = (*this->layers)[i]->clone();
      }
    }
  }
  if (this->validationWarnings != nullptr) {
    result->validationWarnings = new (jrt::kOp) jrt::ArrayList<std::string>(
        static_cast<jrt::Collection<std::string>*>(this->validationWarnings));
  }
  result->copperToEdgeClearanceUm = this->copperToEdgeClearanceUm;
  result->holeClearanceUm = this->holeClearanceUm;
  result->holeToHoleUm = this->holeToHoleUm;
  result->clearanceToleranceUm = this->clearanceToleranceUm;
  result->drcEpsilonUm = this->drcEpsilonUm;
  result->meanderMinAmplitudeMm = this->meanderMinAmplitudeMm;
  result->meanderMaxAmplitudeMm = this->meanderMaxAmplitudeMm;
  result->meanderSpacingMm = this->meanderSpacingMm;
  result->meanderCornerStyle = this->meanderCornerStyle;
  result->meanderRadiusPercentage = this->meanderRadiusPercentage;
  result->meanderSingleSided = this->meanderSingleSided;
  result->planeNets = this->planeNets != nullptr ? this->planeNets->clone() : nullptr;
  result->planeAsObstacle = this->planeAsObstacle;
  result->neckWidthUm = this->neckWidthUm;
  result->exactCopper = this->exactCopper;
  result->strictDrc = this->strictDrc;
  result->tracePullTightAccuracy = this->tracePullTightAccuracy;
  result->viasAllowed = this->viasAllowed;
  result->automaticNeckdown = this->automaticNeckdown;
  result->maxThreads = this->maxThreads;

  // Use proper clone() methods for nested objects
  result->optimizer =
      this->optimizer != nullptr ? this->optimizer->clone() : new (jrt::kBoard) OptimizerSettings();
  result->scoring = this->scoring != nullptr ? this->scoring->clone() : new (jrt::kBoard) RoutingCostSettings();
  result->routerScoring =
      this->routerScoring != nullptr ? this->routerScoring->clone() : new (jrt::kBoard) RouterScoreSettings();
  result->optimizerScoring = this->optimizerScoring != nullptr ? this->optimizerScoring->clone()
                                                               : new (jrt::kBoard) OptimizerScoreSettings();
  result->fanout = this->fanout != nullptr ? this->fanout->clone() : new (jrt::kBoard) FanoutSettings();
  result->autorouter =
      this->autorouter != nullptr ? this->autorouter->clone() : new (jrt::kBoard) AutorouterSettings();
  result->boardSpecificTraceCostsApplied = this->boardSpecificTraceCostsApplied;

  return result;
}

double RouterSettings::getNeckWidthUm() {
  return neckWidthUm.has_value() && *neckWidthUm > 0 ? *neckWidthUm : 0;
}

void RouterSettings::setNeckWidthUm(double width) { this->neckWidthUm = width; }

bool RouterSettings::usesDirectPinRouting() {
  std::optional<std::string> strategy =
      this->fanout != nullptr ? this->fanout->strategy : std::nullopt;
  if (!strategy.has_value() || strategy->empty() || jrt::equalsIgnoreCase("eager", *strategy)) {
    return false;
  }
  if (!jrt::equalsIgnoreCase("direct_first", *strategy)) {
    throw jrt::IllegalArgumentException("router.fanout.strategy must be eager or direct_first");
  }
  return true;
}

bool RouterSettings::isStrictDrc() { return strictDrc.has_value() && *strictDrc; }

int32_t RouterSettings::getStartRipupCosts() {
  return scoring != nullptr && scoring->startRipupCosts.has_value() ? *scoring->startRipupCosts
                                                                     : 1;
}

void RouterSettings::setStartRipupCosts(int32_t value) {
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  scoring->startRipupCosts = jrt::Math::max(value, 1);
}

bool RouterSettings::getRunRouter() {
  std::optional<bool> enabled = autorouterOrCreate()->enabled;
  return enabled.has_value() ? *enabled : true;
}

void RouterSettings::setRunRouter(bool value) { autorouterOrCreate()->enabled = value; }

bool RouterSettings::getRunOptimizer() {
  return optimizer != nullptr && optimizer->enabled.has_value() ? *optimizer->enabled : false;
}

void RouterSettings::setRunOptimizer(bool value) {
  if (optimizer == nullptr) {
    optimizer = new (jrt::kBoard) OptimizerSettings();
  }
  optimizer->enabled = value;
}

bool RouterSettings::getRunFanout() {
  return fanout != nullptr && fanout->enabled.has_value() ? *fanout->enabled : true;
}

bool RouterSettings::isFanoutEnabled() {
  return fanout != nullptr && fanout->enabled.has_value() && *fanout->enabled;
}

void RouterSettings::setFanoutEnabled(std::optional<bool> value) {
  if (this->fanout == nullptr) {
    this->fanout = new (jrt::kBoard) FanoutSettings();
  }
  std::optional<bool> oldValue = this->fanout->enabled;
  this->fanout->enabled = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("fanout.enabled"),
                            PropertyChangeSupport::valueOf(oldValue),
                            PropertyChangeSupport::valueOf(value));
  }
}

bool RouterSettings::getViasAllowed() { return viasAllowed.has_value() ? *viasAllowed : true; }

int32_t RouterSettings::getViaCosts() {
  return scoring != nullptr && scoring->viaCosts.has_value() ? *scoring->viaCosts : 1;
}

void RouterSettings::setViaCosts(int32_t value) {
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  scoring->viaCosts = jrt::Math::max(value, 1);
}

int32_t RouterSettings::getPlaneViaCosts() {
  return scoring != nullptr && scoring->planeViaCosts.has_value() ? *scoring->planeViaCosts : 1;
}

void RouterSettings::setPlaneViaCosts(int32_t value) {
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  scoring->planeViaCosts = jrt::Math::max(value, 1);
}

jrt::Array<std::string>* RouterSettings::getPlaneNets() {
  return planeNets != nullptr ? planeNets->clone() : new (jrt::kOp) jrt::Array<std::string>(0);
}

void RouterSettings::setPlaneNets(jrt::Array<std::string>* value) {
  jrt::Array<std::string>* old = this->planeNets;
  this->planeNets = value != nullptr ? value->clone() : nullptr;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("planeNets"), PropertyChangeSupport::valueOf(old),
                            PropertyChangeSupport::valueOf(this->planeNets));
  }
}

std::optional<bool> RouterSettings::getPlaneAsObstacle() { return planeAsObstacle; }

bool RouterSettings::isPlaneAsObstacle() {
  return planeAsObstacle.has_value() && *planeAsObstacle;
}

void RouterSettings::setPlaneAsObstacle(std::optional<bool> value) {
  std::optional<bool> old = this->planeAsObstacle;
  this->planeAsObstacle = value;
  if (pcs != nullptr) {
    pcs->firePropertyChange(std::string("planeAsObstacle"), PropertyChangeSupport::valueOf(old),
                            PropertyChangeSupport::valueOf(value));
  }
}

void RouterSettings::setLayerActive(int32_t layer, bool value) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.set_layer_active: layer=" + jrt::str(layer) +
                   " out of range [0.." + jrt::str(this->getLayerCount() - 1) + "]");
    return;
  }
  if ((*layers)[layer] == nullptr) {
    (*layers)[layer] = new (jrt::kBoard) LayerSettings();
  }
  (*layers)[layer]->routable = value;
}

bool RouterSettings::getLayerActive(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.get_layer_active: layer=" + jrt::str(layer) +
                   " out of range [0.." + jrt::str(this->getLayerCount() - 1) + "]");
    return false;
  }
  if ((*layers)[layer] == nullptr) {
    return true;
  }
  return (*layers)[layer]->routable.has_value() ? *(*layers)[layer]->routable : true;
}

void RouterSettings::setBendCost(int32_t layer, double value) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("RouterSettings.set_bend_cost: layer out of range");
    return;
  }
  if ((*layers)[layer] == nullptr) {
    (*layers)[layer] = new (jrt::kBoard) LayerSettings();
  }
  (*layers)[layer]->bendCost =
      jrt::Math::max(MIN_BEND_COST, jrt::Math::min(MAX_BEND_COST, value));
}

double RouterSettings::getBendCost(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("RouterSettings.get_bend_cost: layer out of range");
    return 0.0;
  }
  if ((*layers)[layer] == nullptr || !(*layers)[layer]->bendCost.has_value()) {
    return scoring != nullptr && scoring->defaultBendCost.has_value()
               ? jrt::Math::max(MIN_BEND_COST,
                                jrt::Math::min(MAX_BEND_COST, *scoring->defaultBendCost))
               : 0.0;
  }
  return *(*layers)[layer]->bendCost;
}

void RouterSettings::setPreferredDirectionIsHorizontal(int32_t layer, bool value) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.set_preferred_direction_is_horizontal: layer=" +
                   jrt::str(layer) + " out of range [0.." + jrt::str(this->getLayerCount() - 1) +
                   "]");
    return;
  }
  if ((*layers)[layer] == nullptr) {
    (*layers)[layer] = new (jrt::kBoard) LayerSettings();
  }
  (*layers)[layer]->preferredDirectionHorizontal = value;
}

bool RouterSettings::getPreferredDirectionIsHorizontal(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.get_preferred_direction_is_horizontal: layer=" +
                   jrt::str(layer) + " out of range [0.." + jrt::str(this->getLayerCount() - 1) +
                   "]");
    return false;
  }
  if ((*layers)[layer] == nullptr) {
    return layer % 2 == 1;
  }
  return (*layers)[layer]->preferredDirectionHorizontal.has_value()
             ? *(*layers)[layer]->preferredDirectionHorizontal
             : (layer % 2 == 1);
}

void RouterSettings::setPreferredDirectionTraceCosts(int32_t layer, double value) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.set_preferred_direction_trace_costs: layer out of range");
    return;
  }
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  if (scoring->preferredDirectionTraceCost == nullptr ||
      scoring->preferredDirectionTraceCost->length != this->getLayerCount()) {
    scoring->preferredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(this->getLayerCount());
  }
  (*scoring->preferredDirectionTraceCost)[layer] = jrt::Math::max(value, 0.1);
  if (layers != nullptr && layer < layers->length && (*layers)[layer] != nullptr) {
    (*layers)[layer]->preferredDirectionTraceCost = (*scoring->preferredDirectionTraceCost)[layer];
  }
  boardSpecificTraceCostsApplied = true;
}

double RouterSettings::getPreferredDirectionTraceCosts(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.get_preferred_direction_trace_costs: layer out of range");
    return 0;
  }
  if (layers != nullptr && layer < layers->length && (*layers)[layer] != nullptr &&
      (*layers)[layer]->preferredDirectionTraceCost.has_value()) {
    return *(*layers)[layer]->preferredDirectionTraceCost;
  }
  if (scoring == nullptr || scoring->preferredDirectionTraceCost == nullptr ||
      layer >= scoring->preferredDirectionTraceCost->length) {
    return 1.0;
  }
  return (*scoring->preferredDirectionTraceCost)[layer];
}

double RouterSettings::getAgainstPreferredDirectionTraceCosts(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn(
        "AutorouteSettings.get_against_preferred_direction_trace_costs: layer out of range");
    return 0;
  }
  if (layers != nullptr && layer < layers->length && (*layers)[layer] != nullptr &&
      (*layers)[layer]->undesiredDirectionTraceCost.has_value()) {
    return *(*layers)[layer]->undesiredDirectionTraceCost;
  }
  if (scoring == nullptr || scoring->undesiredDirectionTraceCost == nullptr ||
      layer >= scoring->undesiredDirectionTraceCost->length) {
    return 1.0;
  }
  return (*scoring->undesiredDirectionTraceCost)[layer];
}

double RouterSettings::getHorizontalTraceCosts(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn("AutorouteSettings.get_preferred_direction_trace_costs: layer out of range");
    return 0;
  }
  double result;
  if (getPreferredDirectionIsHorizontal(layer)) {
    result = getPreferredDirectionTraceCosts(layer);
  } else {
    result = getAgainstPreferredDirectionTraceCosts(layer);
  }
  return result;
}

void RouterSettings::setAgainstPreferredDirectionTraceCosts(int32_t layer, double value) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn(
        "AutorouteSettings.set_against_preferred_direction_trace_costs: layer out of range");
    return;
  }
  if (scoring == nullptr) {
    scoring = new (jrt::kBoard) RoutingCostSettings();
  }
  if (scoring->undesiredDirectionTraceCost == nullptr ||
      scoring->undesiredDirectionTraceCost->length != this->getLayerCount()) {
    scoring->undesiredDirectionTraceCost = new (jrt::kOp) jrt::Array<double>(this->getLayerCount());
  }
  (*scoring->undesiredDirectionTraceCost)[layer] = jrt::Math::max(value, 0.1);
  if (layers != nullptr && layer < layers->length && (*layers)[layer] != nullptr) {
    (*layers)[layer]->undesiredDirectionTraceCost = (*scoring->undesiredDirectionTraceCost)[layer];
  }
  boardSpecificTraceCostsApplied = true;
}

double RouterSettings::getVerticalTraceCosts(int32_t layer) {
  if (layer < 0 || layer >= this->getLayerCount()) {
    FRLogger::warn(
        "AutorouteSettings.get_against_preferred_direction_trace_costs: layer out of range");
    return 0;
  }
  double result;
  if (getPreferredDirectionIsHorizontal(layer)) {
    result = getAgainstPreferredDirectionTraceCosts(layer);
  } else {
    result = getPreferredDirectionTraceCosts(layer);
  }
  return result;
}

jrt::Array<AutorouteControl_ExpansionCostFactor*>* RouterSettings::getTraceCosts() {
  if (scoring == nullptr || scoring->preferredDirectionTraceCost == nullptr) {
    return new (jrt::kOp) jrt::Array<AutorouteControl_ExpansionCostFactor*>(0);
  }
  auto* result = new (jrt::kOp) jrt::Array<AutorouteControl_ExpansionCostFactor*>(
      scoring->preferredDirectionTraceCost->length);
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] =
        new (jrt::kOp) AutorouteControl_ExpansionCostFactor(getHorizontalTraceCosts(i), getVerticalTraceCosts(i));
  }
  return result;
}

bool RouterSettings::getAutomaticNeckdown() {
  return this->automaticNeckdown.has_value() && *this->automaticNeckdown;
}

void RouterSettings::setAutomaticNeckdown(bool value) { this->automaticNeckdown = value; }

int32_t RouterSettings::applyNewValuesFrom(RouterSettings* settings) {
  if (settings == nullptr) {
    FRLogger::warn("Attempted to apply null settings, skipping");
    return 0;
  }

  int32_t changedCount = copyNewValuesFrom(settings);

  // Fire property change events for key properties to update GUI
  // Note: We fire events even if values didn't change to ensure GUI is in sync
  if (pcs != nullptr) {
    pcs->firePropertyChange(
        std::string("maxPasses"), std::optional<std::string>(),
        PropertyChangeSupport::valueOf(this->autorouter != nullptr ? this->autorouter->maxPasses
                                                                   : std::nullopt));
    pcs->firePropertyChange(std::string("maxThreads"), std::optional<std::string>(),
                            PropertyChangeSupport::valueOf(this->maxThreads));
    pcs->firePropertyChange(std::string("jobTimeoutString"), std::optional<std::string>(),
                            this->jobTimeoutString);
    pcs->firePropertyChange(
        std::string("enabled"), std::optional<std::string>(),
        PropertyChangeSupport::valueOf(this->autorouter != nullptr ? this->autorouter->enabled
                                                                   : std::nullopt));
    pcs->firePropertyChange(
        std::string("optimizer.enabled"), std::optional<std::string>(),
        PropertyChangeSupport::valueOf(this->optimizer != nullptr ? this->optimizer->enabled
                                                                  : std::nullopt));
    pcs->firePropertyChange(
        std::string("fanout.enabled"), std::optional<std::string>(),
        PropertyChangeSupport::valueOf(this->fanout != nullptr ? this->fanout->enabled
                                                               : std::nullopt));
  }

  return changedCount;
}

int32_t RouterSettings::copyNewValuesFrom(RouterSettings* source) {
  int32_t n = 0;
  n += settings_copy::nested(autorouter, source->autorouter);
  n += settings_copy::nested(fanout, source->fanout);
  n += settings_copy::value(copperToEdgeClearanceUm, source->copperToEdgeClearanceUm);
  n += settings_copy::value(holeClearanceUm, source->holeClearanceUm);
  n += settings_copy::value(holeToHoleUm, source->holeToHoleUm);
  n += settings_copy::value(clearanceToleranceUm, source->clearanceToleranceUm);
  n += settings_copy::value(drcEpsilonUm, source->drcEpsilonUm);
  n += settings_copy::array(planeNets, source->planeNets);
  n += settings_copy::value(planeAsObstacle, source->planeAsObstacle);
  n += settings_copy::value(meanderMinAmplitudeMm, source->meanderMinAmplitudeMm);
  n += settings_copy::value(meanderMaxAmplitudeMm, source->meanderMaxAmplitudeMm);
  n += settings_copy::value(meanderSpacingMm, source->meanderSpacingMm);
  n += settings_copy::value(meanderCornerStyle, source->meanderCornerStyle);
  n += settings_copy::value(meanderRadiusPercentage, source->meanderRadiusPercentage);
  n += settings_copy::value(meanderSingleSided, source->meanderSingleSided);
  n += settings_copy::value(neckWidthUm, source->neckWidthUm);
  n += settings_copy::value(exactCopper, source->exactCopper);
  n += settings_copy::value(strictDrc, source->strictDrc);
  n += settings_copy::value(fixKicadFootprints, source->fixKicadFootprints);
  n += settings_copy::value(jobTimeoutString, source->jobTimeoutString);
  n += settings_copy::objectArray(layers, source->layers);
  n += settings_copy::list(validationWarnings, source->validationWarnings);
  n += settings_copy::value(tracePullTightAccuracy, source->tracePullTightAccuracy);
  n += settings_copy::value(viasAllowed, source->viasAllowed);
  n += settings_copy::value(automaticNeckdown, source->automaticNeckdown);
  n += settings_copy::nested(optimizer, source->optimizer);
  n += settings_copy::nested(scoring, source->scoring);
  n += settings_copy::nested(routerScoring, source->routerScoring);
  n += settings_copy::nested(optimizerScoring, source->optimizerScoring);
  n += settings_copy::value(maxThreads, source->maxThreads);
  n += settings_copy::value(resultJsonPath, source->resultJsonPath);
  n += settings_copy::value(boardSpecificTraceCostsApplied, source->boardSpecificTraceCostsApplied);
  return n;
}

void RouterSettings::validateAgainstBoard(RoutingBoard* board) {
  this->validationWarnings = new (jrt::kBoard) jrt::ArrayList<std::string>();

  // Validate maxPasses (0 means no limit)
  if (this->autorouter != nullptr && this->autorouter->maxPasses.has_value()) {
    if (*this->autorouter->maxPasses < 0 ||
        (*this->autorouter->maxPasses > 9999 &&
         *this->autorouter->maxPasses != jrt::Integer::MAX_VALUE)) {
      std::string warning = "Invalid maxPasses value: " + jrt::str(*this->autorouter->maxPasses) +
                            ", using default 0 (no limit)";
      FRLogger::warn(warning);
      this->validationWarnings->add(warning);
      this->autorouter->maxPasses = 0;
    }
  }

  // Validate maxThreads if explicitly specified (0 means no limit - handled during normalization)
  int32_t availableProcessorsCount = availableProcessors();
  if (this->maxThreads.has_value()) {
    if (*this->maxThreads < 0) {
      std::string warning = "Invalid maxThreads value: " + jrt::str(*this->maxThreads) +
                            ", using " + jrt::str(defaultMaxThreads());
      FRLogger::warn(warning);
      this->validationWarnings->add(warning);
      this->maxThreads = defaultMaxThreads();
    } else if (*this->maxThreads > availableProcessorsCount) {
      std::string warning = "Invalid maxThreads value: " + jrt::str(*this->maxThreads) +
                            ", capping at " + jrt::str(availableProcessorsCount);
      FRLogger::warn(warning);
      this->validationWarnings->add(warning);
      this->maxThreads = availableProcessorsCount;
    }
  }

  // Validate tracePullTightAccuracy
  if (this->tracePullTightAccuracy.has_value() && *this->tracePullTightAccuracy < 1) {
    std::string warning = "Invalid tracePullTightAccuracy value: " +
                          jrt::str(*this->tracePullTightAccuracy) + ", using default 500";
    FRLogger::warn(warning);
    this->validationWarnings->add(warning);
    this->tracePullTightAccuracy = 500;
  }

  // Validate layer costs and bounds
  if (this->layers != nullptr) {
    for (int32_t i = 0; i < this->layers->length; i++) {
      LayerSettings* layer = (*this->layers)[i];
      if (layer == nullptr) {
        continue;
      }
      if (layer->bendCost.has_value() &&
          (*layer->bendCost < MIN_BEND_COST || *layer->bendCost > MAX_BEND_COST)) {
        double clamped = jrt::Math::max(MIN_BEND_COST, jrt::Math::min(MAX_BEND_COST, *layer->bendCost));
        std::string warning = "Layer " + jrt::str(i) + " bendCost " + jrt::str(*layer->bendCost) +
                              " out of range [" + jrt::str(MIN_BEND_COST) + ", " +
                              jrt::str(MAX_BEND_COST) + "], clamped to " + jrt::str(clamped);
        FRLogger::warn(warning);
        this->validationWarnings->add(warning);
        layer->bendCost = clamped;
      }
      if (layer->preferredDirectionTraceCost.has_value() &&
          *layer->preferredDirectionTraceCost < 0.1) {
        std::string warning = "Layer " + jrt::str(i) + " preferredDirectionTraceCost " +
                              jrt::str(*layer->preferredDirectionTraceCost) +
                              " below 0.1, clamped to 0.1";
        FRLogger::warn(warning);
        this->validationWarnings->add(warning);
        layer->preferredDirectionTraceCost = 0.1;
      }
      if (layer->undesiredDirectionTraceCost.has_value() &&
          *layer->undesiredDirectionTraceCost < 0.1) {
        std::string warning = "Layer " + jrt::str(i) + " undesiredDirectionTraceCost " +
                              jrt::str(*layer->undesiredDirectionTraceCost) +
                              " below 0.1, clamped to 0.1";
        FRLogger::warn(warning);
        this->validationWarnings->add(warning);
        layer->undesiredDirectionTraceCost = 0.1;
      }
    }
  }

  // Validate ignoreNetClasses against board
  if (board != nullptr && board->rules != nullptr && board->rules->netClasses != nullptr &&
      this->autorouter != nullptr) {
    jrt::Array<std::string>* lists[] = {this->autorouter->ignoreNetClasses,
                                        this->autorouter->priorityNetClasses};
    const char* keys[] = {"ignoreNetClasses", "priorityNetClasses"};
    for (int32_t k = 0; k < 2; k++) {
      jrt::List<std::string>* unknown = new (jrt::kOp) jrt::ArrayList<std::string>();
      resolveNetClasses(board->rules, lists[k], unknown);
      for (const std::string& netClassName : *unknown) {
        std::string warning = "Unknown net class '" + netClassName + "' specified in " +
                              std::string(keys[k]) + " (not present on board)";
        FRLogger::warn(warning);
        this->validationWarnings->add(warning);
      }
    }
  }
}

void RouterSettings::validate() { validateAgainstBoard(nullptr); }

}  // namespace fr
