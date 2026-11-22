#include "core/RoutingJob.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "board/facade/RoutingBoard.h"
#include "board/state/Communication.h"
#include "core/BoardFileDetails.h"
#include "core/RouterJobResourceUsage.h"
#include "core/events/BoardFileDetailsUpdatedEventListener.h"
#include "core/events/RoutingJobLogEntryAddedEvent.h"
#include "core/events/RoutingJobUpdatedEvent.h"
#include "core/results/RoutingResultManifest.h"
#include "io/FileFormat.h"
#include "logger/FRLogger.h"
#include "logger/LogEntry.h"
#include "settings/DesignRulesCheckerSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

namespace {

// PORT NOTE: UUID.randomUUID().toString(): a random version-4 UUID in canonical lower-case form.
std::string randomUuid() {
  static std::random_device rd;
  static std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
  uint64_t hi = gen();
  uint64_t lo = gen();
  hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version 4
  lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // IETF variant
  char buf[37];
  std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx",
                static_cast<unsigned>(hi >> 32), static_cast<unsigned>((hi >> 16) & 0xFFFF),
                static_cast<unsigned>(hi & 0xFFFF), static_cast<unsigned>(lo >> 48),
                static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
  return std::string(buf);
}

// Files.readAllBytes / InputStream.readAllBytes
jrt::Array<int8_t>* readAllBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw jrt::IOException(path);
  }
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto* result = new jrt::Array<int8_t>(static_cast<int32_t>(bytes.size()));
  for (std::size_t i = 0; i < bytes.size(); i++) {
    (*result)[static_cast<int32_t>(i)] = static_cast<int8_t>(bytes[i]);
  }
  return result;
}

bool fileExists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

// File.getName(): the last name element of the path.
std::string fileName(const std::string& path) {
  return std::filesystem::path(path).filename().string();
}

// File.getAbsolutePath()
std::string absolutePath(const std::string& path) {
  std::error_code ec;
  return std::filesystem::absolute(path, ec).string();
}

bool isBlank(const std::optional<std::string>& s) {
  return !s.has_value() || jrt::strip(*s).empty();
}

}  // namespace

RoutingJob::RoutingJob() : id(randomUuid()), createdAt(jrt::System::currentTimeMillis()) {
  this->routerSettings = new RouterSettings();
  this->drcSettings = new DesignRulesCheckerSettings();
  this->resourceUsage = new RouterJobResourceUsage();
  this->resultPhaseMetrics = new RoutingResultManifest_PhaseMetrics();
  this->name = "J-" + jrt::toUpperCase(jrt::substring(this->id, 0, 6));
  this->shortName = jrt::toUpperCase(jrt::substring(this->id, 0, 6));
}

RoutingJob::RoutingJob(const std::string& sessionId) : RoutingJob() {
  this->sessionId = sessionId;
  this->shortName = jrt::toUpperCase(jrt::substring(*this->sessionId, 0, 6)) + "\\" +
                    jrt::toUpperCase(jrt::substring(this->id, 0, 6));
}

io::FileFormat RoutingJob::getFileFormat(jrt::Array<int8_t>* content) {
  if (content == nullptr) {
    return io::FileFormat::UNKNOWN;
  }
  // First, check if it's a text-based format (JSON or SCR)
  int32_t firstNonWs = -1;
  for (int32_t i = 0; i < content->length; i++) {
    int8_t b = (*content)[i];
    if (b != ' ' && b != '\t' && b != '\r' && b != '\n') {
      firstNonWs = i;
      break;
    }
  }
  if (firstNonWs >= 0) {
    if ((*content)[firstNonWs] == '{') {
      return io::FileFormat::KICAD_DESIGN_JSON;
    }
    // ISO_8859_1 decoding + toUpperCase(ROOT): ASCII upper-casing of the bytes
    // PORT NOTE: Latin-1 letters above 0x7F are not upper-cased; they never match the prefixes.
    std::string textSample;
    int32_t n = jrt::Math::min(content->length - firstNonWs, 64);
    for (int32_t i = 0; i < n; i++) {
      textSample += static_cast<char>((*content)[firstNonWs + i]);
    }
    textSample = jrt::toUpperCase(textSample);
    if (jrt::startsWith(textSample, "GRID ") || jrt::startsWith(textSample, "GRID;") ||
        jrt::startsWith(textSample, "SET ") || jrt::startsWith(textSample, "LAYER ") ||
        jrt::startsWith(textSample, "WIRE ") || jrt::startsWith(textSample, "CHANGE LAYER ") ||
        jrt::startsWith(textSample, "SCRIPT") || jrt::startsWith(textSample, "RIPUP")) {
      return io::FileFormat::SCR;
    }
  }

  // Read the first 6 bytes to determine the file format
  int8_t buffer[6] = {0, 0, 0, 0, 0, 0};
  int32_t bytesRead = jrt::Math::min(content->length, 6);
  for (int32_t i = 0; i < bytesRead; i++) {
    buffer[i] = (*content)[i];
  }
  if (bytesRead == 6) {
    // Check if the file is a binary file
    if (buffer[0] == static_cast<int8_t>(0xAC) && buffer[1] == static_cast<int8_t>(0xED) &&
        buffer[2] == static_cast<int8_t>(0x00) && buffer[3] == static_cast<int8_t>(0x05)) {
      return io::FileFormat::FRB;
    }

    // The signature follows all leading line breaks and blanks.
    std::fill(buffer, buffer + 6, static_cast<int8_t>(0));
    if (firstNonWs >= 0) {
      int32_t m = jrt::Math::min(6, content->length - firstNonWs);
      for (int32_t i = 0; i < m; i++) {
        buffer[i] = (*content)[firstNonWs + i];
      }
    }

    // Check if the file is a DSN file (it starts with "(pcb" or "(PCB")
    if ((buffer[0] == 0x28 && buffer[1] == 0x70 && buffer[2] == 0x63 && buffer[3] == 0x62) ||
        (buffer[0] == 0x28 && buffer[1] == 0x50 && buffer[2] == 0x43 && buffer[3] == 0x42)) {
      return io::FileFormat::DSN;
    }

    // Check if the file is a SES file (it starts with "(ses" or "(SES")
    if ((buffer[0] == 0x28 && buffer[1] == 0x73 && buffer[2] == 0x65 && buffer[3] == 0x73) ||
        (buffer[0] == 0x28 && buffer[1] == 0x53 && buffer[2] == 0x45 && buffer[3] == 0x53)) {
      return io::FileFormat::SES;
    }

    // Check if the file is a RULES file (it starts with "(rules" or "(RULES")
    if (buffer[0] == 0x28 && (buffer[1] == 0x72 || buffer[1] == 0x52) &&
        (buffer[2] == 0x75 || buffer[2] == 0x55) && (buffer[3] == 0x6C || buffer[3] == 0x4C)) {
      return io::FileFormat::RULES;
    }
  }

  return io::FileFormat::UNKNOWN;
}

io::FileFormat RoutingJob::getFileFormat(const std::string& path) {
  std::string filename = jrt::toLowerCase(path);
  std::vector<std::string> parts = jrt::split(filename, '.');
  if (parts.size() > 1) {
    std::string extension = jrt::toLowerCase(parts[parts.size() - 1]);
    if (extension == DSN_FILE_EXTENSION) return io::FileFormat::DSN;
    if (extension == BINARY_FILE_EXTENSION) return io::FileFormat::FRB;
    if (extension == "ses") return io::FileFormat::SES;
    if (extension == RULES_FILE_EXTENSION) return io::FileFormat::RULES;
    if (extension == "scr") return io::FileFormat::SCR;
    if (extension == "json") return io::FileFormat::KICAD_DESIGN_JSON;
    return io::FileFormat::UNKNOWN;
  }

  return io::FileFormat::UNKNOWN;
}

std::optional<int64_t> RoutingJob::getDuration() {
  if (!startedAt.has_value()) {
    return std::nullopt;
  }
  if (finishedAt.has_value()) {
    return *finishedAt - *startedAt;
  }
  return jrt::System::currentTimeMillis() - *startedAt;
}

bool RoutingJob::setInput(jrt::Array<int8_t>* inputFileContent) {
  this->input = new BoardFileDetails();
  this->input->addUpdatedEventListener(BoardFileDetailsUpdatedEventListener::of(
      [this](BoardFileDetailsUpdatedEvent*) { this->fireInputUpdatedEvent(); }));
  return this->tryToSetInput(inputFileContent);
}

void RoutingJob::setInput(const std::string& inputFilePath) { setInputFromFile(inputFilePath); }

bool RoutingJob::setRules(jrt::Array<int8_t>* rulesFileContent) {
  this->rules = new BoardFileDetails();
  this->rules->format = io::FileFormat::RULES;
  this->rules->setData(rulesFileContent);
  return true;
}

void RoutingJob::setRules(const std::string& rulesFilePath) {
  if (fileExists(rulesFilePath)) {
    jrt::Array<int8_t>* data = readAllBytes(rulesFilePath);
    this->rules = new BoardFileDetails();
    this->rules->format = io::FileFormat::RULES;
    this->rules->setFilename(fileName(rulesFilePath));
    this->rules->setData(data);
  }
}

bool RoutingJob::setInitialSession(jrt::Array<int8_t>* sessionFileContent,
                                   const std::optional<std::string>& filename) {
  this->initialSession = new BoardFileDetails();
  this->initialSession->setFilename(filename);
  this->initialSession->format = getFileFormat(sessionFileContent);
  if (this->initialSession->format == io::FileFormat::UNKNOWN && filename.has_value()) {
    this->initialSession->format = getFileFormat(*filename);
  }
  this->initialSession->setData(sessionFileContent);
  return true;
}

void RoutingJob::setInitialSession(const std::string& sessionFilePath) {
  if (fileExists(sessionFilePath)) {
    setInitialSession(readAllBytes(sessionFilePath), fileName(sessionFilePath));
  }
}

std::string RoutingJob::getRulesFile() {
  return changeFileExtension(this->output->getAbsolutePath(), RULES_FILE_EXTENSION);
}

void RoutingJob::setDummyInputFile(const std::optional<std::string>& filename) {
  this->input = new BoardFileDetails();

  this->output = new BoardFileDetails();

  if (filename.has_value() && jrt::endsWith(jrt::toLowerCase(*filename), DSN_FILE_EXTENSION)) {
    this->input->format = io::FileFormat::DSN;
    this->input->setFilename(filename);
  }
}

bool RoutingJob::tryToSetInput(jrt::Array<int8_t>* fileContent) {
  if (fileContent == nullptr) {
    return false;
  }

  this->input->format = getFileFormat(fileContent);

  if (this->input->format != io::FileFormat::UNKNOWN) {
    this->input->setData(fileContent);
    fireInputUpdatedEvent();
    return true;
  }

  return false;
}

std::string RoutingJob::changeFileExtension(const std::string& filename,
                                            const std::string& newFileExtension) {
  std::filesystem::path filePath(filename);

  // Get the filename and split it into parts
  std::error_code ec;
  std::string originalFullPathWithoutFilename =
      std::filesystem::absolute(filePath.parent_path(), ec).string();
  std::string originalFilename = filePath.filename().string();
  std::vector<std::string> nameParts = jrt::split(originalFilename, '.');
  if (nameParts.size() > 1) {
    std::string extension = jrt::toLowerCase(nameParts[nameParts.size() - 1]);
    if (extension == newFileExtension) {
      return filePath.string();
    }
    std::string newFilename =
        jrt::substring(originalFilename, 0,
                       static_cast<int32_t>(originalFilename.size()) -
                           static_cast<int32_t>(extension.size()) - 1) +
        "." + newFileExtension;

    return (std::filesystem::path(originalFullPathWithoutFilename) / newFilename).string();
  }

  return (std::filesystem::path(originalFullPathWithoutFilename) /
          (originalFilename + "." + newFileExtension))
      .string();
}

bool RoutingJob::tryToSetOutputFile(const std::optional<std::string>& outputFile) {
  if (!outputFile.has_value()) {
    return false;
  }

  io::FileFormat ff = getFileFormat(*outputFile);

  if ((ff == io::FileFormat::DSN) || (ff == io::FileFormat::FRB) || (ff == io::FileFormat::SES) ||
      (ff == io::FileFormat::SCR) || (ff == io::FileFormat::KICAD_DESIGN_JSON)) {
    this->output = new BoardFileDetails(*outputFile);
    this->output->addUpdatedEventListener(BoardFileDetailsUpdatedEventListener::of(
        [this](BoardFileDetailsUpdatedEvent*) { this->fireInputUpdatedEvent(); }));
    this->output->format =
        ff == io::FileFormat::KICAD_DESIGN_JSON ? io::FileFormat::KICAD_SESSION_JSON : ff;
    fireOutputUpdatedEvent();
    return true;
  } else {
    return false;
  }
}

int32_t RoutingJob::compareTo(RoutingJob* o) {
  if (static_cast<int32_t>(this->priority) < static_cast<int32_t>(o->priority)) {
    return -1;
  } else if (static_cast<int32_t>(this->priority) > static_cast<int32_t>(o->priority)) {
    return 1;
  } else {
    return 0;
  }
}

void RoutingJob::setInputFromFile(const std::string& inputFile) {
  // Read the file contents into a byte array and initialize the RoutingJob object with it
  jrt::Array<int8_t>* content = readAllBytes(inputFile);

  setInput(content);
  input->setFilename(absolutePath(inputFile));
  if (input->format == io::FileFormat::UNKNOWN) {
    // As a fallback method, set the file format based on its extension
    input->format = getFileFormat(input->getAbsolutePath());
  }

  if (this->input->format == io::FileFormat::FRB) {
    this->output = new BoardFileDetails();
    this->output->addUpdatedEventListener(BoardFileDetailsUpdatedEventListener::of(
        [this](BoardFileDetailsUpdatedEvent*) { this->fireOutputUpdatedEvent(); }));
    this->output->setFilename(changeFileExtension(input->getAbsolutePath(), BINARY_FILE_EXTENSION));
  }

  if (this->input->format == io::FileFormat::DSN) {
    this->output = new BoardFileDetails();
    this->output->addUpdatedEventListener(BoardFileDetailsUpdatedEventListener::of(
        [this](BoardFileDetailsUpdatedEvent*) { this->fireOutputUpdatedEvent(); }));
    this->output->setFilename(changeFileExtension(input->getAbsolutePath(), SES_FILE_EXTENSION));
  }

  if (this->input->format == io::FileFormat::KICAD_DESIGN_JSON) {
    this->output = new BoardFileDetails();
    this->output->addUpdatedEventListener(BoardFileDetailsUpdatedEventListener::of(
        [this](BoardFileDetailsUpdatedEvent*) { this->fireOutputUpdatedEvent(); }));
    this->output->setFilename(changeFileExtension(input->getAbsolutePath(), "json"));
  }

  if (this->input->format != io::FileFormat::UNKNOWN) {
    this->name = input->getFilenameWithoutExtension();
  }

  fireInputUpdatedEvent();
}

bool RoutingJob::setSettings(RouterSettings* settings) {
  // Update the router settings that are defined in the settings parameter. All
  // other settings should remain the same.
  bool wereSettingsChanged = this->routerSettings->applyNewValuesFrom(settings) > 0;
  fireSettingsUpdatedEvent();

  return wereSettingsChanged;
}

void RoutingJob::addSettingsUpdatedEventListener(RoutingJobUpdatedEventListener* listener) {
  settingsUpdatedEventListeners->add(listener);
}

void RoutingJob::fireSettingsUpdatedEvent() {
  auto* event = new RoutingJobUpdatedEvent(this, this);
  for (RoutingJobUpdatedEventListener* listener : *settingsUpdatedEventListeners) {
    listener->onRoutingJobUpdated(event);
  }
}

void RoutingJob::addInputUpdatedEventListener(RoutingJobUpdatedEventListener* listener) {
  inputUpdatedEventListeners->add(listener);
}

void RoutingJob::fireInputUpdatedEvent() {
  auto* event = new RoutingJobUpdatedEvent(this, this);
  for (RoutingJobUpdatedEventListener* listener : *inputUpdatedEventListeners) {
    listener->onRoutingJobUpdated(event);
  }
}

void RoutingJob::addOutputUpdatedEventListener(RoutingJobUpdatedEventListener* listener) {
  outputUpdatedEventListeners->add(listener);
}

void RoutingJob::fireOutputUpdatedEvent() {
  auto* event = new RoutingJobUpdatedEvent(this, this);
  for (RoutingJobUpdatedEventListener* listener : *outputUpdatedEventListeners) {
    listener->onRoutingJobUpdated(event);
  }
}

void RoutingJob::addLogEntryAddedEventListener(RoutingJobLogEntryAddedEventListener* listener) {
  logEntryAddedEventListeners->add(listener);
}

void RoutingJob::removeLogEntryAddedEventListener(
    RoutingJobLogEntryAddedEventListener* listener) {
  logEntryAddedEventListeners->remove(listener);
}

void RoutingJob::fireLogEntryAddedEvent(LogEntry* logEntry) {
  auto* event = new RoutingJobLogEntryAddedEvent(this, this, logEntry);
  for (RoutingJobLogEntryAddedEventListener* listener : *logEntryAddedEventListeners) {
    listener->onLogEntryAdded(event);
  }
}

void RoutingJob::logInfo(const std::string& message) {
  LogEntry* logEntry = FRLogger::info("[" + this->shortName + "] " + message, this->id);
  fireLogEntryAddedEvent(logEntry);
}

void RoutingJob::logWarning(const std::string& message) {
  LogEntry* logEntry = FRLogger::warn("[" + this->shortName + "] " + message, this->id);
  fireLogEntryAddedEvent(logEntry);
}

void RoutingJob::logError(const std::string& message, const std::exception* ex) {
  LogEntry* logEntry = FRLogger::error("[" + this->shortName + "] " + message, this->id, ex);
  fireLogEntryAddedEvent(logEntry);
}

void RoutingJob::logDebug(const std::string& message) {
  LogEntry* logEntry = FRLogger::debug("[" + this->shortName + "] " + message, this->id);
  fireLogEntryAddedEvent(logEntry);
}

std::optional<std::string> RoutingJob::getDetectedHost() {
  if (!isBlank(hostCad)) {
    return !isBlank(hostVersion) ? *hostCad + "/" + *hostVersion : *hostCad;
  }
  if (board != nullptr && board->communication != nullptr &&
      board->communication->specctraParserInfo != nullptr) {
    std::optional<std::string> cad = board->communication->specctraParserInfo->hostCad;
    std::optional<std::string> ver = board->communication->specctraParserInfo->hostVersion;
    if (!isBlank(cad)) {
      return !isBlank(ver) ? *cad + "/" + *ver : *cad;
    }
  }
  return std::nullopt;
}

}  // namespace fr
