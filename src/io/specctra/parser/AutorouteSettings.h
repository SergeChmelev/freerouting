#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Reads and writes the freerouting-specific (autoroute_settings ...) scope. */
class AutorouteSettings final : public virtual jrt::Object {
 public:
  /** Reads an autoroute_settings scope; null on error. */
  static fr::RouterSettings* readScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  static fr::RouterSettings* readLayerRule(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                           fr::RouterSettings* settings);

  /** Throws jrt::IOException. */
  static void writeScope(fr::IndentFileWriter* file, fr::RouterSettings* settings,
                         fr::LayerStructure* layerStructure, fr::IdentifierType* identifierType);

 private:
  AutorouteSettings() = default;
};

}  // namespace fr::dsn
