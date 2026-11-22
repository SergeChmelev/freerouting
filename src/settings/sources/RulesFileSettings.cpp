#include "settings/sources/RulesFileSettings.h"

#include <filesystem>
#include <fstream>

#include "io/specctra/RulesReader.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace fr {

RulesFileSettings::RulesFileSettings(std::istream& in, const std::string& fileName) {
  this->fileName = fileName;
  this->settings = loadSettings(in);
}

RulesFileSettings::RulesFileSettings(const std::optional<std::string>& fileName) {
  this->fileName = fileName;
  if (fileName.has_value()) {
    std::error_code ec;
    if (std::filesystem::exists(*fileName, ec)) {
      this->settings = loadFromFile(*fileName);
    } else {
      this->settings = new (jrt::kBoard) RouterSettings();
    }
  } else {
    this->settings = new (jrt::kBoard) RouterSettings();
  }
}

RouterSettings* RulesFileSettings::loadFromFile(const std::string& file) {
  // PORT NOTE: File.getName() -> the last path component.
  std::string name = std::filesystem::path(file).filename().string();
  try {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      throw jrt::IOException(file + " (cannot open)");
    }
    return loadSettings(in);
  } catch (const jrt::Exception& e) {
    FRLogger::warn("Failed to load settings from RULES file: " + name + ": " + e.getMessage());
    return new (jrt::kBoard) RouterSettings();
  }
}

RouterSettings* RulesFileSettings::loadSettings(std::istream& in) {
  try {
    RouterSettings* extracted = dsn::RulesReader::readRouterSettings(in);
    if (extracted != nullptr) {
      FRLogger::debug("Loaded router settings from RULES file: " + jrt::str(fileName));
      return extracted;
    }
    return new (jrt::kBoard) RouterSettings();
  } catch (const jrt::Exception& e) {
    FRLogger::warn("Failed to load settings from RULES file: " + jrt::str(fileName) + ": " +
                   e.getMessage());
    return new (jrt::kBoard) RouterSettings();
  }
}

}  // namespace fr
