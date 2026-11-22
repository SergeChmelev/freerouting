#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Router-related counters that reflect the progress of the router. Pure data, no .cpp. */
// PORT NOTE: Serializable / gson annotations dropped.
class RouterCounters : public virtual jrt::Object {
 public:
  // "pass_count"
  std::optional<int32_t> passCount;
  // "queued_to_be_routed_count": items in the queue to be routed in the current pass
  std::optional<int32_t> queuedToBeRoutedCount;
  // "routed_count": items that got successfully routed in this pass
  std::optional<int32_t> routedCount;
  // "skipped_count": items that were skipped in this pass
  std::optional<int32_t> skippedCount;
  // "ripped_count": items that were ripped in this pass
  std::optional<int32_t> rippedCount;
  // "failed_to_be_routed_count": items that failed to be routed in this pass
  std::optional<int32_t> failedToBeRoutedCount;
  // "incomplete_count": items still in the ratsnest (not yet routed)
  std::optional<int32_t> incompleteCount;
  // "phase": optional phase marker (e.g. "autoroute" or "fanout") for GUI progress rendering
  std::optional<std::string> phase;
  // "fanout_extra_vias_count": fanout-only number of additional vias in the current pass
  std::optional<int32_t> fanoutExtraViasCount;
};

}  // namespace fr
