// Checks of the MCP server and the REST API (server/): the protocol, the tools and endpoints, and
// the SES they produce against the command line's for the same input, byte for byte. Everything
// runs in this one process, several routing jobs in a row (the case the command line never has:
// static state of the core that outlives a job shows up here).
//
// usage: server_test <freerouting-cpp> <fixtures directory> <work directory>
//   freerouting-cpp     the executable, run for the reference SES (-de board.dsn -do cli.ses)
//   fixtures directory  upstream/fixtures (Issue508-DAC2020_bm02.dsn, Issue107-*, Issue313-*, Issue054-*)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "ApiCommon.h"
#include "JobManager.h"
#include "Json.h"
#include "McpServer.h"
#include "RestServer.h"
#include "logger/FRLogger.h"

using frserver::json;
namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAILED: %s\n", what.c_str());
  }
}

std::string exe, fixtures, work;

std::string fixture(const std::string& name) { return (fs::path(fixtures) / name).string(); }
std::string workFile(const std::string& name) { return (fs::path(work) / name).string(); }
std::string fileText(const std::string& path) { return frserver::readFile(path).value_or(std::string()); }

/** The SES the command line writes for `dsn` with `options`, made once per name. */
std::string cliSes(const std::string& name, const std::string& dsn, const std::string& options) {
  std::string out = workFile(name + ".cli.ses");
  std::string cmd = "\"" + exe + "\" -de \"" + dsn + "\" -do \"" + out + "\" " + options + " -q > \"" +
                    workFile(name + ".cli.log") + "\" 2>&1";
#ifdef _WIN32
  cmd = "\"" + cmd + "\"";  // cmd.exe /c drops the outer quotes
#endif
  int rc = std::system(cmd.c_str());
  check(rc == 0, "command line exit code for " + name + " (" + std::to_string(rc) + ")");
  std::string ses = fileText(out);
  check(!ses.empty(), "command line SES of " + name);
  return ses;
}

// ------------------------------------------------------------------------------------ MCP ----

struct Mcp {
  frserver::McpServer server;
  int nextId = 1;
  std::vector<json> notifications;

  json request(const std::string& method, json params = json::object(), const std::function<bool()>& cancelled = nullptr) {
    json msg = {{"jsonrpc", "2.0"}, {"id", nextId++}, {"method", method}, {"params", std::move(params)}};
    return server.handle(msg, [this](const json& n) { notifications.push_back(n); }, cancelled);
  }

  /** tools/call; returns the result object (content, structuredContent, isError). */
  json call(const std::string& tool, json args, const std::function<bool()>& cancelled = nullptr) {
    json params = {{"name", tool}, {"arguments", std::move(args)}, {"_meta", {{"progressToken", tool}}}};
    json r = request("tools/call", std::move(params), cancelled);
    return r.contains("result") ? r["result"] : json();
  }
};

bool isToolError(const json& r) { return !r.is_object() || r.value("isError", true); }
json structured(const json& r) { return r.is_object() && r.contains("structuredContent") ? r["structuredContent"] : json::object(); }

void mcpProtocol() {
  Mcp mcp;
  json r = mcp.request("initialize", {{"protocolVersion", "2024-11-05"},
                                      {"capabilities", json::object()},
                                      {"clientInfo", {{"name", "server_test"}, {"version", "1"}}}});
  check(r["result"]["protocolVersion"] == "2024-11-05", "initialize keeps a known protocol version");
  check(r["result"]["serverInfo"]["name"] == "freerouting-cpp", "initialize: serverInfo.name");
  check(r["result"]["capabilities"].contains("tools"), "initialize: tools capability");
  r = mcp.request("initialize", {{"protocolVersion", "1999-01-01"}});
  check(r["result"]["protocolVersion"] == "2025-06-18", "initialize answers an unknown version with the newest");

  r = mcp.request("ping");
  check(r.contains("result") && r["result"].is_object(), "ping");

  r = mcp.request("tools/list");
  std::vector<std::string> names;
  for (const json& t : r["result"]["tools"]) {
    names.push_back(t["name"]);
    check(t.contains("description") && t["inputSchema"]["type"] == "object", "tool " + t["name"].get<std::string>() + " has a schema");
  }
  for (const char* expected : {"autoroute_board", "check_board_drc", "get_job_details", "cancel_job", "list_jobs",
                               "download_job_output_to_local_file", "get_job_output", "get_job_drc_summary",
                               "get_job_logs", "get_system_status"}) {
    check(std::find(names.begin(), names.end(), expected) != names.end(), std::string("tools/list has ") + expected);
  }

  r = mcp.request("no/such/method");
  check(r["error"]["code"] == -32601, "unknown method: -32601");
  r = mcp.request("tools/call", {{"name", "no_such_tool"}});
  check(r["error"]["code"] == -32602, "unknown tool: -32602");
  r = mcp.server.handle(json{{"id", 1}, {"method", "ping"}});
  check(r["error"]["code"] == -32600, "message without jsonrpc 2.0: -32600");
  r = mcp.server.handle(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
  check(r.is_null(), "a notification gets no response");

  // Argument errors come back as tool errors (isError), not protocol errors
  json t = mcp.call("autoroute_board", json::object());
  check(isToolError(t), "autoroute_board without a design is a tool error");
  t = mcp.call("autoroute_board", {{"filePath", workFile("no-such-board.dsn")}});
  check(isToolError(t), "autoroute_board with a missing file is a tool error");
  t = mcp.call("autoroute_board", {{"fileContent", "(pcb x)"}, {"maxPasses", 0}});
  check(isToolError(t), "maxPasses 0 is a tool error");
  t = mcp.call("autoroute_board", {{"fileContent", "(pcb x)"}, {"routerSettings", {{"no_such_setting", 1}}}});
  check(isToolError(t), "an unknown router setting is a tool error");
  t = mcp.call("get_job_details", {{"jobId", "00000000-0000-0000-0000-000000000000"}});
  check(isToolError(t), "get_job_details of an unknown job is a tool error");
  t = mcp.call("get_system_status", json::object());
  check(!isToolError(t) && structured(t)["status"] == "OK", "get_system_status");
}

void mcpRouting(const std::string& bm02Cli, const std::string& i107Cli) {
  Mcp mcp;
  // bm02 by path, the SES written to a file
  std::string out = workFile("bm02.mcp.ses");
  fs::remove(out);
  json r = mcp.call("autoroute_board", {{"filePath", fixture("Issue508-DAC2020_bm02.dsn")}, {"outputPath", out}});
  json s = structured(r);
  check(!isToolError(r), "autoroute_board bm02: no error");
  check(s["status"] == "COMPLETED", "autoroute_board bm02: COMPLETED (" + s.value("status", std::string("?")) + ")");
  check(s["output_path"] == out, "autoroute_board bm02: output_path");
  check(fileText(out) == bm02Cli, "autoroute_board bm02: SES identical to the command line's");
  check(s.contains("drc_summary") && s["drc_summary"].contains("clearance_violations_count"),
        "autoroute_board with outputPath returns the DRC summary");
  check(!s["outputs"].contains("SES"), "autoroute_board with outputPath keeps the SES out of the result");
  bool progress = false;
  for (const json& n : mcp.notifications)
    progress = progress || (n["method"] == "notifications/progress" && n["params"]["progressToken"] == "autoroute_board");
  check(progress, "autoroute_board sends progress notifications for its progressToken");
  std::string jobId = s.value("job_id", std::string());

  // The job's tools on the finished job
  json d = structured(mcp.call("get_job_details", {{"jobId", jobId}}));
  check(d["state"] == "COMPLETED" && d["has_output"] == true, "get_job_details of the finished job");
  json o = structured(mcp.call("get_job_output", {{"jobId", jobId}}));
  check(o["ses"] == bm02Cli, "get_job_output: the same SES");
  std::string out2 = workFile("bm02.mcp2.ses");
  r = mcp.call("download_job_output_to_local_file", {{"jobId", jobId}, {"filePath", out2}});
  check(!isToolError(r) && fileText(out2) == bm02Cli, "download_job_output_to_local_file: the same SES");
  json drc = structured(mcp.call("get_job_drc_summary", {{"jobId", jobId}}));
  check(drc.contains("clearance_violations_count") && drc.contains("violations"), "get_job_drc_summary");
  json logs = structured(mcp.call("get_job_logs", {{"jobId", jobId}}));
  check(logs["total_lines"].get<int64_t>() > 0, "get_job_logs: the warnings of the bm02 job");
  json list = structured(mcp.call("list_jobs", json::object()));
  bool listed = false;
  for (const json& j : list["jobs"]) listed = listed || j["id"] == jobId;
  check(listed, "list_jobs lists the job");

  // Issue107 with its rules file, given as content, one pass; the SES comes back in the result
  r = mcp.call("autoroute_board", {{"fileContent", fileText(fixture("Issue107-freq_teiler_200kHz_kicad.dsn"))},
                                    {"fileName", "Issue107-freq_teiler_200kHz_kicad.dsn"},
                                    {"rulesContent", fileText(fixture("Issue107-freq_teiler_200kHz_kicad.rules"))},
                                    {"maxPasses", 1}});
  s = structured(r);
  check(s["status"] == "COMPLETED", "autoroute_board Issue107 with rules: COMPLETED");
  check(s["outputs"]["SES"] == i107Cli, "autoroute_board Issue107 with rules, 1 pass: SES identical to the command line's");

  // check_board_drc: no routing, the DRC of the board as it is
  r = mcp.call("check_board_drc", {{"filePath", fixture("Issue508-DAC2020_bm02.dsn")}});
  s = structured(r);
  check(!isToolError(r) && s["status"] == "COMPLETED", "check_board_drc: COMPLETED");
  check(s["drc_summary"]["unconnected_nets_count"].get<int64_t>() > 0, "check_board_drc: the unrouted board has unconnected nets");
}

void mcpCancelAndTimeout(const std::string& bm02Cli) {
  Mcp mcp;
  const std::string board = fixture("Issue054-tairakb.dsn");  // ~18 s from start to end

  // The client cancels the request (notifications/cancelled, or it goes away): the job stops
  auto t0 = std::chrono::steady_clock::now();
  auto elapsed = [&t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
  json r = mcp.call("autoroute_board", {{"filePath", board}}, [&] { return elapsed() > 2.0; });
  json s = structured(r);
  check(s["status"] == "CANCELLED", "a cancelled request cancels the job (" + s.value("status", std::string("?")) + ")");
  check(elapsed() < 10.0, "the cancelled job stops soon (" + std::to_string(elapsed()) + " s)");

  // cancel_job on a job started with wait=false
  t0 = std::chrono::steady_clock::now();
  r = mcp.call("autoroute_board", {{"filePath", board}, {"wait", false}});
  s = structured(r);
  std::string jobId = s.value("job_id", std::string());
  check(!jobId.empty() && s["status"] != "COMPLETED", "wait=false returns at once with the job id");
  std::this_thread::sleep_for(std::chrono::seconds(1));
  mcp.call("cancel_job", {{"jobId", jobId}});
  std::optional<frserver::JobInfo> info = frserver::JobManager::instance().wait(jobId, 30000);
  check(info && info->state == frserver::JobState::CANCELLED, "cancel_job cancels the running job");
  check(elapsed() < 12.0, "cancel_job stops the job soon (" + std::to_string(elapsed()) + " s)");

  // A time limit
  t0 = std::chrono::steady_clock::now();
  r = mcp.call("autoroute_board", {{"filePath", board}, {"timeoutSeconds", 2}});
  s = structured(r);
  check(s["status"] == "TIMED_OUT", "timeoutSeconds 2: TIMED_OUT (" + s.value("status", std::string("?")) + ")");
  check(elapsed() < 12.0, "the timed out job stops soon (" + std::to_string(elapsed()) + " s)");

  // The next job after the stopped ones still gives the command line's SES
  r = mcp.call("autoroute_board", {{"filePath", fixture("Issue508-DAC2020_bm02.dsn")}, {"outputFormats", {"SES"}}});
  check(structured(r)["outputs"]["SES"] == bm02Cli, "bm02 after cancelled and timed out jobs: the same SES");
}

// ----------------------------------------------------------------------------------- REST ----

void rest(const std::string& bm02Cli, const std::string& i313Cli) {
  frserver::RestServer server;
  int port = server.bind("127.0.0.1", 0);
  check(port > 0, "the REST server binds to a free port");
  if (port <= 0) return;
  std::thread listener([&server] { server.listen(); });

  httplib::Client cli("127.0.0.1", port);
  cli.set_read_timeout(120, 0);
  httplib::Headers hdr = {{"Freerouting-Environment-Host", "server_test/1.0"}, {"X-Correlation-ID", "test-cid"}};
  auto get = [&](const std::string& path, int& status) {
    for (int attempt = 0; attempt < 50; ++attempt) {
      if (auto res = cli.Get(path, hdr)) {
        status = res->status;
        return json::parse(res->body, nullptr, false);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));  // listen() not running yet
    }
    status = -1;
    return json();
  };
  auto send = [&](const std::string& method, const std::string& path, const json& body, int& status) {
    httplib::Result res = method == "PUT" ? cli.Put(path, hdr, body.is_null() ? "" : body.dump(), "application/json")
                                          : cli.Post(path, hdr, body.is_null() ? "" : body.dump(), "application/json");
    status = res ? res->status : -1;
    return res && !res->body.empty() ? json::parse(res->body, nullptr, false) : json();
  };
  int st = 0;

  json j = get("/v1/system/status", st);
  check(st == 200 && j["status"] == "OK", "GET /v1/system/status");
  if (auto res = cli.Get("/v1/system/status", hdr))
    check(res->get_header_value("X-Correlation-ID") == "test-cid", "X-Correlation-ID is sent back");
  j = get("/v1/system/environment", st);
  check(st == 200 && j.is_object(), "GET /v1/system/environment");
  j = get("/v1/no/such/path", st);
  check(st == 404 && j.contains("error"), "an unknown path: 404 with a JSON error");

  // A job step by step: session, enqueue, input, settings, start, poll, output
  json session = send("POST", "/v1/sessions/create", json(), st);
  check(st == 200 && session.contains("id"), "POST /v1/sessions/create");
  std::string sid = session.value("id", std::string());
  json job = send("POST", "/v1/jobs/enqueue", {{"session_id", sid}, {"name", "Issue313-FastTest"}}, st);
  check(st == 200 && job["state"] == "QUEUED", "POST /v1/jobs/enqueue: QUEUED");
  std::string jid = job.value("id", std::string());
  send("POST", "/v1/jobs/enqueue", {{"session_id", "no-such-session"}}, st);
  check(st == 400, "enqueue with an unknown session: 400");
  send("PUT", "/v1/jobs/" + jid + "/start", json(), st);
  check(st == 400 || st == 200, "start without input answers");  // Java: the job turns INVALID later
  // (a fresh job for the real run, the first one may have been started without input)
  job = send("POST", "/v1/jobs/enqueue", {{"session_id", sid}, {"name", "Issue313-FastTest"}}, st);
  jid = job.value("id", std::string());
  send("POST", "/v1/jobs/" + jid + "/input", {{"filename", "x.dsn"}, {"data", "%%%"}}, st);
  check(st == 400, "input that is not base 64: 400");
  send("POST", "/v1/jobs/" + jid + "/input", {{"filename", "x.json"}, {"data", frserver::base64Encode("{\"a\":1}")}}, st);
  check(st == 400, "KiCad JSON as input: 400");
  j = send("POST", "/v1/jobs/" + jid + "/input",
           {{"filename", "Issue313-FastTest.dsn"}, {"data", frserver::base64Encode(fileText(fixture("Issue313-FastTest.dsn")))}}, st);
  check(st == 200 && j["input"]["filename"] == "Issue313-FastTest.dsn", "POST /v1/jobs/{id}/input");
  send("POST", "/v1/jobs/" + jid + "/settings", {{"no_such_setting", 1}}, st);
  check(st == 400, "an unknown setting: 400");
  send("POST", "/v1/jobs/" + jid + "/settings", {{"autorouter", {{"max_passes", 1}}}}, st);
  check(st == 200, "POST /v1/jobs/{id}/settings");
  send("POST", "/v1/jobs/" + jid + "/settings", {{"max_threads", 1}}, st);
  j = get("/v1/jobs/" + jid + "/settings", st);
  check(j["autorouter"]["max_passes"] == 1 && j["max_threads"] == 1, "settings posted twice are merged");
  j = send("PUT", "/v1/jobs/" + jid + "/start", json(), st);
  check(st == 200, "PUT /v1/jobs/{id}/start");
  std::string state;
  for (int k = 0; k < 600; ++k) {
    j = get("/v1/jobs/" + jid + "?compact=true", st);
    state = j.value("state", std::string());
    if (state == "COMPLETED" || state == "CANCELLED" || state == "TIMED_OUT" || state == "TERMINATED" || state == "INVALID") break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  check(state == "COMPLETED", "the REST job completes (" + state + ")");
  j = get("/v1/jobs/" + jid + "/output", st);
  check(st == 200 && frserver::base64Decode(j.value("data", std::string())) == std::optional<std::string>(i313Cli),
        "GET /v1/jobs/{id}/output: SES identical to the command line's (Issue313, 1 pass, 1 thread)");
  j = get("/v1/jobs/" + jid + "/drc/summary", st);
  check(st == 200 && j.contains("clearance_violations_count"), "GET /v1/jobs/{id}/drc/summary");
  j = get("/v1/jobs/" + jid + "/logs", st);
  check(st == 200 && j.is_array(), "GET /v1/jobs/{id}/logs");  // lines at the log level (-q: warnings; Issue313 has none)
  j = get("/v1/jobs/list/" + sid, st);
  check(st == 200 && j.is_array() && j.size() == 2, "GET /v1/jobs/list/{session}");
  j = send("POST", "/v1/jobs/" + jid + "/input", {{"data", frserver::base64Encode("(pcb x)")}}, st);
  check(st == 400, "a started job cannot be changed");
  get("/v1/jobs/" + jid + "/output/json", st);
  check(st == 501, "KiCad JSON output: 501");

  // One-call autoroute (Java AutorouteControllerV1)
  j = send("POST", "/v1/autoroute", {{"file_content", fileText(fixture("Issue508-DAC2020_bm02.dsn"))},
                                     {"file_name", "Issue508-DAC2020_bm02.dsn"}}, st);
  check(st == 200 && j["status"] == "COMPLETED", "POST /v1/autoroute: COMPLETED");
  check(j["outputs"]["SES"] == bm02Cli, "POST /v1/autoroute: SES identical to the command line's (bm02)");
  send("POST", "/v1/autoroute", {{"file_content", ""}}, st);
  check(st == 400, "POST /v1/autoroute with an empty design: 400");

  // MCP over HTTP
  j = send("POST", "/v1/mcp", {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "tools/list"}}, st);
  check(st == 200 && j["id"] == 7 && j["result"]["tools"].size() == 10, "POST /v1/mcp tools/list");
  if (auto res = cli.Post("/v1/mcp", hdr, "{not json", "application/json"))
    check(json::parse(res->body, nullptr, false)["error"]["code"] == -32700, "POST /v1/mcp with bad JSON: -32700");
  send("POST", "/v1/mcp", {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, st);
  check(st == 204, "POST /v1/mcp notification: 204");

  server.stop();
  listener.join();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: server_test <freerouting-cpp> <fixtures directory> <work directory>\n");
    return 2;
  }
  exe = fs::absolute(argv[1]).string();
  fixtures = fs::absolute(argv[2]).string();
  work = fs::absolute(argv[3]).string();
  if (!fs::exists(fixture("Issue508-DAC2020_bm02.dsn"))) {
    std::fprintf(stderr, "server_test: no fixtures in %s (upstream/fixtures; set FR_FIXTURES_DIR)\n", fixtures.c_str());
    return 2;
  }
  fs::create_directories(work);
  fr::FRLogger::logLevel = fr::FRLogger::LEVEL_WARN;

  std::string bm02 = cliSes("bm02", fixture("Issue508-DAC2020_bm02.dsn"), "");
  std::string i107 = cliSes("Issue107", fixture("Issue107-freq_teiler_200kHz_kicad.dsn"),
                            "-dr \"" + fixture("Issue107-freq_teiler_200kHz_kicad.rules") + "\" -mp 1");
  std::string i313 = cliSes("Issue313", fixture("Issue313-FastTest.dsn"), "-mp 1 -mt 1");

  mcpProtocol();
  mcpRouting(bm02, i107);
  mcpCancelAndTimeout(bm02);
  rest(bm02, i313);

  frserver::JobManager::instance().shutdown();
  if (failures > 0) {
    std::fprintf(stderr, "server_test: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  std::printf("server_test: all %d checks passed\n", checks);
  return 0;
}
