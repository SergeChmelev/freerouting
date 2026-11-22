#include "autoroute/maze/MazeFanoutDiagnostics.h"

#include "autoroute/maze/AutorouteControl.h"
#include "logger/FRLogger.h"

namespace fr {

bool MazeFanoutDiagnostics::enabled() {
  return ctrl->isFanout && ctrl->fanoutStartPinName.has_value() &&
         jrt::startsWith(*ctrl->fanoutStartPinName, "U27-");
}

std::string MazeFanoutDiagnostics::label() {
  return !ctrl->fanoutStartPinName.has_value()
             ? "fanout-pin(net=" + jrt::str(ctrl->netNumber) + ")"
             : *ctrl->fanoutStartPinName;
}

std::string MazeFanoutDiagnostics::labelForLog() { return label(); }

void MazeFanoutDiagnostics::trace(const std::string& event, const std::string& message) {
  if (!enabled()) {
    return;
  }
  FRLogger::trace("FANOUT_DIAG event=" + event + ", pin=" + label() +
                  ", net=" + jrt::str(ctrl->netNumber) + ", " + message);
}

}  // namespace fr
