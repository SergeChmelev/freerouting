#include "settings/sources/KiCadProjectSettings.h"

#include "io/kicad/KiCadProject.h"
#include "settings/RouterSettings.h"

namespace fr {

KiCadProjectSettings::KiCadProjectSettings(KiCadProject* project)
    : settings(new (jrt::kBoard) RouterSettings()), sourceName(project->sourceName()) {
  if (project->narrowestTraceWidthUm().has_value()) {
    this->settings->neckWidthUm = project->narrowestTraceWidthUm();
  }
}

}  // namespace fr
