#include "io/specctra/parser/Rule.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Circuit.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/CornerStyle.h"
#include "rules/MeanderPatternType.h"
#include "rules/NetClass.h"
#include "rules/NetMeanderConstraint.h"

namespace fr::dsn {

namespace {

// Java unboxing of a null Double throws a NullPointerException.
double unboxDouble(const std::optional<double>& v) {
  if (!v.has_value()) {
    throw jrt::NullPointerException();
  }
  return *v;
}

}  // namespace

jrt::Collection<Rule*>* Rule::readScope(IJFlexScanner* scanner) {
  jrt::Collection<Rule*>* result = new (jrt::kOp) jrt::LinkedList<Rule*>();
  jrt::Object* currentToken = nullptr;
  auto* meanderBuilder = new (jrt::kOp) Circuit_MeanderDescriptorBuilder();
  for (;;) {
    jrt::Object* prevToken = currentToken;
    try {
      currentToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Rule.read_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (currentToken == nullptr) {
      FRLogger::warn("Rule.read_scope: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (currentToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }

    if (prevToken == Keyword::OPEN_BRACKET()) {
      // every rule starts with a "("
      Rule* currentRule = nullptr;
      if (meanderBuilder->readDescriptor(currentToken, scanner)) {
        // Handled meander descriptor - accumulated into meanderBuilder
      } else if (currentToken == Keyword::WIDTH()) {
        // this is a "(width" rule
        currentRule = readWidthRule(scanner);
      } else if (currentToken == Keyword::CLEARANCE()) {
        // this is a "(clear" rule
        currentRule = readClearanceRule(scanner);
      } else if (currentToken == Keyword::LENGTH()) {
        // this is a "(length" rule
        Circuit_LengthMatchingRule* lengthResult = Circuit::readLengthScope(scanner);
        if (lengthResult != nullptr) {
          currentRule = new (jrt::kOp) Rule_LengthRule(lengthResult->maxLength, lengthResult->minLength);
        }
      } else {
        ScopeKeyword::skipScope(scanner);
      }

      if (currentRule != nullptr) {
        result->add(currentRule);
      }
    }
  }
  if (meanderBuilder->hasAttributes()) {
    result->add(meanderBuilder->toMeanderRule());
  }
  return result;
}

Rule_LayerRule* Rule::readLayerRuleScope(IJFlexScanner* scanner) {
  try {
    jrt::Collection<std::string>* layerNames = new (jrt::kOp) jrt::LinkedList<std::string>();
    jrt::Collection<Rule*>* ruleList = new (jrt::kOp) jrt::LinkedList<Rule*>();
    for (;;) {
      scanner->yybegin(SpecctraDsnStreamReader::LAYER_NAME);
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == Keyword::OPEN_BRACKET()) {
        break;
      }
      auto* layerName = jrt::fast_cast<JString>(nextToken);
      if (layerName == nullptr) {

        FRLogger::warn("Rule.read_layer_rule_scope: string expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      layerNames->add(layerName->value);
    }
    for (;;) {
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      if (nextToken != Keyword::RULE()) {

        FRLogger::warn("Rule.read_layer_rule_scope: rule expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      jrt::Collection<Rule*>* readRules = readScope(scanner);
      if (readRules == nullptr) {
        // Java: addAll(null) throws a NullPointerException.
        throw jrt::NullPointerException();
      }
      ruleList->addAll(readRules);
    }
    return new (jrt::kOp) Rule_LayerRule(layerNames, ruleList);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Rule.read_layer_rule_scope: IO error scanning file", &e);
    return nullptr;
  }
}

Rule_WidthRule* Rule::readWidthRule(IJFlexScanner* scanner) {
  double value = unboxDouble(scanner->nextDouble());

  if (!scanner->nextClosingBracket()) {
    return nullptr;
  }

  return new (jrt::kOp) Rule_WidthRule(value);
}

void Rule::writeScope(fr::NetClass* netClass, WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("rule");

  // write the trace width
  int32_t defaultTraceHalfWidth = netClass->getTraceHalfWidth(0);
  double traceWidth = 2 * scopeParameter->coordinateTransform->boardToDsn(defaultTraceHalfWidth);
  scopeParameter->file->newLine();
  scopeParameter->file->write("(width ");
  scopeParameter->file->write(jrt::str(traceWidth));
  scopeParameter->file->write(")");
  scopeParameter->file->endScope();
  for (int32_t i = 1; i < scopeParameter->board->layerStructure->layers->length; i++) {
    if (netClass->getTraceHalfWidth(i) != defaultTraceHalfWidth) {
      writeLayerRule(netClass, i, scopeParameter);
    }
  }
}

void Rule::writeLayerRule(fr::NetClass* netClass, int32_t layerIndex,
                          WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("layer_rule ");

  fr::Layer* currentBoardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];

  scopeParameter->file->write(currentBoardLayer->name);
  scopeParameter->file->startScope();
  scopeParameter->file->write("rule ");

  int32_t currentTraceHalfWidth = netClass->getTraceHalfWidth(layerIndex);

  // write the trace width
  double traceWidth = 2 * scopeParameter->coordinateTransform->boardToDsn(currentTraceHalfWidth);
  scopeParameter->file->newLine();
  scopeParameter->file->write("(width ");
  scopeParameter->file->write(jrt::str(traceWidth));
  scopeParameter->file->write(") ");
  scopeParameter->file->endScope();
  scopeParameter->file->endScope();
}

void Rule::writeDefaultRule(WriteScopeParameter* scopeParameter, int32_t layer) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("rule");
  // write the trace width
  double traceWidth = 2 * scopeParameter->coordinateTransform->boardToDsn(
                              scopeParameter->board->rules->getDefaultNetClass()->getTraceHalfWidth(0));
  scopeParameter->file->newLine();
  scopeParameter->file->write("(width ");
  scopeParameter->file->write(jrt::str(traceWidth));
  scopeParameter->file->write(")");
  // write the default clearance rule
  int32_t defaultClNo = fr::BoardRules::defaultClearanceClass();
  int32_t defaultBoardClearance =
      scopeParameter->board->rules->clearanceMatrix->getValue(defaultClNo, defaultClNo, layer, false);
  double defaultClearance = scopeParameter->coordinateTransform->boardToDsn(defaultBoardClearance);
  scopeParameter->file->newLine();
  // write the default clearance
  scopeParameter->file->write("(clearance ");
  scopeParameter->file->write(jrt::str(defaultClearance));
  scopeParameter->file->write(")");
  // write the smd_to_turn_gap
  double smdToTurnDist = scopeParameter->coordinateTransform->boardToDsn(
      scopeParameter->board->rules->getPinEdgeToTurnDist());
  scopeParameter->file->newLine();
  scopeParameter->file->write("(clearance ");
  scopeParameter->file->write(jrt::str(smdToTurnDist));
  scopeParameter->file->write(" (type smd_to_turn_gap))");

  // write the named clearance rules from the clearance matrix
  writeNamedClearanceRules(scopeParameter, layer);
  // write_non_default_clearance_rules(scopeParameter, layer, defaultBoardClearance);

  scopeParameter->file->endScope();
}

void Rule::writeNonDefaultClearanceRules(WriteScopeParameter* scopeParameter, int32_t layer,
                                         int32_t defaultClearance) {

  fr::ClearanceMatrix* clMatrix = scopeParameter->board->rules->clearanceMatrix;
  int32_t clCount = scopeParameter->board->rules->clearanceMatrix->getClassCount();

  for (int32_t i = 1; i <= clCount; i++) {
    for (int32_t j = i; j < clCount; j++) {
      int32_t currentBoardClearance = clMatrix->getValue(i, j, layer, false);

      if (currentBoardClearance == defaultClearance) {
        continue;
      }

      double currentClearance = scopeParameter->coordinateTransform->boardToDsn(currentBoardClearance);
      scopeParameter->file->newLine();
      scopeParameter->file->write("(clearance ");
      scopeParameter->file->write(jrt::str(currentClearance));
      scopeParameter->file->write(" (type ");
      scopeParameter->identifierType->write(clMatrix->getName(i), scopeParameter->file);
      // Java file.write(char) resolves to Writer.write(int c), which writes the character.
      scopeParameter->file->write(std::string(1, DsnFile::CLASS_CLEARANCE_SEPARATOR));
      scopeParameter->identifierType->write(clMatrix->getName(j), scopeParameter->file);
      scopeParameter->file->write("))");
    }
  }
}

void Rule::writeNamedClearanceRules(WriteScopeParameter* scopeParameter, int32_t layer) {

  fr::ClearanceMatrix* clMatrix = scopeParameter->board->rules->clearanceMatrix;
  int32_t clCount = scopeParameter->board->rules->clearanceMatrix->getClassCount();

  for (int32_t i = 1; i < clCount; i++) {
    if (clMatrix->getName(i) == "default") {
      continue;
    }

    int32_t currentBoardClearance = clMatrix->getValue(i, i, layer, false);
    double currentClearance = scopeParameter->coordinateTransform->boardToDsn(currentBoardClearance);

    scopeParameter->file->newLine();
    scopeParameter->file->write("(clearance ");
    scopeParameter->file->write(jrt::str(currentClearance));
    scopeParameter->file->write(" (type ");
    scopeParameter->identifierType->write(clMatrix->getName(i), scopeParameter->file);
    scopeParameter->file->write("))");
  }
}

Rule_ClearanceRule* Rule::readClearanceRule(IJFlexScanner* scanner) {
  try {
    double value = unboxDouble(scanner->nextDouble());

    jrt::Collection<std::string>* classPairs = new (jrt::kOp) jrt::LinkedList<std::string>();
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      // look for "(type"
      if (nextToken != Keyword::OPEN_BRACKET()) {
        FRLogger::warn("Rule.read_clearance_rule: ( expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      nextToken = scanner->nextToken();
      if (nextToken != Keyword::TYPE()) {
        FRLogger::warn("Rule.read_clearance_rule: type expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }

      jrt::Array<std::string>* pairs = scanner->nextStringList(DsnFile::CLASS_CLEARANCE_SEPARATOR);
      for (const std::string& pair : *pairs) {
        classPairs->add(pair);
      }

      // check the closing ")" of "(type"
      if (!scanner->nextClosingBracket()) {
        FRLogger::warn("Rule.read_clearance_rule: closing bracket expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }

      // check the closing ")" of "(clear"
      if (!scanner->nextClosingBracket()) {
        FRLogger::warn("Rule.read_clearance_rule: closing bracket expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }

    return new (jrt::kOp) Rule_ClearanceRule(value, classPairs);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Rule.read_clearance_rule: IO error scanning file", &e);
    return nullptr;
  }
}

void Rule::writeItemClearanceClass(const std::string& name, fr::IndentFileWriter* file,
                                   fr::IdentifierType* identifierType) {
  file->newLine();
  file->write("(clearance_class ");
  identifierType->write(name, file);
  file->write(")");
}

// --- MeanderRule ------------------------------------------------------------------------------

fr::NetMeanderConstraint* Rule_MeanderRule::applyTo(fr::NetMeanderConstraint* existing,
                                                    fr::io::CoordinateTransform* transform) {
  fr::NetMeanderConstraint* base =
      (existing != nullptr)
          ? existing
          : fr::NetMeanderConstraint::createDefaultPreset(
                this->patternType.has_value() ? *this->patternType
                                              : fr::MeanderPatternType::SINGLE_TRACK,
                1.0);
  return fr::NetMeanderConstraint::fromDsn(this->minAmplitude, this->maxAmplitude, this->spacing,
                                           this->cornerStyle, this->radiusPercentage,
                                           this->singleSided, transform, base);
}

fr::NetMeanderConstraint* Rule_MeanderRule::toMeanderConstraint(
    fr::MeanderPatternType defaultType, fr::io::CoordinateTransform* transform) {
  fr::MeanderPatternType type = this->patternType.has_value() ? *this->patternType : defaultType;
  fr::NetMeanderConstraint* preset = fr::NetMeanderConstraint::createDefaultPreset(type, 1.0);
  return fr::NetMeanderConstraint::fromDsn(this->minAmplitude, this->maxAmplitude, this->spacing,
                                           this->cornerStyle, this->radiusPercentage,
                                           this->singleSided, transform, preset);
}

fr::NetMeanderConstraint* Rule_MeanderRule::toMeanderConstraint(
    fr::io::CoordinateTransform* transform) {
  return toMeanderConstraint(fr::MeanderPatternType::SINGLE_TRACK, transform);
}

}  // namespace fr::dsn
