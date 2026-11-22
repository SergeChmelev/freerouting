#include "settings/RoutingCostSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

RoutingCostSettings* RoutingCostSettings::clone() {
  auto* r = new RoutingCostSettings();
  r->preferredDirectionTraceCost = preferredDirectionTraceCost;
  r->undesiredDirectionTraceCost = undesiredDirectionTraceCost;
  r->defaultPreferredDirectionTraceCost = defaultPreferredDirectionTraceCost;
  r->defaultUndesiredDirectionTraceCost = defaultUndesiredDirectionTraceCost;
  r->viaCosts = viaCosts;
  r->planeViaCosts = planeViaCosts;
  r->startRipupCosts = startRipupCosts;
  r->defaultBendCost = defaultBendCost;
  r->unroutedNetPenalty = unroutedNetPenalty;
  r->clearanceViolationPenalty = clearanceViolationPenalty;
  r->bendPenalty = bendPenalty;
  if (this->preferredDirectionTraceCost != nullptr) {
    r->preferredDirectionTraceCost = this->preferredDirectionTraceCost->clone();
  }
  if (this->undesiredDirectionTraceCost != nullptr) {
    r->undesiredDirectionTraceCost = this->undesiredDirectionTraceCost->clone();
  }
  return r;
}

int32_t RoutingCostSettings::copyNewValuesFrom(RoutingCostSettings* s) {
  int32_t n = 0;
  n += settings_copy::array(preferredDirectionTraceCost, s->preferredDirectionTraceCost);
  n += settings_copy::array(undesiredDirectionTraceCost, s->undesiredDirectionTraceCost);
  n += settings_copy::value(defaultPreferredDirectionTraceCost,
                            s->defaultPreferredDirectionTraceCost);
  n += settings_copy::value(defaultUndesiredDirectionTraceCost,
                            s->defaultUndesiredDirectionTraceCost);
  n += settings_copy::value(viaCosts, s->viaCosts);
  n += settings_copy::value(planeViaCosts, s->planeViaCosts);
  n += settings_copy::value(startRipupCosts, s->startRipupCosts);
  n += settings_copy::value(defaultBendCost, s->defaultBendCost);
  n += settings_copy::value(unroutedNetPenalty, s->unroutedNetPenalty);
  n += settings_copy::value(clearanceViolationPenalty, s->clearanceViolationPenalty);
  n += settings_copy::value(bendPenalty, s->bendPenalty);
  return n;
}

}  // namespace fr
