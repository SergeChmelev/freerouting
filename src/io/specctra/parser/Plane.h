#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing plane scopes from dsn-files. */
class Plane : public ScopeKeyword {
 public:
  /** Creates a new instance of Plane. */
  Plane() : ScopeKeyword("plane") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter, fr::ConductionArea* conduction);

  bool readScope(ReadScopeParameter* scopeParameter) override;
};

}  // namespace fr::dsn
