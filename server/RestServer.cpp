#include "RestServer.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include <httplib.h>  // CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH: cmake/ServerDeps.cmake

#include "ApiCommon.h"
#include "JobManager.h"
#include "Json.h"
#include "McpServer.h"
#include "logger/FRLogger.h"

namespace frserver {

namespace {

const char* const kDocs = "https://github.com/freerouting/freerouting/blob/master/docs/API/API_v1.md";
const int64_t kMaxFileBytes = 100LL * 1024 * 1024;  // Java: 100 MB
const int64_t kStartedMs = JobManager::nowMs();

void reply(httplib::Response& res, int status, const json& body, bool pretty = true) {
  res.status = status;
  res.set_content(pretty ? body.dump(2, ' ', false, json::error_handler_t::replace)
                         : body.dump(-1, ' ', false, json::error_handler_t::replace),
                  "application/json");
}

void replyError(httplib::Response& res, int status, const std::string& message) {
  reply(res, status, json{{"error", message}}, false);
}

/** The request body as JSON; replies 400 and returns nullopt if it is not. */
std::optional<json> body(const httplib::Request& req, httplib::Response& res, const char* invalidMessage) {
  if (req.body.empty()) {
    replyError(res, 400, invalidMessage);
    return std::nullopt;
  }
  json j = json::parse(req.body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    replyError(res, 400, invalidMessage);
    return std::nullopt;
  }
  return j;
}

std::string hostOf(const httplib::Request& req) {
  std::string host = req.get_header_value("Freerouting-Environment-Host");
  if (host.empty() && req.has_param("host")) host = req.get_param_value("host");  // the KiCad plugin
  return host.empty() ? std::string("Unknown/0") : host;
}

std::string profileOf(const httplib::Request& req) {
  std::string id = req.get_header_value("Freerouting-Profile-ID");
  return id.empty() ? std::string("00000000-0000-0000-0000-000000000000") : id;
}

json sessionJson(const std::string& id, const httplib::Request& req) {
  return json{{"id", id}, {"user_id", profileOf(req)}, {"host", JobManager::instance().sessionHost(id)}};
}

/** The job of the path, or a 404 reply. */
std::optional<JobInfo> jobOf(const httplib::Request& req, httplib::Response& res, bool drcStyle = false) {
  std::optional<JobInfo> job = JobManager::instance().info(req.matches[1]);
  if (!job) {
    if (drcStyle) {
      replyError(res, 404, "Job not found.");
    } else {
      reply(res, 404, json::object(), false);
    }
  }
  return job;
}

void replyJob(httplib::Response& res, const std::string& jobId) {
  reply(res, 200, jobToJson(*JobManager::instance().info(jobId)));
}

/** POST /input and /rules: {"filename": ..., "data": base64}. */
void uploadFile(const httplib::Request& req, httplib::Response& res, bool rules) {
  auto job = jobOf(req, res);
  if (!job) return;
  const char* what = rules ? "rules" : "input";
  if (job->state != JobState::QUEUED) return replyError(res, 400, "The job is already started and cannot be changed.");
  auto b = body(req, res, rules ? "The rules data is invalid." : "The input data is invalid.");
  if (!b) return;
  if (!b->contains("data") || !(*b)["data"].is_string())
    return replyError(res, 400, std::string("The ") + what + " data must be base-64 encoded and put into the \"data\" field.");
  const std::string& data64 = (*b)["data"].get_ref<const std::string&>();
  if (static_cast<int64_t>(data64.size()) > kMaxFileBytes * 4 / 3 + 1024)
    return replyError(res, 400, std::string(rules ? "Rules" : "Input") + " file exceeds maximum allowed size of 100MB.");
  std::optional<std::string> data = base64Decode(data64);
  if (!data) return replyError(res, 400, std::string("Invalid base64 encoding for ") + what + " file.");
  std::string filename = b->value("filename", std::string());
  if (!filename.empty()) filename = baseName(filename);
  size_t first = data->find_first_not_of(" \t\r\n");
  if (rules) {
    if (filename.empty()) filename = (job->input ? job->input->filename.substr(0, job->input->filename.find_last_of('.')) : job->name) + ".rules";
    std::string e = JobManager::instance().setRules(job->id, JobFile{filename, std::move(*data), std::nullopt});
    if (!e.empty()) return replyError(res, 400, e);
  } else {
    if (first == std::string::npos) return replyError(res, 400, "The input data is invalid.");
    if ((*data)[first] == '{')
      return replyError(res, 400, "KiCad JSON input is not supported by freerouting-cpp; upload a Specctra DSN file.");
    if (data->compare(first, 4, "(pcb") != 0 && data->compare(first, 4, "(PCB") != 0)
      return replyError(res, 400, "The input data is invalid.");
    if (filename.empty()) filename = job->name + ".dsn";
    if (filename.find('.') == std::string::npos) filename += ".dsn";
    std::string e = JobManager::instance().setInput(job->id, JobFile{filename, std::move(*data), std::nullopt});
    if (!e.empty()) return replyError(res, 400, e);
  }
  replyJob(res, job->id);
}

/** The output of a job as Java's BoardFilePayload. */
json outputPayload(const JobInfo& job, const std::string& ses) {
  json details = fileDetailsToJson(job, true);
  json p = json::object();
  p["job_id"] = job.id;
  p["data"] = base64Encode(ses);
  for (auto it = details.begin(); it != details.end(); ++it) p[it.key()] = it.value();
  return p;
}

std::string sseEvent(const json& payload) {
  std::string out = "id: " + std::to_string(JobManager::nowMs()) + "\n";
  std::string text = payload.dump(2, ' ', false, json::error_handler_t::replace);
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) nl = text.size();
    out += "data: " + text.substr(start, nl - start) + "\n";
    start = nl + 1;
  }
  return out + "\n";
}

void registerRoutes(httplib::Server& svr, McpServer& mcp) {
  JobManager& jobs = JobManager::instance();

  // ------------------------------------------------------------------------------ system --
  svr.Get("/v1/system/status", [&jobs](const httplib::Request&, httplib::Response& res) {
    json j = json::object();
    j["status"] = "OK";
    std::error_code ec;
    auto space = std::filesystem::space(std::filesystem::current_path(ec), ec);
    if (!ec) j["storage_available"] = static_cast<int64_t>(space.available / (1024 * 1024));
    j["session_count"] = static_cast<int64_t>(jobs.sessions().size());
    // PORT EXTENSION
    j["job_count"] = jobs.jobCount();
    j["running_jobs"] = jobs.runningCount();
    j["queued_jobs"] = jobs.queuedCount();
    reply(res, 200, j);
  });
  svr.Get("/v1/system/environment", [](const httplib::Request&, httplib::Response& res) {
    json j = json::object();
    j["freerouting_version"] = kServerVersion;
    j["app_started_at"] = isoTime(kStartedMs);
#if defined(_WIN32)
    j["architecture"] = "Windows,x86_64";
#else
    j["architecture"] = "Linux,x86_64";
#endif
    j["cpu_cores"] = std::thread::hardware_concurrency();
    j["pipeline_type"] = "API";
    reply(res, 200, j);
  });

  // ---------------------------------------------------------------------------- sessions --
  svr.Post("/v1/sessions/create", [&jobs](const httplib::Request& req, httplib::Response& res) {
    reply(res, 200, sessionJson(jobs.createSession(hostOf(req)), req));
  });
  svr.Get("/v1/sessions/list", [&jobs](const httplib::Request&, httplib::Response& res) {
    reply(res, 200, json(jobs.sessions()));
  });
  svr.Get(R"(/v1/sessions/([^/]+)/logs)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    if (!jobs.sessionExists(id)) return reply(res, 404, json::object(), false);
    json out = json::array();
    for (const LogLine& l : jobs.sessionLogs(id)) out.push_back(logLineToJson(l, id));
    reply(res, 200, out);
  });
  svr.Put(R"(/v1/sessions/([^/]+)/monitor)", [](const httplib::Request&, httplib::Response& res) {
    reply(res, 400, json{{"error", "GUI is not running, cannot monitor session visually"}});
  });
  svr.Get(R"(/v1/sessions/([^/]+))", [&jobs](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    if (!jobs.sessionExists(id)) return reply(res, 404, json::object(), false);
    reply(res, 200, sessionJson(id, req));
  });

  // -------------------------------------------------------------------------------- jobs --
  svr.Post("/v1/jobs/enqueue", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto b = body(req, res, "The job data is invalid.");
    if (!b) return;
    std::string sessionId = b->value("session_id", std::string());
    if (sessionId.empty() || !jobs.sessionExists(sessionId))
      return replyError(res, 400, "The session ID '" + sessionId + "' is invalid.");
    std::string priority = b->value("priority", std::string("NORMAL"));
    std::string name = b->value("name", std::string());
    std::string id = jobs.enqueue(sessionId, name, priority);
    if (name.empty()) {
      // Java: "J-" + the first 6 hex digits of the id
      std::string n = "J-" + id.substr(0, 6);
      for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      jobs.setName(id, n);
    }
    if (b->contains("router_settings") && (*b)["router_settings"].is_object()) {
      JobSettings s;
      std::string e = settingsFromJson((*b)["router_settings"], s);
      if (!e.empty()) return replyError(res, 400, e);
      jobs.setSettings(id, s, (*b)["router_settings"].dump());
    }
    replyJob(res, id);
  });
  svr.Get(R"(/v1/jobs/list/([^/]*))", [&jobs](const httplib::Request& req, httplib::Response& res) {
    std::string sid = req.matches[1];
    std::optional<std::string> filter;
    if (!sid.empty() && sid != "all" && jobs.sessionExists(sid)) filter = sid;
    json out = json::array();
    for (const JobInfo& j : jobs.list(filter)) out.push_back(jobToJson(j));
    reply(res, 200, out);
  });
  svr.Put(R"(/v1/jobs/([^/]+)/start)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    if (job->state != JobState::QUEUED) return replyError(res, 400, "The job is already started and cannot be changed.");
    std::string e = jobs.start(job->id);
    if (!e.empty()) return replyError(res, 400, e);
    replyJob(res, job->id);
  });
  svr.Put(R"(/v1/jobs/([^/]+)/cancel)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    jobs.cancel(job->id);
    replyJob(res, job->id);
  });
  svr.Post(R"(/v1/jobs/([^/]+)/settings)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    if (job->state != JobState::QUEUED) return replyError(res, 400, "The job is already started and cannot be changed.");
    auto b = body(req, res, "The router settings are invalid.");
    if (!b) return;
    json merged = json::parse(job->settingsJson.empty() ? "{}" : job->settingsJson, nullptr, false);
    if (merged.is_discarded()) merged = json::object();
    mergeJson(merged, *b);
    JobSettings s;
    s.maxPasses = job->settings.maxPasses;
    s.maxThreads = job->settings.maxThreads;
    s.timeout = job->settings.timeout;
    s.optimizer = job->settings.optimizer;
    std::string e = settingsFromJson(merged, s);
    if (!e.empty()) return replyError(res, 400, e);
    e = jobs.setSettings(job->id, s, merged.dump());
    if (!e.empty()) return replyError(res, 400, e);
    replyJob(res, job->id);
  });
  svr.Get(R"(/v1/jobs/([^/]+)/settings)", [](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    json s = json::parse(job->settingsJson.empty() ? "{}" : job->settingsJson, nullptr, false);
    reply(res, 200, s.is_discarded() ? json::object() : s);
  });
  svr.Post(R"(/v1/jobs/([^/]+)/input)", [](const httplib::Request& req, httplib::Response& res) { uploadFile(req, res, false); });
  svr.Post(R"(/v1/jobs/([^/]+)/rules)", [](const httplib::Request& req, httplib::Response& res) { uploadFile(req, res, true); });
  auto noKiCadJson = [](const httplib::Request&, httplib::Response& res) {
    replyError(res, 501, "KiCad JSON input and output are not supported by freerouting-cpp; use Specctra DSN / SES.");
  };
  svr.Post(R"(/v1/jobs/([^/]+)/input/json)", noKiCadJson);
  svr.Get(R"(/v1/jobs/([^/]+)/output/json(/stream)?)", noKiCadJson);
  svr.Get(R"(/v1/jobs/([^/]+)/output)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    JobState s = job->state;
    if (s == JobState::TERMINATED || s == JobState::CANCELLED || s == JobState::TIMED_OUT || s == JobState::INVALID) {
      // PORT NOTE: a timed out job keeps the SES of what was routed (Java: 400); served with 200.
      if (!(s == JobState::TIMED_OUT && job->hasOutput))
        return reply(res, 400, json{{"error", "The job is in state '" + std::string(jobStateName(s)) + "' and has no valid output."}});
    }
    if (!job->hasOutput) {
      if (s == JobState::RUNNING || s == JobState::PAUSED || s == JobState::STOPPING || s == JobState::READY_TO_START) {
        res.status = 204;
        return;
      }
      return reply(res, 400, json{{"error", "The job hasn't started yet."}});
    }
    reply(res, 200, outputPayload(*job, *jobs.output(job->id)));
  });
  svr.Get(R"(/v1/jobs/([^/]+)/output/stream)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto sent = std::make_shared<int32_t>(0);
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider("text/event-stream", [&jobs, id, sent](size_t, httplib::DataSink& sink) {
      for (;;) {
        std::optional<JobInfo> job = jobs.info(id);
        if (!job) {
          sink.done();
          return false;
        }
        if (job->hasOutput && job->outputVersion != *sent) {
          *sent = job->outputVersion;
          std::string ev = sseEvent(outputPayload(*job, *jobs.output(id)));
          if (!sink.write(ev.data(), ev.size())) return false;
        }
        if (isFinished(job->state)) {
          sink.done();
          return false;
        }
        if (!sink.is_writable()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });
  });
  svr.Get(R"(/v1/jobs/([^/]+)/logs/stream)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto next = std::make_shared<size_t>(0);
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider("text/event-stream", [&jobs, id, next](size_t, httplib::DataSink& sink) {
      for (;;) {
        std::optional<JobInfo> job = jobs.info(id);
        if (!job) {
          sink.done();
          return false;
        }
        std::vector<LogLine> lines;
        *next = jobs.logs(id, *next, lines);
        for (const LogLine& l : lines) {
          std::string ev = sseEvent(logLineToJson(l, id));
          if (!sink.write(ev.data(), ev.size())) return false;
        }
        if (isFinished(job->state) && lines.empty()) {
          sink.done();
          return false;
        }
        if (!sink.is_writable()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });
  });
  svr.Get(R"(/v1/jobs/([^/]+)/logs)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    std::vector<LogLine> lines;
    jobs.logs(job->id, 0, lines);
    json out = json::array();
    for (const LogLine& l : lines) out.push_back(logLineToJson(l, job->id));
    reply(res, 200, out);
  });
  svr.Get(R"(/v1/jobs/([^/]+)/drc/summary)", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res, true);
    if (!job) return;
    std::optional<DrcSummary> drc = jobs.drcSummary(job->id);
    if (!drc) {
      return replyError(res, 400, "The DRC summary is made when the job has finished (state " +
                                      std::string(jobStateName(job->state)) + ").");
    }
    int32_t max = req.has_param("max_violations") ? std::atoi(req.get_param_value("max_violations").c_str()) : -1;
    reply(res, 200, drcSummaryToJson(*drc, max));
  });
  svr.Get(R"(/v1/jobs/([^/]+)/drc)", [](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res, true);
    if (!job) return;
    replyError(res, 501, "The KiCad DRC report is not available in freerouting-cpp; use /v1/jobs/{jobId}/drc/summary.");
  });
  svr.Get(R"(/v1/jobs/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
    auto job = jobOf(req, res);
    if (!job) return;
    bool compact = req.has_param("compact") && req.get_param_value("compact") == "true";
    reply(res, 200, jobToJson(*job, compact), !compact);
  });

  // --------------------------------------------------------------------------- autoroute --
  svr.Post("/v1/autoroute", [&jobs](const httplib::Request& req, httplib::Response& res) {
    auto b = body(req, res, "Request body is missing.");
    if (!b) return;
    AutorouteArgs a;
    std::string e = parseAutorouteArgs(*b, true, a);
    if (!e.empty()) return replyError(res, 400, e);
    a.sessionId = jobs.createSession(hostOf(req));
    a.wait = true;
    json result = runAutoroute(a, e, nullptr, [&req] { return req.is_connection_closed && req.is_connection_closed(); });
    if (!e.empty()) return replyError(res, 400, e);
    reply(res, result["status"] == "TIMED_OUT" ? 202 : 200, result);
  });

  // --------------------------------------------------------------------------------- MCP --
  svr.Post("/v1/mcp", [&mcp](const httplib::Request& req, httplib::Response& res) {
    json msg = json::parse(req.body, nullptr, false);
    if (msg.is_discarded()) return reply(res, 200, json{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "Parse error"}}}}, false);
    json response = mcp.handle(msg, nullptr, [&req] { return req.is_connection_closed && req.is_connection_closed(); });
    if (response.is_null()) {
      res.status = 204;
      return;
    }
    reply(res, 200, response, false);
  });
}

}  // namespace

struct RestServer::Impl {
  McpServer mcp;
  httplib::Server svr;
};

RestServer::RestServer() : impl_(std::make_unique<Impl>()) {
  JobManager::instance();  // the routing thread
  httplib::Server& svr = impl_->svr;
  svr.set_payload_max_length(static_cast<size_t>(kMaxFileBytes * 4 / 3 + 1024 * 1024));
  svr.set_read_timeout(60, 0);
  svr.set_write_timeout(60, 0);
  registerRoutes(svr, impl_->mcp);
  svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    std::string cid = req.get_header_value("X-Correlation-ID");
    res.set_header("X-Correlation-ID", cid.empty() ? JobManager::newUuid() : cid);
    return httplib::Server::HandlerResponse::Unhandled;
  });
  svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (res.status == 404 && res.body.empty())
      reply(res, 404, json{{"error", "The requested resource was not found."}, {"documentation", kDocs}});
  });
  svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
    std::string msg = "Internal error";
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      msg = e.what();
    } catch (...) {
    }
    reply(res, 500, json{{"error", msg}, {"documentation", kDocs}});
  });
}

RestServer::~RestServer() { stop(); }

int RestServer::bind(const std::string& host, int port) {
  if (port == 0) return impl_->svr.bind_to_any_port(host);
  return impl_->svr.bind_to_port(host, port) ? port : -1;
}

bool RestServer::listen() { return impl_->svr.listen_after_bind(); }

void RestServer::stop() {
  if (impl_->svr.is_running()) impl_->svr.stop();
}

int runRestServer(const RestOptions& options) {
  RestServer server;
  if (server.bind(options.host, options.port) < 0) {
    std::fprintf(stderr, "cannot listen on %s:%d\n", options.host.c_str(), options.port);
    return 1;
  }
  fr::FRLogger::info("API web server started successfully at http://" + options.host + ":" +
                     std::to_string(options.port) + " (freerouting-cpp REST API v1, MCP at /v1/mcp).");
  return server.listen() ? 0 : 1;
}

}  // namespace frserver
