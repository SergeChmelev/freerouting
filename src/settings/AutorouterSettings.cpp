#include "settings/AutorouterSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

AutorouterSettings* AutorouterSettings::clone() {
  auto* copy = new AutorouterSettings();
  copy->enabled = enabled;
  copy->algorithm = algorithm;
  copy->maxPasses = maxPasses;
  copy->stagnationPassLimit = stagnationPassLimit;
  copy->maxItems = maxItems;
  copy->maxThreads = maxThreads;
  copy->saveIntermediateStages = saveIntermediateStages;
  copy->ignoreNetClasses = ignoreNetClasses;
  copy->priorityNetClasses = priorityNetClasses;
  copy->priorityViaCosts = priorityViaCosts;
  copy->startNecked = startNecked;
  copy->fallbackStrategies = fallbackStrategies;
  copy->exploreFallbackStrategies = exploreFallbackStrategies;
  copy->smdNetViaCostFactor = smdNetViaCostFactor;
  copy->residualRepairMaxOpen = residualRepairMaxOpen;
  copy->explorePatience = explorePatience;
  copy->itemOrderSeed = itemOrderSeed;
  copy->planeAsPriority = planeAsPriority;
  copy->planeShareGapUm = planeShareGapUm;
  copy->viaInPad = viaInPad;
  copy->viaTouchingPad = viaTouchingPad;
  copy->viaCentreOfGravity = viaCentreOfGravity;
  copy->fanoutViaPadGap = fanoutViaPadGap;
  copy->ballEscapeOrder = ballEscapeOrder;
  copy->exitStubs = exitStubs;
  if (this->ignoreNetClasses != nullptr) {
    copy->ignoreNetClasses = this->ignoreNetClasses->clone();
  }
  if (this->priorityNetClasses != nullptr) {
    copy->priorityNetClasses = this->priorityNetClasses->clone();
  }
  return copy;
}

int32_t AutorouterSettings::copyNewValuesFrom(AutorouterSettings* s) {
  int32_t n = 0;
  n += settings_copy::value(enabled, s->enabled);
  n += settings_copy::value(algorithm, s->algorithm);
  n += settings_copy::value(maxPasses, s->maxPasses);
  n += settings_copy::value(stagnationPassLimit, s->stagnationPassLimit);
  n += settings_copy::value(maxItems, s->maxItems);
  n += settings_copy::value(maxThreads, s->maxThreads);
  n += settings_copy::value(saveIntermediateStages, s->saveIntermediateStages);
  n += settings_copy::array(ignoreNetClasses, s->ignoreNetClasses);
  n += settings_copy::array(priorityNetClasses, s->priorityNetClasses);
  n += settings_copy::value(priorityViaCosts, s->priorityViaCosts);
  n += settings_copy::value(startNecked, s->startNecked);
  n += settings_copy::value(fallbackStrategies, s->fallbackStrategies);
  n += settings_copy::value(exploreFallbackStrategies, s->exploreFallbackStrategies);
  n += settings_copy::value(smdNetViaCostFactor, s->smdNetViaCostFactor);
  n += settings_copy::value(residualRepairMaxOpen, s->residualRepairMaxOpen);
  n += settings_copy::value(explorePatience, s->explorePatience);
  n += settings_copy::value(itemOrderSeed, s->itemOrderSeed);
  n += settings_copy::value(planeAsPriority, s->planeAsPriority);
  n += settings_copy::value(planeShareGapUm, s->planeShareGapUm);
  n += settings_copy::value(viaInPad, s->viaInPad);
  n += settings_copy::value(viaTouchingPad, s->viaTouchingPad);
  n += settings_copy::value(viaCentreOfGravity, s->viaCentreOfGravity);
  n += settings_copy::value(fanoutViaPadGap, s->fanoutViaPadGap);
  n += settings_copy::value(ballEscapeOrder, s->ballEscapeOrder);
  n += settings_copy::value(exitStubs, s->exitStubs);
  return n;
}

}  // namespace fr
