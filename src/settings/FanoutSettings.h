#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Configuration for the SMD-pin fanout pre-pass that runs before the main batch autorouter. During
 * fanout each single-layer (SMD) pin is given a short escape trace and a via so that the main
 * router can work pin-to-via rather than pin-to-pin.
 *
 * <p>All fields follow the RouterSettings nullable-field contract: an empty optional means "this
 * source has no opinion" and is skipped by SettingsMerger. Hardcoded defaults live exclusively in
 * DefaultSettings.
 */
// PORT NOTE: Serializable/Cloneable and gson annotations dropped (JSON names in comments).
class FanoutSettings : public virtual jrt::Object {
 public:
  /** "enabled": whether to run the fanout pre-pass at all. */
  std::optional<bool> enabled;
  /** "max_passes": maximum number of fanout passes (safety cap). */
  std::optional<int32_t> maxPasses;
  /** "max_items": maximum number of escape/fanout routing attempts in the fanout stage. */
  std::optional<int32_t> maxItems;
  /**
   * "max_milliseconds_per_pin": base time budget per SMD pin in pass 1; effective limit is
   * maxMillisecondsPerPin * passNumber.
   */
  std::optional<int64_t> maxMillisecondsPerPin;
  /** "ripup_allowed": whether fanout may rip up existing traces to make room for a via. */
  std::optional<bool> ripupAllowed;
  /** "min_escape_length_mm": minimum escape wire length (mm). */
  std::optional<double> minEscapeLengthMm;
  /** "max_escape_length_mm": maximum escape wire length (mm). */
  std::optional<double> maxEscapeLengthMm;
  /** "start_via_diameter_mm": diameter of starting/escape vias (mm). */
  std::optional<double> startViaDiameterMm;
  /** "end_via_diameter_mm": diameter of landing/end vias (mm). */
  std::optional<double> endViaDiameterMm;
  /** "pin_sorting_order": "inner_first", "outer_first" or "unsorted". */
  std::optional<std::string> pinSortingOrder;
  /** "fallback_to_board_vias": fall back to board-wide via rules if a net has none. */
  std::optional<bool> fallbackToBoardVias;
  /** "retry_without_escape_window": retry a failed escape once without the window. */
  std::optional<bool> retryWithoutEscapeWindow;
  /** "strategy": "eager" or "direct_first". */
  std::optional<std::string> strategy;
  /** "two_layer_bga": "off", "no_fanout" or "direct_first". */
  std::optional<std::string> twoLayerBga;
  /** "timeout": timeout for the fanout stage (e.g. "5m", "300s"); null = no timeout. */
  std::optional<std::string> timeoutString;
  /** "bga_escape_enabled": deterministic multi-layer BGA escape pre-pass. */
  std::optional<bool> bgaEscapeEnabled;
  /** "bga_escape_min_pins": minimum SMD pin count for the BGA escape pre-pass. */
  std::optional<int32_t> bgaEscapeMinPins;

  /** No-arg constructor required for deserialisation and clone(). */
  FanoutSettings() = default;

  /** Java clone(): shallow copy (all fields are immutable values). */
  virtual FanoutSettings* clone();

  /** PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). */
  int32_t copyNewValuesFrom(FanoutSettings* source);
};

}  // namespace fr
