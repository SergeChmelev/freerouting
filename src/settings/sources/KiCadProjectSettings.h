#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/SettingsSource.h"

namespace fr {

/**
 * Router settings taken from a KiCad project file ({@code .kicad_pro}): the larger of its minimum
 * track width and minimum connection width becomes {@code neck_width_um}, the narrowest trace the
 * router may use. It ranks above the DSN file, which does not carry that minimum, and below every
 * setting the user gives (CLI, environment, GUI, API), so an explicit {@code
 * --router.neck_width_um} still wins.
 */
class KiCadProjectSettings : public virtual SettingsSource {
 public:
  explicit KiCadProjectSettings(KiCadProject* project);

  RouterSettings* getSettings() override { return settings; }
  std::string getSourceName() override { return "KiCad project file: " + sourceName; }
  int32_t getPriority() override { return PRIORITY; }

 private:
  static constexpr int32_t PRIORITY = 25;
  RouterSettings* settings;
  std::string sourceName;
};

}  // namespace fr
