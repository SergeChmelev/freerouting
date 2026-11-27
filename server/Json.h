// JSON of the API and MCP servers: nlohmann/json (cmake/ServerDeps.cmake, MIT), with the
// object keys kept in insertion order like Gson writes the Java server's responses.
#pragma once

#include <nlohmann/json.hpp>

namespace frserver {
using json = nlohmann::ordered_json;
}  // namespace frserver
