#include "io/specctra/parser/Parser.h"

#include "board/state/Communication.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

fr::Communication_SpecctraParserInfo_WriteResolution* Parser::readWriteSolution(
    ReadScopeParameter* scopeParameter) {
  try {
    jrt::Object* nextToken = scopeParameter->scanner->nextToken();
    auto* resolutionString = jrt::fast_cast<JString>(nextToken);
    if (resolutionString == nullptr) {
      FRLogger::warn("Parser.read_write_solution: string expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    nextToken = scopeParameter->scanner->nextToken();
    auto* resolutionInteger = jrt::fast_cast<JInteger>(nextToken);
    if (resolutionInteger == nullptr) {
      FRLogger::warn("Parser.read_write_solution: integer expected expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    int32_t resolutionValue = resolutionInteger->value;
    nextToken = scopeParameter->scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Parser.read_write_solution: closing_bracket expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    return new fr::Communication_SpecctraParserInfo_WriteResolution(resolutionString->value,
                                                                    resolutionValue);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Parser.read_write_solution: IO error scanning file", &e);
    return nullptr;
  }
}

jrt::Array<std::string>* Parser::readConstant(ReadScopeParameter* scopeParameter) {
  try {
    auto* result = new jrt::Array<std::string>(2);
    scopeParameter->scanner->yybegin(SpecctraDsnStreamReader::NAME);
    jrt::Object* nextToken = scopeParameter->scanner->nextToken();
    auto* firstString = jrt::fast_cast<JString>(nextToken);
    if (firstString == nullptr) {
      FRLogger::warn("Parser.read_constant: string expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    (*result)[0] = firstString->value;
    scopeParameter->scanner->yybegin(SpecctraDsnStreamReader::NAME);
    nextToken = scopeParameter->scanner->nextToken();
    auto* secondString = jrt::fast_cast<JString>(nextToken);
    if (secondString == nullptr) {
      FRLogger::warn("Parser.read_constant: string expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    (*result)[1] = secondString->value;
    nextToken = scopeParameter->scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Parser.read_constant: closing_bracket expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Parser.read_constant: IO error scanning file", &e);
    return nullptr;
  }
}

void Parser::writeScope(fr::IndentFileWriter* file,
                        fr::Communication_SpecctraParserInfo* parserInfo,
                        fr::IdentifierType* identifierType, bool reduced) {
  file->startScope();
  file->write("parser");
  if (!reduced) {
    file->newLine();
    file->write("(string_quote ");
    file->write(parserInfo->stringQuote);
    file->write(")");
    file->newLine();
    file->write("(space_in_quoted_tokens on)");
  }
  if (parserInfo->hostCad.has_value()) {
    file->newLine();
    file->write("(host_cad ");
    identifierType->write(*parserInfo->hostCad, file);
    file->write(")");
  }
  if (parserInfo->hostVersion.has_value()) {
    file->newLine();
    file->write("(host_version ");
    identifierType->write(*parserInfo->hostVersion, file);
    file->write(")");
  }
  if (parserInfo->constants != nullptr) {
    for (jrt::Array<std::string>* currentConstant : *parserInfo->constants) {
      file->newLine();
      file->write("(constant ");
      for (int32_t i = 0; i < currentConstant->length; i++) {
        identifierType->write((*currentConstant)[i], file);
        file->write(" ");
      }
      file->write(")");
    }
  }
  if (parserInfo->writeResolution != nullptr) {
    file->newLine();
    file->write("(write_resolution ");
    file->write(jrt::substring(parserInfo->writeResolution->charName, 0, 1));
    file->write(" ");
    int32_t positiveInt = parserInfo->writeResolution->positiveInt;
    file->write(jrt::str(positiveInt));
    file->write(")");
  }
  if (!reduced) {
    file->newLine();
    file->write("(generated_by_freerouting)");
  }
  file->endScope();
}

std::optional<std::string> Parser::readQuoteChar(IJFlexScanner* scanner) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    auto* result = jrt::fast_cast<JString>(nextToken);
    if (result == nullptr) {
      FRLogger::warn("Parser.read_quote_char: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return std::nullopt;
    }
    nextToken = scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Parser.read_quote_char: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return std::nullopt;
    }
    return result->value;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Parser.read_quote_char: IO error scanning file", &e);
    return std::nullopt;
  }
}

bool Parser::readScope(ReadScopeParameter* scopeParameter) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException&) {
      FRLogger::warn("Parser.read_scope: IO error scanning file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Parser.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    bool readOk = true;
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == STRING_QUOTE()) {
        std::optional<std::string> quoteChar = readQuoteChar(scopeParameter->scanner);
        if (!quoteChar.has_value()) {
          return false;
        }
        scopeParameter->stringQuote = *quoteChar;
      } else if (nextToken == HOST_CAD()) {
        scopeParameter->hostCad = DsnFile::readStringScope(scopeParameter->scanner);
      } else if (nextToken == HOST_VERSION()) {
        scopeParameter->hostVersion = DsnFile::readStringScope(scopeParameter->scanner);
      } else if (nextToken == CONSTANT()) {
        jrt::Array<std::string>* currentConstant = readConstant(scopeParameter);
        if (currentConstant != nullptr) {
          scopeParameter->constants->add(currentConstant);
        }
      } else if (nextToken == WRITE_RESOLUTION()) {
        scopeParameter->writeResolution = readWriteSolution(scopeParameter);
      } else if (nextToken == GENERATED_BY_FREEROUTING()) {
        scopeParameter->dsnFileGeneratedByHost = false;
        // skip the closing bracket
        skipScope(scopeParameter->scanner);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
    if (!readOk) {
      return false;
    }
  }
  return true;
}

}  // namespace fr::dsn
