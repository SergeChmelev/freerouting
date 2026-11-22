#pragma once

#include "fwd.h"
#include "io/specctra/parser/Keyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Keywords defining a scope object. */
class ScopeKeyword : public Keyword {
 public:
  explicit ScopeKeyword(const std::string& name) : Keyword(name) {}

  /**
   * Skips the current scope while reading a dsn file. Returns false, if no legal scope was found.
   */
  static bool skipScope(IJFlexScanner* scanner);

  /** Reads the next scope of this keyword from dsn file. */
  virtual bool readScope(ReadScopeParameter* scopeParameter);
};

}  // namespace fr::dsn
