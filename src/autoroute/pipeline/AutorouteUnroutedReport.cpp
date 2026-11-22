// Port of app.freerouting.autoroute.pipeline.AutorouteUnroutedReport
#include "autoroute/pipeline/AutorouteUnroutedReport.h"

#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "core/library/Package.h"
#include "drc/AirLine.h"
#include "drc/DesignRulesChecker.h"
#include "rules/Net.h"

namespace fr {

namespace {

// Java String.stripTrailing(): removes trailing Character.isWhitespace characters.
std::string stripTrailing(const std::string& s) {
  size_t end = s.size();
  while (end > 0) {
    unsigned char c = static_cast<unsigned char>(s[end - 1]);
    bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' ||
              (c >= 0x1C && c <= 0x1F);
    if (!ws) break;
    --end;
  }
  return s.substr(0, end);
}

}  // namespace

std::string AutorouteUnroutedReport::build(RoutingBoard* board) {
  DesignRulesChecker* tempDrc = new (jrt::kOp) DesignRulesChecker(board, nullptr);
  tempDrc->calculateAllIncompletes();
  jrt::Array<AirLine*>* airlines = tempDrc->getAllAirlines();

  if (airlines == nullptr || airlines->length == 0) {
    return "  (no unrouted connections found)";
  }

  jrt::Map<std::string, jrt::List<std::string>*>* byNet =
      new (jrt::kOp) jrt::LinkedHashMap<std::string, jrt::List<std::string>*>();
  for (AirLine* airline : *airlines) {
    std::string netName = airline->net != nullptr ? airline->net->name : std::string("(unknown net)");
    std::string fromDesc = describeItem(board, airline->fromItem);
    std::string toDesc = describeItem(board, airline->toItem);
    byNet
        ->computeIfAbsent(
            netName,
            [](const std::string&) -> jrt::List<std::string>* {
              return new (jrt::kOp) jrt::ArrayList<std::string>();
            })
        ->add("    - " + fromDesc + "  ->  " + toDesc);
  }

  std::string result;
  for (auto* entry : *byNet->entrySet()) {
    int32_t count = entry->getValue()->size();
    result += "  Net '";
    result += entry->getKey();
    result += "' (";
    result += jrt::str(count);
    result += " unrouted connection";
    result += count == 1 ? "" : "s";
    result += "):\n";
    for (const std::string& line : *entry->getValue()) {
      result += line;
      result += '\n';
    }
  }
  return stripTrailing(result);
}

std::string AutorouteUnroutedReport::describeItem(RoutingBoard* board, Item* item) {
  if (auto* pin = jrt::fast_cast<Pin>(item)) {
    try {
      Component* component = board->components->get(pin->getComponentId());
      if (component != nullptr) {
        Package* componentPackage = component->getPackage();
        if (componentPackage != nullptr) {
          Package_Pin* packagePin = componentPackage->getPin(pin->pinIndex);
          if (packagePin != nullptr) {
            return component->name + "-" + packagePin->name;
          }
        }
        return component->name + " (pin #" + jrt::str(pin->pinIndex) + ")";
      }
    } catch (const jrt::Exception& e) {
      // Fall through to the generic representation.
    }
  }
  return item != nullptr ? item->toString() : std::string("(unknown)");
}

}  // namespace fr
