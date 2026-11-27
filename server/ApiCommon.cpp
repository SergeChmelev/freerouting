#include "ApiCommon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#include "logger/FRLogger.h"
#include "platform/platform.h"
#include "settings/RouterSettingPaths.h"

namespace frserver {

const char* const kServerVersion = "freerouting-cpp 2.6 (C++ port of freerouting)";

// ----------------------------------------------------------------------------- utilities ----

std::string base64Encode(const std::string& data) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t v = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8) | uint8_t(data[i + 2]);
    out += tbl[v >> 18];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += tbl[v & 63];
  }
  if (i + 1 == data.size()) {
    uint32_t v = uint8_t(data[i]) << 16;
    out += tbl[v >> 18];
    out += tbl[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == data.size()) {
    uint32_t v = (uint8_t(data[i]) << 16) | (uint8_t(data[i + 1]) << 8);
    out += tbl[v >> 18];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

std::optional<std::string> base64Decode(const std::string& text) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::string out;
  out.reserve(text.size() / 4 * 3);
  uint32_t acc = 0;
  int bits = 0;
  bool padding = false;
  for (char c : text) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '=') {
      padding = true;
      continue;
    }
    if (padding) return std::nullopt;  // data after padding
    int v = val(c);
    if (v < 0) return std::nullopt;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  if (bits >= 6) return std::nullopt;  // a lone character
  return out;
}

std::optional<std::string> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeFile(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << data;
  out.close();
  return static_cast<bool>(out);
}

std::string baseName(const std::string& path) {
  auto pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string isoTime(int64_t ms) {
  if (ms <= 0) return "";
  std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  jrt::platform::utc_time(t, &tm);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
  return buf;
}

const char* logLevelName(int32_t level) {
  // Java LogEntryType names
  switch (level) {
    case fr::FRLogger::LEVEL_ERROR: return "Error";
    case fr::FRLogger::LEVEL_WARN: return "Warning";
    case fr::FRLogger::LEVEL_INFO: return "Info";
    case fr::FRLogger::LEVEL_DEBUG: return "Debug";
    default: return "Trace";
  }
}

// ------------------------------------------------------------------------------ settings ----

namespace {

std::string scalarText(const json& v) {
  if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
  if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
  if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
  if (v.is_number_float()) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v.get<double>());
    return buf;
  }
  return v.get<std::string>();
}

void flatten(const json& obj, const std::string& prefix, JobSettings& out, std::vector<std::string>& unknown) {
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    std::string path = prefix.empty() ? it.key() : prefix + "." + it.key();
    const json& v = it.value();
    if (v.is_null()) continue;  // Java: null means "no change"
    if (v.is_object()) {
      flatten(v, path, out, unknown);
    } else if (v.is_boolean() || v.is_number() || v.is_string()) {
      if (fr::hasRouterSetting(path)) {
        out.paths.emplace_back(path, scalarText(v));
      } else {
        unknown.push_back(path);
      }
    } else {
      unknown.push_back(path + " (arrays are not supported)");
    }
  }
}

}  // namespace

std::string settingsFromJson(const json& settings, JobSettings& out) {
  if (settings.is_null()) return "";
  if (!settings.is_object()) return "The router settings must be a JSON object.";
  std::vector<std::string> unknown;
  flatten(settings, "", out, unknown);
  if (unknown.empty()) return "";
  std::string msg = "Unknown or unsupported router settings: ";
  for (size_t i = 0; i < unknown.size(); ++i) msg += (i ? ", " : "") + unknown[i];
  return msg + ". Settings use freerouting's JSON names, e.g. {\"autorouter\": {\"max_passes\": 10}, "
               "\"scoring\": {\"via_costs\": 200}, \"optimizer\": {\"enabled\": false}, \"max_threads\": 4}.";
}

void mergeJson(json& base, const json& patch) {
  if (!base.is_object() || !patch.is_object()) {
    base = patch;
    return;
  }
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object()) {
      mergeJson(base[it.key()], it.value());
    } else if (!it.value().is_null()) {
      base[it.key()] = it.value();
    }
  }
}

// ---------------------------------------------------------------------------------- JSON ----

json drcSummaryToJson(const DrcSummary& d, int32_t maxViolations) {
  json j = json::object();
  j["clearance_violations_count"] = d.clearanceViolationsCount;
  j["unconnected_nets_count"] = d.unconnectedNetsCount;
  json vs = json::array();
  for (const DrcViolation& v : d.violations) {
    if (maxViolations >= 0 && static_cast<int32_t>(vs.size()) >= maxViolations) break;
    json o = json::object();
    o["type"] = v.type;
    o["severity"] = v.severity;
    o["layer"] = v.layer;
    o["explanation"] = v.explanation;
    if (v.expectedClearanceMm) o["expected_clearance_mm"] = *v.expectedClearanceMm;
    if (v.actualClearanceMm) o["actual_clearance_mm"] = *v.actualClearanceMm;
    if (v.shortfallMm) o["shortfall_mm"] = *v.shortfallMm;
    o["items"] = v.items;
    vs.push_back(std::move(o));
  }
  j["violations"] = std::move(vs);
  if (maxViolations >= 0 && static_cast<int32_t>(d.violations.size()) > maxViolations)
    j["violations_omitted"] = static_cast<int32_t>(d.violations.size()) - maxViolations;
  json zones = json::array();
  for (const DrcZone& z : d.congestionZones) {
    zones.push_back(json{{"center_x_mm", z.centerXmm},
                         {"center_y_mm", z.centerYmm},
                         {"radius_mm", z.radiusMm},
                         {"violation_count", z.violationCount},
                         {"description", z.description}});
  }
  j["congestion_zones"] = std::move(zones);
  j["hints"] = d.hints;
  return j;
}

json figuresToJson(const std::optional<BoardFigures>& f) {
  // Java BoardStatistics, the parts the port fills (null fields are left out, like Gson does).
  json j = json::object();
  if (!f) return j;
  auto put = [](json& o, const char* key, const auto& v) {
    if (v) o[key] = *v;
  };
  put(j, "host", f->host);
  json layers = json::object();
  put(layers, "total_count", f->layerCount);
  j["layers"] = layers;
  json components = json::object();
  put(components, "total_count", f->componentCount);
  j["components"] = components;
  json nets = json::object();
  put(nets, "total_count", f->totalNetCount);
  put(nets, "class_count", f->netclassCount);
  put(nets, "unrouted_count", f->unroutedNetCount);
  put(nets, "routed_count", f->routedNetCount);
  j["nets"] = nets;
  json connections = json::object();
  put(connections, "incomplete_count", f->incompleteConnections);
  j["connections"] = connections;
  json traces = json::object();
  put(traces, "total_count", f->traceCount);
  put(traces, "total_length_mm", f->routedNetLengthMm);
  j["traces"] = traces;
  json vias = json::object();
  put(vias, "total_count", f->viaCount);
  j["vias"] = vias;
  json cv = json::object();
  put(cv, "total_count", f->clearanceViolationCount);
  j["clearance_violations"] = cv;
  return j;
}

namespace {

std::string sniffFormat(const std::string& data) {
  size_t i = data.find_first_not_of(" \t\r\n");
  if (i == std::string::npos) return "UNKNOWN";
  auto starts = [&](const char* p) { return data.compare(i, std::strlen(p), p) == 0; };
  if (starts("(pcb") || starts("(PCB")) return "DSN";
  if (starts("(ses") || starts("(SES")) return "SES";
  if (starts("(rul") || starts("(RUL") || starts("(Rul")) return "RULES";
  if (starts("{")) return "KICAD_DESIGN_JSON";
  return "UNKNOWN";
}

}  // namespace

json fileDetailsToJson(const JobInfo& job, bool output) {
  json j = json::object();
  if (output) {
    j["size"] = job.outputSize;
    j["crc32"] = job.outputCrc32;
    j["format"] = "SES";
    j["statistics"] = figuresToJson(job.outputFigures);
    std::string base = job.input ? job.input->filename : job.name;
    auto dot = base.find_last_of('.');
    j["filename"] = (dot == std::string::npos ? base : base.substr(0, dot)) + ".ses";
  } else {
    j["size"] = static_cast<int64_t>(job.input->data.size());
    j["crc32"] = job.input->crc32 ? *job.input->crc32 : 0;
    j["format"] = sniffFormat(job.input->data);
    j["statistics"] = figuresToJson(job.inputFigures);
    j["filename"] = job.input->filename;
  }
  j["path"] = "";
  return j;
}

json jobToJson(const JobInfo& job, bool compact) {
  json j = json::object();
  j["id"] = job.id;
  int64_t now = JobManager::nowMs();
  if (compact) {
    if (!job.sessionId.empty()) j["session_id"] = job.sessionId;
    j["name"] = job.name;
    j["state"] = jobStateName(job.state);
    j["stage"] = job.stage;
    j["current_pass"] = job.currentPass;
    if (job.startedMs) j["started_at"] = isoTime(job.startedMs);
    if (job.finishedMs) j["finished_at"] = isoTime(job.finishedMs);
    if (job.startedMs) j["duration_seconds"] = ((job.finishedMs ? job.finishedMs : now) - job.startedMs) / 1000.0;
    json stats = json::object();
    if (job.unrouted) stats["unrouted_connections"] = *job.unrouted;
    if (job.violations) stats["clearance_violations"] = *job.violations;
    if (!stats.empty()) j["statistics"] = stats;
    if (!job.message.empty()) j["message"] = job.message;
    if (job.error) j["error"] = *job.error;
    return j;
  }
  j["created_at"] = isoTime(job.createdMs);
  j["name"] = job.name;
  if (job.startedMs) j["started_at"] = isoTime(job.startedMs);
  if (job.finishedMs) j["finished_at"] = isoTime(job.finishedMs);
  j["state"] = jobStateName(job.state);
  j["stage"] = job.stage;
  j["priority"] = job.priority;
  if (!job.sessionId.empty()) j["session_id"] = job.sessionId;
  if (job.input) j["input"] = fileDetailsToJson(job, false);
  if (job.hasOutput) j["output"] = fileDetailsToJson(job, true);
  if (job.rules) {
    j["rules"] = json{{"size", static_cast<int64_t>(job.rules->data.size())},
                      {"crc32", job.rules->crc32 ? *job.rules->crc32 : 0},
                      {"format", "RULES"},
                      {"statistics", json::object()},
                      {"filename", job.rules->filename},
                      {"path", ""}};
  }
  json settings = json::parse(job.settingsJson.empty() ? "{}" : job.settingsJson, nullptr, false);
  j["router_settings"] = settings.is_discarded() ? json::object() : settings;
  j["isCancelledByUser"] = job.state == JobState::CANCELLED || job.state == JobState::STOPPING;
  j["current_pass"] = job.currentPass;
  // PORT EXTENSION: the progress the port reports (Java shows it only through the logs).
  if (job.startedMs) j["duration_seconds"] = ((job.finishedMs ? job.finishedMs : now) - job.startedMs) / 1000.0;
  if (job.unrouted) j["unrouted_connections"] = *job.unrouted;
  if (job.violations) j["clearance_violations"] = *job.violations;
  if (!job.message.empty()) j["message"] = job.message;
  if (job.error) j["error"] = *job.error;
  return j;
}

json logLineToJson(const LogLine& line, const std::string& jobId) {
  json j = json::object();
  j["timestamp"] = isoTime(line.timeMs);
  j["type"] = logLevelName(line.level);
  j["message"] = line.text;
  if (!jobId.empty()) j["topic"] = jobId;
  return j;
}

// ----------------------------------------------------------------------------- autoroute ----

json autorouteResult(const JobInfo& job, const AutorouteArgs& args) {
  json j = json::object();
  j["job_id"] = job.id;
  j["session_id"] = job.sessionId;
  j["status"] = jobStateName(job.state);
  int64_t end = job.finishedMs ? job.finishedMs : JobManager::nowMs();
  j["duration_seconds"] = job.startedMs ? std::round((end - job.startedMs) / 10.0) / 100.0 : 0.0;
  if (job.unrouted) j["unrouted_connections"] = *job.unrouted;
  if (job.violations) j["clearance_violations"] = *job.violations;
  json outputs = json::object();
  std::optional<std::string> ses;
  if (job.hasOutput && (args.returnSes || args.outputPath)) ses = JobManager::instance().output(job.id);
  if (args.returnSes && ses) outputs["SES"] = *ses;
  j["outputs"] = std::move(outputs);
  if (args.returnDrcSummary && job.hasDrcSummary) {
    if (auto drc = JobManager::instance().drcSummary(job.id)) j["drc_summary"] = drcSummaryToJson(*drc, args.maxViolations);
  }
  std::string message;
  switch (job.state) {
    case JobState::COMPLETED: {
      char buf[96];
      std::snprintf(buf, sizeof buf, "Routing completed successfully in %.2fs.", j["duration_seconds"].get<double>());
      message = buf;
      break;
    }
    case JobState::TIMED_OUT: message = "Routing timed out; partial output returned."; break;
    case JobState::QUEUED:
    case JobState::READY_TO_START:
    case JobState::RUNNING:
    case JobState::STOPPING:
      message = "The job is running; poll get_job_details with the job_id.";
      break;
    default:
      message = "Routing ended with state: " + std::string(jobStateName(job.state)) + (job.error ? " (" + *job.error + ")" : "");
  }
  if (args.outputPath && ses) {
    if (writeFile(*args.outputPath, *ses)) {
      j["output_path"] = *args.outputPath;
      message += " The SES was written to " + *args.outputPath + " (" + std::to_string(ses->size()) + " bytes).";
    } else {
      message += " ERROR: the SES could not be written to " + *args.outputPath + ".";
    }
  }
  j["message"] = message;
  return j;
}

json runAutoroute(const AutorouteArgs& args, std::string& error, const std::function<void(const JobInfo&)>& progress,
                  const std::function<bool()>& stop) {
  JobManager& jobs = JobManager::instance();
  std::string name = args.name.empty() ? baseName(args.design.filename) : args.name;
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  std::string id = jobs.enqueue(args.sessionId, name);
  std::string e;
  if ((e = jobs.setInput(id, args.design)).empty() && args.rules) e = jobs.setRules(id, *args.rules);
  if (e.empty()) e = jobs.setKiCadFiles(id, args.kicadDesignRules, args.kicadProject, args.kicadBoard);
  if (e.empty()) e = jobs.setSettings(id, args.settings, args.settingsJson.dump());
  if (e.empty()) e = jobs.setWantDrcSummary(id, args.returnDrcSummary);
  if (e.empty()) e = jobs.start(id);
  if (!e.empty()) {
    error = e;
    return json();
  }
  std::optional<JobInfo> info;
  if (args.wait) {
    info = jobs.wait(id, -1, progress, stop);
    if (info && !isFinished(info->state)) {
      // The client gave up (cancelled request / closed connection): stop the job.
      jobs.cancel(id);
      info = jobs.wait(id, 30000);
    }
  } else {
    info = jobs.info(id);
  }
  if (!info) {
    error = "The job disappeared.";
    return json();
  }
  AutorouteArgs a = args;
  if (!isFinished(info->state)) a.outputPath.reset();
  return autorouteResult(*info, a);
}

}  // namespace frserver

namespace frserver {

std::string timeoutString(int64_t seconds) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", static_cast<long long>(seconds / 3600),
                static_cast<long long>(seconds / 60 % 60), static_cast<long long>(seconds % 60));
  return buf;
}

namespace {

const json* arg(const json& in, std::initializer_list<const char*> names) {
  for (const char* n : names) {
    auto it = in.find(n);
    if (it != in.end() && !it->is_null()) return &*it;
  }
  return nullptr;
}

std::optional<std::string> stringArg(const json& in, std::initializer_list<const char*> names, std::string& error) {
  const json* v = arg(in, names);
  if (!v) return std::nullopt;
  if (!v->is_string()) {
    error = std::string("'") + *names.begin() + "' must be a string.";
    return std::nullopt;
  }
  return v->get<std::string>();
}

std::optional<int64_t> intArg(const json& in, std::initializer_list<const char*> names, std::string& error) {
  const json* v = arg(in, names);
  if (!v) return std::nullopt;
  if (v->is_number_integer() || v->is_number_unsigned()) return v->get<int64_t>();
  if (v->is_number_float() && std::floor(v->get<double>()) == v->get<double>()) return static_cast<int64_t>(v->get<double>());
  if (v->is_string()) {
    try {
      return std::stoll(v->get<std::string>());
    } catch (...) {
    }
  }
  error = std::string("'") + *names.begin() + "' must be an integer.";
  return std::nullopt;
}

std::optional<bool> boolArg(const json& in, std::initializer_list<const char*> names, std::string& error) {
  const json* v = arg(in, names);
  if (!v) return std::nullopt;
  if (v->is_boolean()) return v->get<bool>();
  if (v->is_string() && (*v == "true" || *v == "false")) return *v == "true";
  error = std::string("'") + *names.begin() + "' must be true or false.";
  return std::nullopt;
}

/** A file given by path or content; "" or an error. */
std::string fileArg(const json& in, std::initializer_list<const char*> pathNames,
                    std::initializer_list<const char*> contentNames, const std::string& defaultName,
                    std::optional<JobFile>& out) {
  std::string error;
  std::optional<std::string> path = stringArg(in, pathNames, error);
  std::optional<std::string> content = stringArg(in, contentNames, error);
  if (!error.empty()) return error;
  if (path) {
    std::optional<std::string> data = readFile(*path);
    if (!data) return "Cannot read the file '" + *path + "'.";
    out = JobFile{baseName(*path), std::move(*data), std::nullopt};
  } else if (content) {
    out = JobFile{defaultName, *content, std::nullopt};
  }
  return "";
}

}  // namespace

std::string parseAutorouteArgs(const json& in, bool restDefaults, AutorouteArgs& out) {
  if (!in.is_object()) return "The arguments must be a JSON object.";
  std::string error;
  std::optional<std::string> fileName = stringArg(in, {"fileName", "file_name", "filename"}, error);
  std::optional<JobFile> design;
  if (!(error = fileArg(in, {"filePath", "file_path"}, {"fileContent", "file_content"},
                        fileName.value_or("design.dsn"), design)).empty())
    return error;
  if (!design) return "Neither filePath nor fileContent was given for the design.";
  if (design->data.find_first_not_of(" \t\r\n") == std::string::npos) return "The design is empty.";
  out.design = std::move(*design);
  // The rules header names the design: the CLI checks it against the rules file name.
  std::string designBase = out.design.filename;
  auto dot = designBase.find_last_of('.');
  if (dot != std::string::npos) designBase = designBase.substr(0, dot);
  if (!(error = fileArg(in, {"rulesPath", "rules_path"}, {"rulesContent", "rules_content"}, designBase + ".rules",
                        out.rules)).empty())
    return error;
  if (arg(in, {"sessionPath", "session_path", "sessionContent", "session_content"}))
    return "An initial session (SES) is not supported by freerouting-cpp.";
  out.kicadProject = stringArg(in, {"kicadProjectPath", "kicad_project_path"}, error);
  out.kicadDesignRules = stringArg(in, {"kicadDesignRulesPath", "kicad_design_rules_path"}, error);
  out.kicadBoard = stringArg(in, {"kicadBoardPath", "kicad_board_path"}, error);
  out.outputPath = stringArg(in, {"outputPath", "output_path"}, error);
  auto maxPasses = intArg(in, {"maxPasses", "max_passes"}, error);
  auto maxThreads = intArg(in, {"maxThreads", "max_threads"}, error);
  auto timeout = intArg(in, {"timeoutSeconds", "timeout_seconds"}, error);
  auto optimizer = boolArg(in, {"optimizer"}, error);
  auto wait = boolArg(in, {"wait"}, error);
  auto maxViolations = intArg(in, {"maxViolations", "max_violations"}, error);
  if (!error.empty()) return error;
  if (maxPasses) {
    if (*maxPasses < 1) return "'maxPasses' must be at least 1.";
    out.settings.maxPasses = static_cast<int32_t>(*maxPasses);
  }
  if (maxThreads) {
    if (*maxThreads < 1) return "'maxThreads' must be at least 1.";
    out.settings.maxThreads = static_cast<int32_t>(*maxThreads);
  }
  if (!timeout && restDefaults) timeout = 300;  // Java AutorouteControllerV1
  if (timeout && *timeout > 0) out.settings.timeout = timeoutString(*timeout);
  if (optimizer) out.settings.optimizer = *optimizer;
  if (wait) out.wait = *wait;
  if (maxViolations) out.maxViolations = static_cast<int32_t>(*maxViolations);
  if (const json* s = arg(in, {"routerSettings", "router_settings"})) {
    if (!(error = settingsFromJson(*s, out.settings)).empty()) return error;
    out.settingsJson = *s;
  }
  // What to return
  std::vector<std::string> formats;
  if (const json* f = arg(in, {"outputFormats", "output_formats"})) {
    if (!f->is_array()) return "'outputFormats' must be an array of strings.";
    for (const json& v : *f) {
      if (!v.is_string()) return "'outputFormats' must be an array of strings.";
      std::string s = v.get<std::string>();
      for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (s == "SES" || s == "DRC_SUMMARY" || s == "SUMMARY") {
        formats.push_back(s == "SUMMARY" ? "DRC_SUMMARY" : s);
      } else {
        return "Output format '" + v.get<std::string>() + "' is not supported by freerouting-cpp (SES, DRC_SUMMARY).";
      }
    }
  } else if (restDefaults) {
    formats = {"SES"};
  } else if (out.outputPath) {
    formats = {"DRC_SUMMARY"};
  } else {
    formats = {"SES", "DRC_SUMMARY"};
  }
  out.returnSes = std::find(formats.begin(), formats.end(), "SES") != formats.end();
  out.returnDrcSummary = std::find(formats.begin(), formats.end(), "DRC_SUMMARY") != formats.end();
  return "";
}

}  // namespace frserver
