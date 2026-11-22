#include "settings/OptimizerScoreSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

OptimizerScoreSettings* OptimizerScoreSettings::clone() {
  auto* c = new (jrt::kBoard) OptimizerScoreSettings();
  c->version = version;
  c->excessWireLengthWeight = excessWireLengthWeight;
  c->excessViaWeight = excessViaWeight;
  c->excessBendWeight = excessBendWeight;
  c->lengthFloor = lengthFloor;
  c->difficultyScaleFloor = difficultyScaleFloor;
  return c;
}

int32_t OptimizerScoreSettings::copyNewValuesFrom(OptimizerScoreSettings* s) {
  int32_t n = 0;
  n += settings_copy::enumValue(version, s->version);
  n += settings_copy::value(excessWireLengthWeight, s->excessWireLengthWeight);
  n += settings_copy::value(excessViaWeight, s->excessViaWeight);
  n += settings_copy::value(excessBendWeight, s->excessBendWeight);
  n += settings_copy::value(lengthFloor, s->lengthFloor);
  n += settings_copy::value(difficultyScaleFloor, s->difficultyScaleFloor);
  return n;
}

}  // namespace fr
