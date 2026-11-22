#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing parser scopes from dsn-files. */
class Parser : public ScopeKeyword {
 public:
  /** Creates a new instance of Parser. */
  Parser() : ScopeKeyword("parser") {}

  /**
   * Writes parser settings, optionally using the reduced session-file representation.
   * Throws jrt::IOException.
   */
  static void writeScope(fr::IndentFileWriter* file,
                         fr::Communication_SpecctraParserInfo* parserInfo,
                         fr::IdentifierType* identifierType, bool reduced);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  static fr::Communication_SpecctraParserInfo_WriteResolution* readWriteSolution(
      ReadScopeParameter* scopeParameter);

  static jrt::Array<std::string>* readConstant(ReadScopeParameter* scopeParameter);

  /** Returns the quote string, or null (nullopt) on error. */
  static std::optional<std::string> readQuoteChar(IJFlexScanner* scanner);
};

}  // namespace fr::dsn
