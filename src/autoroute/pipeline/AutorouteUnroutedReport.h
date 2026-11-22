#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Formats the diagnostic report emitted when autoroute stagnates. */
class AutorouteUnroutedReport : public virtual jrt::Object {
 private:
  AutorouteUnroutedReport() = default;

 public:
  static std::string build(RoutingBoard* board);

 private:
  /**
   * Returns a short, user-friendly description of a board item for the stagnation report. For
   * pins the format is ComponentName-PinName; all other item types use a generic form.
   */
  static std::string describeItem(RoutingBoard* board, Item* item);
};

}  // namespace fr
