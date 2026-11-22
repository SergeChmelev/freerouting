#include "io/specctra/parser/PolygonWireScan.h"

#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"

namespace fr::dsn {

jrt::Map<std::string, jrt::Set<std::string>*>* PolygonWireScan::netsByLayer(std::istream& dsn) {
  jrt::Map<std::string, jrt::Set<std::string>*>* result =
      new (jrt::kOp) jrt::LinkedHashMap<std::string, jrt::Set<std::string>*>();
  SpecctraDsnStreamReader* scanner = new (jrt::kOp) SpecctraDsnStreamReader(dsn);
  int32_t depth = 0;
  int32_t wireDepth = -1;
  std::optional<std::string> wireLayer;
  std::optional<std::string> wireNet;
  jrt::Object* prevToken = nullptr;
  try {
    for (;;) {
      jrt::Object* token = scanner->nextToken();
      if (token == nullptr) {
        break;
      }
      if (token == Keyword::OPEN_BRACKET()) {
        ++depth;
      } else if (token == Keyword::CLOSED_BRACKET()) {
        if (depth == wireDepth) {
          if (wireLayer.has_value()) {
            jrt::Set<std::string>* nets = result->computeIfAbsent(
                *wireLayer, [](const std::string&) -> jrt::Set<std::string>* {
                  return new (jrt::kOp) jrt::LinkedHashSet<std::string>();
                });
            if (wireNet.has_value()) {
              nets->add(*wireNet);
            }
          }
          wireDepth = -1;
          wireLayer = std::nullopt;
          wireNet = std::nullopt;
        }
        --depth;
      } else if (prevToken == Keyword::OPEN_BRACKET()) {
        if (token == Keyword::WIRE() && wireDepth < 0) {
          wireDepth = depth;
        } else if (wireDepth >= 0 && depth == wireDepth + 1) {
          if (token == Keyword::POLYGON()) {
            jrt::Object* layerToken = scanner->nextToken();
            if (auto* layerName = jrt::fast_cast<JString>(layerToken)) {
              wireLayer = layerName->value;
            }
          } else if (token == Keyword::NET()) {
            wireNet = scanner->nextString();
          }
        }
      }
      prevToken = token;
    }
  } catch (const jrt::IOException&) {
    return new (jrt::kOp) jrt::HashMap<std::string, jrt::Set<std::string>*>();
  } catch (const jrt::RuntimeException&) {
    return new (jrt::kOp) jrt::HashMap<std::string, jrt::Set<std::string>*>();
  }
  return result;
}

}  // namespace fr::dsn
