#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/SettingsSource.h"

namespace fr {

/**
 * Provides router settings from API callers (in the headless engine: HeadlessRoutingRequest
 * settings). Only the settings specified via API are non-null. Highest priority (70).
 */
class ApiSettings : public virtual SettingsSource {
 public:
  /** @param settings router settings from the API (only specified fields non-null); null = empty. */
  explicit ApiSettings(RouterSettings* settings);

  RouterSettings* getSettings() override { return settings; }
  std::string getSourceName() override { return "API Settings"; }
  int32_t getPriority() override { return PRIORITY; }

 private:
  static constexpr int32_t PRIORITY = 70;
  RouterSettings* settings;
};

}  // namespace fr
