#pragma once

#include <initializer_list>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/SettingsSource.h"  // complete type needed by the inline ArrayList

namespace fr {

/**
 * Central class responsible for merging router settings from multiple sources into a single,
 * resolved RouterSettings instance.
 *
 * <p>Sources are sorted by ascending priority (stable sort) and applied one on top of another. The
 * first source returning non-null settings (normally DefaultSettings, priority 0) is cloned as the
 * base; every later source is applied with RouterSettings::applyNewValuesFrom(), which copies a
 * field only when the source value is non-null. Finally the merged settings are validate()d.
 *
 * <p>All RouterSettings fields must therefore stay nullable with no default initializers; the
 * hardcoded defaults live exclusively in DefaultSettings::getSettings().
 *
 * <p>Priority order: Default (0) < JSON file (10) < DSN file (20) < KiCad project (25) < SES file
 * (30) < RULES file (40) < environment (55) < CLI (60) < GUI (65) < API (70).
 */
class SettingsMerger : public virtual jrt::Object {
 public:
  /** Creates a SettingsMerger with the specified settings sources (Java varargs). */
  SettingsMerger(std::initializer_list<SettingsSource*> sources);
  /** Creates a SettingsMerger with the specified list of settings sources. */
  explicit SettingsMerger(jrt::List<SettingsSource*>* sources);

  /** Adds or replaces settings sources in the merger (Java varargs). */
  void addOrReplaceSources(std::initializer_list<SettingsSource*> sources);
  /** PORT NOTE: convenience for the single-argument varargs call. */
  void addOrReplaceSources(SettingsSource* source);
  /**
   * Adds or replaces settings sources in the merger. An existing source is replaced when it has
   * the same class as the new source (or, in Java, a supertype of it); otherwise the new source is
   * appended.
   */
  // PORT NOTE: Java also replaces when the existing source's class is a supertype of the new one
  // (GuiSettingsSource -> WorkspaceSettings). Those GUI sources are not ported; the C++ check is
  // typeid equality only.
  void addOrReplaceSources(jrt::List<SettingsSource*>* newSources);

  /** Merges router settings from all sources according to their priority. */
  RouterSettings* merge();

  /** Java clone(): a new merger over the same source list. */
  virtual SettingsMerger* clone();

 private:
  jrt::List<SettingsSource*>* sources = new jrt::ArrayList<SettingsSource*>();
};

}  // namespace fr
