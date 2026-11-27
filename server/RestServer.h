// Local REST API v1 of freerouting-cpp (PORT EXTENSION; Java: api.v1.*Controller, served by
// Jetty/Jersey). Same paths and JSON shapes as Java's where the port has the data; bound to
// 127.0.0.1 by default. Not served: API keys / analytics / rate limiting (cloud service parts),
// Swagger, KiCad JSON input and output (/input/json, /output/json), the KiCad DRC report (/drc;
// /drc/summary is served). POST /v1/mcp is the MCP server over HTTP (McpServer).
#pragma once

#include <string>

namespace frserver {

struct RestOptions {
  std::string host = "127.0.0.1";
  int port = 37864;  // Java api_server default
};

/** Serves the API until the process is stopped; returns the exit code (1 if it cannot listen). */
int runRestServer(const RestOptions& options);

}  // namespace frserver
