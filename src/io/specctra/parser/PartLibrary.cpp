#include "io/specctra/parser/PartLibrary.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "core/library/BoardLibrary.h"
#include "core/library/LogicalPart.h"
#include "core/library/LogicalParts.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

void PartLibrary::writeScope(WriteScopeParameter* scopeParameter) {
  fr::LogicalParts* logicalParts = scopeParameter->board->library->logicalParts;
  if (logicalParts->count() <= 0) {
    return;
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("part_library");

  // write the logical part mappings

  for (int32_t i = 1; i <= logicalParts->count(); i++) {
    fr::LogicalPart* currentPart = logicalParts->get(i);
    scopeParameter->file->startScope();
    scopeParameter->file->write("logical_part_mapping ");
    scopeParameter->identifierType->write(currentPart->name, scopeParameter->file);
    scopeParameter->file->newLine();
    scopeParameter->file->write("(comp");
    for (int32_t j = 1; j <= scopeParameter->board->components->count(); j++) {
      fr::Component* currentComponent = scopeParameter->board->components->get(j);
      if (currentComponent->getLogicalPart() == currentPart) {
        scopeParameter->file->write(" ");
        scopeParameter->file->write(currentComponent->name);
      }
    }
    scopeParameter->file->write(")");
    scopeParameter->file->endScope();
  }

  // write the logical parts.

  for (int32_t i = 1; i <= logicalParts->count(); i++) {
    fr::LogicalPart* currentPart = logicalParts->get(i);

    scopeParameter->file->startScope();
    scopeParameter->file->write("logical_part ");
    scopeParameter->identifierType->write(currentPart->name, scopeParameter->file);
    scopeParameter->file->newLine();
    for (int32_t j = 0; j < currentPart->pinCount(); j++) {
      scopeParameter->file->newLine();
      fr::LogicalPart_PartPin* currentPin = currentPart->getPin(j);
      scopeParameter->file->write("(pin ");
      scopeParameter->identifierType->write(currentPin->pinName, scopeParameter->file);
      scopeParameter->file->write(" 0 ");
      scopeParameter->identifierType->write(currentPin->gateName, scopeParameter->file);
      scopeParameter->file->write(" ");
      const int32_t gateSwapCode = currentPin->gateSwapCode;
      scopeParameter->file->write(jrt::str(gateSwapCode));
      scopeParameter->file->write(" ");
      scopeParameter->identifierType->write(currentPin->gatePinName, scopeParameter->file);
      scopeParameter->file->write(" ");
      int32_t gatePinSwapCode = currentPin->gatePinSwapCode;
      scopeParameter->file->write(jrt::str(gatePinSwapCode));
      scopeParameter->file->write(")");
    }
    scopeParameter->file->endScope();
  }
  scopeParameter->file->endScope();
}

bool PartLibrary::readScope(ReadScopeParameter* scopeParameter) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("PartLibrary.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("PartLibrary.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      // "logicalPart".equals(nextToken instanceof Keyword k ? k.getName() : nextToken)
      bool isLogicalPartName = false;
      if (auto* k = jrt::fast_cast<Keyword>(nextToken)) {
        isLogicalPartName = k->getName() == "logicalPart";
      } else if (auto* s = jrt::fast_cast<JString>(nextToken)) {
        isLogicalPartName = s->value == "logicalPart";
      }
      if (nextToken == LOGICAL_PART_MAPPING()) {
        PartLibrary_LogicalPartMapping* nextMapping =
            readLogicalPartMapping(scopeParameter->scanner);
        if (nextMapping == nullptr) {
          return false;
        }
        scopeParameter->logicalPartMappings->add(nextMapping);
      } else if (nextToken == LOGICAL_PART() || isLogicalPartName) {
        PartLibrary_LogicalPart* nextPart = readLogicalPart(scopeParameter->scanner);
        if (nextPart == nullptr) {
          return false;
        }
        scopeParameter->logicalParts->add(nextPart);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
  }
  return true;
}

PartLibrary_LogicalPartMapping* PartLibrary::readLogicalPartMapping(IJFlexScanner* scanner) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    auto* name = jrt::fast_cast<JString>(nextToken);
    if (name == nullptr) {
      FRLogger::warn("PartLibrary.read_logical_part_mapping: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    nextToken = scanner->nextToken();
    if (nextToken != OPEN_BRACKET()) {
      FRLogger::warn("PartLibrary.read_logical_part_mapping: open bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    nextToken = scanner->nextToken();
    if (nextToken != COMPONENT_SCOPE()) {
      FRLogger::warn(
          "PartLibrary.read_logical_part_mapping: Keyword.COMPONENT_SCOPE expected at '" +
          scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    jrt::TreeSet<std::string>* result = new jrt::TreeSet<std::string>();
    for (;;) {
      scanner->yybegin(SpecctraDsnStreamReader::NAME);
      nextToken = scanner->nextToken();
      if (nextToken == CLOSED_BRACKET()) {
        break;
      }
      auto* componentName = jrt::fast_cast<JString>(nextToken);
      if (componentName == nullptr) {
        FRLogger::warn("PartLibrary.read_logical_part_mapping: string expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      result->add(componentName->value);
    }
    nextToken = scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("PartLibrary.read_logical_part_mapping: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    return new PartLibrary_LogicalPartMapping(name->value, result);
  } catch (const jrt::IOException& e) {
    FRLogger::error("PartLibrary.read_logical_part_mapping: IO error scanning file", &e);
    return nullptr;
  }
}

PartLibrary_LogicalPart* PartLibrary::readLogicalPart(IJFlexScanner* scanner) {
  jrt::Collection<PartLibrary_PartPin*>* const partPins =
      new jrt::LinkedList<PartLibrary_PartPin*>();
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("PartLibrary.read_logical_part: IO error scanning file", &e);
    return nullptr;
  }
  auto* partName = jrt::fast_cast<JString>(nextToken);
  if (partName == nullptr) {
    FRLogger::warn("PartLibrary.read_logical_part: string expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  scanner->setScopeIdentifier(partName->value);
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("PartLibrary.read_logical_part: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("PartLibrary.read_logical_part: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    bool readOk = true;
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == PIN()) {
        PartLibrary_PartPin* currentPartPin = readPartPin(scanner);
        if (currentPartPin == nullptr) {
          return nullptr;
        }
        partPins->add(currentPartPin);
      } else {
        skipScope(scanner);
      }
    }
    if (!readOk) {
      return nullptr;
    }
  }
  return new PartLibrary_LogicalPart(partName->value, partPins);
}

PartLibrary_PartPin* PartLibrary::readPartPin(IJFlexScanner* scanner) {
  try {
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    jrt::Object* nextToken = scanner->nextToken();
    auto* pinName = jrt::fast_cast<JString>(nextToken);
    if (pinName == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    scanner->setScopeIdentifier(pinName->value);
    nextToken = scanner->nextToken();
    if (jrt::fast_cast<JInteger>(nextToken) == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: integer expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    nextToken = scanner->nextToken();
    auto* gateName = jrt::fast_cast<JString>(nextToken);
    if (gateName == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    scanner->setScopeIdentifier(gateName->value);
    nextToken = scanner->nextToken();
    if (jrt::fast_cast<JInteger>(nextToken) == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: integer expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    const int32_t gateSwapCode = jrt::fast_cast<JInteger>(nextToken)->value;
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    nextToken = scanner->nextToken();
    auto* gatePinName = jrt::fast_cast<JString>(nextToken);
    if (gatePinName == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    scanner->setScopeIdentifier(gatePinName->value);
    nextToken = scanner->nextToken();
    if (jrt::fast_cast<JInteger>(nextToken) == nullptr) {
      FRLogger::warn("PartLibrary.read_part_pin: integer expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    int32_t gatePinSwapCode = jrt::fast_cast<JInteger>(nextToken)->value;
    // overread subgates
    do {
      nextToken = scanner->nextToken();
    } while (nextToken != CLOSED_BRACKET() && nextToken != nullptr);
    return new PartLibrary_PartPin(pinName->value, gateName->value, gateSwapCode,
                                   gatePinName->value, gatePinSwapCode);
  } catch (const jrt::IOException& e) {
    FRLogger::error("PartLibrary.read_part_pin: IO error scanning file", &e);
    return nullptr;
  }
}

}  // namespace fr::dsn
