#include "McpServer.h"

#include <atomic>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "ApiCommon.h"
#include "JobManager.h"
#include "logger/FRLogger.h"
#include "platform/platform.h"

namespace frserver {

namespace {

// Protocol versions this server speaks (the newest first); the client's choice is kept if known.
const char* const kProtocolVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

json rpcResult(const json& id, json result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json rpcError(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", json{{"code", code}, {"message", message}}}};
}

/** A successful tool result: the structured object, also as text (clients without structuredContent). */
json toolResult(json structured) {
  json r = json::object();
  r["content"] = json::array({json{{"type", "text"}, {"text", structured.dump(2)}}});
  r["structuredContent"] = std::move(structured);
  r["isError"] = false;
  return r;
}

/** A failed tool call: an error the model can read and act on. */
json toolError(const std::string& message) {
  json r = json::object();
  r["content"] = json::array({json{{"type", "text"}, {"text", message}}});
  r["isError"] = true;
  return r;
}

std::string stringArg(const json& args, const char* name) {
  auto it = args.find(name);
  return it != args.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int32_t intArg(const json& args, const char* name, int32_t fallback) {
  auto it = args.find(name);
  return it != args.end() && it->is_number() ? it->get<int32_t>() : fallback;
}

json jobStatus(const JobInfo& j) {
  json s = jobToJson(j, true);
  if (j.input) s["input_file"] = j.input->filename;
  s["has_output"] = j.hasOutput;
  return s;
}

int32_t levelOf(const std::string& name) {
  if (name == "ERROR") return fr::FRLogger::LEVEL_ERROR;
  if (name == "WARN") return fr::FRLogger::LEVEL_WARN;
  if (name == "DEBUG") return fr::FRLogger::LEVEL_DEBUG;
  if (name == "TRACE") return fr::FRLogger::LEVEL_TRACE;
  return fr::FRLogger::LEVEL_INFO;
}

}  // namespace

McpServer::McpServer() = default;

const json& McpServer::tools() {
  static const json list = json::parse(
#include "McpTools.inc"
  );
  return list;
}

std::string McpServer::session() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sessionId_.empty()) sessionId_ = JobManager::instance().createSession(clientInfo_);
  return sessionId_;
}

json McpServer::handle(const json& message, const Notify& notify, const std::function<bool()>& cancelled) {
  if (!message.is_object() || message.value("jsonrpc", "") != "2.0" || !message.contains("method") ||
      !message["method"].is_string()) {
    return rpcError(message.is_object() && message.contains("id") ? message["id"] : json(), -32600, "Invalid Request");
  }
  const std::string method = message["method"].get<std::string>();
  const bool isNotification = !message.contains("id");
  const json id = isNotification ? json() : message["id"];
  const json params = message.contains("params") && message["params"].is_object() ? message["params"] : json::object();

  if (isNotification) return json();  // notifications/initialized, notifications/cancelled (stdio loop)

  try {
    if (method == "initialize") {
      if (params.contains("clientInfo") && params["clientInfo"].is_object()) {
        const json& ci = params["clientInfo"];
        std::lock_guard<std::mutex> lock(mutex_);
        clientInfo_ = ci.value("name", std::string("Unknown")) + "/" + ci.value("version", std::string("1.0"));
      }
      std::string version = kProtocolVersions[0];
      std::string asked = params.value("protocolVersion", "");
      for (const char* v : kProtocolVersions)
        if (asked == v) version = v;
      json result = json::object();
      result["protocolVersion"] = version;
      result["capabilities"] = json{{"tools", json{{"listChanged", false}}}};
      result["serverInfo"] = json{{"name", "freerouting-cpp"}, {"title", "Freerouting (C++)"}, {"version", kServerVersion}};
      result["instructions"] =
          "PCB autorouter (freerouting, C++). Route a Specctra DSN design with autoroute_board "
          "(filePath + outputPath keep the files out of the conversation); the routed SES imports "
          "back into the PCB editor. Long jobs: autoroute_board with wait=false, then poll "
          "get_job_details every few seconds.";
      return rpcResult(id, result);
    }
    if (method == "ping") return rpcResult(id, json::object());
    if (method == "tools/list") return rpcResult(id, json{{"tools", tools()}});
    if (method == "tools/call") {
      if (!params.contains("name") || !params["name"].is_string())
        return rpcError(id, -32602, "Missing required parameter: name");
      std::string name = params["name"].get<std::string>();
      bool known = false;
      for (const json& t : tools()) known = known || t["name"] == name;
      if (!known) return rpcError(id, -32602, "Unknown tool: " + name);
      json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
      json token = params.contains("_meta") && params["_meta"].is_object() && params["_meta"].contains("progressToken")
                       ? params["_meta"]["progressToken"]
                       : json();
      return rpcResult(id, callTool(name, args, token, notify, cancelled));
    }
    if (method == "resources/list") return rpcResult(id, json{{"resources", json::array()}});
    if (method == "prompts/list") return rpcResult(id, json{{"prompts", json::array()}});
    return rpcError(id, -32601, "Method not found: " + method);
  } catch (const std::exception& e) {
    return rpcError(id, -32603, std::string("Internal error: ") + e.what());
  }
}

json McpServer::callTool(const std::string& name, const json& args, const json& progressToken, const Notify& notify,
                         const std::function<bool()>& cancelled) {
  JobManager& jobs = JobManager::instance();

  if (name == "autoroute_board" || name == "check_board_drc") {
    AutorouteArgs a;
    std::string error = parseAutorouteArgs(args, false, a);
    if (!error.empty()) return toolError(error);
    a.sessionId = session();
    if (name == "check_board_drc") {
      // No routing: the pipeline with every stage switched off, then the DRC of the loaded board.
      a.settings = JobSettings{};
      a.settings.optimizer = false;
      a.settings.paths = {{"autorouter.enabled", "false"}, {"fanout.enabled", "false"}, {"optimizer.enabled", "false"}};
      a.settingsJson = json{{"autorouter", {{"enabled", false}}}, {"fanout", {{"enabled", false}}}, {"optimizer", {{"enabled", false}}}};
      a.returnSes = false;
      a.returnDrcSummary = true;
      a.outputPath.reset();
      a.wait = true;
    }
    // MCP progress notifications (when the client asked for them with a progressToken).
    int64_t lastSent = 0;
    int32_t step = 0;
    auto progress = [&](const JobInfo& j) {
      if (!notify || progressToken.is_null()) return;
      int64_t now = JobManager::nowMs();
      if (now - lastSent < 1000) return;
      lastSent = now;
      std::string msg = std::string(jobStateName(j.state)) + " " + j.stage;
      if (j.currentPass > 0) msg += ", pass " + std::to_string(j.currentPass);
      if (j.unrouted) msg += ", " + std::to_string(*j.unrouted) + " unrouted";
      if (j.violations) msg += ", " + std::to_string(*j.violations) + " violations";
      notify(json{{"jsonrpc", "2.0"},
                  {"method", "notifications/progress"},
                  {"params", json{{"progressToken", progressToken}, {"progress", ++step}, {"message", msg}}}});
    };
    json result = runAutoroute(a, error, progress, cancelled);
    if (!error.empty()) return toolError(error);
    if (name == "check_board_drc") {
      json r = json::object();
      r["job_id"] = result["job_id"];
      r["status"] = result["status"];
      if (result.contains("unrouted_connections")) r["unrouted_connections"] = result["unrouted_connections"];
      if (result.contains("clearance_violations")) r["clearance_violations"] = result["clearance_violations"];
      if (result.contains("drc_summary")) r["drc_summary"] = result["drc_summary"];
      return toolResult(r);
    }
    return toolResult(result);
  }

  if (name == "get_system_status") {
    json r = json::object();
    r["status"] = "OK";
    r["version"] = kServerVersion;
    r["cpu_cores"] = std::thread::hardware_concurrency();
    r["job_count"] = jobs.jobCount();
    r["running_jobs"] = jobs.runningCount();
    r["queued_jobs"] = jobs.queuedCount();
    return toolResult(r);
  }
  if (name == "list_jobs") {
    json list = json::array();
    for (const JobInfo& j : jobs.list(std::nullopt)) list.push_back(jobStatus(j));
    return toolResult(json{{"jobs", list}});
  }

  // The tools of one job
  std::string jobId = stringArg(args, "jobId");
  if (jobId.empty()) return toolError("'jobId' is required.");
  std::optional<JobInfo> job = jobs.info(jobId);
  if (!job) return toolError("Job '" + jobId + "' not found.");

  if (name == "get_job_details") return toolResult(jobStatus(*job));
  if (name == "cancel_job") {
    std::string e = jobs.cancel(jobId);
    if (!e.empty()) return toolError(e);
    return toolResult(jobStatus(*jobs.info(jobId)));
  }
  if (name == "get_job_output" || name == "download_job_output_to_local_file") {
    if (!isFinished(job->state))
      return toolError("The job is " + std::string(jobStateName(job->state)) + "; wait until it is finished.");
    std::optional<std::string> ses = jobs.output(jobId);
    if (!ses) return toolError("The job has no output (state " + std::string(jobStateName(job->state)) + ").");
    if (name == "get_job_output") return toolResult(json{{"job_id", jobId}, {"format", "SES"}, {"ses", *ses}});
    std::string path = stringArg(args, "filePath");
    if (path.empty()) return toolError("'filePath' is required.");
    if (!writeFile(path, *ses)) return toolError("Cannot write the file '" + path + "'.");
    return toolResult(json{{"message", "Saved the routed SES (" + std::to_string(ses->size()) + " bytes) to " + path + "."}});
  }
  if (name == "get_job_drc_summary") {
    std::optional<DrcSummary> drc = jobs.drcSummary(jobId);
    if (!drc) return toolError("The job has no DRC summary (state " + std::string(jobStateName(job->state)) + ").");
    return toolResult(drcSummaryToJson(*drc, intArg(args, "maxViolations", 50)));
  }
  if (name == "get_job_logs") {
    std::vector<LogLine> lines;
    jobs.logs(jobId, 0, lines);
    int32_t minLevel = levelOf(stringArg(args, "minLevel"));
    std::vector<const LogLine*> shown;
    for (const LogLine& l : lines)
      if (l.level <= minLevel) shown.push_back(&l);
    size_t tail = static_cast<size_t>(std::max(1, intArg(args, "tail", 50)));
    json out = json::array();
    for (size_t i = shown.size() > tail ? shown.size() - tail : 0; i < shown.size(); ++i)
      out.push_back(std::string(logLevelName(shown[i]->level)) + ": " + shown[i]->text);
    return toolResult(json{{"job_id", jobId}, {"lines", out}, {"total_lines", lines.size()}});
  }
  return toolError("Unknown tool: " + name);
}

int McpServer::runStdio() {
  jrt::platform::set_binary_stdio();
  std::mutex outMutex;
  auto send = [&outMutex](const json& msg) {
    std::string text = msg.dump(-1, ' ', false, json::error_handler_t::replace);
    std::lock_guard<std::mutex> lock(outMutex);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
  };

  std::mutex callsMutex;
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> inFlight;  // request id -> cancelled
  std::vector<std::thread> threads;
  std::atomic<bool> closed{false};

  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    json msg = json::parse(line, nullptr, false);
    if (msg.is_discarded()) {
      send(rpcError(json(), -32700, "Parse error"));
      continue;
    }
    std::vector<json> batch;
    if (msg.is_array()) {
      for (json& m : msg) batch.push_back(std::move(m));
    } else {
      batch.push_back(std::move(msg));
    }
    for (json& m : batch) {
      std::string method = m.is_object() ? m.value("method", "") : "";
      if (method == "notifications/cancelled") {
        if (m.contains("params") && m["params"].is_object() && m["params"].contains("requestId")) {
          std::lock_guard<std::mutex> lock(callsMutex);
          auto it = inFlight.find(m["params"]["requestId"].dump());
          if (it != inFlight.end()) *it->second = true;
        }
        continue;
      }
      if (method == "tools/call" && m.contains("id")) {
        // Tool calls may run for a long time: each on a thread of its own, so that pings,
        // cancellations and other calls keep being served.
        auto flag = std::make_shared<std::atomic<bool>>(false);
        std::string key = m["id"].dump();
        {
          std::lock_guard<std::mutex> lock(callsMutex);
          inFlight[key] = flag;
        }
        threads.emplace_back([this, m = std::move(m), flag, key, &send, &callsMutex, &inFlight, &closed] {
          json response = handle(m, send, [flag, &closed] { return flag->load() || closed.load(); });
          {
            std::lock_guard<std::mutex> lock(callsMutex);
            inFlight.erase(key);
          }
          if (!flag->load()) send(response);  // no response to a cancelled request
        });
        continue;
      }
      json response = handle(m, send);
      if (!response.is_null()) send(response);
    }
  }
  // The client went away: stop the calls still waiting (their jobs are cancelled), then exit.
  closed = true;
  for (std::thread& t : threads) t.join();
  return 0;
}

}  // namespace frserver
