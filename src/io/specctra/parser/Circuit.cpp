#include "io/specctra/parser/Circuit.h"

#include "io/CoordinateTransform.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/Structure.h"
#include "logger/FRLogger.h"
#include "rules/CornerStyle.h"
#include "rules/MeanderPatternType.h"
#include "rules/NetMeanderConstraint.h"

namespace fr::dsn {

namespace {

// Java would throw a NullPointerException when a null collection / array is used.
template <class T>
T* requireNonNull(T* p) {
  if (p == nullptr) {
    throw jrt::NullPointerException();
  }
  return p;
}

}  // namespace

Circuit_ReadScopeResult* Circuit::readScope(IJFlexScanner* scanner) {
  jrt::Object* nextToken = nullptr;
  double minTraceLength = 0;
  double maxTraceLength = 0;
  jrt::Collection<std::string>* useVia = new (jrt::kOp) jrt::LinkedList<std::string>();
  jrt::Collection<std::string>* useLayer = new (jrt::kOp) jrt::LinkedList<std::string>();
  auto* meanderBuilder = new (jrt::kOp) Circuit_MeanderDescriptorBuilder();

  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Circuit.read_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Circuit.read_scope: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      if (meanderBuilder->readDescriptor(nextToken, scanner)) {
        // Handled meander descriptor
      } else if (Keyword::matches(nextToken, Keyword::LENGTH())) {
        Circuit_LengthMatchingRule* lengthRule = readLengthScope(scanner);
        if (lengthRule != nullptr) {
          minTraceLength = lengthRule->minLength;
          maxTraceLength = lengthRule->maxLength;
        }
      } else if (nextToken == Keyword::USE_VIA()) {
        useVia->addAll(requireNonNull(Structure::readViaPadstacks(scanner)));
      } else if (nextToken == Keyword::USE_LAYER()) {
        jrt::Array<std::string>* layerNames =
            requireNonNull(DsnFile::readStringListScope(scanner));
        for (const std::string& layerName : *layerNames) {
          useLayer->add(layerName);
        }
      } else {
        ScopeKeyword::skipScope(scanner);
      }
    }
  }
  return new (jrt::kOp) Circuit_ReadScopeResult(
      maxTraceLength, minTraceLength, useVia, useLayer, meanderBuilder->minAmplitude,
      meanderBuilder->maxAmplitude, meanderBuilder->spacing, meanderBuilder->cornerStyle,
      meanderBuilder->radiusPercentage, meanderBuilder->singleSided, meanderBuilder->patternType);
}

Circuit_LengthMatchingRule* Circuit::readLengthScope(IJFlexScanner* scanner) {
  double maxLength = -1;
  double minLength = 0;
  jrt::Object* nextToken;

  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Circuit.read_length_scope: IO error scanning file", &e);
    return nullptr;
  }

  if (auto* doubleVal = jrt::fast_cast<JDouble>(nextToken)) {
    maxLength = doubleVal->value;
  } else if (auto* intVal = jrt::fast_cast<JInteger>(nextToken)) {
    maxLength = intVal->value;
  } else {
    FRLogger::warn("Circuit.read_length_scope: number expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }

  bool minLengthRead = false;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Circuit.read_length_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Circuit.read_length_scope: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (!minLengthRead && prevToken != Keyword::OPEN_BRACKET()) {
      if (auto* doubleVal = jrt::fast_cast<JDouble>(nextToken)) {
        minLength = doubleVal->value;
        minLengthRead = true;
        continue;
      } else if (auto* intVal = jrt::fast_cast<JInteger>(nextToken)) {
        minLength = intVal->value;
        minLengthRead = true;
        continue;
      }
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      ScopeKeyword::skipScope(scanner);
    }
  }
  return new (jrt::kOp) Circuit_LengthMatchingRule(maxLength, minLength);
}

std::optional<double> Circuit::parseDouble(jrt::Object* token) {
  if (auto* d = jrt::fast_cast<JDouble>(token)) {
    return d->value;
  }
  if (auto* i = jrt::fast_cast<JInteger>(token)) {
    return static_cast<double>(i->value);
  }
  if (auto* s = jrt::fast_cast<JString>(token)) {
    try {
      return jrt::Double::parseDouble(s->value);
    } catch (const jrt::NumberFormatException&) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

Circuit_AmplitudeRule* Circuit::readLengthAmplitudeScope(IJFlexScanner* scanner) {
  double maxAmp = -1;
  double minAmp = 0;
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Circuit.read_length_amplitude: IO error", &e);
    return nullptr;
  }
  std::optional<double> parsedMax = parseDouble(nextToken);
  if (parsedMax.has_value()) {
    maxAmp = *parsedMax;
  } else {
    FRLogger::warn("Circuit.read_length_amplitude: number expected at '" +
                   scanner->getScopeIdentifier() + "'");
    skipToClosedBracket(scanner);
    return nullptr;
  }

  bool minRead = false;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Circuit.read_length_amplitude: IO error", &e);
      return nullptr;
    }
    if (nextToken == nullptr || nextToken == Keyword::CLOSED_BRACKET()) {
      break;
    }
    if (!minRead && prevToken != Keyword::OPEN_BRACKET()) {
      std::optional<double> parsedMin = parseDouble(nextToken);
      if (parsedMin.has_value()) {
        minAmp = *parsedMin;
        minRead = true;
        continue;
      }
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      ScopeKeyword::skipScope(scanner);
    }
  }
  if (maxAmp > 0 && minAmp > 0 && minAmp > maxAmp) {
    double temp = minAmp;
    minAmp = maxAmp;
    maxAmp = temp;
  }
  return new (jrt::kOp) Circuit_AmplitudeRule(maxAmp, minAmp);
}

std::optional<double> Circuit::readNumberScope(IJFlexScanner* scanner) {
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Circuit.read_number_scope: IO error scanning file", &e);
    return std::nullopt;
  }
  std::optional<double> result = parseDouble(nextToken);
  if (!result.has_value()) {
    FRLogger::warn("Circuit.read_number_scope: number expected at '" +
                   scanner->getScopeIdentifier() + "'");
  }
  skipToClosedBracket(scanner);
  return result;
}

std::optional<std::string> Circuit::readStringScope(IJFlexScanner* scanner) {
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Circuit.read_string_scope: IO error scanning file", &e);
    return std::nullopt;
  }
  std::optional<std::string> result;
  if (auto* str = jrt::fast_cast<JString>(nextToken)) {
    result = str->value;
  } else if (auto* kw = jrt::fast_cast<Keyword>(nextToken)) {
    result = kw->getName();
  }
  skipToClosedBracket(scanner);
  return result;
}

std::optional<bool> Circuit::readBooleanScope(IJFlexScanner* scanner) {
  std::optional<std::string> str = readStringScope(scanner);
  if (!str.has_value()) {
    return std::nullopt;
  }
  return jrt::equalsIgnoreCase(*str, "on") || jrt::equalsIgnoreCase(*str, "true") ||
         jrt::equalsIgnoreCase(*str, "yes") || jrt::equalsIgnoreCase(*str, "1");
}

void Circuit::readMeanderPatternScopeInto(IJFlexScanner* scanner,
                                          Circuit_MeanderDescriptorBuilder* builder) {
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Circuit.readMeanderPatternScopeInto: IO error scanning file", &e);
      return;
    }
    if (nextToken == nullptr || nextToken == Keyword::CLOSED_BRACKET()) {
      break;
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      if (Keyword::matches(nextToken, Keyword::TYPE()) ||
          Keyword::matches(nextToken, Keyword::PATTERN_TYPE())) {
        std::optional<std::string> typeStr = readStringScope(scanner);
        if (typeStr.has_value()) {
          builder->patternType = fr::MeanderPatternType_parse(typeStr);
        }
      } else if (!builder->readDescriptor(nextToken, scanner)) {
        ScopeKeyword::skipScope(scanner);
      }
    }
  }
}

void Circuit::skipToClosedBracket(IJFlexScanner* scanner) {
  for (;;) {
    try {
      jrt::Object* token = scanner->nextToken();
      if (token == nullptr || token == Keyword::CLOSED_BRACKET()) {
        break;
      }
      if (token == Keyword::OPEN_BRACKET()) {
        ScopeKeyword::skipScope(scanner);
      }
    } catch (const jrt::IOException& e) {
      FRLogger::error("Circuit.skipToClosedBracket: IO error", &e);
      break;
    }
  }
}

// --- MeanderDescriptorBuilder -----------------------------------------------------------------

bool Circuit_MeanderDescriptorBuilder::readDescriptor(jrt::Object* token,
                                                      IJFlexScanner* scanner) {
  if (Keyword::matches(token, Keyword::LENGTH_AMPLITUDE())) {
    Circuit_AmplitudeRule* amp = Circuit::readLengthAmplitudeScope(scanner);
    if (amp != nullptr) {
      this->maxAmplitude = amp->maxAmplitude;
      this->minAmplitude = amp->minAmplitude;
    }
    return true;
  }
  if (Keyword::matches(token, Keyword::LENGTH_GAP())) {
    std::optional<double> gap = Circuit::readNumberScope(scanner);
    if (gap.has_value()) {
      this->spacing = gap;
    }
    return true;
  }
  if (Keyword::matches(token, Keyword::CORNER_STYLE())) {
    std::optional<std::string> styleStr = Circuit::readStringScope(scanner);
    if (styleStr.has_value()) {
      this->cornerStyle = fr::CornerStyle_parse(styleStr);
    }
    return true;
  }
  if (Keyword::matches(token, Keyword::RADIUS())) {
    std::optional<double> rad = Circuit::readNumberScope(scanner);
    if (rad.has_value()) {
      this->radiusPercentage = rad;
    }
    return true;
  }
  if (Keyword::matches(token, Keyword::SINGLE_SIDED())) {
    std::optional<bool> ss = Circuit::readBooleanScope(scanner);
    if (ss.has_value()) {
      this->singleSided = ss;
    }
    return true;
  }
  if (Keyword::matches(token, Keyword::MEANDER_PATTERN())) {
    Circuit::readMeanderPatternScopeInto(scanner, this);
    return true;
  }
  return false;
}

Rule_MeanderRule* Circuit_MeanderDescriptorBuilder::toMeanderRule() {
  return new (jrt::kOp) Rule_MeanderRule(minAmplitude, maxAmplitude, spacing, cornerStyle, radiusPercentage,
                              singleSided, patternType);
}

// --- ReadScopeResult --------------------------------------------------------------------------

fr::NetMeanderConstraint* Circuit_ReadScopeResult::toMeanderConstraint(
    fr::MeanderPatternType defaultType, fr::io::CoordinateTransform* transform) {
  if (!hasMeanderConstraint()) {
    return nullptr;
  }
  fr::MeanderPatternType type = this->patternType.has_value() ? *this->patternType : defaultType;
  fr::NetMeanderConstraint* preset = fr::NetMeanderConstraint::createDefaultPreset(type, 1.0);
  return fr::NetMeanderConstraint::fromDsn(this->minAmplitude, this->maxAmplitude, this->spacing,
                                           this->cornerStyle, this->radiusPercentage,
                                           this->singleSided, transform, preset);
}

fr::NetMeanderConstraint* Circuit_ReadScopeResult::toMeanderConstraint(
    fr::io::CoordinateTransform* transform) {
  return toMeanderConstraint(fr::MeanderPatternType::SINGLE_TRACK, transform);
}

}  // namespace fr::dsn
