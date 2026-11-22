#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading place_control scopes from dsn-files. */
class PlaceControl : public ScopeKeyword {
 public:
  /** Creates a new instance of PlaceControl. */
  PlaceControl() : ScopeKeyword("place_control") {}

  /** Returns true, if rotate_first is read, else false. */
  static bool readFlipStyleRotateFirst(IJFlexScanner* scanner);

  /** Reads the flip_style. */
  bool readScope(ReadScopeParameter* scopeParameter) override;
};

}  // namespace fr::dsn
