#pragma once

#include <istream>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/SettingsSource.h"

namespace fr {

/**
 * Extracts router settings from DSN (Specctra Design) files. Only the settings present in the DSN
 * file are non-null. Priority 20.
 */
class DsnFileSettings : public virtual SettingsSource {
 public:
  /**
   * Creates a DsnFileSettings source from a DSN file stream: a lightweight metadata-only parse
   * (DsnReader::readMetadata) that stops after the (structure ...) scope. The settings of the
   * DSN's autoroute block are used (or an empty RouterSettings); the layer arrays are always
   * seeded from the DSN layer count (setLayerCount) when they are still empty.
   *
   * @param inputStream DSN file input stream
   * @param filename name of the DSN file (for logging)
   */
  DsnFileSettings(std::istream& inputStream, const std::string& filename);

  RouterSettings* getSettings() override { return settings; }
  std::string getSourceName() override { return "DSN file: " + filename; }
  int32_t getPriority() override { return PRIORITY; }

 private:
  static constexpr int32_t PRIORITY = 20;
  RouterSettings* settings;
  std::string filename;
};

}  // namespace fr
