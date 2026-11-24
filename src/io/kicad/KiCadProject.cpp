#include "io/kicad/KiCadProject.h"

#include "util/FileText.h"
#include "util/JsonReader.h"

namespace fr {

std::optional<double> KiCadProject::narrowestTraceWidthUm() {
  if (!minTrackWidthUm_.has_value()) {
    return minConnectionWidthUm_;
  }
  if (!minConnectionWidthUm_.has_value()) {
    return minTrackWidthUm_;
  }
  return jrt::Math::max(*minTrackWidthUm_, *minConnectionWidthUm_);
}

KiCadProject* KiCadProject::read(const std::string& path) {
  return parse(FileText::readString(path), FileText::fileName(path));
}

KiCadProject* KiCadProject::parse(const std::string& text, const std::string& sourceName) {
  JsonValue* root;
  try {
    root = JsonReader::parseString(text);
  } catch (const JsonReader::JsonParseException& e) {
    throw jrt::IllegalArgumentException("not a KiCad project file: " + e.getMessage());
  }
  if (!root->isJsonObject()) {
    throw jrt::IllegalArgumentException("not a KiCad project file: no JSON object");
  }
  JsonValue* rules = member(member(member(root, "board"), "design_settings"), "rules");
  return new (jrt::kBoard) KiCadProject(sourceName, positiveUm(member(rules, "min_track_width")),
                                        positiveUm(member(rules, "min_connection")));
}

std::optional<double> KiCadProject::positiveUm(JsonValue* value) {
  if (value != nullptr && value->isJsonPrimitive() && value->isNumber()) {
    double um = value->getAsDouble() * 1000.0;  // KiCad stores lengths in millimetres
    if (um > 0) {
      return um;
    }
  }
  return std::nullopt;
}

JsonValue* KiCadProject::member(JsonValue* element, const std::string& name) {
  if (element == nullptr || !element->isJsonObject()) {
    return nullptr;
  }
  return element->get(name);
}

}  // namespace fr
