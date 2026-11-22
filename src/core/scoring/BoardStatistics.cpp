// Port of app.freerouting.core.scoring.BoardStatistics.
#include "core/scoring/BoardStatistics.h"

#include <cmath>
#include <vector>

#include "board/facade/BasicBoard.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/scoring/BoardStatisticsBoundsCalculator.h"
#include "core/scoring/BoardStatisticsRectangle.h"
#include "datastructures/UndoableObjects.h"
#include "drc/ClearanceBaseline.h"
#include "drc/ClearanceViolation.h"
#include "drc/DesignRulesChecker.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "io/FileFormat.h"
#include "rules/BoardRules.h"
#include "rules/NetClasses.h"
#include "rules/Nets.h"
#include "settings/OptimizerScoreSettings.h"
#include "settings/OptimizerScoringVersion.h"
#include "settings/RouterScoreSettings.h"
#include "settings/RouterScoringVersion.h"
#include "settings/RouterSettings.h"
#include "settings/RoutingCostSettings.h"
#include "settings/sources/DefaultSettings.h"
#include "util/TextManager.h"

namespace fr {

namespace {

// PORT NOTE: Constants.FREEROUTING_VERSION (build-generated in Java) -> the ported version
// (same value as in core/results/RoutingResultManifest.cpp).
const char* const FREEROUTING_VERSION = "2.5.1-SNAPSHOT";

/**
 * PORT NOTE: Java DoubleStream.sum() (JDK 18+) uses Kahan compensated summation
 * (Collectors.sumWithCompensation / computeFinalSum); reproduced here so that the trace length
 * total is bit-identical.
 */
class CompensatedDoubleSum {
 public:
  void add(double value) {
    // Collectors.sumWithCompensation
    double tmp = value - c_;
    double sum = sum_;
    double velvel = sum + tmp;  // Little wolf of rounding error
    c_ = (velvel - sum) - tmp;
    sum_ = velvel;
    simple_ += value;
  }
  double result() {
    // Collectors.computeFinalSum
    double tmp = sum_ - c_;
    if (std::isnan(tmp) && std::isinf(simple_)) {
      return simple_;
    }
    return tmp;
  }

 private:
  double sum_ = 0.0;
  double c_ = 0.0;
  double simple_ = 0.0;
};

/** Java String.split with a literal (non-regex-special) separator of length > 0. */
std::vector<std::string> splitLiteral(const std::string& s, const std::string& sep) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  std::size_t found = s.find(sep, pos);
  if (found == std::string::npos) {
    out.push_back(s);
    return out;
  }
  while (found != std::string::npos) {
    out.push_back(s.substr(pos, found - pos));
    pos = found + sep.size();
    found = s.find(sep, pos);
  }
  out.push_back(s.substr(pos));
  // Java String.split drops trailing empty strings.
  while (!out.empty() && out.back().empty()) out.pop_back();
  return out;
}

/**
 * PORT NOTE: minimal stand-in for GsonProvider.GSON.toJson (pretty printing, no HTML escaping,
 * nulls omitted, Float/Double written with String.format("%.2f") through BigDecimal, non-finite
 * values omitted). The "%.2f" rounding is C printf (exact binary value, half-even) instead of
 * Java's Formatter (HALF_UP on the shortest decimal representation); this only affects the
 * textual output.
 */
class JsonOut {
 public:
  std::string s;

  void beginObject() {
    s += "{";
    hasField_.push_back(false);
  }
  void endObject() {
    bool had = hasField_.back();
    hasField_.pop_back();
    if (had) {
      s += "\n" + std::string(2 * hasField_.size(), ' ');
    }
    s += "}";
  }
  void name(const std::string& key) {
    if (hasField_.back()) s += ",";
    hasField_.back() = true;
    s += "\n" + std::string(2 * hasField_.size(), ' ') + quote(key) + ": ";
  }
  void field(const std::string& key, const std::optional<int32_t>& v) {
    if (!v.has_value()) return;
    name(key);
    s += jrt::str(*v);
  }
  void field(const std::string& key, const std::optional<float>& v) {
    if (!v.has_value() || !std::isfinite(*v)) return;
    name(key);
    s += twoDecimals(static_cast<double>(*v));
  }
  void field(const std::string& key, const std::optional<double>& v) {
    if (!v.has_value() || !std::isfinite(*v)) return;
    name(key);
    s += twoDecimals(*v);
  }
  void field(const std::string& key, const std::optional<std::string>& v) {
    if (!v.has_value()) return;
    name(key);
    s += quote(*v);
  }

 private:
  std::vector<bool> hasField_;

  static std::string twoDecimals(double v) {
    std::string r = jrt::format("%.2f", v);
    if (r == "-0.00") r = "0.00";  // new BigDecimal("-0.00").toString() is "0.00"
    return r;
  }

  static std::string quote(const std::string& v) {
    std::string r = "\"";
    for (std::size_t i = 0; i < v.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(v[i]);
      switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\t': r += "\\t"; break;
        case '\b': r += "\\b"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\f': r += "\\f"; break;
        default:
          if (c < 0x20) {
            r += jrt::format("\\u%04x", c);
          } else if (c == 0xE2 && i + 2 < v.size() &&
                     static_cast<unsigned char>(v[i + 1]) == 0x80 &&
                     (static_cast<unsigned char>(v[i + 2]) == 0xA8 ||
                      static_cast<unsigned char>(v[i + 2]) == 0xA9)) {
            r += static_cast<unsigned char>(v[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
          } else {
            r += static_cast<char>(c);
          }
      }
    }
    r += "\"";
    return r;
  }
};

void writeRectangle(JsonOut& out, const std::string& key, BoardStatisticsRectangle* r) {
  if (r == nullptr) return;
  out.name(key);
  out.beginObject();
  out.field("x", std::optional<float>(r->x));
  out.field("y", std::optional<float>(r->y));
  out.field("width", std::optional<float>(r->width));
  out.field("height", std::optional<float>(r->height));
  out.endObject();
}

}  // namespace

BoardStatistics::BoardStatistics(BasicBoard* board) : BoardStatistics(board, std::nullopt, true) {}

BoardStatistics::BoardStatistics(BasicBoard* board, std::optional<Unit> unit)
    : BoardStatistics(board, unit, true) {}

BoardStatistics::BoardStatistics(BasicBoard* board, std::optional<Unit> unit,
                                 bool includeClearanceViolations)
    : BoardStatistics(board, unit, includeClearanceViolations, true) {}

BoardStatistics::BoardStatistics(BasicBoard* board, std::optional<Unit> unit,
                                 bool includeClearanceViolations, bool includeConnections) {
  if (board != nullptr && includeClearanceViolations) {
    board->awaitPostLoad();
  }
  IntBox* bb = board->getBoundingBox();

  this->host = jrt::str(board->communication->specctraParserInfo->hostCad) + "," +
               jrt::str(board->communication->specctraParserInfo->hostVersion);
  if ((!host.has_value()) || host->empty()) {
    this->host = std::string("Freerouting,") + FREEROUTING_VERSION;
  }
  this->host = TextManager::unescapeUnicode(*this->host);

  this->unit = Unit_toString(board->communication->unit);

  // Board
  this->board->boundingBox = new BoardStatisticsRectangle(
      static_cast<float>(bb->ll->x), static_cast<float>(bb->ll->y),
      static_cast<float>(bb->ur->x - bb->ll->x), static_cast<float>(bb->ur->y - bb->ll->y));
  this->board->size = new BoardStatisticsRectangle(
      0, 0,
      jrt::Math::abs(static_cast<float>(board->getBoundingBox()->ll->x) -
                     static_cast<float>(board->getBoundingBox()->ur->x)),
      jrt::Math::abs(static_cast<float>(board->getBoundingBox()->ll->y) -
                     static_cast<float>(board->getBoundingBox()->ur->y)));

  // Layers
  this->layers->totalCount = board->getLayerCount();
  this->layers->signalCount = board->layerStructure->signalLayerCount();

  // Items
  this->items->totalCount = 0;
  this->items->traceCount = 0;
  this->items->viaCount = 0;
  this->items->conductionAreaCount = 0;
  this->items->drillItemCount = 0;
  this->items->pinCount = 0;
  this->items->componentOutlineCount = 0;
  this->items->otherCount = 0;
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    (*this->items->totalCount)++;
    if (jrt::fast_cast<Trace>(currentItem) != nullptr) {
      (*this->items->traceCount)++;
    } else if (jrt::fast_cast<Via>(currentItem) != nullptr) {
      (*this->items->viaCount)++;
    } else if (jrt::fast_cast<ConductionArea>(currentItem) != nullptr) {
      (*this->items->conductionAreaCount)++;
    } else if (jrt::fast_cast<Pin>(currentItem) != nullptr) {
      (*this->items->pinCount)++;
    } else if (jrt::fast_cast<DrillItem>(currentItem) != nullptr) {
      (*this->items->drillItemCount)++;
    } else if (jrt::fast_cast<ComponentOutline>(currentItem) != nullptr) {
      (*this->items->componentOutlineCount)++;
    } else {
      (*this->items->otherCount)++;
    }
  }

  // Components
  this->components->totalCount = board->components->count();

  // Pads
  this->pads->totalCount = board->getPinCount();

  // Nets
  this->nets->totalCount = board->rules->nets->maxNetNumber();
  this->nets->classCount = board->rules->netClasses->count();

  // Traces
  this->traces->totalCount = board->getTraces()->size();
  {
    CompensatedDoubleSum lengthSum;
    for (Trace* trace : *board->getTraces()) {
      lengthSum.add(trace->getLength());
    }
    this->traces->totalLength = static_cast<float>(lengthSum.result());
  }
  // Normalise trace length to millimetres so that calculateScore() uses a physically meaningful
  // cost regardless of DSN coordinate resolution.
  double boardUnitToMmFactor =
      Unit_scale(1.0, board->communication->unit, Unit::MM) /
      (board->communication->resolution > 0 ? board->communication->resolution : 1);
  double boardUnitToUmFactor =
      Unit_scale(1.0, board->communication->unit, Unit::UM) /
      (board->communication->resolution > 0 ? board->communication->resolution : 1);
  this->board->areaCm2 = static_cast<float>(this->board->size->width * boardUnitToMmFactor *
                                            this->board->size->height * boardUnitToMmFactor /
                                            100.0);
  this->difficulty->pinCount = this->items->pinCount;
  this->difficulty->signalLayerCount = this->layers->signalCount;
  this->difficulty->complexityC = jrt::Math::max(
      1, this->difficulty->pinCount.value() * this->difficulty->signalLayerCount.value());
  this->difficulty->boardAreaCm2 = this->board->areaCm2;
  // D is the scoring size scale, not an ETA. It equals C so via/bend/DRC penalties stay
  // comparable across small and large boards.
  this->difficulty->difficultyD = static_cast<float>(this->difficulty->complexityC.value());
  this->bounds = BoardStatisticsBoundsCalculator::calculate(board);
  this->traces->totalLengthMm =
      static_cast<float>(this->traces->totalLength.value() * boardUnitToMmFactor);
  if (this->traces->totalCount.value() > 0) {
    this->traces->averageLength =
        this->traces->totalLength.value() / this->traces->totalCount.value();
  } else {
    this->traces->averageLength = 0.0f;
  }
  this->traces->totalSegmentCount = 0;
  this->traces->totalHorizontalLength = 0.0f;
  this->traces->totalVerticalLength = 0.0f;
  this->traces->totalAngledLength = 0.0f;
  for (Trace* trace : *board->getTraces()) {
    // Calculate segments for this trace
    if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
      Polyline* polyline = polylineTrace->polyline();
      int32_t cornerCount = polyline->cornerCount();
      // Number of segments is cornerCount - 1
      if (cornerCount > 1) {
        *this->traces->totalSegmentCount += cornerCount - 1;
      }

      for (Line* line : *polyline->lines) {
        FloatPoint* a = line->a->toFloat();
        FloatPoint* b = line->b->toFloat();
        float length = static_cast<float>(
            jrt::Math::sqrt(jrt::Math::pow(a->x - b->x, 2) + jrt::Math::pow(a->y - b->y, 2)));

        if (a->x == b->x) {
          *this->traces->totalVerticalLength += length;
        } else if (a->y == b->y) {
          *this->traces->totalHorizontalLength += length;
        } else {
          *this->traces->totalAngledLength += length;
        }
      }
    }
  }

  this->traces->totalWeightedLength = 0.0f;
  int32_t defaultClearanceClass = BoardRules::defaultClearanceClass();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it2 = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(it2);
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
      FixedState fixedState = currentTrace->getFixedState();
      if (fixedState == FixedState::UNFIXED || fixedState == FixedState::SHOVE_FIXED) {
        double weightedTraceLength =
            currentTrace->getLength() *
            (currentTrace->getHalfWidth() +
             board->clearanceValue(currentTrace->clearanceClassIndex(), defaultClearanceClass,
                                   currentTrace->getLayer()));
        if (fixedState == FixedState::SHOVE_FIXED) {
          // to produce less violations with pin exit directions.
          weightedTraceLength /= 2;
        }
        *this->traces->totalWeightedLength += static_cast<float>(weightedTraceLength);
      }
    }
  }

  // Connections
  if (includeConnections) {
    NetRoutingLedger* ledger = board->routingLedger();
    this->connections->maximumCount = ledger->maximumConnections();
    this->connections->incompleteCount = ledger->incompleteCount();
  }

  // Bends
  this->bends->totalCount = 0;
  this->bends->ninetyDegreeCount = 0;
  this->bends->fortyFiveDegreeCount = 0;
  this->bends->otherAngleCount = 0;
  for (Trace* trace : *board->getTraces()) {
    if (auto* polylineTrace = jrt::fast_cast<PolylineTrace>(trace)) {
      // Polyline traces can have bends between consecutive line segments
      Polyline* polyline = polylineTrace->polyline();
      int32_t cornerCount = polyline->cornerCount();

      // We have (cornerCount - 2) internal corners, each representing a potential bend
      if (cornerCount >= 3) {
        // Count all internal corners as bends
        int32_t bendsInTrace = cornerCount - 2;
        *this->bends->totalCount += bendsInTrace;

        // Now classify each bend by angle
        for (int32_t i = 1; i < cornerCount - 1; i++) {
          FloatPoint* prev = polyline->corner(i - 1)->toFloat();
          FloatPoint* current = polyline->corner(i)->toFloat();
          FloatPoint* next = polyline->corner(i + 1)->toFloat();

          // Calculate vectors for the two segments
          double dx1 = current->x - prev->x;
          double dy1 = current->y - prev->y;
          double dx2 = next->x - current->x;
          double dy2 = next->y - current->y;

          // Calculate the angle between the two segments
          double angle = jrt::Math::abs(
              jrt::Math::toDegrees(jrt::Math::atan2(dy2, dx2) - jrt::Math::atan2(dy1, dx1)));
          // Normalize the angle to [0, 180]
          angle = jrt::Math::min(angle, 360 - angle);
          angle = angle > 180 ? 360 - angle : angle;

          // Classify the bend - use a small tolerance for comparison
          if (jrt::Math::abs(angle - 90) < 1) {
            (*this->bends->ninetyDegreeCount)++;
          } else if (jrt::Math::abs(angle - 45) < 1 || jrt::Math::abs(angle - 135) < 1) {
            (*this->bends->fortyFiveDegreeCount)++;
          } else {
            (*this->bends->otherAngleCount)++;
          }
        }
      }
    }
  }

  // Vias
  this->vias->totalCount = board->getViaCount();
  this->vias->throughHoleCount = 0;
  this->vias->blindCount = 0;
  this->vias->buriedCount = 0;
  for (Via* via : *board->getVias()) {
    if ((via->firstLayer() == 0) && (via->lastLayer() == this->layers->totalCount.value() - 1)) {
      (*this->vias->throughHoleCount)++;
    } else if ((via->firstLayer() == 0) ||
               (via->lastLayer() == this->layers->totalCount.value() - 1)) {
      (*this->vias->blindCount)++;
    } else {
      (*this->vias->buriedCount)++;
    }
  }

  if (includeClearanceViolations) {
    DesignRulesChecker* clearanceDrc = new DesignRulesChecker(board, nullptr);
    jrt::Collection<ClearanceViolation*>* violationsList =
        clearanceDrc->getAllClearanceViolations();
    this->clearanceViolations->totalCount = violationsList->size();
    if (!violationsList->isEmpty()) {
      double minViolation = jrt::Double::MAX_VALUE;
      double maxViolation = 0.0;
      double sumViolation = 0.0;
      for (ClearanceViolation* cv : *violationsList) {
        double shortfall = shortfallUm(cv, boardUnitToUmFactor);
        minViolation = jrt::Math::min(minViolation, shortfall);
        maxViolation = jrt::Math::max(maxViolation, shortfall);
        sumViolation += shortfall;
      }
      this->clearanceViolations->totalViolationUm = sumViolation;
      this->clearanceViolations->minViolationUm = minViolation;
      this->clearanceViolations->maxViolationUm = maxViolation;
      this->clearanceViolations->avgViolationUm = sumViolation / violationsList->size();
    } else {
      this->clearanceViolations->totalViolationUm = 0.0;
      this->clearanceViolations->minViolationUm = 0.0;
      this->clearanceViolations->maxViolationUm = 0.0;
      this->clearanceViolations->avgViolationUm = 0.0;
    }
    this->clearanceViolations->preExistingCount = board->preExistingClearanceViolationsCount;
    int32_t unfixable = 0;
    double fixableViolationUm = 0.0;
    for (ClearanceViolation* cv : *violationsList) {
      if (cv->isUnfixable()) {
        unfixable++;
      } else {
        fixableViolationUm += shortfallUm(cv, boardUnitToUmFactor);
      }
    }
    this->clearanceViolations->unfixableCount = unfixable;
    this->clearanceViolations->fixableViolationUm = fixableViolationUm;
    this->clearanceViolations->newOrChangedCount =
        board->clearanceBaseline != nullptr
            ? std::optional<int32_t>(board->clearanceBaseline->countNewOrChanged(violationsList))
            : std::nullopt;
    this->clearanceViolations->routerIntroducedCount =
        jrt::Math::max(0, this->clearanceViolations->totalCount.value() -
                              board->preExistingClearanceViolationsCount);
  } else {
    this->clearanceViolations->totalCount = 0;
    this->clearanceViolations->preExistingCount = 0;
    this->clearanceViolations->unfixableCount = 0;
    this->clearanceViolations->newOrChangedCount = 0;
    this->clearanceViolations->routerIntroducedCount = 0;
    this->clearanceViolations->totalViolationUm = 0.0;
    this->clearanceViolations->fixableViolationUm = 0.0;
    this->clearanceViolations->minViolationUm = 0.0;
    this->clearanceViolations->maxViolationUm = 0.0;
    this->clearanceViolations->avgViolationUm = 0.0;
  }

  // Convert all length values from board.communication.unit to the preferred unit
  if (!unit.has_value()) {
    unit = Unit::MM;
  }

  if (*unit != board->communication->unit) {
    // convert all length values to the preferred unit
    Unit fromUnit = board->communication->unit;
    Unit toUnit = *unit;
    this->unit = Unit_toString(*unit);

    // Board
    this->board->boundingBox = new BoardStatisticsRectangle(
        static_cast<float>(Unit_scale(this->board->boundingBox->x, fromUnit, toUnit)),
        static_cast<float>(Unit_scale(this->board->boundingBox->y, fromUnit, toUnit)),
        static_cast<float>(Unit_scale(this->board->boundingBox->width, fromUnit, toUnit)),
        static_cast<float>(Unit_scale(this->board->boundingBox->height, fromUnit, toUnit)));
    this->board->size = new BoardStatisticsRectangle(
        0, 0, static_cast<float>(Unit_scale(this->board->size->width, fromUnit, toUnit)),
        static_cast<float>(Unit_scale(this->board->size->height, fromUnit, toUnit)));

    // Traces
    this->traces->totalLength =
        static_cast<float>(Unit_scale(this->traces->totalLength.value(), fromUnit, toUnit));
    this->traces->totalWeightedLength = static_cast<float>(
        Unit_scale(this->traces->totalWeightedLength.value(), fromUnit, toUnit));
    this->traces->averageLength =
        static_cast<float>(Unit_scale(this->traces->averageLength.value(), fromUnit, toUnit));
    this->traces->totalHorizontalLength = static_cast<float>(
        Unit_scale(this->traces->totalHorizontalLength.value(), fromUnit, toUnit));
    this->traces->totalVerticalLength = static_cast<float>(
        Unit_scale(this->traces->totalVerticalLength.value(), fromUnit, toUnit));
    this->traces->totalAngledLength = static_cast<float>(
        Unit_scale(this->traces->totalAngledLength.value(), fromUnit, toUnit));
  }

  // Calculate fanout statistics
  jrt::Collection<Pin*>* smdPins = board->getSmdPins();
  int32_t total = 0;
  int32_t escaped = 0;
  int32_t alreadyConnected = 0;
  // The pins of a net walk the same items: query the contacts of each item, and the connectable
  // items of each net, once for all pins.
  jrt::Map<Item*, jrt::Collection<Item*>*>* contacts =
      new jrt::HashMap<Item*, jrt::Collection<Item*>*>();
  jrt::Map<int32_t, jrt::Collection<Item*>*>* connectableItems =
      new jrt::HashMap<int32_t, jrt::Collection<Item*>*>();
  for (Pin* pin : *smdPins) {
    if (pin->netCount() > 0) {
      total++;
      int32_t netNumber = pin->getNetNumber(0);
      bool connected = netNumber > 0
                           ? isConnectedToWholeNet(board, pin, netNumber, contacts, connectableItems)
                           : pin->getUnconnectedSet(netNumber)->isEmpty();
      if (connected) {
        alreadyConnected++;
      }
      if (isPinEscaped(pin)) {
        escaped++;
      }
    }
  }
  this->fanout->totalSmdPins = total;
  this->fanout->pinsToEscape = total - alreadyConnected;
  this->fanout->escapedCount = escaped;
}

BoardStatistics::BoardStatistics(jrt::Array<int8_t>* data, std::optional<io::FileFormat> format) {
  if (data == nullptr || !format.has_value()) {
    return;
  }
  // set the statistical data based on the file content
  if (*format == io::FileFormat::SES) {
    // read the content as text
    // PORT NOTE: the bytes are kept as UTF-8 (no UTF-16 decoding); only substring counts and
    // positions relative to the same text are used.
    std::string content(reinterpret_cast<const char*>(data->begin()),
                        static_cast<std::size_t>(data->length));

    // to get the affected layers, we need to all "(path {layer}" occurrences, and count the
    // different layers
    std::vector<std::string> lines = splitLiteral(content, "(path ");

    // create a list to store the layer names
    jrt::List<std::string>* layers = new jrt::ArrayList<std::string>();
    // iterate over the lines
    for (int32_t i = 0; i < static_cast<int32_t>(lines.size()); i++) {
      std::string line = lines[i];
      std::vector<std::string> words = splitLiteral(line, " ");

      if ((i > 0) && (words.size() >= 2)) {
        // get the layer name
        std::string layer = words[0];
        // add the layer name to the list
        if (!layers->contains(layer)) {
          layers->add(layer);
        }
      }
    }

    // get the number of components and nets in the SES file
    this->layers->totalCount = layers->size();
    this->components->totalCount = countOccurrences(content, "(component");
    this->nets->totalCount = countOccurrences(content, "(net");
    this->traces->totalCount = countOccurrences(content, "(wire");
    this->vias->totalCount = countOccurrences(content, "(via");
  } else if (*format == io::FileFormat::DSN) {
    // read the content as text
    std::string content(reinterpret_cast<const char*>(data->begin()),
                        static_cast<std::size_t>(data->length));
    // extract the host from the DSN file without splitting the whole content by lines
    std::optional<std::string> hostCad;
    std::optional<std::string> hostVersion;
    int32_t parserIndex = jrt::indexOf(content, "(parser");
    if (parserIndex != -1) {
      int32_t searchLimit = jrt::indexOf(content, ")", parserIndex);
      if (searchLimit == -1) {
        searchLimit =
            jrt::Math::min(static_cast<int32_t>(content.size()), parserIndex + 1000);
      } else {
        searchLimit = jrt::Math::min(static_cast<int32_t>(content.size()), searchLimit + 1);
      }
      std::string parserScope = jrt::substring(content, parserIndex, searchLimit);
      int32_t hcIdx = jrt::indexOf(parserScope, "(hostCad");
      if (hcIdx != -1) {
        int32_t hcEnd = jrt::indexOf(parserScope, ")", hcIdx);
        if (hcEnd != -1) {
          std::string val = jrt::trim(jrt::substring(parserScope, hcIdx + 9, hcEnd));
          hostCad = TextManager::removeQuotes(val);
        }
      }
      int32_t hvIdx = jrt::indexOf(parserScope, "(hostVersion");
      if (hvIdx != -1) {
        int32_t hvEnd = jrt::indexOf(parserScope, ")", hvIdx);
        if (hvEnd != -1) {
          std::string val = jrt::trim(jrt::substring(parserScope, hvIdx + 13, hvEnd));
          hostVersion = TextManager::removeQuotes(val);
        }
      }
    }

    if (hostCad.has_value() && hostVersion.has_value()) {
      this->host = *hostCad + "," + *hostVersion;
    } else if (hostCad.has_value()) {
      this->host = hostCad;
    }

    // get the number of layers and nets in the DSN file
    this->layers->totalCount = countOccurrences(content, "(layer");
    this->components->totalCount = countOccurrences(content, "(component");
    this->nets->classCount = countOccurrences(content, "(class");
    this->nets->totalCount = countOccurrences(content, "(net");
    this->traces->totalCount = countOccurrences(content, "(wire");
    this->vias->totalCount = countOccurrences(content, "(via");
  }
  // PORT NOTE: the KICAD_DESIGN_JSON branch (gson JSON parsing) is not ported: KiCad input is
  // treated as absent, so the statistics stay empty for that format.
}

bool BoardStatistics::isConnectedToWholeNet(
    BasicBoard* board, Pin* pin, int32_t netNumber,
    jrt::Map<Item*, jrt::Collection<Item*>*>* contacts,
    jrt::Map<int32_t, jrt::Collection<Item*>*>* connectableItems) {
  if (!pin->containsNet(netNumber)) {
    return true;
  }
  jrt::Set<Item*>* connected = new jrt::TreeSet<Item*>();
  connected->add(pin);
  jrt::ArrayDeque<Item*>* toExpand = new jrt::ArrayDeque<Item*>();
  toExpand->push(pin);
  while (!toExpand->isEmpty()) {
    jrt::Collection<Item*>* itemContacts = contacts->computeIfAbsent(
        toExpand->pop(), [](Item* item) -> jrt::Collection<Item*>* {
          return item->getNormalContacts();
        });
    if (itemContacts == nullptr) {
      continue;
    }
    for (Item* contact : *itemContacts) {
      if (contact->containsNet(netNumber) && connected->add(contact)) {
        toExpand->push(contact);
      }
    }
  }
  return connected->containsAll(connectableItems->computeIfAbsent(
      netNumber, [board](int32_t n) { return board->getConnectableItems(n); }));
}

bool BoardStatistics::isPinEscaped(Pin* pin) {
  jrt::Set<Item*>* contacts = pin->getNormalContacts();
  for (Item* contact : *contacts) {
    if (auto* trace = jrt::fast_cast<Trace>(contact)) {
      if (trace->clearanceViolations()->isEmpty()) {
        return true;
      }
    } else if (auto* via = jrt::fast_cast<Via>(contact)) {
      if (via->clearanceViolations()->isEmpty()) {
        jrt::Set<Item*>* viaContacts = via->getNormalContacts();
        for (Item* viaContact : *viaContacts) {
          if (jrt::fast_cast<Trace>(viaContact) != nullptr ||
              jrt::fast_cast<ConductionArea>(viaContact) != nullptr) {
            return true;
          }
        }
      }
    } else if (auto* ca = jrt::fast_cast<ConductionArea>(contact)) {
      if (!ca->isFractured()) {
        return true;
      }
    }
  }
  return false;
}

double BoardStatistics::shortfallUm(ClearanceViolation* violation, double boardUnitToUmFactor) {
  return jrt::Math::max(0.0, violation->expectedClearance - violation->actualClearance) *
         boardUnitToUmFactor;
}

int32_t BoardStatistics::countOccurrences(const std::string& text, const std::string& target) {
  int32_t count = 0;
  int32_t index = 0;
  while ((index = jrt::indexOf(text, target, index)) != -1) {
    count++;
    index += static_cast<int32_t>(target.size());
  }
  return count;
}

std::string BoardStatistics::toString() {
  // PORT NOTE: GsonProvider.GSON.toJson(this) written by hand (see JsonOut), same field names
  // and order as the @SerializedName annotations.
  JsonOut out;
  out.beginObject();
  out.field("host", host);
  out.field("unit", unit);
  if (board != nullptr) {
    out.name("board");
    out.beginObject();
    writeRectangle(out, "bounding_box", board->boundingBox);
    writeRectangle(out, "size", board->size);
    out.field("area_cm2", board->areaCm2);
    out.endObject();
  }
  if (layers != nullptr) {
    out.name("layers");
    out.beginObject();
    out.field("total_count", layers->totalCount);
    out.field("signal_count", layers->signalCount);
    out.endObject();
  }
  if (items != nullptr) {
    out.name("items");
    out.beginObject();
    out.field("total_count", items->totalCount);
    out.field("trace_count", items->traceCount);
    out.field("via_count", items->viaCount);
    out.field("conduction_area_count", items->conductionAreaCount);
    out.field("drill_item_count", items->drillItemCount);
    out.field("pin_count", items->pinCount);
    out.field("component_count", items->componentOutlineCount);
    out.field("other_count", items->otherCount);
    out.endObject();
  }
  if (components != nullptr) {
    out.name("components");
    out.beginObject();
    out.field("total_count", components->totalCount);
    out.endObject();
  }
  if (pads != nullptr) {
    out.name("pads");
    out.beginObject();
    out.field("total_count", pads->totalCount);
    out.endObject();
  }
  if (nets != nullptr) {
    out.name("nets");
    out.beginObject();
    out.field("total_count", nets->totalCount);
    out.field("class_count", nets->classCount);
    out.endObject();
  }
  if (connections != nullptr) {
    out.name("connections");
    out.beginObject();
    out.field("maximum_count", connections->maximumCount);
    out.field("incomplete_count", connections->incompleteCount);
    out.field("design_blocked_count", connections->designBlockedCount);
    out.endObject();
  }
  if (traces != nullptr) {
    out.name("traces");
    out.beginObject();
    out.field("total_count", traces->totalCount);
    out.field("total_segment_count", traces->totalSegmentCount);
    out.field("total_length", traces->totalLength);
    out.field("total_length_mm", traces->totalLengthMm);
    out.field("total_weighted_length", traces->totalWeightedLength);
    out.field("average_length", traces->averageLength);
    out.field("total_vertical_length", traces->totalVerticalLength);
    out.field("total_horizontal_length", traces->totalHorizontalLength);
    out.field("total_angled_length", traces->totalAngledLength);
    out.endObject();
  }
  if (bends != nullptr) {
    out.name("bends");
    out.beginObject();
    out.field("total_count", bends->totalCount);
    out.field("90_degree_count", bends->ninetyDegreeCount);
    out.field("45_degree_count", bends->fortyFiveDegreeCount);
    out.field("other_angle_count", bends->otherAngleCount);
    out.endObject();
  }
  if (vias != nullptr) {
    out.name("vias");
    out.beginObject();
    out.field("total_count", vias->totalCount);
    out.field("through_hole_count", vias->throughHoleCount);
    out.field("blind_count", vias->blindCount);
    out.field("buried_count", vias->buriedCount);
    out.endObject();
  }
  if (clearanceViolations != nullptr) {
    out.name("clearance_violations");
    out.beginObject();
    out.field("total_count", clearanceViolations->totalCount);
    out.field("pre_existing_count", clearanceViolations->preExistingCount);
    out.field("unfixable_count", clearanceViolations->unfixableCount);
    out.field("router_introduced_count", clearanceViolations->routerIntroducedCount);
    out.field("new_or_changed_count", clearanceViolations->newOrChangedCount);
    out.field("total_violation_um", clearanceViolations->totalViolationUm);
    out.field("fixable_violation_um", clearanceViolations->fixableViolationUm);
    out.field("min_violation_um", clearanceViolations->minViolationUm);
    out.field("max_violation_um", clearanceViolations->maxViolationUm);
    out.field("avg_violation_um", clearanceViolations->avgViolationUm);
    out.endObject();
  }
  if (difficulty != nullptr) {
    out.name("difficulty");
    out.beginObject();
    out.field("pin_count", difficulty->pinCount);
    out.field("signal_layer_count", difficulty->signalLayerCount);
    out.field("complexity_c", difficulty->complexityC);
    out.field("difficulty_d", difficulty->difficultyD);
    out.field("board_area_cm2", difficulty->boardAreaCm2);
    out.endObject();
  }
  if (bounds != nullptr) {
    out.name("bounds");
    out.beginObject();
    out.field("min_trace_length_mm", bounds->minTraceLengthMm);
    out.field("min_via_count", bounds->minViaCount);
    out.field("min_bend_count", bounds->minBendCount);
    out.endObject();
  }
  if (fanout != nullptr) {
    out.name("fanout");
    out.beginObject();
    out.field("total_smd_pins", std::optional<int32_t>(fanout->totalSmdPins));
    out.field("pins_to_escape", std::optional<int32_t>(fanout->pinsToEscape));
    out.field("escaped_count", std::optional<int32_t>(fanout->escapedCount));
    out.endObject();
  }
  out.endObject();
  return out.s;
}

float BoardStatistics::calculateScore(RoutingCostSettings* scoringSettings) {
  float maximumScore = getMaximumScore(scoringSettings);
  // PORT NOTE: Java unboxes Integer/Float here (NPE on null); .value() throws instead.
  float penalties =
      this->connections->incompleteCount.value() * scoringSettings->unroutedNetPenalty.value() +
      this->clearanceViolations->fixableCount() *
          scoringSettings->clearanceViolationPenalty.value() +
      this->bends->totalCount.value() * scoringSettings->bendPenalty.value();
  // Use the mm-normalised trace length so that the trace-cost term is comparable to the
  // unroutedNetPenalty regardless of the DSN internal coordinate resolution.
  float traceLengthForCost = this->traces->totalLengthMm.has_value()
                                 ? *this->traces->totalLengthMm
                                 : this->traces->totalLength.value();
  float costs = static_cast<float>(
      traceLengthForCost * scoringSettings->defaultPreferredDirectionTraceCost.value() +
      this->vias->totalCount.value() * scoringSettings->viaCosts.value());

  return maximumScore - penalties - costs;
}

float BoardStatistics::getMaximumScore(RoutingCostSettings* scoringSettings) {
  return this->connections->maximumCount.value() * scoringSettings->unroutedNetPenalty.value();
}

float BoardStatistics::getLegacyNormalizedScore(RoutingCostSettings* scoringSettings) {
  float maximumScore = getMaximumScore(scoringSettings);
  if (maximumScore <= 0.0f) {
    // Guard against division by zero and negative maximum scores; prevents NaN propagation.
    return 0.0f;
  }
  return jrt::Math::max(0.0f, calculateScore(scoringSettings) / maximumScore) * 1000;
}

float BoardStatistics::getRouterScore(RoutingCostSettings* scoringSettings) {
  return getLegacyNormalizedScore(scoringSettings);
}

float BoardStatistics::getRouterScore(RouterSettings* routerSettings) {
  if (routerSettings == nullptr || routerSettings->routerScoring == nullptr ||
      routerSettings->routerScoring->version != RouterScoringVersion::V2_CONTINUOUS) {
    return getLegacyNormalizedScore(legacyScoringOrDefault(routerSettings));
  }
  return getV2RouterScore(routerSettings->routerScoring);
}

float BoardStatistics::getV2RouterScore(RouterScoreSettings* settings) {
  ensureDifficulty();
  double difficulty = this->difficulty->difficultyD.has_value()
                          ? jrt::Math::max(1.0, static_cast<double>(*this->difficulty->difficultyD))
                          : 1.0;
  double connections = this->connections->maximumCount.has_value()
                           ? jrt::Math::max(0, *this->connections->maximumCount)
                           : 0;
  double incomplete = this->connections->incompleteCount.has_value()
                          ? jrt::Math::max(0, *this->connections->incompleteCount)
                          : 0;
  double violationCount = this->clearanceViolations->fixableCount();
  double violationDepth = this->clearanceViolations->fixableShortfallUm();
  double split = jrt::Math::min(
      1.0, jrt::Math::max(0.0, static_cast<double>(
                                   valueOrDefault(settings->unroutedFreeFraction, 0.5f))));
  double firstHalfWeight = valueOrDefault(settings->unroutedFirstHalfWeight, 1000.0f / 3.0f);
  double secondHalfWeight = valueOrDefault(settings->unroutedSecondHalfWeight, 2000.0f / 3.0f);
  double openFraction = connections > 0 ? incomplete / connections : 0.0;
  double firstHalfOpen;
  double secondHalfOpen;
  if (connections <= 0) {
    firstHalfOpen = 0.0;
    secondHalfOpen = 0.0;
  } else if (split <= 0.0) {
    firstHalfOpen = 0.0;
    secondHalfOpen = openFraction;
  } else if (split >= 1.0) {
    firstHalfOpen = openFraction;
    secondHalfOpen = 0.0;
  } else {
    firstHalfOpen =
        jrt::Math::min(1.0, jrt::Math::max(0.0, (openFraction - split) / (1.0 - split)));
    secondHalfOpen = jrt::Math::min(1.0, openFraction / split);
  }
  double unroutedPenalty = firstHalfWeight * firstHalfOpen + secondHalfWeight * secondHalfOpen;
  double drcPenalty = valueOrDefault(settings->clearanceViolationCountWeight, 25.0f) *
                      violationCount / difficulty;
  double depthScale = jrt::Math::max(
      1.0, static_cast<double>(valueOrDefault(settings->clearanceViolationDepthScale, 1000.0f)));
  drcPenalty += valueOrDefault(settings->clearanceViolationDepthWeight, 300.0f) * violationDepth /
                depthScale / difficulty;
  return static_cast<float>(jrt::Math::max(0.0, 1000.0 - unroutedPenalty - drcPenalty));
}

void BoardStatistics::ensureDifficulty() {
  if (this->difficulty == nullptr) {
    this->difficulty = new BoardStatisticsDifficulty();
  }
  if (this->difficulty->difficultyD.has_value()) {
    return;
  }
  if (!this->difficulty->pinCount.has_value() || *this->difficulty->pinCount <= 0) {
    if (this->items != nullptr && this->items->pinCount.has_value() &&
        *this->items->pinCount > 0) {
      this->difficulty->pinCount = this->items->pinCount;
    } else if (this->pads != nullptr && this->pads->totalCount.has_value() &&
               *this->pads->totalCount > 0) {
      this->difficulty->pinCount = this->pads->totalCount;
    } else {
      this->difficulty->pinCount = 0;
    }
  }
  if (!this->difficulty->signalLayerCount.has_value() ||
      *this->difficulty->signalLayerCount <= 0) {
    if (this->layers != nullptr && this->layers->signalCount.has_value() &&
        *this->layers->signalCount > 0) {
      this->difficulty->signalLayerCount = this->layers->signalCount;
    } else if (this->layers != nullptr && this->layers->totalCount.has_value() &&
               *this->layers->totalCount > 0) {
      this->difficulty->signalLayerCount = this->layers->totalCount;
    } else {
      this->difficulty->signalLayerCount = 0;
    }
  }
  if (!this->difficulty->complexityC.has_value() || *this->difficulty->complexityC <= 0) {
    int32_t pins = this->difficulty->pinCount.has_value() ? *this->difficulty->pinCount : 0;
    int32_t layers =
        this->difficulty->signalLayerCount.has_value() ? *this->difficulty->signalLayerCount : 0;
    this->difficulty->complexityC = jrt::Math::max(1, pins * layers);
  }
  if (!this->difficulty->difficultyD.has_value()) {
    this->difficulty->difficultyD = static_cast<float>(*this->difficulty->complexityC);
  }
}

RoutingCostSettings* BoardStatistics::legacyScoringOrDefault(RouterSettings* routerSettings) {
  if (routerSettings != nullptr && routerSettings->scoring != nullptr) {
    return routerSettings->scoring;
  }
  return (new DefaultSettings())->getSettings()->scoring;
}

float BoardStatistics::getOptimizerScore(RoutingCostSettings* scoringSettings) {
  return getLegacyNormalizedScore(scoringSettings);
}

float BoardStatistics::getOptimizerScore(RouterSettings* routerSettings) {
  if (routerSettings != nullptr && routerSettings->optimizerScoring != nullptr &&
      routerSettings->optimizerScoring->version == OptimizerScoringVersion::V2_LOWER_BOUND) {
    return getV2OptimizerScore(routerSettings->optimizerScoring);
  }
  return getLegacyNormalizedScore(legacyScoringOrDefault(routerSettings));
}

float BoardStatistics::getOptimizerRank(RouterSettings* routerSettings) {
  if (routerSettings != nullptr && routerSettings->optimizerScoring != nullptr &&
      routerSettings->optimizerScoring->version == OptimizerScoringVersion::V2_LOWER_BOUND) {
    return static_cast<float>(v2OptimizerScoreUnbounded(routerSettings->optimizerScoring));
  }
  return getOptimizerScore(routerSettings);
}

float BoardStatistics::getV2OptimizerScore(OptimizerScoreSettings* settings) {
  return static_cast<float>(jrt::Math::max(0.0, v2OptimizerScoreUnbounded(settings)));
}

double BoardStatistics::v2OptimizerScoreUnbounded(OptimizerScoreSettings* settings) {
  double difficulty = this->difficulty->difficultyD.has_value()
                          ? jrt::Math::max(1.0, static_cast<double>(*this->difficulty->difficultyD))
                          : 1.0;
  double minTraceLength =
      this->bounds->minTraceLengthMm.has_value()
          ? jrt::Math::max(0.0, static_cast<double>(*this->bounds->minTraceLengthMm))
          : 0.0;
  double minViaCount =
      this->bounds->minViaCount.has_value() ? jrt::Math::max(0, *this->bounds->minViaCount) : 0;
  double minBendCount =
      this->bounds->minBendCount.has_value() ? jrt::Math::max(0, *this->bounds->minBendCount) : 0;
  double actualTraceLength =
      this->traces->totalLengthMm.has_value()
          ? jrt::Math::max(0.0, static_cast<double>(*this->traces->totalLengthMm))
          : 0.0;
  double actualViaCount =
      this->vias->totalCount.has_value() ? jrt::Math::max(0, *this->vias->totalCount) : 0;
  double actualBendCount =
      this->bends->totalCount.has_value() ? jrt::Math::max(0, *this->bends->totalCount) : 0;
  double lengthFloor =
      jrt::Math::max(0.0, static_cast<double>(valueOrDefault(settings->lengthFloor, 1.0f)));
  double difficultyFloor = jrt::Math::max(
      1.0, static_cast<double>(valueOrDefault(settings->difficultyScaleFloor, 1.0f)));
  double lengthPenalty = valueOrDefault(settings->excessWireLengthWeight, 1000.0f) *
                         jrt::Math::max(0.0, actualTraceLength - minTraceLength) /
                         jrt::Math::max(minTraceLength, lengthFloor);
  double viaPenalty = valueOrDefault(settings->excessViaWeight, 2000.0f) *
                      jrt::Math::max(0.0, actualViaCount - minViaCount) /
                      jrt::Math::max(difficulty, difficultyFloor);
  double bendPenalty = valueOrDefault(settings->excessBendWeight, 500.0f) *
                       jrt::Math::max(0.0, actualBendCount - minBendCount) /
                       jrt::Math::max(difficulty, difficultyFloor);
  return 1000.0 - lengthPenalty - viaPenalty - bendPenalty;
}

float BoardStatistics::getNormalizedScore(RoutingCostSettings* scoringSettings) {
  return getRouterScore(scoringSettings);
}

// ---- OptimizerMetrics ----------------------------------------------------------------------

BoardStatistics_OptimizerMetrics* BoardStatistics_OptimizerMetrics::of(BasicBoard* board) {
  int32_t viaCount = 0;
  float totalLength = 0.0f;
  float totalWeightedLength = 0.0f;
  int32_t defaultClearanceClass = BoardRules::defaultClearanceClass();

  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (jrt::fast_cast<Via>(currentItem) != nullptr) {
      viaCount++;
    } else if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
      totalLength += static_cast<float>(currentTrace->getLength());
      FixedState fixedState = currentTrace->getFixedState();
      if (fixedState == FixedState::UNFIXED || fixedState == FixedState::SHOVE_FIXED) {
        double weightedTraceLength =
            currentTrace->getLength() *
            (currentTrace->getHalfWidth() +
             board->clearanceValue(currentTrace->clearanceClassIndex(), defaultClearanceClass,
                                   currentTrace->getLayer()));
        if (fixedState == FixedState::SHOVE_FIXED) {
          weightedTraceLength /= 2;
        }
        totalWeightedLength += static_cast<float>(weightedTraceLength);
      }
    }
  }
  // PORT NOTE: Communication::unit is a non-nullable enum, so Java's `unit != null` is always true.
  Unit unit = board->communication->unit;
  if (unit != Unit::MM) {
    totalLength = static_cast<float>(Unit_scale(totalLength, unit, Unit::MM));
    totalWeightedLength = static_cast<float>(Unit_scale(totalWeightedLength, unit, Unit::MM));
  }
  return new BoardStatistics_OptimizerMetrics(viaCount, totalLength, totalWeightedLength);
}

}  // namespace fr
