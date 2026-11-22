// Port of app.freerouting.rules.DifferentialPairDetector
#include "rules/DifferentialPairDetector.h"

#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/DifferentialPair.h"
#include "rules/DifferentialPairs.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

const std::regex& DifferentialPairDetector::POWER_RAIL_PATTERN() {
  static const std::regex v("^[+-]?\\d+([.vV]\\d*)?[vV]?$",
                            std::regex::ECMAScript | std::regex::icase);
  return v;
}

bool DifferentialPairDetector::isExcludedNet(const std::optional<std::string>& netName) {
  if (!netName.has_value() || jrt::strip(*netName).empty()) {
    return true;
  }

  std::string trimmed = jrt::trim(*netName);
  std::string upper = jrt::toUpperCase(trimmed);

  // Strip optional leading hierarchical slashes for name inspection
  int32_t lastSlash = jrt::lastIndexOf(upper, "/");
  std::string localName = (lastSlash >= 0) ? jrt::substring(upper, lastSlash + 1) : upper;

  // Direct power rail names and voltages (e.g. +5V, -12V, 3V3, 1V8, 5V)
  if (std::regex_match(localName, POWER_RAIL_PATTERN())) {
    return true;
  }

  // Common power/ground identifiers
  if (localName == "GND" || localName == "GROUND" || jrt::startsWith(localName, "GND_") ||
      jrt::startsWith(localName, "GND-") || jrt::endsWith(localName, "GND") ||
      localName == "VCC" || jrt::startsWith(localName, "VCC_") ||
      jrt::endsWith(localName, "VCC") || localName == "VDD" ||
      jrt::startsWith(localName, "VDD_") || jrt::endsWith(localName, "VDD") ||
      localName == "VSS" || jrt::startsWith(localName, "VSS_") ||
      jrt::endsWith(localName, "VSS") || localName == "VBUS" ||
      jrt::startsWith(localName, "VBUS_") || localName == "VBAT" ||
      jrt::startsWith(localName, "VBAT_") || jrt::startsWith(localName, "BAT") ||
      localName == "V+" || localName == "V-" || localName == "VS+" || localName == "VS-" ||
      jrt::startsWith(localName, "VIN") || jrt::startsWith(localName, "VOUT") ||
      localName == "PWR" || localName == "POWER") {
    return true;
  }

  return false;
}

DifferentialPairDetector_ExtractedNetInfo* DifferentialPairDetector::extract(
    const std::optional<std::string>& netName) {
  if (!netName.has_value() || jrt::strip(*netName).empty() || isExcludedNet(netName)) {
    return nullptr;
  }

  std::string trimmed = jrt::trim(*netName);
  std::string upper = jrt::toUpperCase(trimmed);

  for (DifferentialPairDetector_SuffixTokenPattern* pattern : *DELIMITED_PATTERNS()) {
    DifferentialPairDetector_ExtractedNetInfo* info = tryExtractDelimited(
        trimmed, upper, pattern->posToken_, pattern->negToken_, pattern->style_);
    if (info != nullptr) {
      return info;
    }
  }

  // Scoped bare D+ / D- exception for USB legacy/standard nets
  int32_t lastSlash = jrt::lastIndexOf(upper, "/");
  std::string localUpper = (lastSlash >= 0) ? jrt::substring(upper, lastSlash + 1) : upper;
  if (localUpper == "D+") {
    std::string base = jrt::substring(trimmed, 0, static_cast<int32_t>(trimmed.length()) - 1);
    return new (jrt::kBoard) DifferentialPairDetector_ExtractedNetInfo(
        base, DifferentialPairDetector_Polarity::POSITIVE,
        DifferentialPairDetector_SuffixStyle::BARE_D_PLUS_MINUS);
  } else if (localUpper == "D-") {
    std::string base = jrt::substring(trimmed, 0, static_cast<int32_t>(trimmed.length()) - 1);
    return new (jrt::kBoard) DifferentialPairDetector_ExtractedNetInfo(
        base, DifferentialPairDetector_Polarity::NEGATIVE,
        DifferentialPairDetector_SuffixStyle::BARE_D_PLUS_MINUS);
  }

  return nullptr;
}

DifferentialPairDetector_ExtractedNetInfo* DifferentialPairDetector::extractUndelimited(
    const std::optional<std::string>& netName) {
  if (!netName.has_value() || isExcludedNet(netName)) {
    return nullptr;
  }
  std::string trimmed = jrt::trim(*netName);
  std::string upper = jrt::toUpperCase(trimmed);
  if (!jrt::endsWith(upper, "P") && !jrt::endsWith(upper, "N")) {
    return nullptr;
  }
  std::string base = jrt::substring(trimmed, 0, static_cast<int32_t>(trimmed.length()) - 1);
  int32_t localStart = jrt::lastIndexOf(base, "/") + 1;
  if (static_cast<int32_t>(base.length()) - localStart < 1 ||
      !jrt::Character::isLetterOrDigit(static_cast<unsigned char>(base[base.length() - 1])) ||
      isExcludedNet(base)) {
    return nullptr;
  }
  return new (jrt::kBoard) DifferentialPairDetector_ExtractedNetInfo(
      base,
      jrt::endsWith(upper, "P") ? DifferentialPairDetector_Polarity::POSITIVE
                                : DifferentialPairDetector_Polarity::NEGATIVE,
      DifferentialPairDetector_SuffixStyle::P_N_UNDELIMITED);
}

jrt::Array<DifferentialPairDetector_SuffixTokenPattern*>*
DifferentialPairDetector::DELIMITED_PATTERNS() {
  using S = DifferentialPairDetector_SuffixStyle;
  using P = DifferentialPairDetector_SuffixTokenPattern;
  static jrt::Array<P*>* v = jrt::Array<P*>::of({
      new (jrt::kOp) P("POS", "NEG", S::POS_NEG),
      new (jrt::kOp) P("DP", "DM", S::DP_DM),
      new (jrt::kOp) P("D+", "D-", S::D_PLUS_MINUS),
      new (jrt::kOp) P("P", "N", S::P_N),
      new (jrt::kOp) P("+", "-", S::PLUS_MINUS),
  });
  return v;
}

DifferentialPairDetector_ExtractedNetInfo* DifferentialPairDetector::tryExtractDelimited(
    const std::string& trimmed, const std::string& upper, const std::string& posToken,
    const std::string& negToken, DifferentialPairDetector_SuffixStyle style) {
  DifferentialPairDetector_Polarity polarity;
  int32_t tokenLen;
  if (jrt::endsWith(upper, posToken)) {
    polarity = DifferentialPairDetector_Polarity::POSITIVE;
    tokenLen = static_cast<int32_t>(posToken.length());
  } else if (jrt::endsWith(upper, negToken)) {
    polarity = DifferentialPairDetector_Polarity::NEGATIVE;
    tokenLen = static_cast<int32_t>(negToken.length());
  } else {
    return nullptr;
  }

  int32_t delimIndex = static_cast<int32_t>(trimmed.length()) - tokenLen - 1;
  if (delimIndex >= 0) {
    char delim = trimmed[delimIndex];
    if (delim == '_' || delim == '.') {
      std::string base = jrt::substring(trimmed, 0, delimIndex);
      if (!base.empty() && !isExcludedNet(base)) {
        return new (jrt::kBoard) DifferentialPairDetector_ExtractedNetInfo(base, polarity, style);
      }
    }
  }
  return nullptr;
}

jrt::List<DifferentialPair*>* DifferentialPairDetector::detect(BoardRules* boardRules) {
  if (boardRules == nullptr || boardRules->nets == nullptr ||
      boardRules->differentialPairs == nullptr) {
    return new (jrt::kOp) jrt::ArrayList<DifferentialPair*>();
  }

  auto* candidates = new (jrt::kOp) jrt::LinkedHashMap<DifferentialPairDetector_CandidateKey*,
                                            DifferentialPairDetector_CandidateEntry*>();
  int32_t maxNetNumber = boardRules->nets->maxNetNumber();

  for (int32_t i = 1; i <= maxNetNumber; i++) {
    Net* net = boardRules->nets->get(i);
    if (net == nullptr || !Nets::isNormalNetNumber(net->netNumber)) {
      continue;
    }
    if (boardRules->differentialPairs->findForNet(net) != nullptr) {
      continue;
    }

    DifferentialPairDetector_ExtractedNetInfo* extracted = extract(net->name);
    DifferentialPairDetector_ExtractedNetInfo* info =
        extracted != nullptr ? extracted : extractUndelimited(net->name);
    if (info == nullptr) {
      continue;
    }
    if (boardRules->differentialPairs->get(info->baseName()) != nullptr) {
      continue;
    }

    auto* key = new (jrt::kBoard) DifferentialPairDetector_CandidateKey(jrt::toLowerCase(info->baseName()),
                                                         info->style());
    DifferentialPairDetector_CandidateEntry* entry =
        candidates->computeIfAbsent(key, [info](DifferentialPairDetector_CandidateKey*) {
          return new (jrt::kBoard) DifferentialPairDetector_CandidateEntry(info->baseName(), info->style());
        });

    if (info->polarity() == DifferentialPairDetector_Polarity::POSITIVE) {
      if (entry->positiveNet != nullptr) {
        if (entry->positiveNet->name != net->name ||
            entry->positiveNet->subnetNumber == net->subnetNumber) {
          entry->ambiguous = true;
        }
      } else {
        entry->positiveNet = net;
      }
    } else {
      if (entry->negativeNet != nullptr) {
        if (entry->negativeNet->name != net->name ||
            entry->negativeNet->subnetNumber == net->subnetNumber) {
          entry->ambiguous = true;
        }
      } else {
        entry->negativeNet = net;
      }
    }
  }

  auto* bestByBase = new (jrt::kOp) jrt::LinkedHashMap<std::string, DifferentialPairDetector_CandidateEntry*>();
  for (DifferentialPairDetector_CandidateEntry* entry : *candidates->values()) {
    if (!entry->ambiguous && entry->positiveNet != nullptr && entry->negativeNet != nullptr) {
      std::string baseKey = jrt::toLowerCase(entry->baseName);
      DifferentialPairDetector_CandidateEntry* existing = bestByBase->get(baseKey);
      if (existing == nullptr ||
          static_cast<int32_t>(entry->style) < static_cast<int32_t>(existing->style)) {
        bestByBase->put(baseKey, entry);
      }
    }
  }

  auto* result = new (jrt::kOp) jrt::ArrayList<DifferentialPair*>();
  auto* takenNets = new (jrt::kOp) jrt::HashSet<Net*>();
  for (DifferentialPairDetector_CandidateEntry* entry : *bestByBase->values()) {
    if (takenNets->contains(entry->positiveNet) || takenNets->contains(entry->negativeNet)) {
      continue;
    }
    if (boardRules->differentialPairs->findForNet(entry->positiveNet) == nullptr &&
        boardRules->differentialPairs->findForNet(entry->negativeNet) == nullptr &&
        boardRules->differentialPairs->get(entry->baseName) == nullptr) {
      result->add(new (jrt::kBoard) DifferentialPair(entry->positiveNet, entry->negativeNet, entry->baseName));
      takenNets->add(entry->positiveNet);
      takenNets->add(entry->negativeNet);
    }
  }

  return result;  // Java: Collections.unmodifiableList(result)
}

jrt::List<DifferentialPair*>* DifferentialPairDetector::detectAndRegister(BoardRules* boardRules) {
  if (boardRules == nullptr || boardRules->differentialPairs == nullptr) {
    return new (jrt::kOp) jrt::ArrayList<DifferentialPair*>();
  }

  jrt::List<DifferentialPair*>* detected = detect(boardRules);
  for (DifferentialPair* pair : *detected) {
    boardRules->differentialPairs->add(pair);
    FRLogger::info("Auto-detected differential pair: " + pair->baseName() + " (" +
                   pair->positiveNet()->name + ", " + pair->negativeNet()->name + ")");
  }
  return detected;
}

}  // namespace fr
