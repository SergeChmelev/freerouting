#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::io {

/**
 * Normalizes KiCad net-class naming between Specctra DSN export and KiCad JSON/IPC export.
 *
 * KiCad renames its {@code Default} net class to {@code kicad_default} in Specctra DSN files to
 * avoid colliding with Freerouting's reserved internal {@code default} class.
 */
class KiCadNetClassNames final : public virtual jrt::Object {
 public:
  static inline const std::string KICAD_DSN_DEFAULT = "kicad_default";

  /**
   * Returns whether a name identifies KiCad's default net class.
   * PORT NOTE: Java's null check is represented by std::nullopt.
   */
  static bool isKiCadDefaultNetClassName(const std::optional<std::string>& name) {
    if (!name.has_value() || name->empty()) {
      return false;
    }
    return jrt::equalsIgnoreCase("default", *name) ||
           jrt::equalsIgnoreCase(KICAD_DSN_DEFAULT, *name);
  }

  /** Resolves a KiCad net-class name against the board rules; null when no class exists. */
  static fr::NetClass* resolveNetClass(fr::BoardRules* rules, const std::string& name);

 private:
  KiCadNetClassNames() = default;
};

}  // namespace fr::io
