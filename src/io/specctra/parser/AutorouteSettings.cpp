#include "io/specctra/parser/AutorouteSettings.h"

#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace fr::dsn {

fr::RouterSettings* AutorouteSettings::readScope(IJFlexScanner* scanner,
                                                 LayerStructure* layerStructure) {
  fr::RouterSettings* result = new fr::RouterSettings();
  result->setLayerCount(layerStructure->layers->length);
  bool withAutoroute = true;
  bool withPostroute = true;
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("AutorouteSettings.read_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("AutorouteSettings.read_scope: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      if (nextToken == Keyword::FANOUT()) {
        DsnFile::readOnOffScope(scanner);
      } else if (nextToken == Keyword::AUTOROUTE()) {
        withAutoroute = DsnFile::readOnOffScope(scanner);
      } else if (nextToken == Keyword::POSTROUTE()) {
        withPostroute = DsnFile::readOnOffScope(scanner);
      } else if (nextToken == Keyword::VIAS()) {
        result->setViasAllowed(DsnFile::readOnOffScope(scanner));
      } else if (nextToken == Keyword::VIA_COSTS()) {
        result->setViaCosts(DsnFile::readIntegerScope(scanner));
      } else if (nextToken == Keyword::PLANE_VIA_COSTS()) {
        result->setPlaneViaCosts(DsnFile::readIntegerScope(scanner));
      } else if (nextToken == Keyword::START_RIPUP_COSTS()) {
        result->setStartRipupCosts(DsnFile::readIntegerScope(scanner));
      } else if (nextToken == Keyword::LAYER_RULE()) {
        result = readLayerRule(scanner, layerStructure, result);
        if (result == nullptr) {
          return nullptr;
        }
      } else {
        ScopeKeyword::skipScope(scanner);
      }
    }
  }
  result->setRunRouter(withAutoroute);
  result->setRunOptimizer(withPostroute);
  return result;
}

fr::RouterSettings* AutorouteSettings::readLayerRule(IJFlexScanner* scanner,
                                                     LayerStructure* layerStructure,
                                                     fr::RouterSettings* settings) {
  scanner->yybegin(SpecctraDsnStreamReader::NAME);
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("AutorouteSettings.read_layer_rule: IO error scanning file", &e);
    return nullptr;
  }
  auto* layerName = dynamic_cast<JString*>(nextToken);
  if (layerName == nullptr) {
    FRLogger::warn("AutorouteSettings.read_layer_rule: String expected at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  int32_t layerIndex = layerStructure->getNo(layerName->value);
  if (layerIndex < 0) {
    FRLogger::warn("AutorouteSettings.read_layer_rule: layer not found at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("AutorouteSettings.read_layer_rule: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("AutorouteSettings.read_layer_rule: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == Keyword::OPEN_BRACKET()) {
      if (nextToken == Keyword::ACTIVE()) {
        settings->setLayerActive(layerIndex, DsnFile::readOnOffScope(scanner));
      } else if (nextToken == Keyword::PREFERRED_DIRECTION()) {
        try {
          bool prefDirIsHorizontal = true;
          nextToken = scanner->nextToken();
          if (nextToken == Keyword::VERTICAL()) {
            prefDirIsHorizontal = false;
          } else if (nextToken != Keyword::HORIZONTAL()) {
            FRLogger::warn("AutorouteSettings.read_layer_rule: unexpected key word at '" +
                           scanner->getScopeIdentifier() + "'");
            return nullptr;
          }
          settings->setPreferredDirectionIsHorizontal(layerIndex, prefDirIsHorizontal);
          nextToken = scanner->nextToken();
          if (nextToken != Keyword::CLOSED_BRACKET()) {
            FRLogger::warn("AutorouteSettings.read_layer_rule: closing bracket expected at '" +
                           scanner->getScopeIdentifier() + "'");
            return nullptr;
          }
        } catch (const jrt::IOException& e) {
          FRLogger::error("AutorouteSettings.read_layer_rule: IO error scanning file", &e);
          return nullptr;
        }
      } else if (nextToken == Keyword::PREFERRED_DIRECTION_TRACE_COSTS()) {
        settings->setPreferredDirectionTraceCosts(layerIndex, DsnFile::readFloatScope(scanner));
      } else if (nextToken == Keyword::AGAINST_PREFERRED_DIRECTION_TRACE_COSTS()) {
        settings->setAgainstPreferredDirectionTraceCosts(layerIndex,
                                                         DsnFile::readFloatScope(scanner));
      } else {
        ScopeKeyword::skipScope(scanner);
      }
    }
  }
  return settings;
}

void AutorouteSettings::writeScope(fr::IndentFileWriter* file, fr::RouterSettings* settings,
                                   fr::LayerStructure* layerStructure,
                                   fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("autoroute_settings");
  file->newLine();
  file->write("(autoroute ");
  if (settings->getRunRouter()) {
    file->write("on)");
  } else {
    file->write("off)");
  }
  file->newLine();
  file->write("(postroute ");
  if (settings->getRunOptimizer()) {
    file->write("on)");
  } else {
    file->write("off)");
  }
  file->newLine();
  file->write("(vias ");
  if (settings->getViasAllowed()) {
    file->write("on)");
  } else {
    file->write("off)");
  }
  file->newLine();
  file->write("(via_costs ");
  {
    int32_t viaCosts = settings->getViaCosts();
    file->write(jrt::str(viaCosts));
  }
  file->write(")");
  file->newLine();
  file->write("(plane_via_costs ");
  {
    int32_t viaCosts = settings->getPlaneViaCosts();
    file->write(jrt::str(viaCosts));
  }
  file->write(")");
  file->newLine();
  file->write("(start_ripup_costs ");
  {
    int32_t ripupCosts = settings->getStartRipupCosts();
    file->write(jrt::str(ripupCosts));
  }
  file->write(")");
  file->newLine();
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    fr::Layer* const currentLayer = (*layerStructure->layers)[i];
    file->startScope();
    file->write("layer_rule ");
    identifierType->write(currentLayer->name, file);
    file->newLine();
    file->write("(active ");
    if (settings->getLayerActive(i)) {
      file->write("on)");
    } else {
      file->write("off)");
    }
    file->newLine();
    file->write("(preferred_direction ");
    if (settings->getPreferredDirectionIsHorizontal(i)) {
      file->write("horizontal)");
    } else {
      file->write("vertical)");
    }
    file->newLine();
    file->write("(preferred_direction_trace_costs ");
    float traceCosts = static_cast<float>(settings->getPreferredDirectionTraceCosts(i));
    file->write(jrt::str(traceCosts));
    file->write(")");
    file->newLine();
    file->write("(against_preferred_direction_trace_costs ");
    traceCosts = static_cast<float>(settings->getAgainstPreferredDirectionTraceCosts(i));
    file->write(jrt::str(traceCosts));
    file->write(")");
    file->endScope();
  }
  file->endScope();
}

}  // namespace fr::dsn
