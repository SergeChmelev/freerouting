#include "settings/OptimizerSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

OptimizerSettings* OptimizerSettings::clone() {
  auto* r = new OptimizerSettings();
  r->enabled = enabled;
  r->algorithm = algorithm;
  r->maxPasses = maxPasses;
  r->maxItems = maxItems;
  r->maxThreads = maxThreads;
  r->optimizationImprovementThreshold = optimizationImprovementThreshold;
  r->enablePreflightGuards = enablePreflightGuards;
  r->maxConsecutiveFailures = maxConsecutiveFailures;
  r->maxConsecutiveFailuresPass1 = maxConsecutiveFailuresPass1;
  r->additionalRipupCostFactorAtStart = additionalRipupCostFactorAtStart;
  r->keepIncreasedRipupCosts = keepIncreasedRipupCosts;
  r->viaNudgeEnabled = viaNudgeEnabled;
  r->stepSlideEnabled = stepSlideEnabled;
  r->traceRipupCostFactor = traceRipupCostFactor;
  r->maxAutoroutePasses = maxAutoroutePasses;
  r->boardUpdateStrategy = boardUpdateStrategy;
  r->itemSelectionStrategy = itemSelectionStrategy;
  r->timeoutString = timeoutString;
  return r;
}

int32_t OptimizerSettings::copyNewValuesFrom(OptimizerSettings* s) {
  int32_t n = 0;
  n += settings_copy::value(enabled, s->enabled);
  n += settings_copy::value(algorithm, s->algorithm);
  n += settings_copy::value(maxPasses, s->maxPasses);
  n += settings_copy::value(maxItems, s->maxItems);
  n += settings_copy::value(maxThreads, s->maxThreads);
  n += settings_copy::value(optimizationImprovementThreshold,
                            s->optimizationImprovementThreshold);
  n += settings_copy::value(enablePreflightGuards, s->enablePreflightGuards);
  n += settings_copy::value(maxConsecutiveFailures, s->maxConsecutiveFailures);
  n += settings_copy::value(maxConsecutiveFailuresPass1, s->maxConsecutiveFailuresPass1);
  n += settings_copy::value(additionalRipupCostFactorAtStart,
                            s->additionalRipupCostFactorAtStart);
  n += settings_copy::value(keepIncreasedRipupCosts, s->keepIncreasedRipupCosts);
  n += settings_copy::value(viaNudgeEnabled, s->viaNudgeEnabled);
  n += settings_copy::value(stepSlideEnabled, s->stepSlideEnabled);
  n += settings_copy::value(traceRipupCostFactor, s->traceRipupCostFactor);
  n += settings_copy::value(maxAutoroutePasses, s->maxAutoroutePasses);
  n += settings_copy::enumValue(boardUpdateStrategy, s->boardUpdateStrategy);
  n += settings_copy::enumValue(itemSelectionStrategy, s->itemSelectionStrategy);
  n += settings_copy::value(timeoutString, s->timeoutString);
  return n;
}

}  // namespace fr
