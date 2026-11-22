#include "io/specctra/parser/ScopeKeyword.h"

#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

bool ScopeKeyword::skipScope(IJFlexScanner* scanner) {
  int32_t openBrackedCount = 1;
  while (openBrackedCount > 0) {
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    jrt::Object* currentToken;
    try {
      currentToken = scanner->nextToken();
    } catch (const jrt::Exception& e) {
      FRLogger::error("ScopeKeyword.skip_scope: Error while scanning file", &e);
      return false;
    }
    if (currentToken == nullptr) {
      return false;  // end of file
    }
    if (currentToken == Keyword::OPEN_BRACKET()) {
      ++openBrackedCount;
    } else if (currentToken == Keyword::CLOSED_BRACKET()) {
      --openBrackedCount;
    }
  }
  scanner->yybegin(SpecctraDsnStreamReader::YYINITIAL);
  return true;
}

bool ScopeKeyword::readScope(ReadScopeParameter* scopeParameter) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("ScopeKeyword.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      // end of file
      return true;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }

    if (prevToken == OPEN_BRACKET()) {
      ScopeKeyword* nextScope;
      // a new scope is expected
      if (auto* keyword = dynamic_cast<ScopeKeyword*>(nextToken)) {
        // read the next scope, which is the "structure" part of the DSN file
        nextScope = keyword;
        if (!nextScope->readScope(scopeParameter)) {
          return false;
        }
      } else {
        // skip unknown scope
        skipScope(scopeParameter->scanner);
      }
    }
  }
  return true;
}

}  // namespace fr::dsn
