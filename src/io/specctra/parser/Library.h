#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing library scopes from dsn-files. */
class Library : public ScopeKeyword {
 public:
  /** Creates a new instance of Library. */
  Library() : ScopeKeyword("library") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);

  /** Throws jrt::IOException. */
  static void writePadstackScope(WriteScopeParameter* scopeParameter, fr::Padstack* padstack);

  static bool readPadstackScope(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                fr::io::CoordinateTransform* coordinateTransform,
                                fr::Padstacks* boardPadstacks);

  /**
   * Reads a padstack scope. If kicadDsnUnit is not null, the file was written by KiCad in that
   * unit and KiCad round-rect pad approximations are corrected (see KiCadRoundRectPad).
   */
  static bool readPadstackScope(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                fr::io::CoordinateTransform* coordinateTransform,
                                fr::Padstacks* boardPadstacks,
                                std::optional<fr::Unit> kicadDsnUnit);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  static bool isKiCad(ReadScopeParameter* scopeParameter);

  void generateMissingKeepoutNames(const std::string& keepoutType,
                                   jrt::Collection<Shape_ReadAreaScopeResult*>* keepoutList);
};

}  // namespace fr::dsn
