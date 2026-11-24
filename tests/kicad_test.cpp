// Small checks of the KiCad project inputs (src/io/kicad), after the Java tests
// KiCadProjectTest and KiCadDesignRulesTest.
//
// usage: kicad_test [<fixtures directory>]   (the translator checks need Issue110-RelayModule.*)

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "io/BoardReadResult.h"
#include "io/kicad/KiCadBoardZones.h"
#include "io/kicad/KiCadDesignRules.h"
#include "io/kicad/KiCadDesignRulesTranslator.h"
#include "io/kicad/KiCadProject.h"
#include "io/kicad/KiCadRuleCondition.h"
#include "io/specctra/DsnReader.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    failures++;
  }
}

bool near(std::optional<double> v, double expected) {
  return v.has_value() && std::fabs(*v - expected) < 1e-9;
}

std::string project(const std::string& rules) {
  return "{\"board\": {\"design_settings\": {\"rules\": {" + rules + "}}}}";
}

void projectTests() {
  using fr::KiCadProject;
  check(near(KiCadProject::parse(project("\"min_track_width\": 0.2"), "p")->minTrackWidthUm(), 200.0),
        "min_track_width");
  KiCadProject* both =
      KiCadProject::parse(project("\"min_track_width\": 0.15, \"min_connection\": 0.2032"), "p");
  check(near(both->minTrackWidthUm(), 150.0), "both: track");
  check(near(both->minConnectionWidthUm(), 203.2), "both: connection");
  check(near(both->narrowestTraceWidthUm(), 203.2), "both: narrowest");
  check(!KiCadProject::parse(project("\"min_track_width\": 0.0"), "p")->minTrackWidthUm().has_value(),
        "zero width");
  check(!KiCadProject::parse(project("\"min_track_width\": \"0.2\""), "p")->minTrackWidthUm().has_value(),
        "quoted width");
  check(!KiCadProject::parse("{\"meta\": {\"version\": 3}}", "p")->minTrackWidthUm().has_value(),
        "no rules");
  for (const char* bad : {"(rules)", "[1, 2]", "{\"a\": ", ""}) {
    bool thrown = false;
    try {
      KiCadProject::parse(bad, "p");
    } catch (const jrt::IllegalArgumentException&) {
      thrown = true;
    }
    check(thrown, std::string("not a project: '") + bad + "'");
  }
}

void designRulesTests() {
  fr::KiCadDesignRules* rules = fr::KiCadDesignRules::parse(
      "(version 1)\n"
      "# a comment\n"
      "(rule \"a\" (layer outer) (condition \"A.NetClass == 'HV'\")\n"
      "  (constraint clearance (min 0.5mm)) (constraint track_width (min 10mil)))\n"
      "(rule b (constraint clearance (min 20mil)) (severity ignore))\n"
      "(rule \"c\" (constraint clearance (min 0.3)) (constraint creepage (min 1in)))\n",
      "test.kicad_dru");
  auto* list = rules->rules();
  check(list->size() == 3, "3 rules");
  check(list->get(0)->layer() == std::optional<std::string>("outer"), "layer outer");
  check(near(list->get(0)->clearanceUm(), 500.0), "clearance 0.5mm");
  check(list->get(0)->otherConstraints()->size() == 1 &&
            list->get(0)->otherConstraints()->get(0) == "track_width 0.2540 mm",
        "other constraint text: " + list->get(0)->otherConstraints()->get(0));
  check(near(list->get(1)->clearanceUm(), 508.0), "clearance 20mil");
  check(list->get(1)->ignored(), "severity ignore");
  check(list->get(1)->condition() == nullptr, "no condition");
  check(near(list->get(2)->clearanceUm(), 300.0), "no unit = mm");
  check(near(list->get(2)->creepageUm(), 25400.0), "creepage 1in");
  check(jrt::contains(list->get(2)->warnings()->get(0), "no unit"), "no unit warning");
  check(fr::KiCadDesignRules::formatMm(1500) == "1.500 mm", "formatMm 1.5");
  check(fr::KiCadDesignRules::formatMm(123456) == "123.5 mm", "formatMm 123.456");
  check(fr::KiCadDesignRules::formatMm(0.05) == "5.000e-05 mm", "formatMm small");
}

void conditionTests() {
  using fr::KiCadRuleCondition;
  using Tri = fr::KiCadRuleCondition_Tri;
  using Type = fr::KiCadRuleCondition_ItemType;
  auto* hv = new (jrt::kOp) fr::KiCadRuleCondition_Item(Type::TRACK, "MAINS", "HV,Default", std::nullopt,
                                                        std::nullopt, false);
  auto* pad = new (jrt::kOp) fr::KiCadRuleCondition_Item(Type::PAD, "GND", "Default", std::string("K1"),
                                                         std::string("1"), true, std::string("Lib:Relay"));
  KiCadRuleCondition* c = KiCadRuleCondition::parse("A.NetClass == 'hv' && B.NetClass != 'HV'");
  check(c->evaluate(hv, pad) == Tri::TRUE_, "composite class, case-insensitive, both orders");
  check(c->evaluate(hv, hv) == Tri::FALSE_, "same class");
  check(KiCadRuleCondition::parse("A.memberOfFootprint('k1')")->evaluate(pad, pad) == Tri::FALSE_,
        "memberOfFootprint is case-sensitive");
  check(KiCadRuleCondition::parse("A.memberOfFootprint('Lib:*')")->evaluate(pad, hv) == Tri::TRUE_,
        "memberOfFootprint library id");
  check(KiCadRuleCondition::parse("A.Pad_Type == 'SMD'")->evaluate(pad, hv) == Tri::TRUE_, "pad type");
  KiCadRuleCondition* geometric = KiCadRuleCondition::parse("A.intersectsArea('x')");
  check(geometric->evaluate(pad, hv) == Tri::UNKNOWN, "unknown function");
  check(geometric->unsupportedParts()->size() == 1, "unsupported part named");
  check(KiCadRuleCondition::parse("A.NetName == 'GND' ||")->unsupportedParts()->size() == 1,
        "unparsable condition");
  check(KiCadRuleCondition::wildcardMatches("F.*", "f.cu", false), "layer wildcard");
}

void zonesTests() {
  fr::KiCadBoardZones* zones = fr::KiCadBoardZones::parse(
      "(kicad_pcb (version 20240108) (layers (0 \"F.Cu\" signal) (31 \"B.Cu\" signal) (32 B.Adhes user))\n"
      " (footprint \"R\" (at 10 20 90) (zone_connect 2)\n"
      "   (pad \"1\" thru_hole circle (at 1 0) (size 1.6 1.6) (drill 0.8) (layers \"*.Cu\" \"*.Mask\")))\n"
      " (zone (net 1) (net_name \"GND\") (layer \"F.Cu\") (min_thickness 0.25)\n"
      "   (connect_pads (clearance 0.5)) (fill yes (thermal_gap 0.4) (thermal_bridge_width 0.3)"
      " (smoothing fillet) (radius 1))\n"
      "   (polygon (pts (xy 0 0) (xy 50 0) (xy 50 40) (xy 0 40))))\n"
      " (zone (net 0) (net_name \"\") (layer \"F.Cu\") (keepout (tracks not_allowed))\n"
      "   (polygon (pts (xy 0 0) (xy 1 1)))))\n",
      "z.kicad_pcb");
  check(zones->zones()->size() == 1, "one copper zone, the keepout left out");
  fr::KiCadBoardZones_Zone* zone = zones->zones()->get(0);
  check(zone->netName() == "GND" && zone->clearanceUm() == 500.0 && zone->minThicknessUm() == 250.0,
        "zone values");
  check(zone->maxXUm() == 50000.0 && zone->maxYUm() == 40000.0, "zone box");
  check(zone->cornerSmoothing() == fr::KiCadBoardZones_CornerSmoothing::FILLET &&
            zone->cornerRadiusUm() == 1000.0,
        "zone smoothing");
  check(zones->pads()->size() == 1, "one pad");
  fr::KiCadBoardZones_Pad* pad = zones->pads()->get(0);
  check(std::fabs(pad->xUm() - 10000.0) < 1e-6 && std::fabs(pad->yUm() - 19000.0) < 1e-6,
        "pad rotated with its footprint");
  check(pad->connection() == fr::KiCadBoardZones_PadConnection::SOLID, "footprint zone_connect");
  check(pad->isOnLayer("B.Cu") && pad->spokeAngleDeg() == 45, "pad layers and spoke angle");
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int32_t mm(fr::BasicBoard* board, double value) {
  int32_t units =
      jrt::d2i(std::ceil(board->communication->physicalToBoard(value * 1000.0, fr::Unit::UM)));
  return (units + 1) / 2 * 2;
}

int32_t traceValue(fr::BasicBoard* board, const std::string& first, const std::string& second) {
  fr::Net* a = board->rules->nets->get(first, 1);
  fr::Net* b = board->rules->nets->get(second, 1);
  return board->rules->clearanceMatrix->getValue(a->getNetClass()->getTraceClearanceClass(),
                                                 b->getNetClass()->getTraceClearanceClass(), 0, false);
}

void translatorTests(const std::string& fixtures) {
  std::string dsn = readFile(fixtures + "/Issue110-RelayModule.dsn");
  if (dsn.empty()) {
    std::printf("skipped the translator checks (no fixture in '%s')\n", fixtures.c_str());
    return;
  }
  auto* bytes = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(dsn.size()));
  std::copy(dsn.begin(), dsn.end(), reinterpret_cast<char*>(bytes->data()));
  auto* read = fr::dsn::DsnReader::readBoard(bytes, nullptr, nullptr, std::string("Issue110-RelayModule.dsn"));
  auto* success = dynamic_cast<fr::io::BoardReadResult_Success*>(read);
  check(success != nullptr, "fixture loads");
  if (success == nullptr) return;
  fr::BasicBoard* board = success->board();
  int32_t design = traceValue(board, "GND", "+5V");
  auto* result = fr::KiCadDesignRulesTranslator::apply(
      board, fr::KiCadDesignRules::read(fixtures + "/Issue110-RelayModule.kicad_dru"));
  check(result->translatedRules() == 4, "4 rules translated");
  check(result->addedClearanceClasses() > 0, "classes added");
  check(result->splitNetClasses().size() == 1 && result->splitNetClasses()[0].first == "HighVoltage" &&
            result->splitNetClasses()[0].second ==
                std::vector<std::string>{"HighVoltage", "HighVoltage_2"},
        "HighVoltage split");
  check(traceValue(board, "Net-(FU1-Pad1)", "Net-(FU1-Pad2)") == mm(board, 1.5), "1.5 mm inside FU1");
  check(traceValue(board, "Net-(FU1-Pad1)", "Net-(FU2-Pad1)") == mm(board, 4.0), "4 mm, may match");
  check(traceValue(board, "Net-(FU2-Pad1)", "GND") == mm(board, 3.0), "3 mm mains isolation");
  check(traceValue(board, "GND", "+5V") == design, "design clearance kept");
  std::printf("%s\n", fr::KiCadDesignRulesTranslator::summary(result, "Issue110-RelayModule.kicad_dru").c_str());
}

}  // namespace

int main(int argc, char** argv) {
  jrt::JobRegion job;
  fr::FRLogger::logLevel = fr::FRLogger::LEVEL_ERROR;  // the DSN reader warns about the Cyrillic names
  projectTests();
  designRulesTests();
  conditionTests();
  zonesTests();
  translatorTests(argc > 1 ? argv[1] : "upstream/fixtures");
  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("kicad_test: all checks passed\n");
  return 0;
}
