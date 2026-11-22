#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading resolution scopes from dsn-files. */
class Resolution : public ScopeKeyword {
 public:
  /** Creates a new instance of Resolution. */
  Resolution() : ScopeKeyword("resolution") {}

  /** Writes "(resolution <unit> <resolution>)". Throws jrt::IOException. */
  static void writeScope(fr::IndentFileWriter* file, fr::Communication* boardCommunication);

  bool readScope(ReadScopeParameter* scopeParameter) override;
};

}  // namespace fr::dsn
