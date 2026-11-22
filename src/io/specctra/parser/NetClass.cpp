#include "io/specctra/parser/NetClass.h"

#include "io/specctra/parser/Circuit.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

namespace {

/** Java Collection.addAll(null) throws NullPointerException; keep that instead of crashing. */
template <class T>
void addAllNonNull(jrt::Collection<T>* target, jrt::Collection<T>* source) {
  if (source == nullptr) {
    throw jrt::NullPointerException("Collection.addAll(null)");
  }
  target->addAll(source);
}

}  // namespace

NetClass* NetClass::readScope(IJFlexScanner* scanner) {
  try {
    // read the class name
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    std::string className = scanner->nextString();

    jrt::Collection<std::string>* netList = new (jrt::kOp) jrt::LinkedList<std::string>();
    bool rulesMissing = false;
    // read the nets belonging to the class
    jrt::Array<std::string>* netsInTheClass = scanner->nextStringList();
    for (const std::string& netName : *netsInTheClass) {
      netList->add(netName);
    }

    jrt::Collection<Rule*>* rules = new (jrt::kOp) jrt::LinkedList<Rule*>();
    jrt::Collection<Rule_LayerRule*>* layerRules = new (jrt::kOp) jrt::LinkedList<Rule_LayerRule*>();
    jrt::Collection<std::string>* useVia = new (jrt::kOp) jrt::LinkedList<std::string>();
    jrt::Collection<std::string>* useLayer = new (jrt::kOp) jrt::LinkedList<std::string>();
    std::optional<std::string> viaRule;
    std::optional<std::string> traceClearanceClass;
    bool pullTight = true;
    bool shoveFixed = false;
    double minTraceLength = 0;
    double maxTraceLength = 0;
    Circuit_ReadScopeResult* circuit = nullptr;

    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      return new (jrt::kBoard) NetClass(className, traceClearanceClass, netList, rules, layerRules, useVia,
                          useLayer, viaRule, shoveFixed, pullTight, minTraceLength,
                          maxTraceLength);
    }
    if (!rulesMissing) {
      jrt::Object* prevToken = nextToken;
      for (;;) {
        nextToken = scanner->nextToken();
        if (nextToken == nullptr) {
          FRLogger::warn("NetClass.read_scope: unexpected end of file at '" +
                         scanner->getScopeIdentifier() + "'");
          return nullptr;
        }
        if (nextToken == Keyword::CLOSED_BRACKET()) {
          // end of scope
          break;
        }
        if (prevToken == Keyword::OPEN_BRACKET()) {
          if (nextToken == Keyword::RULE()) {
            addAllNonNull(rules, Rule::readScope(scanner));
          } else if (nextToken == Keyword::LAYER_RULE()) {
            layerRules->add(Rule::readLayerRuleScope(scanner));
          } else if (nextToken == Keyword::VIA_RULE()) {
            viaRule = DsnFile::readStringScope(scanner);
          } else if (nextToken == Keyword::CIRCUIT()) {
            Circuit_ReadScopeResult* currentRule = Circuit::readScope(scanner);
            if (currentRule != nullptr) {
              circuit = currentRule;
              maxTraceLength = currentRule->maxLength;
              minTraceLength = currentRule->minLength;
              addAllNonNull(useVia, currentRule->useVia);
              addAllNonNull(useLayer, currentRule->useLayer);
            }
          } else if (nextToken == Keyword::CLEARANCE_CLASS()) {
            traceClearanceClass = DsnFile::readStringScope(scanner);
            if (!traceClearanceClass.has_value()) {
              return nullptr;
            }
          } else if (nextToken == Keyword::SHOVE_FIXED()) {
            shoveFixed = DsnFile::readOnOffScope(scanner);
          } else if (nextToken == Keyword::PULL_TIGHT()) {
            pullTight = DsnFile::readOnOffScope(scanner);
          } else {
            ScopeKeyword::skipScope(scanner);
          }
        }
        prevToken = nextToken;
      }
    }
    return new (jrt::kBoard) NetClass(className, traceClearanceClass, netList, rules, layerRules, useVia,
                        useLayer, viaRule, shoveFixed, pullTight, minTraceLength, maxTraceLength,
                        circuit);
  } catch (const jrt::IOException& e) {
    FRLogger::error("NetClass.read_scope: IO error while scanning file", &e);
    return nullptr;
  }
}

NetClass_ClassClass* NetClass::readClassClassScope(IJFlexScanner* scanner) {
  try {
    jrt::Collection<std::string>* classes = new (jrt::kOp) jrt::LinkedList<std::string>();
    jrt::Collection<Rule*>* rules = new (jrt::kOp) jrt::LinkedList<Rule*>();
    jrt::Collection<Rule_LayerRule*>* layerRules = new (jrt::kOp) jrt::LinkedList<Rule_LayerRule*>();
    jrt::Object* prevToken = nullptr;
    for (;;) {
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == nullptr) {
        FRLogger::warn("ClassClass.read_scope: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        // end of scope
        break;
      }
      if (prevToken == Keyword::OPEN_BRACKET()) {
        if (nextToken == Keyword::CLASSES()) {
          jrt::Array<std::string>* classNames = DsnFile::readStringListScope(scanner);
          if (classNames == nullptr) {
            throw jrt::NullPointerException("Arrays.stream(null)");
          }
          for (const std::string& className : *classNames) {
            classes->add(className);
          }
        } else if (nextToken == Keyword::RULE()) {
          addAllNonNull(rules, Rule::readScope(scanner));
        } else if (nextToken == Keyword::LAYER_RULE()) {
          layerRules->add(Rule::readLayerRuleScope(scanner));
        }
      }
      prevToken = nextToken;
    }
    return new (jrt::kOp) NetClass_ClassClass(classes, rules, layerRules);
  } catch (const jrt::IOException& e) {
    FRLogger::error("NetClass.read_scope: IO error while scanning file", &e);
    return nullptr;
  }
}

}  // namespace fr::dsn
