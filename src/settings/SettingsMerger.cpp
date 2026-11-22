#include "settings/SettingsMerger.h"

#include <typeinfo>

#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace fr {

SettingsMerger::SettingsMerger(std::initializer_list<SettingsSource*> sources) {
  this->addOrReplaceSources(sources);
}

SettingsMerger::SettingsMerger(jrt::List<SettingsSource*>* sources) {
  this->addOrReplaceSources(sources);
}

void SettingsMerger::addOrReplaceSources(std::initializer_list<SettingsSource*> sources) {
  addOrReplaceSources(static_cast<jrt::List<SettingsSource*>*>(
      new jrt::ArrayList<SettingsSource*>(sources)));
}

void SettingsMerger::addOrReplaceSources(SettingsSource* source) {
  addOrReplaceSources({source});
}

void SettingsMerger::addOrReplaceSources(jrt::List<SettingsSource*>* newSources) {
  for (SettingsSource* newSource : *newSources) {
    bool replaced = false;
    for (int32_t i = 0; i < sources->size(); i++) {
      SettingsSource* existingSource = sources->get(i);
      // Replace if same class (see header PORT NOTE about the supertype check).
      if (typeid(*existingSource) == typeid(*newSource)) {
        sources->set(i, newSource);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      sources->add(newSource);
    }
  }
}

RouterSettings* SettingsMerger::merge() {
  if (sources == nullptr || sources->isEmpty()) {
    FRLogger::warn("No settings sources provided, using defaults");
    return new RouterSettings();
  }

  // Sort sources by priority (lowest to highest), stable like Java's List.sort
  auto* sortedSources =
      new jrt::ArrayList<SettingsSource*>(static_cast<jrt::Collection<SettingsSource*>*>(sources));
  sortedSources->sort([](SettingsSource* a, SettingsSource* b) {
    return jrt::Integer::compare(a->getPriority(), b->getPriority());
  });

  FRLogger::debug("Merging settings from " + jrt::str(sortedSources->size()) + " sources:");

  // The first non-null source (usually DefaultSettings) provides the base.
  RouterSettings* mergedSettings = nullptr;
  for (SettingsSource* source : *sortedSources) {
    RouterSettings* sourceSettings = source->getSettings();
    if (sourceSettings == nullptr) {
      FRLogger::debug("  - " + source->getSourceName() + " (priority " +
                      jrt::str(source->getPriority()) + "): no settings");
      continue;
    }

    if (mergedSettings == nullptr) {
      // First source provides the base settings
      mergedSettings = sourceSettings->clone();
      FRLogger::debug("  - " + source->getSourceName() + " (priority " +
                      jrt::str(source->getPriority()) + "): base settings");
    } else {
      // Apply this source's settings on top of the merged settings
      int32_t fieldsChanged = mergedSettings->applyNewValuesFrom(sourceSettings);
      FRLogger::debug("  - " + source->getSourceName() + " (priority " +
                      jrt::str(source->getPriority()) + "): " + jrt::str(fieldsChanged) +
                      " fields changed");
    }
  }

  if (mergedSettings == nullptr) {
    FRLogger::warn("No valid settings found in any source, using defaults");
    mergedSettings = new RouterSettings();
  }

  // Validate the merged settings
  mergedSettings->validate();

  FRLogger::debug("Settings merged successfully from " + jrt::str(sortedSources->size()) +
                  " source(s)");

  return mergedSettings;
}

SettingsMerger* SettingsMerger::clone() { return new SettingsMerger(this->sources); }

}  // namespace fr
