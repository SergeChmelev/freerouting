#pragma once

#include <optional>
#include <string>

#include "core/RoutingJobPriority.h"
#include "core/RoutingJobState.h"
#include "core/RoutingStage.h"
// complete listener types needed by the inline ArrayList initialisers
#include "core/events/RoutingJobLogEntryAddedEventListener.h"
#include "core/events/RoutingJobUpdatedEventListener.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Represents a job that needs to be processed by the router. */
// PORT NOTE: reduced to what the headless pipeline uses (board, settings, state/stage, name,
// thread/stop handling, current pass, resource usage, phase metrics, input/rules file details,
// log helpers and events, the KiCad inputs kicadDesignRules / kicadProject / kicadBoardZones with
// their load* methods). Removed: apiKeyHash,
// toCompactJsonObject() (gson), getFusionScriptFile(), getInputFileDetails() /
// getOutputFileDetails(). UUID -> std::string (random UUID text); Instant -> epoch milliseconds
// (std::optional<int64_t>, nullopt = null); Duration -> milliseconds; File/Path -> std::string.
// `volatile`/`transient` dropped.
class RoutingJob : public virtual jrt::Object {
 public:
  static inline const std::string DSN_FILE_EXTENSION = "dsn";
  static inline const std::string BINARY_FILE_EXTENSION = "frb";

 private:
  static inline const std::string RULES_FILE_EXTENSION = "rules";
  static inline const std::string SES_FILE_EXTENSION = "ses";
  static inline const std::string FUSION_SCRIPT_FILE_EXTENSION = "scr";

 public:
  // "id": unique identifier for the routing job (UUID.randomUUID().toString())
  const std::string id;
  // "created_at": job creation timestamp (epoch millis)
  const int64_t createdAt;

 protected:
  // events to signal input and output updates
  jrt::List<RoutingJobUpdatedEventListener*>* settingsUpdatedEventListeners =
      new (jrt::kBoard) jrt::ArrayList<RoutingJobUpdatedEventListener*>();
  jrt::List<RoutingJobUpdatedEventListener*>* inputUpdatedEventListeners =
      new (jrt::kBoard) jrt::ArrayList<RoutingJobUpdatedEventListener*>();
  jrt::List<RoutingJobUpdatedEventListener*>* outputUpdatedEventListeners =
      new (jrt::kBoard) jrt::ArrayList<RoutingJobUpdatedEventListener*>();
  jrt::List<RoutingJobLogEntryAddedEventListener*>* logEntryAddedEventListeners =
      new (jrt::kBoard) jrt::ArrayList<RoutingJobLogEntryAddedEventListener*>();

 public:
  // "short_name": a short name for the job
  std::string shortName = "N/A";
  // "name": the name of the job
  std::string name;
  // "started_at": job start timestamp (epoch millis), null before start
  std::optional<int64_t> startedAt;
  // "finished_at": job completion timestamp (epoch millis)
  std::optional<int64_t> finishedAt;
  // "state": the current state of the job
  RoutingJobState state = RoutingJobState::INVALID;
  // "stage": the current stage of the job
  RoutingStage stage = RoutingStage::IDLE;
  // "priority": the priority of the job
  RoutingJobPriority priority = RoutingJobPriority::NORMAL;
  // "session_id": the session ID the job belongs to (PORT NOTE: Session management not ported)
  std::optional<std::string> sessionId;
  // "user_id": the user ID the job belongs to
  std::optional<std::string> userId;
  // "input": details of the uploaded input design file
  BoardFileDetails* input = nullptr;
  // "output": details of the routed output design file
  BoardFileDetails* output = nullptr;
  // "rules": details of the uploaded design rules (.rules) file
  BoardFileDetails* rules = nullptr;
  // "initial_session": details of the initial session (.ses or .json) file to import
  BoardFileDetails* initialSession = nullptr;
  // "host_cad": the CAD system that generated the board
  std::optional<std::string> hostCad;
  // "host_version": the version of the CAD system that generated the board
  std::optional<std::string> hostVersion;
  // "drc": details of the design rules check output
  BoardFileDetails* drc = nullptr;
  /**
   * KiCad custom design rules (.kicad_dru), translated into clearance classes once the board is
   * loaded; null if none was given.
   */
  KiCadDesignRules* kicadDesignRules = nullptr;
  /**
   * The board settings of the KiCad project (.kicad_pro) given with the design, whose minimum track
   * width bounds the neck width; null if none was given.
   */
  KiCadProject* kicadProject = nullptr;
  /**
   * The zones of the KiCad board (.kicad_pcb) given with the design, whose clearance and minimum
   * width are applied to the planes of the loaded board; null if none was given.
   */
  KiCadBoardZones* kicadBoardZones = nullptr;
  // "router_settings": router configuration settings (new RouterSettings() in the constructor)
  RouterSettings* routerSettings = nullptr;
  // "drc_settings": DRC configuration settings (new DesignRulesCheckerSettings())
  DesignRulesCheckerSettings* drcSettings = nullptr;
  // "resource_usage": resource usage stats (new RouterJobResourceUsage())
  RouterJobResourceUsage* resourceUsage = nullptr;

  StoppableThread* thread = nullptr;
  RoutingBoard* board = nullptr;

  /** Per-stage before/after metrics retained for the result manifest (new PhaseMetrics()). */
  RoutingResultManifest_PhaseMetrics* resultPhaseMetrics = nullptr;

  // epoch millis, null = no timeout
  std::optional<int64_t> timeoutAt;

 private:
  bool isCancelledByUser_ = false;  // PORT NOTE: field renamed (collides with the getter)

 public:
  /** Returns whether the user cancelled this job. */
  bool isCancelledByUser() { return isCancelledByUser_; }

  /** Sets whether the user cancelled this job. */
  void setCancelledByUser(bool cancelledByUser) { isCancelledByUser_ = cancelledByUser; }

 private:
  // "current_pass": current routing pass
  int32_t currentPass = 0;

 public:
  /** Parameterless constructor: random id, name "J-XXXXXX", shortName "XXXXXX". */
  RoutingJob();

  /** Creates a job belonging to a session (shortName "SSSSSS\\XXXXXX"). */
  explicit RoutingJob(const std::string& sessionId);
  /** Stage E: frees a job nobody else refers to (an optimizer candidate's) with the settings,
   * DRC settings, resource usage and phase metrics it owns. */
  static void disposePrivate(RoutingJob* job);

  /** Detects the file format from its binary content. */
  static io::FileFormat getFileFormat(jrt::Array<int8_t>* content);

  /** Detects the file format from a file path extension (Java: getFileFormat(Path)). */
  static io::FileFormat getFileFormat(const std::string& path);

  /** Returns the current routing pass. */
  int32_t getCurrentPass() { return currentPass; }

  /** Sets the current routing pass. */
  void setCurrentPass(int32_t currentPass) { this->currentPass = currentPass; }

  /** Returns the elapsed job duration in milliseconds, or null before the job starts. */
  std::optional<int64_t> getDuration();

  /** Sets the input from file content. */
  bool setInput(jrt::Array<int8_t>* inputFileContent);

  /** Loads the input file from the specified path (Java: setInput(String) / setInput(File)). */
  void setInput(const std::string& inputFilePath);

  /** Sets the rules from file content. */
  bool setRules(jrt::Array<int8_t>* rulesFileContent);

  /** Loads the rules file from the specified path, if it exists (Java: String / File). */
  void setRules(const std::string& rulesFilePath);

  /**
   * Reads the KiCad custom design rules of this job when explicitly provided.
   *
   * @param explicitFile the .kicad_dru given with the design, or null
   * @return false if {@code explicitFile} cannot be read
   */
  bool loadKiCadDesignRules(const std::optional<std::string>& explicitFile);

  /**
   * Reads the KiCad project file of this job when explicitly provided.
   *
   * @param explicitFile the .kicad_pro given with the design, or null
   * @return false if {@code explicitFile} cannot be read
   */
  bool loadKiCadProject(const std::optional<std::string>& explicitFile);

  /**
   * Reads the zones of the KiCad board file of this job when explicitly provided.
   *
   * @param explicitFile the .kicad_pcb given with the design, or null
   * @return false if {@code explicitFile} cannot be read
   */
  bool loadKiCadBoardZones(const std::optional<std::string>& explicitFile);

  /** Sets the initial session from file content. */
  bool setInitialSession(jrt::Array<int8_t>* sessionFileContent,
                         const std::optional<std::string>& filename);

  /** Loads the initial session file from the specified path, if it exists (Java: File). */
  void setInitialSession(const std::string& sessionFilePath);

  /** Returns the rules file associated with the output file. */
  std::string getRulesFile();

  /** Sets a placeholder input file for the specified path. */
  void setDummyInputFile(const std::optional<std::string>& filename);

 private:
  bool tryToSetInput(jrt::Array<int8_t>* fileContent);

  // Changes the file extension of the selected file
  std::string changeFileExtension(const std::string& filename,
                                  const std::string& newFileExtension);

 public:
  /** Attempts to set the output file and returns whether its format is supported. */
  bool tryToSetOutputFile(const std::optional<std::string>& outputFile);

  /** Comparable<RoutingJob>: by priority ordinal. */
  int32_t compareTo(RoutingJob* o);

  /** Returns the input file details. */
  BoardFileDetails* getInput() { return input; }

 private:
  void setInputFromFile(const std::string& inputFile);

 public:
  /** Applies the supplied router settings and returns whether any value changed. */
  bool setSettings(RouterSettings* settings);

  /** Registers a listener for settings updates. */
  void addSettingsUpdatedEventListener(RoutingJobUpdatedEventListener* listener);
  /** Notifies listeners that settings changed. */
  void fireSettingsUpdatedEvent();
  /** Registers a listener for input updates. */
  void addInputUpdatedEventListener(RoutingJobUpdatedEventListener* listener);
  /** Notifies listeners that input changed. */
  void fireInputUpdatedEvent();
  /** Registers a listener for output updates. */
  void addOutputUpdatedEventListener(RoutingJobUpdatedEventListener* listener);
  /** Notifies listeners that output changed. */
  void fireOutputUpdatedEvent();
  /** Registers a listener for new log entries. */
  void addLogEntryAddedEventListener(RoutingJobLogEntryAddedEventListener* listener);
  /** Unregisters a listener for new log entries. */
  void removeLogEntryAddedEventListener(RoutingJobLogEntryAddedEventListener* listener);
  /** Notifies listeners that a log entry was added. */
  void fireLogEntryAddedEvent(LogEntry* logEntry);

  /** Adds an informational log entry for this job ("[shortName] message"). */
  void logInfo(const std::string& message);
  /** Adds a warning log entry for this job. */
  void logWarning(const std::string& message);
  /** Adds an error log entry for this job (ex may be null). */
  void logError(const std::string& message, const std::exception* ex);
  /** Adds a debug log entry for this job. */
  void logDebug(const std::string& message);

  /** Detected host CAD name and version ("cad/version"), or null. */
  std::optional<std::string> getDetectedHost();
};

}  // namespace fr
