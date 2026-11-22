#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for writing placement scopes from dsn-files. */
class Placement : public ScopeKeyword {
 public:
  /** Creates a new instance of Placement. */
  Placement() : ScopeKeyword("placement") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);
};

}  // namespace fr::dsn
