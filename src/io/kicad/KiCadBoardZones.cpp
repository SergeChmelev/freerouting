#include "io/kicad/KiCadBoardZones.h"

#include <optional>
#include <vector>

#include "util/FileText.h"

namespace fr {

bool KiCadBoardZones_Pad::isOnLayer(const std::string& layerName) {
  return KiCadBoardZones::matchesLayer(layers_, layerName);
}

bool KiCadBoardZones_Zone::isOnLayer(const std::string& layerName) {
  return KiCadBoardZones::matchesLayer(layers_, layerName);
}

bool KiCadBoardZones::matchesLayer(jrt::List<std::string>* layers, const std::string& layerName) {
  for (const std::string& layer : *layers) {
    if (layer == layerName || (layer == "*.Cu" && jrt::endsWith(layerName, ".Cu")) ||
        (layer == "F&B.Cu" && (layerName == "F.Cu" || layerName == "B.Cu"))) {
      return true;
    }
  }
  return false;
}

/** A cursor over the S-expressions of the file, which stores nothing it is not asked for. */
class KiCadBoardZones_Reader {
 public:
  explicit KiCadBoardZones_Reader(const std::string& text) : text(text) {}

  bool isFinished() { return pos >= text.size(); }

  void skipBlanks() {
    while (pos < text.size() && jrt::Character::isWhitespace(static_cast<unsigned char>(text[pos]))) {
      pos++;
    }
  }

  /** Consumes a '(' if it is next. */
  bool enterList() {
    if (pos < text.size() && text[pos] == '(') {
      pos++;
      return true;
    }
    return false;
  }

  /** Consumes a ')' if it is next. */
  bool leaveList() {
    if (pos < text.size() && text[pos] == ')') {
      pos++;
      return true;
    }
    return false;
  }

  /** Reads a symbol or a quoted string; an empty string if a parenthesis or the end is next. */
  std::string readAtom() {
    skipBlanks();
    if (pos >= text.size()) {
      return "";
    }
    char c = text[pos];
    if (c == '(' || c == ')') {
      return "";
    }
    if (c == '"') {
      pos++;
      std::string result;
      while (pos < text.size() && text[pos] != '"') {
        char current = text[pos++];
        if (current == '\\' && pos < text.size()) {
          current = text[pos++];
        }
        result += current;
      }
      pos++;
      return result;
    }
    std::size_t start = pos;
    while (pos < text.size() && !jrt::Character::isWhitespace(static_cast<unsigned char>(text[pos])) &&
           text[pos] != '(' && text[pos] != ')') {
      pos++;
    }
    return text.substr(start, pos - start);
  }

  /** Skips the rest of the current list, its closing parenthesis included. */
  void skipToEndOfList() {
    int32_t depth = 1;
    while (pos < text.size() && depth > 0) {
      char c = text[pos];
      if (c == '"') {
        pos++;
        while (pos < text.size() && text[pos] != '"') {
          if (text[pos] == '\\') {
            pos++;
          }
          pos++;
        }
      } else if (c == '(') {
        depth++;
      } else if (c == ')') {
        depth--;
      }
      pos++;
    }
  }

 private:
  const std::string& text;
  std::size_t pos = 0;
};

/** A pad as written in its footprint: positions relative to it, in micrometres. */
class KiCadBoardZones_PadDraft {
 public:
  double at[3] = {0, 0, 0};
  double size[2] = {0, 0};
  double offset[2] = {0, 0};
  std::vector<std::string> layers;
  bool circular = false;
  bool throughHole = false;
  KiCadBoardZones_PadConnection connection = KiCadBoardZones_PadConnection::INHERITED;
  double thermalGap = 0;
  double thermalBridgeWidth = 0;
  double spokeAngle = 0;
};

namespace {

using Reader = KiCadBoardZones_Reader;
using PadDraft = KiCadBoardZones_PadDraft;
using PadConnection = KiCadBoardZones_PadConnection;
using Pad = KiCadBoardZones_Pad;
using Zone = KiCadBoardZones_Zone;

bool isNumber(const std::string& value) {
  try {
    jrt::Double::parseDouble(value);
    return true;
  } catch (const jrt::NumberFormatException& e) {
    return false;
  }
}

/** A number as written; 0 if it is not a number. */
double toNumber(const std::string& value) {
  try {
    return jrt::Double::parseDouble(value);
  } catch (const jrt::NumberFormatException& e) {
    return 0;
  }
}

/** A length written in millimetres as micrometres; 0 if it is not a number. */
double toUm(const std::string& millimetres) {
  try {
    return jrt::Double::parseDouble(millimetres) * 1000.0;
  } catch (const jrt::NumberFormatException& e) {
    return 0;
  }
}

/** Reads the numbers that follow, at most as many as the array holds. */
void readNumbers(Reader& reader, double* numbers, int32_t length) {
  for (int32_t i = 0; i < length; i++) {
    std::string value = reader.readAtom();
    if (value.empty() || !isNumber(value)) {
      return;
    }
    numbers[i] = jrt::Double::parseDouble(value);
  }
}

/**
 * Reads the rest of the layer table of the board, {@code (layers (0 F.Cu signal) (31 B.Cu signal)
 * (32 B.Adhes user) ...)}, for the names of its copper layers, from front to back.
 */
void readCopperLayers(Reader& reader, std::vector<std::string>& copperLayers) {
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished() || reader.leaveList()) {
      return;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    reader.readAtom();  // its number
    std::string name = reader.readAtom();
    std::string type = reader.readAtom();
    if (!name.empty() &&
        (type == "signal" || type == "power" || type == "mixed" || type == "jumper")) {
      copperLayers.push_back(name);
    }
    reader.skipToEndOfList();
  }
}

/**
 * The layers of a pad with the wildcards of KiCad replaced by the copper layers of the board they
 * stand for: {@code *.Cu} by all of them, {@code F&B.Cu} by the first and the last. Without a
 * layer table the layers are returned as written.
 */
jrt::List<std::string>* resolveLayers(const std::vector<std::string>& layers,
                                      const std::vector<std::string>& copperLayers) {
  auto* result = new (jrt::kBoard) jrt::ArrayList<std::string>();
  if (copperLayers.empty()) {
    for (const std::string& layer : layers) result->add(layer);
    return result;
  }
  for (const std::string& layer : layers) {
    if (layer == "*.Cu") {
      for (const std::string& copper : copperLayers) result->add(copper);
    } else if (layer == "F&B.Cu") {
      result->add(copperLayers.front());
      result->add(copperLayers.back());
    } else {
      result->add(layer);
    }
  }
  return result;
}

/** Reads the rest of {@code (drill [oval] 0.8 [1.2] (offset 0.1 0))} for the offset, in um. */
void readDrillOffset(Reader& reader, double* offset) {
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished() || reader.leaveList()) {
      return;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    if (reader.readAtom() == "offset") {
      readNumbers(reader, offset, 2);
      offset[0] *= 1000.0;
      offset[1] *= 1000.0;
    }
    reader.skipToEndOfList();
  }
}

/** Reads the rest of {@code (options (clearance outline) (anchor circle))}. */
bool readCircleAnchor(Reader& reader) {
  bool circle = false;
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished() || reader.leaveList()) {
      return circle;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    if (reader.readAtom() == "anchor") {
      circle = reader.readAtom() == "circle";
    }
    reader.skipToEndOfList();
  }
}

/** Reads the rest of {@code (pad "1" thru_hole circle (at ...) (size ...) ...)}. */
PadDraft readPad(Reader& reader, int32_t version) {
  PadDraft pad;
  reader.readAtom();  // its number, which can be empty
  pad.throughHole = reader.readAtom() == "thru_hole";
  std::string shape = reader.readAtom();
  bool circleAnchor = false;
  std::optional<double> spokeAngle;
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished()) {
      throw jrt::IllegalArgumentException("not a KiCad board file: unfinished pad");
    }
    if (reader.leaveList()) {
      break;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    std::string keyword = reader.readAtom();
    if (keyword == "at") {
      readNumbers(reader, pad.at, 3);
      pad.at[0] *= 1000.0;
      pad.at[1] *= 1000.0;
      reader.skipToEndOfList();
    } else if (keyword == "size") {
      readNumbers(reader, pad.size, 2);
      pad.size[0] *= 1000.0;
      pad.size[1] *= 1000.0;
      reader.skipToEndOfList();
    } else if (keyword == "drill") {
      readDrillOffset(reader, pad.offset);
    } else if (keyword == "layers") {
      for (std::string layer = reader.readAtom(); !layer.empty(); layer = reader.readAtom()) {
        pad.layers.push_back(layer);
      }
      reader.skipToEndOfList();
    } else if (keyword == "zone_connect") {
      pad.connection = KiCadBoardZones_PadConnection_fromKiCad(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "thermal_gap") {
      pad.thermalGap = toUm(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "thermal_bridge_width" || keyword == "thermal_width") {
      pad.thermalBridgeWidth = toUm(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "thermal_bridge_angle") {
      spokeAngle = toNumber(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "options") {
      circleAnchor = readCircleAnchor(reader);
    } else {
      reader.skipToEndOfList();
    }
  }
  pad.circular = shape == "circle" || (shape == "oval" && pad.size[0] == pad.size[1]);
  if (spokeAngle.has_value()) {
    pad.spokeAngle = *spokeAngle;
  } else if (shape == "circle" || (shape == "custom" && circleAnchor && version > 20211014)) {
    // the defaults of the board reader of KiCad
    pad.spokeAngle = 45;
  } else {
    pad.spokeAngle = 90;
  }
  return pad;
}

/** Reads the rest of a {@code (footprint ...)} and adds its pads. */
void readFootprint(Reader& reader, int32_t version, const std::vector<std::string>& copperLayers,
                   jrt::List<Pad*>* pads) {
  double at[3] = {0, 0, 0};
  PadConnection connection = PadConnection::INHERITED;
  std::vector<PadDraft> drafts;
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished()) {
      throw jrt::IllegalArgumentException("not a KiCad board file: unfinished footprint");
    }
    if (reader.leaveList()) {
      break;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    std::string keyword = reader.readAtom();
    if (keyword == "at") {
      readNumbers(reader, at, 3);
      at[0] *= 1000.0;
      at[1] *= 1000.0;
      reader.skipToEndOfList();
    } else if (keyword == "zone_connect") {
      connection = KiCadBoardZones_PadConnection_fromKiCad(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "pad") {
      drafts.push_back(readPad(reader, version));
    } else {
      reader.skipToEndOfList();
    }
  }
  // the position of the footprint can follow its pads in the file
  double cos = jrt::Math::cos(jrt::Math::toRadians(at[2]));
  double sin = jrt::Math::sin(jrt::Math::toRadians(at[2]));
  for (const PadDraft& draft : drafts) {
    // RotatePoint of KiCad: the y axis points down, a positive angle turns counter-clockwise
    double x = at[0] + draft.at[0] * cos + draft.at[1] * sin;
    double y = at[1] - draft.at[0] * sin + draft.at[1] * cos;
    double padCos = jrt::Math::cos(jrt::Math::toRadians(draft.at[2]));
    double padSin = jrt::Math::sin(jrt::Math::toRadians(draft.at[2]));
    double shapeX = x + draft.offset[0] * padCos + draft.offset[1] * padSin;
    double shapeY = y - draft.offset[0] * padSin + draft.offset[1] * padCos;
    pads->add(new (jrt::kBoard) Pad(
        x, y, shapeX, shapeY, draft.at[2], draft.size[0], draft.size[1], draft.circular,
        draft.throughHole, resolveLayers(draft.layers, copperLayers),
        draft.connection != PadConnection::INHERITED ? draft.connection : connection,
        draft.thermalGap, draft.thermalBridgeWidth, draft.spokeAngle));
  }
}

/**
 * Reads the rest of {@code (fill [yes] (thermal_gap 0.5) (thermal_bridge_width 0.5) (smoothing
 * fillet) (radius 1) ...)} into thermal: gap, spoke width, radius, smoothing (1 fillet, 2
 * chamfer).
 */
void readFill(Reader& reader, double* thermal) {
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished() || reader.leaveList()) {
      return;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    std::string keyword = reader.readAtom();
    if (keyword == "thermal_gap") {
      thermal[0] = toUm(reader.readAtom());
    } else if (keyword == "thermal_bridge_width") {
      thermal[1] = toUm(reader.readAtom());
    } else if (keyword == "radius") {
      thermal[2] = toUm(reader.readAtom());
    } else if (keyword == "smoothing") {
      std::string smoothing = reader.readAtom();
      thermal[3] = smoothing == "fillet" ? 1 : smoothing == "chamfer" ? 2 : 0;
    }
    reader.skipToEndOfList();
  }
}

/**
 * Reads the rest of {@code (connect_pads [yes|no|thru_hole_only] (clearance 0.5))}: returns the
 * clearance and sets the connection, a thermal relief when no keyword is written.
 */
double readConnectPads(Reader& reader, PadConnection* padConnection) {
  double clearance = 0;
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished() || reader.leaveList()) {
      return clearance;
    }
    if (!reader.enterList()) {
      std::string keyword = reader.readAtom();
      if (keyword == "yes") {
        padConnection[0] = PadConnection::SOLID;
      } else if (keyword == "no") {
        padConnection[0] = PadConnection::NONE;
      } else if (keyword == "thru_hole_only") {
        padConnection[0] = PadConnection::THT_THERMAL;
      }
      // an unknown keyword changes nothing
      continue;
    }
    if (reader.readAtom() == "clearance") {
      clearance = toUm(reader.readAtom());
    }
    reader.skipToEndOfList();
  }
}

/** Reads the rest of {@code (polygon (pts (xy 1 2) ...))} into the bounding box. */
void readOutline(Reader& reader, double* box) {
  int32_t depth = 1;
  while (depth > 0) {
    reader.skipBlanks();
    if (reader.isFinished()) {
      return;
    }
    if (reader.leaveList()) {
      depth--;
    } else if (reader.enterList()) {
      depth++;
      if (reader.readAtom() == "xy") {
        double x = toUm(reader.readAtom());
        double y = toUm(reader.readAtom());
        box[0] = jrt::Math::min(box[0], x);
        box[1] = jrt::Math::min(box[1], y);
        box[2] = jrt::Math::max(box[2], x);
        box[3] = jrt::Math::max(box[3], y);
      }
    } else {
      reader.readAtom();
    }
  }
}

/** Reads the rest of a {@code (zone ...)}; returns null for a rule area. */
Zone* readZone(Reader& reader) {
  std::string netName;
  auto* layers = new (jrt::kBoard) jrt::ArrayList<std::string>();
  double clearance = 0;
  double minThickness = 0;
  bool ruleArea = false;
  PadConnection padConnection[1] = {PadConnection::THERMAL};
  double thermal[4] = {0, 0, 0, 0};
  double box[4] = {jrt::Double::POSITIVE_INFINITY, jrt::Double::POSITIVE_INFINITY,
                   jrt::Double::NEGATIVE_INFINITY, jrt::Double::NEGATIVE_INFINITY};
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished()) {
      throw jrt::IllegalArgumentException("not a KiCad board file: unfinished zone");
    }
    if (reader.leaveList()) {
      break;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    std::string keyword = reader.readAtom();
    if (keyword == "net_name") {
      netName = reader.readAtom();
      reader.skipToEndOfList();
    } else if (keyword == "net") {
      // KiCad 10 writes the name here; older versions a number, with the name in net_name
      std::string value = reader.readAtom();
      if (netName.empty() && !isNumber(value)) {
        netName = value;
      }
      reader.skipToEndOfList();
    } else if (keyword == "layer" || keyword == "layers") {
      for (std::string layer = reader.readAtom(); !layer.empty(); layer = reader.readAtom()) {
        layers->add(layer);
      }
      reader.skipToEndOfList();
    } else if (keyword == "keepout") {
      ruleArea = true;
      reader.skipToEndOfList();
    } else if (keyword == "min_thickness") {
      minThickness = toUm(reader.readAtom());
      reader.skipToEndOfList();
    } else if (keyword == "connect_pads") {
      clearance = readConnectPads(reader, padConnection);
    } else if (keyword == "fill") {
      readFill(reader, thermal);
    } else if (keyword == "polygon") {
      readOutline(reader, box);
    } else {
      reader.skipToEndOfList();
    }
  }
  if (ruleArea || box[0] > box[2]) {
    return nullptr;
  }
  return new (jrt::kBoard) Zone(
      netName, layers, clearance, minThickness, box[0], box[1], box[2], box[3], padConnection[0],
      thermal[0], thermal[1],
      thermal[3] == 1   ? KiCadBoardZones_CornerSmoothing::FILLET
      : thermal[3] == 2 ? KiCadBoardZones_CornerSmoothing::CHAMFER
                        : KiCadBoardZones_CornerSmoothing::NONE,
      thermal[2]);
}

}  // namespace

KiCadBoardZones* KiCadBoardZones::read(const std::string& path) {
  return parse(FileText::readString(path), FileText::fileName(path));
}

KiCadBoardZones* KiCadBoardZones::parse(const std::string& text, const std::string& sourceName) {
  Reader reader(text);
  reader.skipBlanks();
  if (!reader.enterList() || reader.readAtom() != "kicad_pcb") {
    throw jrt::IllegalArgumentException("not a KiCad board file: no (kicad_pcb ...)");
  }
  auto* zones = new (jrt::kBoard) jrt::ArrayList<Zone*>();
  auto* pads = new (jrt::kBoard) jrt::ArrayList<Pad*>();
  std::vector<std::string> copperLayers;
  int32_t version = 0;
  while (true) {
    reader.skipBlanks();
    if (reader.isFinished()) {
      throw jrt::IllegalArgumentException("not a KiCad board file: missing ')'");
    }
    if (reader.leaveList()) {
      break;
    }
    if (!reader.enterList()) {
      reader.readAtom();
      continue;
    }
    std::string keyword = reader.readAtom();
    if (keyword == "zone") {
      Zone* zone = readZone(reader);
      if (zone != nullptr) {
        zones->add(zone);
      }
    } else if (keyword == "footprint" || keyword == "module") {
      readFootprint(reader, version, copperLayers, pads);
    } else if (keyword == "layers") {
      readCopperLayers(reader, copperLayers);
    } else if (keyword == "version") {
      version = jrt::d2i(toNumber(reader.readAtom()));
      reader.skipToEndOfList();
    } else {
      reader.skipToEndOfList();
    }
  }
  return new (jrt::kBoard) KiCadBoardZones(sourceName, zones, pads);
}

}  // namespace fr
