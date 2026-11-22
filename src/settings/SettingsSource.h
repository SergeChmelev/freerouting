#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Base interface for all settings sources. Each source provides router settings from a specific
 * origin (defaults, DSN file, RULES file, API, ...).
 *
 * <p>Settings sources use nullable fields to indicate which settings they provide. A null value
 * means the source does not specify that particular setting. SettingsMerger applies sources in
 * ascending priority order (lower first, higher overrides).
 */
class SettingsSource : public virtual jrt::Object {
 public:
  /**
   * Gets the router settings provided by this source. Only the settings that this source explicitly
   * provides should be non-null.
   */
  virtual RouterSettings* getSettings() = 0;

  /** Gets a human-readable name for this settings source (logging only). */
  virtual std::string getSourceName() = 0;

  /**
   * Gets the priority of this settings source: 0 = defaults, 10 = JSON file, 20 = DSN file,
   * 25 = KiCad project, 30 = SES file, 40 = RULES file, 55 = environment, 60 = CLI, 65 = GUI,
   * 70 = API.
   */
  virtual int32_t getPriority() = 0;
};

}  // namespace fr
