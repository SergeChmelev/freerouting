#pragma once

#include <istream>
#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/SettingsSource.h"

namespace fr {

/**
 * Extracts router settings from RULES files (RulesReader::readRouterSettings). Only the settings
 * present in the RULES file are non-null. Priority 40. Read failures are logged and yield an empty
 * RouterSettings.
 */
class RulesFileSettings : public virtual SettingsSource {
 public:
  /**
   * Creates a RulesFileSettings source from an input stream.
   *
   * @param in input stream of the RULES file
   * @param fileName name of the RULES file (for logging)
   */
  RulesFileSettings(std::istream& in, const std::string& fileName);

  /**
   * Creates a RulesFileSettings source from a RULES file path; a missing file (or null) yields an
   * empty RouterSettings.
   */
  // PORT NOTE: the Java File / Path / String constructors are merged into this one. Only the
  // source name differs in Java (File/Path use the bare file name, String the given string); the
  // port uses the given string.
  explicit RulesFileSettings(const std::optional<std::string>& fileName);

  RouterSettings* getSettings() override { return settings; }
  std::string getSourceName() override {
    return "RULES file: " + (fileName ? *fileName : std::string("null"));
  }
  int32_t getPriority() override { return PRIORITY; }

 private:
  RouterSettings* loadFromFile(const std::string& file);
  RouterSettings* loadSettings(std::istream& in);

  static constexpr int32_t PRIORITY = 40;
  RouterSettings* settings = nullptr;
  std::optional<std::string> fileName;
};

}  // namespace fr
