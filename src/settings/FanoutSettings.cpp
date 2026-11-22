#include "settings/FanoutSettings.h"

#include "settings/SettingsCopy.h"

namespace fr {

FanoutSettings* FanoutSettings::clone() {
  auto* c = new FanoutSettings();
  c->enabled = enabled;
  c->maxPasses = maxPasses;
  c->maxItems = maxItems;
  c->maxMillisecondsPerPin = maxMillisecondsPerPin;
  c->ripupAllowed = ripupAllowed;
  c->minEscapeLengthMm = minEscapeLengthMm;
  c->maxEscapeLengthMm = maxEscapeLengthMm;
  c->startViaDiameterMm = startViaDiameterMm;
  c->endViaDiameterMm = endViaDiameterMm;
  c->pinSortingOrder = pinSortingOrder;
  c->fallbackToBoardVias = fallbackToBoardVias;
  c->retryWithoutEscapeWindow = retryWithoutEscapeWindow;
  c->strategy = strategy;
  c->twoLayerBga = twoLayerBga;
  c->timeoutString = timeoutString;
  c->bgaEscapeEnabled = bgaEscapeEnabled;
  c->bgaEscapeMinPins = bgaEscapeMinPins;
  return c;
}

int32_t FanoutSettings::copyNewValuesFrom(FanoutSettings* s) {
  int32_t n = 0;
  n += settings_copy::value(enabled, s->enabled);
  n += settings_copy::value(maxPasses, s->maxPasses);
  n += settings_copy::value(maxItems, s->maxItems);
  n += settings_copy::value(maxMillisecondsPerPin, s->maxMillisecondsPerPin);
  n += settings_copy::value(ripupAllowed, s->ripupAllowed);
  n += settings_copy::value(minEscapeLengthMm, s->minEscapeLengthMm);
  n += settings_copy::value(maxEscapeLengthMm, s->maxEscapeLengthMm);
  n += settings_copy::value(startViaDiameterMm, s->startViaDiameterMm);
  n += settings_copy::value(endViaDiameterMm, s->endViaDiameterMm);
  n += settings_copy::value(pinSortingOrder, s->pinSortingOrder);
  n += settings_copy::value(fallbackToBoardVias, s->fallbackToBoardVias);
  n += settings_copy::value(retryWithoutEscapeWindow, s->retryWithoutEscapeWindow);
  n += settings_copy::value(strategy, s->strategy);
  n += settings_copy::value(twoLayerBga, s->twoLayerBga);
  n += settings_copy::value(timeoutString, s->timeoutString);
  n += settings_copy::value(bgaEscapeEnabled, s->bgaEscapeEnabled);
  n += settings_copy::value(bgaEscapeMinPins, s->bgaEscapeMinPins);
  return n;
}

}  // namespace fr
