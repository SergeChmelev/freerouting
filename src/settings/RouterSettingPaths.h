// PORT EXTENSION: router settings by their JSON path, as Java's CLI sets them with
// "--router.<path>=<value>" (CliSettings.applyRouterSetting). Shared by the command line and the
// API / MCP servers (which flatten a nested settings JSON object into these paths).
#pragma once

#include <string>

#include "fwd.h"

namespace fr {

/**
 * Sets one router setting by its JSON path ("max_threads", "autorouter.max_passes",
 * "scoring.via_costs", "optimizer.enabled", ...; a leading "router." is ignored) to a value given
 * as text. Logs a warning and returns false if the path is unknown or the value is bad.
 */
bool applyRouterSetting(RouterSettings* s, const std::string& property, const std::string& value);

/** Whether a setting path is known (see applyRouterSetting). */
bool hasRouterSetting(const std::string& path);

}  // namespace fr
