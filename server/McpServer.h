// MCP (Model Context Protocol) server of freerouting-cpp (PORT EXTENSION; Java:
// api.mcp.McpControllerV1, whose tools are generated from the REST API). JSON-RPC 2.0, over stdio
// (`freerouting-cpp --mcp`, one JSON message per line) or over HTTP (POST /v1/mcp of the REST
// server). The tools route a design on the local engine (JobManager): one-call autoroute_board,
// job progress / cancel / output / logs, DRC summary.
#pragma once

#include <functional>
#include <mutex>
#include <string>

#include "Json.h"

namespace frserver {

class McpServer {
 public:
  using Notify = std::function<void(const json&)>;

  McpServer();

  /**
   * Handles one JSON-RPC message. Returns the response, or a null json for a notification.
   * `notify` sends a notification to the client (progress), may be null; `cancelled` tells
   * whether the client cancelled this request (or went away), may be null.
   */
  json handle(const json& message, const Notify& notify = nullptr, const std::function<bool()>& cancelled = nullptr);

  /** Serves MCP over stdin / stdout until stdin is closed; returns the exit code. */
  int runStdio();

  /** The tool list (tools/list). */
  static const json& tools();

 private:
  json callTool(const std::string& name, const json& args, const json& progressToken, const Notify& notify,
                const std::function<bool()>& cancelled);
  std::string session();

  std::mutex mutex_;
  std::string sessionId_;
  std::string clientInfo_ = "MCP-Client/1.0";
};

}  // namespace frserver
