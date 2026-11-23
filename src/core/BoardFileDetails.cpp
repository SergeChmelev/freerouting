#include "core/BoardFileDetails.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/RoutingJob.h"
#include "core/events/BoardFileDetailsUpdatedEvent.h"
#include "core/scoring/BoardStatistics.h"
#include "io/FileFormat.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {

// java.util.zip.CRC32 (IEEE 802.3 polynomial, reflected).
class Crc32 {
 public:
  void update(const int8_t* data, int32_t len) {
    static const uint32_t* table = jrt::onHeap([&] { return makeTable(); });
    for (int32_t i = 0; i < len; i++) {
      crc_ = table[(crc_ ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (crc_ >> 8);
    }
  }
  int64_t getValue() { return static_cast<int64_t>(crc_ ^ 0xFFFFFFFFu); }

 private:
  static const uint32_t* makeTable() {
    static uint32_t t[256];
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) {
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[n] = c;
    }
    return t;
  }
  uint32_t crc_ = 0xFFFFFFFFu;
};

std::string jsonEscape(const std::string& s) {
  std::string r;
  for (char c : s) {
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          r += jrt::format("\\u%04x", static_cast<unsigned>(c));
        } else {
          r += c;
        }
    }
  }
  return r;
}

}  // namespace

BoardFileDetails::BoardFileDetails() {
  this->format = io::FileFormat::UNKNOWN;
  this->statistics = new (jrt::kBoard) BoardStatistics();
}

BoardFileDetails::BoardFileDetails(const std::string& file) : BoardFileDetails() {
  std::error_code ec;
  this->setFilename(std::filesystem::absolute(file, ec).string());

  std::ifstream fis(file, std::ios::binary);
  if (fis) {
    std::vector<char> bytes((std::istreambuf_iterator<char>(fis)),
                            std::istreambuf_iterator<char>());
    if (!fis.bad()) {
      auto* data = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(bytes.size()));
      for (std::size_t i = 0; i < bytes.size(); i++) {
        (*data)[static_cast<int32_t>(i)] = static_cast<int8_t>(bytes[i]);
      }
      this->setData(data);
    }
  }
  // Ignore I/O failures and continue with the default values
}

BoardFileDetails::BoardFileDetails(BasicBoard* board) : BoardFileDetails() {
  this->statistics = new (jrt::kBoard) BoardStatistics(board);
}

int64_t BoardFileDetails::calculateCrc32(std::istream& inputStream) {
  Crc32 crc;
  char buffer[8192];
  while (inputStream) {
    inputStream.read(buffer, sizeof buffer);
    std::streamsize bytesRead = inputStream.gcount();
    if (bytesRead <= 0) break;
    crc.update(reinterpret_cast<const int8_t*>(buffer), static_cast<int32_t>(bytesRead));
  }
  if (inputStream.bad()) {
    jrt::IOException e("I/O error while reading the stream");
    FRLogger::error(e.getMessage(), &e);
  }
  return crc.getValue();
}

void BoardFileDetails::saveAs(const std::string& filename) {
  std::ofstream writer(filename, std::ios::binary);
  if (!writer) {
    throw jrt::IOException(filename);
  }
  writer << this->toString();
  if (!writer) {
    throw jrt::IOException(filename);
  }
}

std::string BoardFileDetails::getAbsolutePath() {
  return (std::filesystem::path(this->directoryPath) / this->filename).string();
}

jrt::Array<int8_t>* BoardFileDetails::getData() { return this->dataBytes; }

void BoardFileDetails::setData(jrt::Array<int8_t>* data) {
  this->size = data->length;
  Crc32 crc;
  crc.update(data->data(), data->length);
  this->crc32 = crc.getValue();

  // read the file contents to determine the file format
  io::FileFormat detectedFormat = RoutingJob::getFileFormat(data);
  // JSON sniffing identifies the shared board representation, not whether it is a design or
  // routing session. Keep an explicitly selected session format during output publication.
  bool isKnownJsonSession = this->format == io::FileFormat::KICAD_SESSION_JSON &&
                            detectedFormat == io::FileFormat::KICAD_DESIGN_JSON;
  if (detectedFormat != io::FileFormat::UNKNOWN && !isKnownJsonSession) {
    this->format = detectedFormat;
  } else if (this->format == io::FileFormat::UNKNOWN && !this->filename.empty()) {
    this->format = RoutingJob::getFileFormat(this->filename);
  }

  // set the statistical data based on the file content
  this->statistics = new (jrt::kOp) BoardStatistics(data, this->format);

  // Publish the complete snapshot after its metadata is ready for polling readers.
  this->dataBytes = data;
  fireUpdatedEvent();
}

void BoardFileDetails::invalidateData() {
  this->size = 0;
  this->crc32 = 0;
  this->statistics = new (jrt::kBoard) BoardStatistics();
  this->dataBytes = new (jrt::kBoard) jrt::Array<int8_t>(0);
}

std::string BoardFileDetails::toString() {
  // PORT NOTE: GsonProvider.GSON.toJson(this) replaced by a simplified hand-written JSON.
  std::string s = "{\n";
  s += "  \"size\": " + jrt::str(size) + ",\n";
  s += "  \"crc32\": " + jrt::str(crc32) + ",\n";
  s += "  \"format\": \"" + io::FileFormat_name(format) + "\",\n";
  if (statistics != nullptr) {
    s += "  \"statistics\": " + statistics->toString() + ",\n";
  }
  s += "  \"filename\": \"" + jsonEscape(filename) + "\",\n";
  s += "  \"path\": \"" + jsonEscape(directoryPath) + "\"\n";
  s += "}";
  return s;
}

std::optional<std::string> BoardFileDetails::getFile() {
  if (!this->filename.empty()) {
    return (std::filesystem::path(this->directoryPath) / this->filename).string();
  }
  return std::nullopt;
}

void BoardFileDetails::setFilename(const std::optional<std::string>& filenameArg) {
  if (!filenameArg.has_value()) {
    this->directoryPath = "";
    this->filename = "";
    return;
  }
  const std::string& filename = *filenameArg;

  // PORT NOTE: File.separatorChar is '/' on the target platform (Linux).
  std::string normalized = jrt::replace(filename, "\\", "/");
  std::error_code ec;
  std::filesystem::path path = std::filesystem::absolute(normalized, ec);

  if (jrt::contains(filename, "/") || jrt::contains(filename, "\\")) {
    // separate the filename into its absolute path and its filename only
    std::filesystem::path parent = path.parent_path();
    this->directoryPath = parent.string();
    // replace the redundant "\.\" with a simple "\"
    this->directoryPath = jrt::replace(this->directoryPath, "\\.\\", "\\");
    // remove the "/", "\" from the end of the directory path
    while (!this->directoryPath.empty() &&
           (this->directoryPath.back() == '/' || this->directoryPath.back() == '\\')) {
      this->directoryPath.pop_back();
    }
    // remove the "\." (backslash + any char) from the end of the directory path
    if (this->directoryPath.size() >= 2 &&
        this->directoryPath[this->directoryPath.size() - 2] == '\\' &&
        this->directoryPath.back() != '\n') {
      this->directoryPath.resize(this->directoryPath.size() - 2);
    }
  } else {
    this->directoryPath = "";
  }

  // set the filename only
  this->filename = path.filename().string();

  if (this->format == io::FileFormat::UNKNOWN) {
    // try to read the file contents to determine the file format
    this->format = RoutingJob::getFileFormat(this->filename);
  }

  // add the default file extension if it is missing
  if ((this->format != io::FileFormat::UNKNOWN) && (!jrt::contains(this->filename, "."))) {
    std::string extension;
    switch (this->format) {
      case io::FileFormat::SES: extension = "ses"; break;
      case io::FileFormat::DSN: extension = "dsn"; break;
      case io::FileFormat::FRB: extension = "frb"; break;
      case io::FileFormat::RULES: extension = "rules"; break;
      case io::FileFormat::SCR: extension = "scr"; break;
      default: extension = ""; break;
    }

    if (!extension.empty()) {
      this->filename = this->filename + "." + extension;
    }
  }

  fireUpdatedEvent();
}

std::string BoardFileDetails::getFilenameWithoutExtension() {
  if (jrt::contains(this->filename, ".")) {
    return jrt::substring(this->filename, 0,
                          static_cast<int32_t>(this->filename.rfind('.')));
  }
  return this->filename;
}

void BoardFileDetails::addUpdatedEventListener(BoardFileDetailsUpdatedEventListener* listener) {
  updatedEventListeners->add(listener);
}

void BoardFileDetails::fireUpdatedEvent() {
  auto* event = new (jrt::kBoard) BoardFileDetailsUpdatedEvent(this, this);
  for (BoardFileDetailsUpdatedEventListener* listener : *updatedEventListeners) {
    listener->onBoardFileDetailsUpdated(event);
  }
}

}  // namespace fr
