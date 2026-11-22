#include "io/specctra/RulesReader.h"

#include <iterator>
#include <sstream>

#include "board/facade/BasicBoard.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/LayerStructure.h"
#include "board/state/Communication.h"
#include "core/library/BoardLibrary.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/AutorouteSettings.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/Library.h"
#include "io/specctra/parser/NetClass.h"
#include "io/specctra/parser/Network.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/Structure.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "settings/RouterSettings.h"

namespace fr::dsn {

namespace {

inline jrt::Object* obj(jrt::Object* o) { return o; }

}  // namespace

bool RulesReader::read(std::istream& in, const std::string& designName, fr::BasicBoard* board) {
  return read(in, designName, board, nullptr);
}

bool RulesReader::read(std::istream& in, const std::string& designName, fr::BasicBoard* board,
                       fr::RouterSettings* targetSettings) {
  if (board == nullptr) {
    FRLogger::warn("RulesReader.read: board is null");
    closeQuietly(in);
    return false;
  }

  IJFlexScanner* scanner = new SpecctraDsnStreamReader(in);

  // Java `finally` block: runs on every exit of the try statement below.
  auto finallyBlock = [&]() {
    closeQuietly(in);
    // The width rules read above, net class rules included, can lower the smallest trace width.
    if (board->getOutline() != nullptr) {
      board->getOutline()->updateSlicesToTraceWidths();
    }
  };

  auto body = [&]() -> bool {
    // Validate the "(rules PCB <name>" header
    jrt::Object* currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::OPEN_BRACKET())) {
      FRLogger::warn("RulesReader.read: open bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }
    currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::RULES())) {
      FRLogger::warn("RulesReader.read: keyword 'rules' expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }
    currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::PCB_SCOPE())) {
      FRLogger::warn("RulesReader.read: keyword 'pcb' expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    currentToken = scanner->nextToken();
    auto* currentString = dynamic_cast<JString*>(currentToken);
    if (currentString == nullptr || currentString->value != designName) {
      FRLogger::warn("RulesReader.read: designName not matching at '" +
                     scanner->getScopeIdentifier() + "' (expected '" + designName + "', got '" +
                     jrt::str(currentToken) + "')");
      // non-fatal: continue reading
    }

    auto* layerStructure = new LayerStructure(board->layerStructure);
    fr::io::CoordinateTransform* coordinateTransform = board->communication->coordinateTransform;

    // Parse all top-level scopes in the rules body
    jrt::Object* nextToken = nullptr;
    for (;;) {
      jrt::Object* prevToken = nextToken;
      try {
        nextToken = scanner->nextToken();
      } catch (const jrt::IOException& e) {
        FRLogger::error("RulesReader.read: IO error scanning rules body", &e);
        return false;
      }
      if (nextToken == nullptr) {
        FRLogger::warn("RulesReader.read: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      if (nextToken == obj(Keyword::CLOSED_BRACKET())) {
        // end of (rules ...) scope: success
        break;
      }
      if (prevToken == obj(Keyword::OPEN_BRACKET())) {
        if (nextToken == obj(Keyword::RULE())) {
          applyRules(Rule::readScope(scanner), board, std::nullopt);
        } else if (nextToken == obj(Keyword::LAYER())) {
          applyLayerRules(scanner, board);
        } else if (nextToken == obj(Keyword::PADSTACK())) {
          Library::readPadstackScope(scanner, layerStructure, coordinateTransform,
                                     board->library->padstacks);
        } else if (nextToken == obj(Keyword::VIA())) {
          applyViaInfo(scanner, board);
        } else if (nextToken == obj(Keyword::VIA_RULE())) {
          applyViaRule(scanner, board);
        } else if (nextToken == obj(Keyword::CLASS())) {
          applyNetClass(scanner, layerStructure, board);
        } else if (nextToken == obj(Keyword::SNAP_ANGLE())) {
          std::optional<fr::AngleRestriction> snapAngle = Structure::readSnapAngle(scanner);
          if (snapAngle.has_value()) {
            board->rules->setTraceAngleRestriction(*snapAngle);
          }
        } else if (nextToken == obj(Keyword::AUTOROUTE_SETTINGS())) {
          fr::RouterSettings* parsedSettings = AutorouteSettings::readScope(scanner, layerStructure);
          if (targetSettings != nullptr && parsedSettings != nullptr) {
            targetSettings->applyNewValuesFrom(parsedSettings);
          }
        } else {
          ScopeKeyword::skipScope(scanner);
        }
      }
    }
    return true;
  };

  bool result;
  try {
    result = body();
  } catch (const jrt::IOException& e) {
    FRLogger::error("RulesReader.read: IO error scanning rules header", &e);
    result = false;
  } catch (...) {
    finallyBlock();
    throw;
  }
  finallyBlock();
  return result;
}

fr::RouterSettings* RulesReader::readRouterSettings(std::istream& in) {
  jrt::Array<int8_t>* data;
  {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    closeQuietly(in);
    if (in.bad()) {
      jrt::IOException e("error reading stream");
      FRLogger::error("RulesReader.readRouterSettings: error reading stream", &e);
      return nullptr;
    }
    std::string bytes = buffer.str();
    data = new jrt::Array<int8_t>(static_cast<int32_t>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); i++) {
      (*data)[static_cast<int32_t>(i)] = static_cast<int8_t>(bytes[i]);
    }
  }

  if (data->length == 0) {
    return nullptr;
  }

  LayerStructure* layerStructure = discoverLayerStructure(data);
  std::istringstream dataStream(
      std::string(reinterpret_cast<const char*>(data->data()), static_cast<std::size_t>(data->length)));
  IJFlexScanner* scanner = new SpecctraDsnStreamReader(dataStream);
  try {
    // Validate header
    jrt::Object* currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::OPEN_BRACKET())) {
      return nullptr;
    }
    currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::RULES())) {
      return nullptr;
    }
    currentToken = scanner->nextToken();
    if (currentToken != obj(Keyword::PCB_SCOPE())) {
      return nullptr;
    }
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    scanner->nextToken();  // designName

    jrt::Object* nextToken = nullptr;
    for (;;) {
      jrt::Object* prevToken = nextToken;
      nextToken = scanner->nextToken();
      if (nextToken == nullptr || nextToken == obj(Keyword::CLOSED_BRACKET())) {
        break;
      }
      if (prevToken == obj(Keyword::OPEN_BRACKET())) {
        if (nextToken == obj(Keyword::AUTOROUTE_SETTINGS())) {
          return AutorouteSettings::readScope(scanner, layerStructure);
        } else {
          ScopeKeyword::skipScope(scanner);
        }
      }
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("RulesReader.readRouterSettings: IO error scanning file", &e);
  }
  return nullptr;
}

LayerStructure* RulesReader::discoverLayerStructure(jrt::Array<int8_t>* data) {
  auto* layerNames = new jrt::LinkedHashSet<std::string>();
  std::istringstream dataStream(
      std::string(reinterpret_cast<const char*>(data->data()), static_cast<std::size_t>(data->length)));
  IJFlexScanner* scanner = new SpecctraDsnStreamReader(dataStream);
  try {
    jrt::Object* token = nullptr;
    for (;;) {
      jrt::Object* prev = token;
      token = scanner->nextToken();
      if (token == nullptr) {
        break;
      }
      if (prev == obj(Keyword::OPEN_BRACKET())) {
        if (token == obj(Keyword::LAYER_RULE()) || token == obj(Keyword::LAYER())) {
          scanner->yybegin(SpecctraDsnStreamReader::NAME);
          jrt::Object* nameToken = scanner->nextToken();
          if (auto* s = dynamic_cast<JString*>(nameToken);
              s != nullptr && !jrt::strip(s->value).empty()) {
            layerNames->add(s->value);
          }
        }
      }
    }
  } catch (const jrt::IOException&) {
    // ignore
  }

  if (layerNames->isEmpty()) {
    layerNames->add("F.Cu");
    layerNames->add("B.Cu");
  }

  auto* list = new jrt::ArrayList<Layer*>();
  int32_t idx = 0;
  for (const std::string& name : *layerNames) {
    list->add(new Layer(name, idx++, true));
  }
  return new LayerStructure(static_cast<jrt::Collection<Layer*>*>(list));
}

void RulesReader::applyRules(jrt::Collection<Rule*>* rules, fr::BasicBoard* board,
                             const std::optional<std::string>& layerName) {
  if (rules == nullptr) {
    return;
  }
  int32_t layerIndex = -1;
  if (layerName.has_value()) {
    layerIndex = board->layerStructure->getNo(*layerName);
    if (layerIndex < 0) {
      FRLogger::warn("RulesReader.applyRules: layer not found: '" + *layerName + "'");
    }
  }
  fr::io::CoordinateTransform* coordinateTransform = board->communication->coordinateTransform;
  std::string stringQuote = board->communication->specctraParserInfo->stringQuote;
  for (Rule* rule : *rules) {
    if (auto* widthRule = dynamic_cast<Rule_WidthRule*>(rule)) {
      int32_t traceHalfwidth =
          jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(widthRule->value) / 2));
      if (layerIndex < 0) {
        board->rules->setDefaultTraceHalfWidths(traceHalfwidth);
      } else {
        board->rules->setDefaultTraceHalfWidth(layerIndex, traceHalfwidth);
      }
    } else if (auto* clearanceRule = dynamic_cast<Rule_ClearanceRule*>(rule)) {
      Structure::setClearanceRule(clearanceRule, layerIndex, coordinateTransform, board->rules,
                                  stringQuote);
    }
  }
}

void RulesReader::applyLayerRules(IJFlexScanner* scanner, fr::BasicBoard* board) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    auto* layerString = dynamic_cast<JString*>(nextToken);
    if (layerString == nullptr) {
      FRLogger::warn("RulesReader.applyLayerRules: String expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return;
    }
    nextToken = scanner->nextToken();
    while (nextToken != obj(Keyword::CLOSED_BRACKET())) {
      if (nextToken != obj(Keyword::OPEN_BRACKET())) {
        FRLogger::warn("RulesReader.applyLayerRules: '(' expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return;
      }
      nextToken = scanner->nextToken();
      if (nextToken == obj(Keyword::RULE())) {
        applyRules(Rule::readScope(scanner), board, layerString->value);
      } else {
        ScopeKeyword::skipScope(scanner);
      }
      nextToken = scanner->nextToken();
    }
  } catch (const jrt::IOException& e) {
    FRLogger::error("RulesReader.applyLayerRules: IO error scanning file", &e);
  }
}

void RulesReader::applyViaInfo(IJFlexScanner* scanner, fr::BasicBoard* board) {
  fr::ViaInfo* viaInfo = Network::readViaInfo(scanner, board);
  if (viaInfo == nullptr) {
    return;
  }
  fr::ViaInfo* existing = board->rules->viaInfos->get(viaInfo->getName());
  if (existing != nullptr) {
    board->rules->viaInfos->remove(existing);
  }
  board->rules->viaInfos->add(viaInfo);
}

void RulesReader::applyViaRule(IJFlexScanner* scanner, fr::BasicBoard* board) {
  jrt::Collection<std::string>* viaRule = Network::readViaRule(scanner, board);
  if (viaRule != nullptr) {
    Network::addViaRule(viaRule, board);
  }
}

void RulesReader::applyNetClass(IJFlexScanner* scanner, LayerStructure* layerStructure,
                                fr::BasicBoard* board) {
  NetClass* netClass = NetClass::readScope(scanner);
  if (netClass == nullptr) {
    return;
  }
  Network::insertNetClass(netClass, layerStructure, board,
                          board->communication->coordinateTransform, false);
}

}  // namespace fr::dsn
