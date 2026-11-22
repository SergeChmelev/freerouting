#include "settings/RouterScoreSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

RouterScoreSettings* RouterScoreSettings::clone() {
  auto* c = new (jrt::kBoard) RouterScoreSettings();
  c->version = version;
  c->unroutedConnectionWeight = unroutedConnectionWeight;
  c->unroutedFreeFraction = unroutedFreeFraction;
  c->unroutedFirstHalfWeight = unroutedFirstHalfWeight;
  c->unroutedSecondHalfWeight = unroutedSecondHalfWeight;
  c->clearanceViolationCountWeight = clearanceViolationCountWeight;
  c->clearanceViolationDepthWeight = clearanceViolationDepthWeight;
  c->clearanceViolationDepthScale = clearanceViolationDepthScale;
  return c;
}

int32_t RouterScoreSettings::copyNewValuesFrom(RouterScoreSettings* s) {
  int32_t n = 0;
  n += settings_copy::enumValue(version, s->version);
  n += settings_copy::value(unroutedConnectionWeight, s->unroutedConnectionWeight);
  n += settings_copy::value(unroutedFreeFraction, s->unroutedFreeFraction);
  n += settings_copy::value(unroutedFirstHalfWeight, s->unroutedFirstHalfWeight);
  n += settings_copy::value(unroutedSecondHalfWeight, s->unroutedSecondHalfWeight);
  n += settings_copy::value(clearanceViolationCountWeight, s->clearanceViolationCountWeight);
  n += settings_copy::value(clearanceViolationDepthWeight, s->clearanceViolationDepthWeight);
  n += settings_copy::value(clearanceViolationDepthScale, s->clearanceViolationDepthScale);
  return n;
}

}  // namespace fr
