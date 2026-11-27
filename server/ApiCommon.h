// What the REST API and the MCP server share: job JSON, settings JSON, the one-call autoroute.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "JobManager.h"
#include "Json.h"

namespace frserver {

/** Version string of the servers (system status, MCP serverInfo). */
extern const char* const kServerVersion;

std::string base64Encode(const std::string& data);
/** Decodes Base64 (standard or URL alphabet, padding optional, whitespace ignored); nullopt if bad. */
std::optional<std::string> base64Decode(const std::string& text);

/** Reads a whole file; nullopt if it cannot be read. */
std::optional<std::string> readFile(const std::string& path);
/** Writes a whole file; false if it cannot be written. */
bool writeFile(const std::string& path, const std::string& data);
std::string baseName(const std::string& path);

/** ISO-8601 UTC time of epoch milliseconds (Java Instant.toString()), "" for 0. */
std::string isoTime(int64_t ms);

/**
 * Router settings JSON (nested like Java's RouterSettings JSON: {"autorouter": {"max_passes": 5},
 * "scoring": {"via_costs": 42}, "max_threads": 2}) -> setting paths of applyRouterSetting.
 * Returns an error message ("" if all settings are known).
 */
std::string settingsFromJson(const json& settings, JobSettings& out);
/** Deep merge of `patch` into `base` (objects merged, other values replaced). */
void mergeJson(json& base, const json& patch);

json drcSummaryToJson(const DrcSummary& d, int32_t maxViolations = -1);
json figuresToJson(const std::optional<BoardFigures>& f);
/** Java BoardFileDetails of the input / output of a job. */
json fileDetailsToJson(const JobInfo& job, bool output);
/** GET /v1/jobs/{id}: Java's RoutingJob JSON (as far as the port has it). */
json jobToJson(const JobInfo& job, bool compact = false);
json logLineToJson(const LogLine& line, const std::string& jobId);
const char* logLevelName(int32_t level);

/** Arguments of the one-call autoroute (MCP autoroute_board, REST POST /v1/autoroute). */
struct AutorouteArgs {
  JobFile design;
  std::optional<JobFile> rules;
  std::optional<std::string> kicadProject, kicadDesignRules, kicadBoard;
  JobSettings settings;
  json settingsJson = json::object();
  std::optional<std::string> outputPath;
  bool returnSes = true;
  bool returnDrcSummary = true;
  int32_t maxViolations = 50;
  bool wait = true;
  std::string sessionId;
  std::string name;
};

/**
 * Starts the job of `args`, waits for it if args.wait (calling `progress` on each change,
 * stopping early if `stop` returns true: the job is then cancelled), and returns the result in the
 * shape of Java's AutorouteResponse (job_id, session_id, status, duration_seconds,
 * unrouted_connections, clearance_violations, outputs, drc_summary, message). `error` is set if the
 * job could not be started or the output could not be written.
 */
json runAutoroute(const AutorouteArgs& args, std::string& error,
                  const std::function<void(const JobInfo&)>& progress = nullptr,
                  const std::function<bool()>& stop = nullptr);

/** The result JSON of a job as runAutoroute returns it (for a job that may still run). */
json autorouteResult(const JobInfo& job, const AutorouteArgs& args);

}  // namespace frserver

namespace frserver {

/**
 * Reads the arguments of a one-call autoroute: MCP autoroute_board (camelCase names) or REST
 * POST /v1/autoroute (Java AutorouteRequest snake_case names); both spellings are accepted.
 * `restDefaults`: Java's REST defaults (output ["SES"], a 300 s budget). Returns an error message
 * ("" if the arguments are good); files given by path are read here.
 */
std::string parseAutorouteArgs(const json& in, bool restDefaults, AutorouteArgs& out);

/** "HH:MM:SS" of a number of seconds (the engine's job timeout syntax). */
std::string timeoutString(int64_t seconds);

}  // namespace frserver
