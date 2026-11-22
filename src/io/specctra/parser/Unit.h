#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading resolution scopes from dsn-files. */
class Unit : public ScopeKeyword {
 public:
  /** Creates a new instance of Unit. */
  Unit() : ScopeKeyword("unit") {}

  /** Writes "(unit <unit>)". Throws jrt::IOException. */
  static void writeScope(fr::IndentFileWriter* file, fr::Unit unit);

  bool readScope(ReadScopeParameter* scopeParameter) override { return false; }
};

}  // namespace fr::dsn
