// MCP tool definitions of freerouting-cpp (JSON text, parsed once). Names follow the Java MCP
// server (api/mcp/OpenApiMcpToolRegistry) where the tool does the same thing.
R"JSON([
{
  "name": "autoroute_board",
  "description": "Autoroutes a PCB design (Specctra DSN) in one call: runs the freerouting autorouter and optimizer, then a design rules check, and returns the routed Specctra session (SES) and a DRC summary. Prefer filePath / outputPath (the file contents then never pass through the conversation). With wait=false it returns at once with a jobId; then poll get_job_details every few seconds. Import the SES back into the PCB editor (KiCad: File > Import > Specctra Session).",
  "inputSchema": {
    "type": "object",
    "properties": {
      "filePath": {"type": "string", "description": "Path of the design file (.dsn) on this machine."},
      "fileContent": {"type": "string", "description": "The design (Specctra DSN text) itself, if there is no file."},
      "fileName": {"type": "string", "description": "Name of the design given as fileContent (default design.dsn); the SES header uses it."},
      "rulesPath": {"type": "string", "description": "Optional Specctra .rules file (net classes, clearances, via rules)."},
      "rulesContent": {"type": "string", "description": "Optional .rules text, if there is no file."},
      "kicadProjectPath": {"type": "string", "description": "Optional KiCad project (.kicad_pro): its minimum track width and net class settings are respected."},
      "kicadDesignRulesPath": {"type": "string", "description": "Optional KiCad custom design rules (.kicad_dru), translated into clearance rules."},
      "kicadBoardPath": {"type": "string", "description": "Optional KiCad board (.kicad_pcb): the fill settings of its zones."},
      "outputPath": {"type": "string", "description": "Where to write the routed SES file. If given, the SES is not returned unless outputFormats asks for it."},
      "outputFormats": {"type": "array", "items": {"type": "string", "enum": ["SES", "DRC_SUMMARY"]}, "description": "What to return: SES (the routed session text) and/or DRC_SUMMARY. Default: [\"DRC_SUMMARY\"] with outputPath, else [\"SES\", \"DRC_SUMMARY\"]."},
      "maxPasses": {"type": "integer", "minimum": 1, "description": "Maximum autorouter passes (CLI -mp)."},
      "maxThreads": {"type": "integer", "minimum": 1, "description": "Threads of the optimizer (CLI -mt; 1 = sequential)."},
      "optimizer": {"type": "boolean", "description": "Run the optimizer after routing (default true)."},
      "timeoutSeconds": {"type": "integer", "minimum": 1, "description": "Routing time budget; the job stops when it is used up and returns what it has."},
      "routerSettings": {"type": "object", "description": "Router settings by their JSON names, nested like freerouting's settings, e.g. {\"autorouter\": {\"max_passes\": 10}, \"scoring\": {\"via_costs\": 200}, \"optimizer\": {\"enabled\": false}, \"max_threads\": 4}."},
      "maxViolations": {"type": "integer", "minimum": 0, "description": "At most this many violations in the DRC summary (default 50)."},
      "wait": {"type": "boolean", "description": "Wait for the job to finish (default true). false: return the jobId at once."}
    },
    "additionalProperties": false
  }
},
{
  "name": "check_board_drc",
  "description": "Runs the design rules check on a design without routing it: clearance violations and unconnected nets, with explanations, congestion hotspots and hints.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "filePath": {"type": "string", "description": "Path of the design file (.dsn)."},
      "fileContent": {"type": "string", "description": "The design (Specctra DSN text) itself."},
      "rulesPath": {"type": "string", "description": "Optional .rules file."},
      "maxViolations": {"type": "integer", "minimum": 0, "description": "At most this many violations in the answer (default 50)."}
    },
    "additionalProperties": false
  }
},
{
  "name": "get_job_details",
  "description": "State and progress of a routing job: state (QUEUED, READY_TO_START, RUNNING, COMPLETED, CANCELLED, TIMED_OUT, TERMINATED), stage, current pass, unrouted connections, clearance violations, elapsed time. Poll every 2-5 seconds while it runs.",
  "inputSchema": {
    "type": "object",
    "properties": {"jobId": {"type": "string", "description": "Job id from autoroute_board."}},
    "required": ["jobId"],
    "additionalProperties": false
  }
},
{
  "name": "cancel_job",
  "description": "Cancels a queued or running routing job.",
  "inputSchema": {
    "type": "object",
    "properties": {"jobId": {"type": "string"}},
    "required": ["jobId"],
    "additionalProperties": false
  }
},
{
  "name": "list_jobs",
  "description": "Lists the routing jobs of this server with their state.",
  "inputSchema": {"type": "object", "properties": {}, "additionalProperties": false}
},
{
  "name": "download_job_output_to_local_file",
  "description": "Saves the routed output (Specctra SES) of a finished job to a local file.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "jobId": {"type": "string", "description": "Unique identifier of the job"},
      "filePath": {"type": "string", "description": "The path on the local disk where the routed output (Specctra SES) should be saved."}
    },
    "required": ["jobId", "filePath"],
    "additionalProperties": false
  }
},
{
  "name": "get_job_output",
  "description": "Returns the routed output (Specctra SES text) of a finished job. Prefer download_job_output_to_local_file for large boards.",
  "inputSchema": {
    "type": "object",
    "properties": {"jobId": {"type": "string"}},
    "required": ["jobId"],
    "additionalProperties": false
  }
},
{
  "name": "get_job_drc_summary",
  "description": "DRC summary of the routed board of a finished job: clearance violations and unconnected nets with explanations, congestion hotspots and hints.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "jobId": {"type": "string"},
      "maxViolations": {"type": "integer", "minimum": 0, "description": "At most this many violations in the answer (default 50)."}
    },
    "required": ["jobId"],
    "additionalProperties": false
  }
},
{
  "name": "get_job_logs",
  "description": "Log lines of a routing job (the last ones by default).",
  "inputSchema": {
    "type": "object",
    "properties": {
      "jobId": {"type": "string"},
      "tail": {"type": "integer", "minimum": 1, "description": "Only the last N lines (default 50)."},
      "minLevel": {"type": "string", "enum": ["ERROR", "WARN", "INFO", "DEBUG", "TRACE"], "description": "Lowest level shown (default INFO)."}
    },
    "required": ["jobId"],
    "additionalProperties": false
  }
},
{
  "name": "get_system_status",
  "description": "Status of the freerouting-cpp server: version, jobs, CPU threads.",
  "inputSchema": {"type": "object", "properties": {}, "additionalProperties": false}
}
])JSON"
