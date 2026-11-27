// Local REST API v1 of freerouting-cpp (PORT EXTENSION; Java: api.v1.*Controller, served by
// Jetty/Jersey). Same paths and JSON shapes as Java's where the port has the data; bound to
// 127.0.0.1 by default. Not served: API keys / analytics / rate limiting (cloud service parts),
// Swagger, KiCad JSON input and output (/input/json, /output/json), the KiCad DRC report (/drc;
// /drc/summary is served). POST /v1/mcp is the MCP server over HTTP (McpServer).
#pragma once

#include <memory>
#include <string>

namespace frserver {

struct RestOptions {
  std::string host = "127.0.0.1";
  int port = 37864;  // Java api_server default
};

/** The API server: routes registered on construction, then bind, then listen (blocks) until stop. */
class RestServer {
 public:
  RestServer();
  ~RestServer();
  RestServer(const RestServer&) = delete;
  RestServer& operator=(const RestServer&) = delete;

  /** Binds to host:port (port 0: a free one); returns the port, or -1. */
  int bind(const std::string& host, int port);
  /** Serves requests until stop(); false if it could not. */
  bool listen();
  /** Stops listen() (from another thread). */
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/** Serves the API until the process is stopped; returns the exit code (1 if it cannot listen). */
int runRestServer(const RestOptions& options);

}  // namespace frserver
