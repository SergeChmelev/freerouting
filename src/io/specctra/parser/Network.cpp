#include "io/specctra/parser/Network.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/BoardLibrary.h"
#include "core/library/LogicalPart.h"
#include "core/library/LogicalParts.h"
#include "core/library/Package.h"
#include "core/library/Packages.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Vector.h"
#include "io/CoordinateTransform.h"
#include "io/KiCadNetClassNames.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "io/specctra/parser/Circuit.h"
#include "io/specctra/parser/ComponentPlacement.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/Net.h"
#include "io/specctra/parser/NetClass.h"
#include "io/specctra/parser/NetList.h"
#include "io/specctra/parser/PartLibrary.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "io/specctra/parser/Structure.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/MeanderPatternType.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/NetClasses.h"
#include "rules/NetLengthConstraint.h"
#include "rules/NetMeanderConstraint.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "rules/ViaRule.h"

namespace fr::dsn {

using ItemClass = fr::DefaultItemClearanceClasses_ItemClass;

namespace {

/** Java Collection.addAll(null) throws NullPointerException; keep that instead of crashing. */
template <class T>
void addAllNonNull(jrt::Collection<T>* target, jrt::Collection<T>* source) {
  if (source == nullptr) {
    throw jrt::NullPointerException("Collection.addAll(null)");
  }
  target->addAll(source);
}

/** Java s.replaceAll("\\.\\d+", ""): removes every '.' followed by one or more ASCII digits. */
std::string removeDotDigits(const std::string& s) {
  std::string result;
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '.' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') {
      i += 1;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
      }
      continue;
    }
    result += s[i];
    ++i;
  }
  return result;
}

/** Java Objects.hashCode for the record components. */
int32_t objectHash(jrt::Object* o) { return o == nullptr ? 0 : o->hashCode(); }

}  // namespace

// -------------------------------------------------------------------------------------------------
// Records
// -------------------------------------------------------------------------------------------------

int32_t Network_ClassPairRule::specificity() {
  return (layerIndex_ < 0 ? 0 : 2) + (clearance_->clearanceClassPairs->isEmpty() ? 0 : 1);
}

bool Network_ClassPairRule::equals(jrt::Object* o) {
  if (o == this) {
    return true;
  }
  auto* other = dynamic_cast<Network_ClassPairRule*>(o);
  if (other == nullptr) {
    return false;
  }
  auto objEquals = [](jrt::Object* a, jrt::Object* b) {
    return a == b || (a != nullptr && a->equals(b));
  };
  return objEquals(firstClass_, other->firstClass_) &&
         objEquals(secondClass_, other->secondClass_) &&
         objEquals(clearance_, other->clearance_) && layerIndex_ == other->layerIndex_;
}

int32_t Network_ClassPairRule::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, objectHash(firstClass_));
  h = jrt::hashCombine(h, objectHash(secondClass_));
  h = jrt::hashCombine(h, objectHash(clearance_));
  h = jrt::hashCombine(h, jrt::Integer::hashCode(layerIndex_));
  return h;
}

std::string Network_ClassPairRule::toString() {
  return "ClassPairRule[firstClass=" + jrt::str(firstClass_) +
         ", secondClass=" + jrt::str(secondClass_) + ", clearance=" + jrt::str(clearance_) +
         ", layerIndex=" + jrt::str(layerIndex_) + "]";
}

bool Network_PlacedComponent::equals(jrt::Object* o) {
  if (o == this) {
    return true;
  }
  auto* other = dynamic_cast<Network_PlacedComponent*>(o);
  if (other == nullptr) {
    return false;
  }
  return libraryName_ == other->libraryName_ &&
         (location_ == other->location_ ||
          (location_ != nullptr && location_->equals(other->location_)));
}

int32_t Network_PlacedComponent::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::stringHashCode(libraryName_));
  h = jrt::hashCombine(h, objectHash(location_));
  return h;
}

std::string Network_PlacedComponent::toString() {
  return "PlacedComponent[libraryName=" + libraryName_ + ", location=" + jrt::str(location_) +
         "]";
}

// -------------------------------------------------------------------------------------------------
// Network
// -------------------------------------------------------------------------------------------------

jrt::List<std::string>* Network::NON_WIRE_ITEM_CLASS_NAMES() {
  static jrt::List<std::string>* v =
      new jrt::ArrayList<std::string>({"via", "smd", "pin", "area"});
  return v;
}

void Network::writeScope(WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("network");
  jrt::Collection<fr::Pin*>* boardPins = scopeParameter->board->getPins();
  for (int32_t i = 1; i <= scopeParameter->board->rules->nets->maxNetNumber(); i++) {
    Net::writeScope(scopeParameter, scopeParameter->board->rules->nets->get(i), boardPins);
  }
  writeViaInfos(scopeParameter->board->rules, scopeParameter->file,
                scopeParameter->identifierType);
  writeViaRules(scopeParameter->board->rules, scopeParameter->file,
                scopeParameter->identifierType);
  writeNetClasses(scopeParameter);
  scopeParameter->file->endScope();
}

void Network::writeViaInfos(fr::BoardRules* rules, fr::IndentFileWriter* file,
                            fr::IdentifierType* identifierType) {
  for (int32_t i = 0; i < rules->viaInfos->count(); i++) {
    fr::ViaInfo* const currentVia = rules->viaInfos->get(i);
    file->startScope();
    file->write("via ");
    file->newLine();
    identifierType->write(currentVia->getName(), file);
    file->write(" ");
    identifierType->write(currentVia->getPadstack()->name, file);
    file->write(" ");
    identifierType->write(rules->clearanceMatrix->getName(currentVia->getClearanceClassIndex()),
                          file);
    if (currentVia->attachSmdAllowed()) {
      file->write(" attach");
    }
    file->endScope();
  }
}

void Network::writeViaRules(fr::BoardRules* rules, fr::IndentFileWriter* file,
                            fr::IdentifierType* identifierType) {
  for (fr::ViaRule* currentRule : *rules->viaRules) {
    file->startScope();
    file->write("viaRule");
    file->newLine();
    identifierType->write(currentRule->name, file);
    for (int32_t i = 0; i < currentRule->viaCount(); i++) {
      file->write(" ");
      identifierType->write(currentRule->getVia(i)->getName(), file);
    }
    file->endScope();
  }
}

void Network::writeNetClasses(WriteScopeParameter* scopeParameter) {
  for (int32_t i = 0; i < scopeParameter->board->rules->netClasses->count(); i++) {
    writeNetClass(scopeParameter->board->rules->netClasses->get(i), scopeParameter);
  }
}

void Network::writeNetClass(fr::NetClass* netClass, WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("class ");
  scopeParameter->identifierType->write(netClass->getName(), scopeParameter->file);
  const int32_t netsPerRow = 8;
  int32_t netCounter = 0;
  for (int32_t i = 1; i <= scopeParameter->board->rules->nets->maxNetNumber(); i++) {
    if (scopeParameter->board->rules->nets->get(i)->getNetClass() == netClass) {
      if (netCounter % netsPerRow == 0) {
        scopeParameter->file->newLine();
      } else {
        scopeParameter->file->write(" ");
      }
      scopeParameter->identifierType->write(scopeParameter->board->rules->nets->get(i)->name,
                                            scopeParameter->file);
      ++netCounter;
    }
  }

  // write the trace clearance class
  Rule::writeItemClearanceClass(
      scopeParameter->board->rules->clearanceMatrix->getName(netClass->getTraceClearanceClass()),
      scopeParameter->file, scopeParameter->identifierType);

  if (netClass->getViaRule() != nullptr) {
    // write the via rule
    scopeParameter->file->newLine();
    scopeParameter->file->write("(via_rule ");
    scopeParameter->identifierType->write(netClass->getViaRule()->name, scopeParameter->file);
    scopeParameter->file->write(")");
  }

  // write the rules, if they are different from the default rule.
  Rule::writeScope(netClass, scopeParameter);

  writeCircuit(netClass, scopeParameter);

  if (!netClass->getPullTight()) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(pull_tight off)");
  }

  if (netClass->isShoveFixed()) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(shove_fixed on)");
  }

  scopeParameter->file->endScope();
}

void Network::writeCircuit(fr::NetClass* netClass, WriteScopeParameter* scopeParameter) {
  const double minTraceLength = netClass->getMinimumTraceLength();
  const double maxTraceLength = netClass->getMaximumTraceLength();
  scopeParameter->file->startScope();
  scopeParameter->file->write("circuit ");
  scopeParameter->file->newLine();
  scopeParameter->file->write("(use_layer");
  int32_t layerCount = netClass->layerCount();
  for (int32_t i = 0; i < layerCount; i++) {
    if (netClass->isActiveRoutingLayer(i)) {
      scopeParameter->file->write(" ");
      scopeParameter->file->write((*scopeParameter->board->layerStructure->layers)[i]->name);
    }
  }
  scopeParameter->file->write(")");
  if (minTraceLength > 0 || maxTraceLength > 0) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(length ");
    double transformedMaxLength;
    if (maxTraceLength <= 0) {
      transformedMaxLength = -1;
    } else {
      transformedMaxLength = scopeParameter->coordinateTransform->boardToDsn(maxTraceLength);
    }
    scopeParameter->file->write(jrt::str(transformedMaxLength));
    scopeParameter->file->write(" ");
    double transformedMinLength;
    if (minTraceLength <= 0) {
      transformedMinLength = 0;
    } else {
      transformedMinLength = scopeParameter->coordinateTransform->boardToDsn(minTraceLength);
    }
    scopeParameter->file->write(jrt::str(transformedMinLength));
    scopeParameter->file->write(")");
  }
  if (netClass->hasMeanderConstraint()) {
    Net::writeMeanderProperties(netClass->getMeanderConstraint(), scopeParameter);
  }
  scopeParameter->file->endScope();
}

jrt::Collection<jrt::Collection<Net_Pin*>*>* Network::createOrderedSubnets(
    jrt::Collection<Net_Pin*>* pinList) {
  jrt::Collection<jrt::Collection<Net_Pin*>*>* result =
      new jrt::LinkedList<jrt::Collection<Net_Pin*>*>();
  if (pinList->isEmpty()) {
    return result;
  }

  jrt::Iterator<Net_Pin*>* it = pinList->iterator();
  Net_Pin* prevPin = it->next();
  while (it->hasNext()) {
    Net_Pin* nextPin = it->next();
    jrt::Set<Net_Pin*>* currentSubnetPinList = new jrt::TreeSet<Net_Pin*>();
    currentSubnetPinList->add(prevPin);
    currentSubnetPinList->add(nextPin);
    result->add(currentSubnetPinList);
    prevPin = nextPin;
  }
  return result;
}

bool Network::readNetPins(IJFlexScanner* scanner, jrt::Collection<Net_Pin*>* pinList) {
  jrt::Object* nextToken;
  std::string componentName;
  std::string pinName;
  // Java: ((SpecctraDsnStreamReader) scanner).nextString(true, '-')
  auto* streamReader = dynamic_cast<SpecctraDsnStreamReader*>(scanner);
  if (streamReader == nullptr) {
    throw jrt::ClassCastException("Network.read_net_pins: SpecctraDsnStreamReader expected");
  }
  while (!(componentName = streamReader->nextString(true, u'-')).empty()) {
    try {
      scanner->yybegin(SpecctraDsnStreamReader::SPEC_CHAR);
      nextToken = scanner->nextToken();  // overread the hyphen
    } catch (const jrt::IOException& e) {
      FRLogger::error("Network.read_net_pins: IO error while scanning file", &e);
      return false;
    }

    pinName = scanner->nextString(true);
    Net_Pin* currentEntry = new Net_Pin(componentName, pinName);
    pinList->add(currentEntry);
  }

  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Network.read_net_pins: IO error scanning file", &e);
    return false;
  }
  if (nextToken == nullptr) {
    FRLogger::warn("Network.read_net_pins: unexpected end of file at '" +
                   scanner->getScopeIdentifier() + "'");
    return false;
  }
  if (nextToken != CLOSED_BRACKET()) {
    // not end of scope
    FRLogger::warn("Network.read_net_pins: expected closed bracket is missing at '" +
                   scanner->getScopeIdentifier() + "'");
  }

  return true;
}

fr::ViaInfo* Network::readViaInfo(IJFlexScanner* scanner, fr::BasicBoard* board) {
  try {
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    jrt::Object* nextToken = scanner->nextToken();
    auto* name = dynamic_cast<JString*>(nextToken);
    if (name == nullptr) {
      FRLogger::warn("Network.read_via_info: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    nextToken = scanner->nextToken();
    auto* padstackNameToken = dynamic_cast<JString*>(nextToken);
    if (padstackNameToken == nullptr) {
      FRLogger::warn("Network.read_via_info: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    const std::string padstackName = padstackNameToken->value;
    scanner->setScopeIdentifier(padstackName);
    fr::Padstack* viaPadstack = board->library->getViaPadstack(padstackName);
    if (viaPadstack == nullptr) {
      viaPadstack = board->library->getViaPadstack(removeDotDigits(padstackName));
    }
    if (viaPadstack == nullptr) {
      // The padstack may not yet be inserted into the list of via padstacks
      viaPadstack = board->library->padstacks->get(padstackName);
      if (viaPadstack == nullptr) {
        viaPadstack = board->library->padstacks->get(removeDotDigits(padstackName));
      }
      if (viaPadstack == nullptr) {
        FRLogger::warn("Network.read_via_info: padstack not found at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      board->library->addViaPadstack(viaPadstack);
    }
    scanner->yybegin(SpecctraDsnStreamReader::NAME);
    nextToken = scanner->nextToken();
    auto* clearanceName = dynamic_cast<JString*>(nextToken);
    if (clearanceName == nullptr) {
      FRLogger::warn("Network.read_via_info: string expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    int32_t clearanceClass = board->rules->clearanceMatrix->getNo(clearanceName->value);
    if (clearanceClass < 0) {
      // Clearance class not stored, because it is identical to the default clearance netClass.
      clearanceClass = fr::BoardRules::defaultClearanceClass();
    }
    bool attachAllowed = false;
    nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      if (nextToken != Keyword::ATTACH()) {
        FRLogger::warn("Network.read_via_info: Keyword.ATTACH expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      attachAllowed = true;
      nextToken = scanner->nextToken();
      if (nextToken != Keyword::CLOSED_BRACKET()) {
        FRLogger::warn("Network.read_via_info: closing bracket expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    return new fr::ViaInfo(name->value, viaPadstack, clearanceClass, attachAllowed, board->rules);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Network.read_via_info: IO error while scanning file", &e);
    return nullptr;
  }
}

jrt::Collection<std::string>* Network::readViaRule(IJFlexScanner* scanner, fr::BasicBoard* board) {
  try {
    jrt::Collection<std::string>* result = new jrt::LinkedList<std::string>();
    for (;;) {
      scanner->yybegin(SpecctraDsnStreamReader::NAME);
      jrt::Object* nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      auto* viaName = dynamic_cast<JString*>(nextToken);
      if (viaName == nullptr) {
        FRLogger::warn("Network.read_via_rule: string expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      result->add(viaName->value);
    }
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Network.read_via_rule: IO error while scanning file", &e);
    return nullptr;
  }
}

void Network::insertViaInfos(jrt::Collection<fr::ViaInfo*>* viaInfos, fr::RoutingBoard* board,
                             bool attachAllowed) {
  if (!viaInfos->isEmpty()) {
    for (fr::ViaInfo* currentInfo : *viaInfos) {
      board->rules->viaInfos->add(currentInfo);
    }
  } else {
    // No via infos found; create default via infos from the via padstacks.
    createDefaultViaInfos(board, board->rules->getDefaultNetClass(), attachAllowed);
  }
}

void Network::createDefaultViaInfos(fr::BasicBoard* board, fr::NetClass* netClass,
                                    bool attachAllowed) {
  int32_t clearanceClassIndex = netClass->defaultItemClearanceClasses->get(ItemClass::VIA);
  bool isDefaultClass = netClass == board->rules->getDefaultNetClass();
  for (int32_t i = 0; i < board->library->viaPadstackCount(); i++) {
    fr::Padstack* currentPadstack = board->library->getViaPadstack(i);
    bool viaAttachAllowed = attachAllowed && currentPadstack->attachAllowed;
    std::string viaName;
    if (isDefaultClass) {
      viaName = currentPadstack->name;
    } else {
      viaName = currentPadstack->name + DsnFile::CLASS_CLEARANCE_SEPARATOR + netClass->getName();
    }
    fr::ViaInfo* foundViaInfo = new fr::ViaInfo(viaName, currentPadstack, clearanceClassIndex,
                                                viaAttachAllowed, board->rules);
    board->rules->viaInfos->add(foundViaInfo);
  }
}

void Network::insertViaRules(jrt::Collection<jrt::Collection<std::string>*>* viaRules,
                             fr::BasicBoard* board) {
  bool ruleFound = false;
  for (jrt::Collection<std::string>* currentList : *viaRules) {
    if (currentList->size() < 2) {
      continue;
    }
    if (addViaRule(currentList, board)) {
      ruleFound = true;
    }
  }
  if (!ruleFound) {
    board->rules->createDefaultViaRule(board->rules->getDefaultNetClass(), "default");
  }
  for (int32_t i = 0; i < board->rules->netClasses->count(); i++) {
    board->rules->netClasses->get(i)->setViaRule(board->rules->getDefaultViaRule());
  }
}

bool Network::addViaRule(jrt::Collection<std::string>* nameList, fr::BasicBoard* board) {
  jrt::Iterator<std::string>* it = nameList->iterator();
  std::string ruleName = it->next();
  fr::ViaRule* existingRule = board->rules->getViaRule(ruleName);
  fr::ViaRule* currentRule = new fr::ViaRule(ruleName);
  bool ruleOk = true;
  while (it->hasNext()) {
    fr::ViaInfo* const currentVia = board->rules->viaInfos->get(it->next());
    if (currentVia != nullptr) {
      currentRule->appendVia(currentVia);
    } else {
      FRLogger::warn("Network.insert_via_rules: viaInfo not found");
      ruleOk = false;
    }
  }
  if (ruleOk) {
    if (existingRule != nullptr) {
      // Replace already existing rule.
      board->rules->viaRules->remove(existingRule);
    }
    board->rules->viaRules->add(currentRule);
  }
  return ruleOk;
}

void Network::insertNetClasses(jrt::Collection<NetClass*>* netClasses,
                               ReadScopeParameter* scopeParameter) {
  fr::BasicBoard* routingBoard = scopeParameter->boardHandling->getRoutingBoard();
  for (NetClass* currentClass : *netClasses) {
    insertNetClass(currentClass, scopeParameter->layerStructure, routingBoard,
                   scopeParameter->coordinateTransform, scopeParameter->viaAtSmdAllowed);
  }
}

void Network::insertNetClass(NetClass* netClass, LayerStructure* layerStructure,
                             fr::BasicBoard* board,
                             fr::io::CoordinateTransform* coordinateTransform,
                             bool viaAtSmdAllowed) {
  fr::NetClass* boardNetClass = fr::io::KiCadNetClassNames::isKiCadDefaultNetClassName(netClass->name)
                                    ? board->rules->getDefaultNetClass()
                                    : board->rules->appendNetClass(netClass->name);
  if (netClass->traceClearanceClass.has_value()) {
    int32_t traceClearanceClass = board->rules->clearanceMatrix->getNo(*netClass->traceClearanceClass);
    if (traceClearanceClass >= 0) {
      boardNetClass->setTraceClearanceClass(traceClearanceClass);
    } else {
      FRLogger::warn("Network.insert_net_class: clearance class not found at '" +
                     boardNetClass->getName() + "'");
    }
  }
  if (netClass->viaRule.has_value()) {
    fr::ViaRule* viaRule = board->rules->getViaRule(*netClass->viaRule);
    if (viaRule != nullptr) {
      boardNetClass->setViaRule(viaRule);
    } else {
      FRLogger::warn("Network.insert_net_class: via rule not found at '" +
                     boardNetClass->getName() + "'");
    }
  }
  if (netClass->maxTraceLength > 0) {
    boardNetClass->setMaximumTraceLength(coordinateTransform->dsnToBoard(netClass->maxTraceLength));
  }
  if (netClass->minTraceLength > 0) {
    boardNetClass->setMinimumTraceLength(coordinateTransform->dsnToBoard(netClass->minTraceLength));
  }
  for (const std::string& currentNetName : *netClass->netList) {
    jrt::Collection<fr::Net*>* currentNetList = board->rules->nets->get(currentNetName);
    for (fr::Net* currentNet : *currentNetList) {
      currentNet->setClass(boardNetClass);
    }
  }

  // read the trace width and clearance rules.

  bool clearanceRuleFound = false;

  for (Rule* currentRule : *netClass->rules) {
    if (auto* rule1 = dynamic_cast<Rule_WidthRule*>(currentRule)) {
      int32_t traceHalfwidth =
          jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(rule1->value / 2)));
      boardNetClass->setTraceHalfWidth(traceHalfwidth);
    } else if (auto* rule = dynamic_cast<Rule_ClearanceRule*>(currentRule)) {
      addClearanceRule(board->rules, boardNetClass, rule, -1, coordinateTransform);
      clearanceRuleFound = true;
    } else if (auto* lengthRule = dynamic_cast<Rule_LengthRule*>(currentRule)) {
      if (lengthRule->maxLength > 0) {
        boardNetClass->setMaximumTraceLength(coordinateTransform->dsnToBoard(lengthRule->maxLength));
      }
      if (lengthRule->minLength > 0) {
        boardNetClass->setMinimumTraceLength(coordinateTransform->dsnToBoard(lengthRule->minLength));
      }
    } else if (auto* meanderRule = dynamic_cast<Rule_MeanderRule*>(currentRule)) {
      boardNetClass->setMeanderConstraint(
          meanderRule->applyTo(boardNetClass->getMeanderConstraint(), coordinateTransform));
    } else {
      FRLogger::warn("Network.insert_net_class: rule type not yet implemented at '" +
                     boardNetClass->getName() + "'");
    }
  }

  // read the layer dependent rules.

  for (Rule_LayerRule* currentLayerRule : *netClass->layerRules) {
    for (const std::string& currentLayerName : *currentLayerRule->layerNames) {
      int32_t layerIndex = board->layerStructure->getNo(currentLayerName);
      if (layerIndex < 0) {
        FRLogger::warn("Network.insert_net_class: layer not found at '" +
                       boardNetClass->getName() + "'");
        continue;
      }
      for (Rule* currentRule : *currentLayerRule->rules) {
        if (auto* rule1 = dynamic_cast<Rule_WidthRule*>(currentRule)) {
          int32_t traceHalfwidth =
              jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(rule1->value / 2)));
          boardNetClass->setTraceHalfWidth(layerIndex, traceHalfwidth);
        } else if (auto* rule = dynamic_cast<Rule_ClearanceRule*>(currentRule)) {
          addClearanceRule(board->rules, boardNetClass, rule, layerIndex, coordinateTransform);
          clearanceRuleFound = true;
        } else {
          FRLogger::warn("Network.insert_net_class: layer rule type not yet implemented at '" +
                         boardNetClass->getName() + "'");
        }
      }
    }
  }

  boardNetClass->setPullTight(netClass->pullTight);
  boardNetClass->setShoveFixed(netClass->shoveFixed);
  if (netClass->circuit != nullptr && netClass->circuit->hasMeanderConstraint()) {
    boardNetClass->setMeanderConstraint(netClass->circuit->toMeanderConstraint(
        fr::MeanderPatternType::SINGLE_TRACK, coordinateTransform));
  }
  bool viaInfosCreated = false;

  if (clearanceRuleFound && boardNetClass != board->rules->getDefaultNetClass()) {
    createDefaultViaInfos(board, boardNetClass, viaAtSmdAllowed);
    viaInfosCreated = true;
  }

  if (!netClass->useVia->isEmpty()) {
    createViaRule(netClass->useVia, boardNetClass, board, viaAtSmdAllowed);
  } else if (viaInfosCreated) {
    board->rules->createDefaultViaRule(boardNetClass, boardNetClass->getName());
  }
  if (!netClass->useLayer->isEmpty()) {
    createActiveTraceLayers(netClass->useLayer, layerStructure, boardNetClass);
  }
}

void Network::insertClassPairs(jrt::Collection<NetClass_ClassClass*>* classClasses,
                               ReadScopeParameter* scopeParameter) {
  fr::BasicBoard* routingBoard = scopeParameter->boardHandling->getRoutingBoard();
  // Localize all participants before writing any pair. Otherwise an inherited item entry can
  // change unrelated net classes, or a later split can lose an earlier pair's clearance.
  jrt::Set<fr::NetClass*>* preparedClasses = new jrt::LinkedHashSet<fr::NetClass*>();
  for (NetClass_ClassClass* classClass : *classClasses) {
    for (const std::string& name : *classClass->classNames) {
      fr::NetClass* netClass = fr::io::KiCadNetClassNames::resolveNetClass(routingBoard->rules, name);
      if (netClass != nullptr && preparedClasses->add(netClass)) {
        prepareClassPairClearances(routingBoard, netClass);
      }
    }
  }
  jrt::List<Network_ClassPairRule*>* pairRules = new jrt::ArrayList<Network_ClassPairRule*>();
  for (NetClass_ClassClass* currentClassClass : *classClasses) {
    jrt::List<fr::NetClass*>* classes = new jrt::ArrayList<fr::NetClass*>();
    for (const std::string& name : *currentClassClass->classNames) {
      fr::NetClass* netClass = fr::io::KiCadNetClassNames::resolveNetClass(routingBoard->rules, name);
      if (netClass != nullptr) {
        classes->add(netClass);
      } else {
        FRLogger::warn("Network.insert_class_pairs: class '" + name + "' not found");
      }
    }
    for (int32_t i = 0; i < classes->size(); i++) {
      for (int32_t j = i + 1; j < classes->size(); j++) {
        collectClassPairRules(currentClassClass, classes->get(i), classes->get(j), routingBoard,
                              pairRules);
      }
    }
  }
  // Repeated declarations for the same pair share a scope. Apply more specific rules last,
  // retaining source order among rules of equal specificity, including overlapping class lists.
  pairRules->sort(jrt::comparingInt<Network_ClassPairRule*>(
      [](Network_ClassPairRule* rule) { return rule->specificity(); }));
  for (Network_ClassPairRule* rule : *pairRules) {
    addMixedClearanceRule(routingBoard->rules->clearanceMatrix, rule->firstClass(),
                          rule->secondClass(), rule->clearance(), rule->layerIndex(),
                          scopeParameter->coordinateTransform);
  }
  // Structure creates planes before the network assigns net classes. Keep inherited plane
  // clearances aligned with the same effective entries used by pads and newly imported wiring.
  for (fr::ConductionArea* plane : *scopeParameter->planesWithInheritedClearance) {
    fr::NetClass* netClass = routingBoard->rules->nets->get(plane->getNetNumber(0))->getNetClass();
    plane->changeClearanceClassIndex(netClass->defaultItemClearanceClasses->get(ItemClass::AREA));
  }
}

void Network::prepareClassPairClearances(fr::BasicBoard* board, fr::NetClass* netClass) {
  fr::ClearanceMatrix* matrix = board->rules->clearanceMatrix;
  int32_t oldViaClass = netClass->defaultItemClearanceClasses->get(ItemClass::VIA);
  for (ItemClass type : *fr::DefaultItemClearanceClasses_ItemClass_values()) {
    if (type == ItemClass::NONE) {
      continue;
    }
    int32_t source = type == ItemClass::TRACE ? netClass->getTraceClearanceClass()
                                              : netClass->defaultItemClearanceClasses->get(type);
    std::string suffix = type == ItemClass::TRACE
                             ? std::string("wire")
                             : jrt::toLowerCase(fr::DefaultItemClearanceClasses_ItemClass_name(type));
    int32_t localized = copyPrivateClearanceClass(
        matrix, netClass->getName() + DsnFile::CLASS_CLEARANCE_SEPARATOR + suffix, source);
    netClass->defaultItemClearanceClasses->set(type, localized);
    if (type == ItemClass::TRACE) {
      netClass->setTraceClearanceClass(localized);
    }
  }

  // Selected vias can use a different clearance from the net class's default VIA entry.
  // Copy every selected source row rather than silently collapsing explicit via rules.
  fr::ViaRule* oldRule = netClass->getViaRule();
  if (oldRule == nullptr) {
    return;
  }
  jrt::Map<int32_t, int32_t>* viaClasses = new jrt::LinkedHashMap<int32_t, int32_t>();
  viaClasses->put(oldViaClass, netClass->defaultItemClearanceClasses->get(ItemClass::VIA));
  std::string ruleName = netClass->getName() + "_class_class";
  while (board->rules->getViaRule(ruleName) != nullptr) {
    ruleName += "_";
  }
  fr::ViaRule* newRule = new fr::ViaRule(ruleName);
  for (int32_t i = 0; i < oldRule->viaCount(); i++) {
    fr::ViaInfo* via = oldRule->getVia(i);
    int32_t localized = viaClasses->computeIfAbsent(
        via->getClearanceClassIndex(), [&](const int32_t& source) -> int32_t {
          return copyPrivateClearanceClass(
              matrix, netClass->getName() + DsnFile::CLASS_CLEARANCE_SEPARATOR + "via", source);
        });
    std::string viaName = via->getName() + "_" + ruleName;
    while (board->rules->viaInfos->nameExists(viaName)) {
      viaName += "_";
    }
    via = new fr::ViaInfo(viaName, via->getPadstack(), localized, via->attachSmdAllowed(),
                          board->rules);
    board->rules->viaInfos->add(via);
    newRule->appendVia(via);
  }
  board->rules->viaRules->add(newRule);
  netClass->setViaRule(newRule);
}

int32_t Network::copyPrivateClearanceClass(fr::ClearanceMatrix* matrix, const std::string& nameIn,
                                           int32_t source) {
  std::string name = nameIn;
  while (matrix->getNo(name) >= 0) {
    name += "_";
  }
  return copyClearanceClass(matrix, name, source);
}

int32_t Network::copyClearanceClass(fr::ClearanceMatrix* matrix, const std::string& name,
                                    int32_t source) {
  if (!matrix->appendClass(name)) {
    return matrix->getNo(name);
  }
  int32_t result = matrix->getNo(name);
  for (int32_t layer = 0; layer < matrix->getLayerCount(); layer++) {
    for (int32_t other = 1; other < result; other++) {
      int32_t value = matrix->getValue(source, other, layer, false);
      matrix->setValue(result, other, layer, value);
      matrix->setValue(other, result, layer, value);
    }
    matrix->setValue(result, result, layer, matrix->getValue(source, source, layer, false));
  }
  return result;
}

void Network::collectClassPairRules(NetClass_ClassClass* classClass, fr::NetClass* firstClass,
                                    fr::NetClass* secondClass, fr::BasicBoard* board,
                                    jrt::List<Network_ClassPairRule*>* rules) {
  for (Rule* currentRule : *classClass->rules) {
    if (auto* clearance = dynamic_cast<Rule_ClearanceRule*>(currentRule)) {
      rules->add(new Network_ClassPairRule(firstClass, secondClass, clearance, -1));
    } else {
      FRLogger::warn("Network.insert_class_pair_info: unexpected rule");
    }
  }
  for (Rule_LayerRule* layerRule : *classClass->layerRules) {
    for (const std::string& layerName : *layerRule->layerNames) {
      int32_t layerIndex = board->layerStructure->getNo(layerName);
      if (layerIndex < 0) {
        FRLogger::warn("Network.insert_class_pair_info: layer not found at '" + layerName + "'");
        continue;
      }
      for (Rule* currentRule : *layerRule->rules) {
        if (auto* clearance = dynamic_cast<Rule_ClearanceRule*>(currentRule)) {
          rules->add(new Network_ClassPairRule(firstClass, secondClass, clearance, layerIndex));
        } else {
          FRLogger::warn("Network.insert_class_pair_info: unexpected layer rule type");
        }
      }
    }
  }
}

jrt::Set<int32_t>* Network::effectiveClearanceClasses(fr::NetClass* netClass,
                                                      std::optional<ItemClass> type) {
  jrt::Set<int32_t>* result = new jrt::LinkedHashSet<int32_t>();
  if (!type.has_value()) {
    for (ItemClass item : *fr::DefaultItemClearanceClasses_ItemClass_values()) {
      if (item != ItemClass::NONE) {
        result->add(netClass->defaultItemClearanceClasses->get(item));
      }
    }
  } else {
    result->add(netClass->defaultItemClearanceClasses->get(*type));
  }
  if ((!type.has_value() || *type == ItemClass::VIA) && netClass->getViaRule() != nullptr) {
    for (int32_t i = 0; i < netClass->getViaRule()->viaCount(); i++) {
      result->add(netClass->getViaRule()->getVia(i)->getClearanceClassIndex());
    }
  }
  return result;
}

jrt::Set<int32_t>* Network::mixedClearanceClasses(fr::ClearanceMatrix* matrix,
                                                  fr::NetClass* netClass,
                                                  const std::string& itemName) {
  std::optional<ItemClass> type;
  if (itemName == "wire") {
    type = ItemClass::TRACE;
  } else if (itemName == "smd") {
    type = ItemClass::SMD;
  } else if (itemName == "pin") {
    type = ItemClass::PIN;
  } else if (itemName == "via") {
    type = ItemClass::VIA;
  } else if (itemName == "area") {
    type = ItemClass::AREA;
  } else {
    type = std::nullopt;
  }
  if (!type.has_value()) {
    // Java Set.of(x): an immutable one-element set.
    jrt::Set<int32_t>* single = new jrt::LinkedHashSet<int32_t>();
    single->add(getClearanceClass(matrix, netClass, itemName));
    return single;
  }
  return effectiveClearanceClasses(netClass, type);
}

void Network::setMixedClearance(fr::ClearanceMatrix* matrix, jrt::Set<int32_t>* firstClasses,
                                jrt::Set<int32_t>* secondClasses, int32_t layer, int32_t value) {
  for (int32_t first : *firstClasses) {
    for (int32_t second : *secondClasses) {
      if (layer < 0) {
        matrix->setValue(first, second, value);
        matrix->setValue(second, first, value);
      } else {
        matrix->setValue(first, second, layer, value);
        matrix->setValue(second, first, layer, value);
      }
    }
  }
}

void Network::addMixedClearanceRule(fr::ClearanceMatrix* clearanceMatrix,
                                    fr::NetClass* firstClass, fr::NetClass* secondClass,
                                    Rule_ClearanceRule* clearanceRule, int32_t layerIndex,
                                    fr::io::CoordinateTransform* coordinateTransform) {
  int32_t currentClearance =
      jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(clearanceRule->value)));
  if (clearanceRule->clearanceClassPairs->isEmpty()) {
    // PORT NOTE: Java evaluates the arguments left to right; made explicit here.
    jrt::Set<int32_t>* firstClasses = effectiveClearanceClasses(firstClass, std::nullopt);
    jrt::Set<int32_t>* secondClasses = effectiveClearanceClasses(secondClass, std::nullopt);
    setMixedClearance(clearanceMatrix, firstClasses, secondClasses, layerIndex, currentClearance);
    return;
  }
  for (const std::string& pairName : *clearanceRule->clearanceClassPairs) {
    std::vector<std::string> pair = jrt::split(pairName, '_');
    if (pair.size() != 2) {
      continue;
    }
    // PORT NOTE: argument evaluation order made explicit (mixedClearanceClasses may append
    // clearance classes, so the Java left-to-right order matters).
    jrt::Set<int32_t>* a1 = mixedClearanceClasses(clearanceMatrix, firstClass, pair[0]);
    jrt::Set<int32_t>* a2 = mixedClearanceClasses(clearanceMatrix, secondClass, pair[1]);
    setMixedClearance(clearanceMatrix, a1, a2, layerIndex, currentClearance);
    jrt::Set<int32_t>* b1 = mixedClearanceClasses(clearanceMatrix, secondClass, pair[0]);
    jrt::Set<int32_t>* b2 = mixedClearanceClasses(clearanceMatrix, firstClass, pair[1]);
    setMixedClearance(clearanceMatrix, b1, b2, layerIndex, currentClearance);
  }
}

void Network::createDefaultClearanceClasses(fr::NetClass* netClass,
                                            fr::ClearanceMatrix* clearanceMatrix) {
  for (const std::string& itemClassName : *NON_WIRE_ITEM_CLASS_NAMES()) {
    getClearanceClass(clearanceMatrix, netClass, itemClassName);
  }
}

void Network::createViaRule(jrt::Collection<std::string>* useVia, fr::NetClass* netClass,
                            fr::BasicBoard* board, bool attachAllowed) {
  fr::ViaRule* newViaRule = new fr::ViaRule(netClass->getName());
  int32_t defaultViaClClass = netClass->defaultItemClearanceClasses->get(ItemClass::VIA);
  for (const std::string& currentViaName : *useVia) {
    // PORT NOTE: currentViaName is never null in C++.
    std::optional<std::string> cleanedViaName = removeDotDigits(currentViaName);
    for (int32_t i = 0; i < board->rules->viaInfos->count(); i++) {
      fr::ViaInfo* currentViaInfo = board->rules->viaInfos->get(i);
      if (currentViaInfo->getClearanceClassIndex() == defaultViaClClass) {
        if (currentViaInfo->getPadstack()->name == currentViaName ||
            (cleanedViaName.has_value() && currentViaInfo->getPadstack()->name == *cleanedViaName)) {
          newViaRule->appendVia(currentViaInfo);
        }
      }
    }
  }
  board->rules->viaRules->add(newViaRule);
  netClass->setViaRule(newViaRule);
}

void Network::createActiveTraceLayers(jrt::Collection<std::string>* useLayer,
                                      LayerStructure* layerStructure, fr::NetClass* netClass) {
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    netClass->setActiveRoutingLayer(i, false);
  }
  for (const std::string& curLayerName : *useLayer) {
    int32_t currentNo = layerStructure->getNo(curLayerName);
    netClass->setActiveRoutingLayer(currentNo, true);
  }
  // currently all inactive layers have tracewidth 0.
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    if (!netClass->isActiveRoutingLayer(i)) {
      netClass->setTraceHalfWidth(i, 0);
    }
  }
}

void Network::addClearanceRule(fr::BoardRules* rules, fr::NetClass* netClass,
                               Rule_ClearanceRule* rule, int32_t layerIndex,
                               fr::io::CoordinateTransform* coordinateTransform) {
  fr::ClearanceMatrix* clearanceMatrix = rules->clearanceMatrix;
  int32_t currentClearance = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(rule->value)));
  const std::string className = netClass->getName();
  int32_t classNo = clearanceMatrix->getNo(className);
  bool isNewClass = classNo < 0;
  if (isNewClass) {
    // class not yet existing, create a new class
    clearanceMatrix->appendClass(className);
    classNo = clearanceMatrix->getNo(className);
    netClass->defaultItemClearanceClasses->setAll(classNo);
  }
  netClass->setTraceClearanceClass(classNo);
  if (rule->clearanceClassPairs->isEmpty()) {
    if (layerIndex < 0) {
      clearanceMatrix->setValue(classNo, classNo, currentClearance);
    } else {
      clearanceMatrix->setValue(classNo, classNo, layerIndex, currentClearance);
    }
    // Against the clearance class of another net class, the larger of the two net class
    // clearances applies, as in KiCad. Against any other class, the new class gets the maximum
    // of its own effective clearance and the default value that appendClass copied in.
    jrt::Set<int32_t>* netClassClearanceClasses = new jrt::LinkedHashSet<int32_t>();
    for (int32_t i = 0; i < rules->netClasses->count(); i++) {
      netClassClearanceClasses->add(rules->netClasses->get(i)->getTraceClearanceClass());
    }
    int32_t startLayer = (layerIndex < 0 || isNewClass) ? 0 : layerIndex;
    int32_t endLayer =
        (layerIndex < 0 || isNewClass) ? clearanceMatrix->getLayerCount() : layerIndex + 1;
    for (int32_t i = 1; i < clearanceMatrix->getClassCount(); i++) {
      if (i == classNo) {
        continue;
      }
      bool otherNetClass = netClassClearanceClasses->contains(i);
      if (!otherNetClass && !isNewClass) {
        continue;
      }
      for (int32_t j = startLayer; j < endLayer; j++) {
        int32_t ownValue = clearanceMatrix->getValue(classNo, classNo, j, false);
        int32_t otherValue = otherNetClass ? clearanceMatrix->getValue(i, i, j, false)
                                           : clearanceMatrix->getValue(classNo, i, j, false);
        int32_t currentValue = jrt::Math::max(otherValue, ownValue);
        clearanceMatrix->setValue(classNo, i, j, currentValue);
        clearanceMatrix->setValue(i, classNo, j, currentValue);
      }
    }
    return;
  }
  if (Structure::containsWireClearancePair(rule->clearanceClassPairs)) {
    createDefaultClearanceClasses(netClass, clearanceMatrix);
  }
  for (const std::string& currentString : *rule->clearanceClassPairs) {
    std::vector<std::string> currentPair = jrt::split(currentString, '_');
    if (currentPair.size() != 2) {
      continue;
    }

    int32_t firstClassNo = getClearanceClass(clearanceMatrix, netClass, currentPair[0]);
    int32_t secondClassNo = getClearanceClass(clearanceMatrix, netClass, currentPair[1]);

    if (layerIndex < 0) {
      clearanceMatrix->setValue(firstClassNo, secondClassNo, currentClearance);
      clearanceMatrix->setValue(secondClassNo, firstClassNo, currentClearance);
    } else {
      clearanceMatrix->setValue(firstClassNo, secondClassNo, layerIndex, currentClearance);
      clearanceMatrix->setValue(secondClassNo, firstClassNo, layerIndex, currentClearance);
    }
  }
}

int32_t Network::getClearanceClass(fr::ClearanceMatrix* clearanceMatrix, fr::NetClass* netClass,
                                   const std::string& itemClassName) {
  if ("wire" == itemClassName) {
    return netClass->getTraceClearanceClass();
  }
  std::optional<ItemClass> itemClass;
  if (itemClassName == "via") {
    itemClass = ItemClass::VIA;
  } else if (itemClassName == "pin") {
    itemClass = ItemClass::PIN;
  } else if (itemClassName == "smd") {
    itemClass = ItemClass::SMD;
  } else if (itemClassName == "area") {
    itemClass = ItemClass::AREA;
  } else {
    itemClass = std::nullopt;
  }
  std::string name = netClass->getName() + DsnFile::CLASS_CLEARANCE_SEPARATOR + itemClassName;
  int32_t source = !itemClass.has_value() ? netClass->getTraceClearanceClass()
                                          : netClass->defaultItemClearanceClasses->get(*itemClass);
  int32_t result = copyClearanceClass(clearanceMatrix, name, source);
  if (itemClass.has_value()) {
    netClass->defaultItemClearanceClasses->set(*itemClass, result);
  }
  return result;
}

void Network::insertComponents(ReadScopeParameter* scopeParameter) {
  jrt::List<Network_PlacedComponent*>* components = new jrt::ArrayList<Network_PlacedComponent*>();
  for (ComponentPlacement* nextLibComponent : *scopeParameter->placementList) {
    for (ComponentPlacement_ComponentLocation* nextComponent : *nextLibComponent->locations) {
      components->add(new Network_PlacedComponent(nextLibComponent->libName, nextComponent));
    }
  }
  // Allocation order determines item IDs and connected-set representatives before routing.
  // Stable sorting preserves the existing first-match behavior of duplicate reference names.
  if (scopeParameter->canonicalPlacementOrder) {
    // PORT NOTE: Comparator.nullsLast(naturalOrder()) on the location name; the name is never
    // null in C++, so this is the natural String order.
    components->sort([](Network_PlacedComponent* a, Network_PlacedComponent* b) -> int32_t {
      return jrt::compareStrings(a->location()->name, b->location()->name);
    });
  }
  for (Network_PlacedComponent* component : *components) {
    insertComponent(component->location(), component->libraryName(), scopeParameter);
  }
}

bool Network::insertLogicalParts(ReadScopeParameter* scopeParameter) {
  fr::BasicBoard* routingBoard = scopeParameter->boardHandling->getRoutingBoard();
  for (PartLibrary_LogicalPart* nextPart : *scopeParameter->logicalParts) {
    fr::Package* libPackage =
        searchLibPackage(nextPart->name, scopeParameter->logicalPartMappings, routingBoard);
    if (libPackage == nullptr) {
      return false;
    }
    jrt::Array<fr::LogicalPart_PartPin*>* boardPartPins =
        new jrt::Array<fr::LogicalPart_PartPin*>(nextPart->partPins->size());
    int32_t currentIndex = 0;
    for (PartLibrary_PartPin* currentPartPin : *nextPart->partPins) {
      int32_t pinIndex = libPackage->getPinIndex(currentPartPin->pinName);
      if (pinIndex < 0) {
        FRLogger::warn("Network.insert_logical_parts: package pin not found at '" +
                       currentPartPin->pinName + "'");
        return false;
      }
      (*boardPartPins)[currentIndex] = new fr::LogicalPart_PartPin(
          pinIndex, currentPartPin->pinName, currentPartPin->gateName,
          currentPartPin->gateSwapCode, currentPartPin->gatePinName,
          currentPartPin->gatePinSwapCode);
      ++currentIndex;
    }
    routingBoard->library->logicalParts->add(nextPart->name, boardPartPins);
  }

  for (PartLibrary_LogicalPartMapping* nextMapping : *scopeParameter->logicalPartMappings) {
    fr::LogicalPart* currentLogicalPart = routingBoard->library->logicalParts->get(nextMapping->name);
    {
      if (currentLogicalPart == nullptr) {
        FRLogger::warn("Network.insert_logical_parts: logical part not found at '" +
                       nextMapping->name + "'");
      }
    }
    for (const std::string& currentCmpName : *nextMapping->components) {
      fr::Component* currentComponent = routingBoard->components->get(currentCmpName);
      if (currentComponent != nullptr) {
        currentComponent->setLogicalPart(currentLogicalPart);
      } else {
        FRLogger::warn("Network.insert_logical_parts: board component not found at '" +
                       currentCmpName + "'");
      }
    }
  }
  return true;
}

fr::Package* Network::searchLibPackage(
    const std::string& partName,
    jrt::Collection<PartLibrary_LogicalPartMapping*>* logicalPartMappings, fr::BasicBoard* board) {
  for (PartLibrary_LogicalPartMapping* currentMapping : *logicalPartMappings) {
    if (currentMapping->name == partName) {
      if (currentMapping->components->isEmpty()) {
        FRLogger::warn("Network.search_lib_package: component list empty at '" + partName + "'");
        return nullptr;
      }
      // PORT NOTE: SortedSet.getFirst() == first(); the name is never null in C++.
      std::string componentName = currentMapping->components->first();
      fr::Component* currentComponent = board->components->get(componentName);
      if (currentComponent == nullptr) {
        FRLogger::warn("Network.search_lib_package: component not found at '" + componentName +
                       "'");
        return nullptr;
      }
      return currentComponent->getPackage();
    }
  }
  FRLogger::warn("Network.search_lib_package: library package '" + partName + "' not found");
  return nullptr;
}

void Network::insertComponent(ComponentPlacement_ComponentLocation* location,
                              const std::string& libKey, ReadScopeParameter* scopeParameter) {
  fr::RoutingBoard* routingBoard = scopeParameter->boardHandling->getRoutingBoard();
  fr::Package* currentFrontPackage = routingBoard->library->packages->get(libKey, true);
  fr::Package* currentBackPackage = routingBoard->library->packages->get(libKey, false);
  if (currentFrontPackage == nullptr || currentBackPackage == nullptr) {
    FRLogger::warn("Network.insert_component: component package not found at '" +
                   scopeParameter->scanner->getScopeIdentifier() + "'");
    return;
  }

  fr::FloatPoint* exactLocation;
  if (location->coor != nullptr) {
    exactLocation = scopeParameter->coordinateTransform->dsnToBoard(location->coor);
  } else {
    exactLocation = nullptr;
  }
  double rotationInDegree = location->rotation;

  fr::Component* newComponent = routingBoard->components->addPrecise(
      location->name, exactLocation, rotationInDegree, location->isFront, currentFrontPackage,
      currentBackPackage, location->positionFixed, location->partNumber);

  if (!newComponent->isPlaced()) {
    return;  // component is not yet placed.
  }
  fr::Vector* componentTranslation = newComponent->getLocation()->differenceBy(fr::Point::ZERO());
  fr::FixedState fixedState;
  if (location->positionFixed) {
    fixedState = fr::FixedState::SYSTEM_FIXED;
  } else {
    fixedState = fr::FixedState::UNFIXED;
  }
  fr::Package* currentPackage = newComponent->getPackage();
  for (int32_t i = 0; i < currentPackage->pinCount(); i++) {
    fr::Package_Pin* currentPin = currentPackage->getPin(i);
    fr::Padstack* currentPadstack = routingBoard->library->padstacks->get(currentPin->padstackId);
    if (currentPadstack == nullptr) {
      FRLogger::warn("Network.insert_component: pin padstack not found at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return;
    }
    jrt::Collection<Net*>* pinNets = scopeParameter->netlist->getNets(location->name, currentPin->name);
    jrt::Collection<int32_t>* pinNetNumbers = new jrt::LinkedList<int32_t>();
    for (Net* currentPinNet : *pinNets) {
      fr::Net* currentBoardNet =
          routingBoard->rules->nets->get(currentPinNet->id->name, currentPinNet->id->subnetNumber);
      if (currentBoardNet == nullptr) {
        FRLogger::warn("Network.insert_component: board net not found at '" +
                       scopeParameter->scanner->getScopeIdentifier() + "'");
      } else {
        pinNetNumbers->add(currentBoardNet->netNumber);
      }
    }
    jrt::Array<int32_t>* netNumberArray = new jrt::Array<int32_t>(pinNetNumbers->size());
    int32_t netIndex = 0;
    for (int32_t currentNetNumber : *pinNetNumbers) {
      (*netNumberArray)[netIndex] = currentNetNumber;
      ++netIndex;
    }
    fr::NetClass* netClass;
    fr::Net* boardNet;
    if (netNumberArray->length > 0) {
      boardNet = routingBoard->rules->nets->get((*netNumberArray)[0]);
    } else {
      boardNet = nullptr;
    }
    if (boardNet != nullptr) {
      netClass = boardNet->getNetClass();
    } else {
      netClass = routingBoard->rules->getDefaultNetClass();
    }
    int32_t clearanceClass = -1;
    ComponentPlacement_ItemClearanceInfo* pinInfo = location->pin_infos->get(currentPin->name);
    if (pinInfo != nullptr) {
      clearanceClass = routingBoard->rules->clearanceMatrix->getNo(pinInfo->clearanceClass.value());
    }
    if (clearanceClass < 0) {
      if (currentPadstack->fromLayer() == currentPadstack->toLayer()) {
        clearanceClass = netClass->defaultItemClearanceClasses->get(ItemClass::SMD);
      } else {
        clearanceClass = netClass->defaultItemClearanceClasses->get(ItemClass::PIN);
      }
    }
    fr::Pin* newPin =
        routingBoard->insertPin(newComponent->id, i, netNumberArray, clearanceClass, fixedState);
    if (currentPadstack->hasAuxiliaryShapes()) {
      for (int32_t padLayer = 0; padLayer < currentPadstack->boardLayerCount(); padLayer++) {
        jrt::Array<fr::ConvexShape*>* auxShapes = currentPadstack->getAuxiliaryShapes(padLayer);
        if (auxShapes == nullptr || auxShapes->length == 0) {
          continue;
        }
        int32_t boardLayer;
        if (newComponent->placedOnFront() || currentPadstack->placedAbsolute) {
          boardLayer = padLayer;
        } else {
          boardLayer = currentPadstack->boardLayerCount() - padLayer - 1;
        }
        if (boardLayer < 0 || boardLayer >= routingBoard->getLayerCount()) {
          continue;
        }
        for (fr::ConvexShape* aux : *auxShapes) {
          fr::Shape* transformedAux = newPin->transformToBoard(aux);
          fr::Area* auxArea = nullptr;
          if (auto* polyShape = dynamic_cast<fr::PolylineShape*>(transformedAux)) {
            auxArea = new fr::PolylineArea(polyShape, new jrt::Array<fr::PolylineShape*>(0));
          } else if (transformedAux != nullptr) {
            auxArea = transformedAux;
          }
          if (auxArea != nullptr && !auxArea->isEmpty()) {
            routingBoard->insertObstacle(auxArea, boardLayer, netNumberArray, clearanceClass,
                                         newComponent->id, currentPin->name + "_aux", fixedState);
          }
        }
      }
    }
  }

  // insert the keepouts belonging to the package (k = 1 for via keepouts)
  for (int32_t k = 0; k <= 2; k++) {
    jrt::Array<fr::Package_Keepout*>* keepouts;
    jrt::Map<std::string, ComponentPlacement_ItemClearanceInfo*>* currentKeepoutInfos;
    if (k == 0) {
      keepouts = currentPackage->keepouts;
      currentKeepoutInfos = location->keepout_infos;
    } else if (k == 1) {
      keepouts = currentPackage->viaKeepouts;
      currentKeepoutInfos = location->via_keepout_infos;
    } else {
      keepouts = currentPackage->placeKeepoutArr;
      currentKeepoutInfos = location->place_keepout_infos;
    }
    for (int32_t i = 0; i < keepouts->length; i++) {
      fr::Package_Keepout* currentKeepout = (*keepouts)[i];
      int32_t layer = currentKeepout->layer;
      if (layer >= routingBoard->getLayerCount()) {
        FRLogger::warn("Network.insert_component: keepout layer is to big at '" +
                       scopeParameter->scanner->getScopeIdentifier() + "'");
        continue;
      }
      if (layer >= 0 && !location->isFront) {
        layer = routingBoard->getLayerCount() - currentKeepout->layer - 1;
      }
      int32_t clearanceClass =
          routingBoard->rules->getDefaultNetClass()->defaultItemClearanceClasses->get(
              ItemClass::AREA);
      ComponentPlacement_ItemClearanceInfo* keepoutInfo =
          currentKeepoutInfos->get(currentKeepout->name);
      if (keepoutInfo != nullptr) {
        int32_t currentClearanceClass =
            routingBoard->rules->clearanceMatrix->getNo(keepoutInfo->clearanceClass.value());
        if (currentClearanceClass > 0) {
          clearanceClass = currentClearanceClass;
        }
      }
      if (layer >= 0) {
        if (k == 0) {
          routingBoard->insertObstacle(currentKeepout->area, layer, componentTranslation,
                                       rotationInDegree, !location->isFront, clearanceClass,
                                       newComponent->id, currentKeepout->name, fixedState);
        } else if (k == 1) {
          routingBoard->insertViaObstacle(currentKeepout->area, layer, componentTranslation,
                                          rotationInDegree, !location->isFront, clearanceClass,
                                          newComponent->id, currentKeepout->name, fixedState);
        } else {
          routingBoard->insertComponentObstacle(currentKeepout->area, layer, componentTranslation,
                                                rotationInDegree, !location->isFront,
                                                clearanceClass, newComponent->id,
                                                currentKeepout->name, fixedState);
        }
      } else {
        // insert the obstacle on all signal layers
        for (int32_t j = 0; j < routingBoard->layerStructure->layers->length; j++) {
          if ((*routingBoard->layerStructure->layers)[j]->isSignal) {
            if (k == 0) {
              routingBoard->insertObstacle(currentKeepout->area, j, componentTranslation,
                                           rotationInDegree, !location->isFront, clearanceClass,
                                           newComponent->id, currentKeepout->name, fixedState);
            } else if (k == 1) {
              routingBoard->insertViaObstacle(currentKeepout->area, j, componentTranslation,
                                              rotationInDegree, !location->isFront,
                                              clearanceClass, newComponent->id,
                                              currentKeepout->name, fixedState);
            } else {
              routingBoard->insertComponentObstacle(currentKeepout->area, j, componentTranslation,
                                                    rotationInDegree, !location->isFront,
                                                    clearanceClass, newComponent->id,
                                                    currentKeepout->name, fixedState);
            }
          }
        }
      }
    }
  }
  // insert the outline as component keepout
  int32_t courtyardIdx = -1;
  if (currentPackage->outline != nullptr && currentPackage->outline->length > 1) {
    double maxArea = -1;
    for (int32_t i = 0; i < currentPackage->outline->length; i++) {
      if ((*currentPackage->outline)[i] != nullptr &&
          (*currentPackage->outline)[i]->boundingBox() != nullptr) {
        double area = (*currentPackage->outline)[i]->boundingBox()->area();
        if (area > maxArea) {
          maxArea = area;
          courtyardIdx = i;
        }
      }
    }
  }
  if (currentPackage->outline != nullptr) {
    for (int32_t i = 0; i < currentPackage->outline->length; i++) {
      bool isCourtyard = i == courtyardIdx;
      if (currentPackage->outlineWidths != nullptr && i < currentPackage->outlineWidths->length) {
        if ((*currentPackage->outlineWidths)[i] == 0.0) {
          isCourtyard = true;
        }
      }
      bool isFabrication = false;
      if (!isCourtyard && currentPackage->outlineWidths != nullptr &&
          i < currentPackage->outlineWidths->length) {
        if ((*currentPackage->outlineWidths)[i] <= 110.0) {
          isFabrication = true;
        }
      }
      bool isClosed = false;
      if (currentPackage->outlineIsClosed != nullptr &&
          i < currentPackage->outlineIsClosed->length) {
        isClosed = (*currentPackage->outlineIsClosed)[i];
      }
      routingBoard->insertComponentOutline((*currentPackage->outline)[i], location->isFront,
                                           componentTranslation, rotationInDegree,
                                           newComponent->id, isCourtyard, isFabrication, isClosed,
                                           fixedState);
    }
  }
}

bool Network::readScope(ReadScopeParameter* scopeParameter) {
  jrt::Collection<NetClass*>* classes = new jrt::LinkedList<NetClass*>();
  jrt::Collection<NetClass_ClassClass*>* classClassList =
      new jrt::LinkedList<NetClass_ClassClass*>();
  jrt::Collection<fr::ViaInfo*>* viaInfos = new jrt::LinkedList<fr::ViaInfo*>();
  jrt::Collection<jrt::Collection<std::string>*>* viaRules =
      new jrt::LinkedList<jrt::Collection<std::string>*>();
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Network.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Network.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == Keyword::NET()) {
        readNetScope(scopeParameter->scanner, scopeParameter->netlist,
                     scopeParameter->boardHandling->getRoutingBoard(),
                     scopeParameter->coordinateTransform, scopeParameter->layerStructure);
      } else if (nextToken == Keyword::VIA()) {
        fr::ViaInfo* currentViaInfo =
            readViaInfo(scopeParameter->scanner, scopeParameter->boardHandling->getRoutingBoard());
        if (currentViaInfo == nullptr) {
          return false;
        }
        viaInfos->add(currentViaInfo);
      } else if (nextToken == Keyword::VIA_RULE()) {
        jrt::Collection<std::string>* currentViaRule =
            readViaRule(scopeParameter->scanner, scopeParameter->boardHandling->getRoutingBoard());
        if (currentViaRule == nullptr) {
          return false;
        }
        viaRules->add(currentViaRule);
      } else if (nextToken == Keyword::CLASS()) {
        NetClass* currentClass = NetClass::readScope(scopeParameter->scanner);
        if (currentClass == nullptr) {
          return false;
        }
        classes->add(currentClass);
      } else if (nextToken == Keyword::CLASS_CLASS()) {
        NetClass_ClassClass* currentClassClass =
            NetClass::readClassClassScope(scopeParameter->scanner);
        if (currentClassClass == nullptr) {
          return false;
        }
        classClassList->add(currentClassClass);
      } else {
        skipScope(scopeParameter->scanner);
      }
    }
  }

  // Add any vias defined in the Netclasses to the list of vias to be instantiated
  for (NetClass* n : *classes) {
    if (scopeParameter->viaPadstackNames != nullptr) {
      scopeParameter->viaPadstackNames->addAll(n->useVia);
    } else {
      scopeParameter->viaPadstackNames = new jrt::ArrayList<std::string>(n->useVia);
    }
  }

  fr::RoutingBoard* board = scopeParameter->boardHandling->getRoutingBoard();

  // Set the via padstacks after network parsing, so that named vias from both structure and
  // network DSN sections are properly instantiated .
  if (scopeParameter->viaPadstackNames != nullptr) {
    jrt::Array<fr::Padstack*>* viaPadstacks =
        new jrt::Array<fr::Padstack*>(scopeParameter->viaPadstackNames->size());
    jrt::Iterator<std::string>* it = scopeParameter->viaPadstackNames->iterator();
    int32_t foundPadstackCount = 0;
    for (int32_t i = 0; i < viaPadstacks->length; i++) {
      std::string currentPadstackName = it->next();
      fr::Padstack* currentPadstack = board->library->padstacks->get(currentPadstackName);
      if (currentPadstack == nullptr) {
        currentPadstack = board->library->padstacks->get(removeDotDigits(currentPadstackName));
      }
      if (currentPadstack != nullptr) {
        (*viaPadstacks)[foundPadstackCount] = currentPadstack;
        ++foundPadstackCount;
      } else {
        FRLogger::warn("Library.read_scope: via padstack with name '" + currentPadstackName +
                       " not found at '" + scopeParameter->scanner->getScopeIdentifier() + "'");
      }
    }
    if (foundPadstackCount != viaPadstacks->length) {
      // Some via padstacks were not found in the padstacks scope of the dsn-file.
      jrt::Array<fr::Padstack*>* correctedPadstacks =
          new jrt::Array<fr::Padstack*>(foundPadstackCount);
      jrt::arraycopy(viaPadstacks, 0, correctedPadstacks, 0, foundPadstackCount);
      viaPadstacks = correctedPadstacks;
    }
    board->library->setViaPadstacks(viaPadstacks);
  }

  insertViaInfos(viaInfos, scopeParameter->boardHandling->getRoutingBoard(),
                 scopeParameter->viaAtSmdAllowed);
  insertViaRules(viaRules, scopeParameter->boardHandling->getRoutingBoard());
  insertNetClasses(classes, scopeParameter);
  insertClassPairs(classClassList, scopeParameter);
  insertComponents(scopeParameter);
  insertLogicalParts(scopeParameter);
  // The outline was cut before the net classes existed; a narrower class needs finer slices.
  fr::BoardOutline* outline = scopeParameter->boardHandling->getRoutingBoard()->getOutline();
  if (outline != nullptr) {
    outline->updateSlicesToTraceWidths();
  }
  return true;
}

bool Network::readNetScope(IJFlexScanner* scanner, NetList* netList, fr::RoutingBoard* board,
                           fr::io::CoordinateTransform* coordinateTransform,
                           LayerStructure* layerStructure) {
  // read the net name
  const std::string netName = scanner->nextString();

  jrt::Object* nextToken;
  int32_t subnetNumber = 1;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException& e) {
    FRLogger::error("Network.read_net_scope: IO error while scanning file", &e);
    return false;
  }
  bool scopeIsEmpty = nextToken == CLOSED_BRACKET();
  if (auto* integer = dynamic_cast<JInteger*>(nextToken)) {
    subnetNumber = integer->value;
  }
  bool pinOrderFound = false;
  jrt::Collection<Net_Pin*>* pinList = new jrt::LinkedList<Net_Pin*>();
  jrt::Collection<Rule*>* netRules = new jrt::LinkedList<Rule*>();
  Circuit_ReadScopeResult* netCircuit = nullptr;
  jrt::Collection<jrt::Collection<Net_Pin*>*>* subnetPinLists =
      new jrt::LinkedList<jrt::Collection<Net_Pin*>*>();
  if (!scopeIsEmpty) {
    for (;;) {
      jrt::Object* prevToken = nextToken;
      try {
        nextToken = scanner->nextToken();
      } catch (const jrt::IOException& e) {
        FRLogger::error("Network.read_net_scope: IO error scanning file", &e);
        return false;
      }
      if (nextToken == nullptr) {
        FRLogger::warn("Network.read_net_scope: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      if (nextToken == CLOSED_BRACKET()) {
        // end of scope
        break;
      }
      if (prevToken == OPEN_BRACKET()) {
        if (nextToken == Keyword::PINS()) {
          if (!readNetPins(scanner, pinList)) {
            return false;
          }
        } else if (nextToken == Keyword::ORDER()) {
          pinOrderFound = true;
          if (!readNetPins(scanner, pinList)) {
            return false;
          }
        } else if (nextToken == Keyword::FROMTO()) {
          jrt::Set<Net_Pin*>* currentSubnetPinList = new jrt::TreeSet<Net_Pin*>();
          if (!readNetPins(scanner, currentSubnetPinList)) {
            return false;
          }
          subnetPinLists->add(currentSubnetPinList);
        } else if (nextToken == Keyword::RULE()) {
          addAllNonNull(netRules, Rule::readScope(scanner));
        } else if (nextToken == Keyword::CIRCUIT()) {
          Circuit_ReadScopeResult* circuitResult = Circuit::readScope(scanner);
          if (circuitResult != nullptr) {
            netCircuit = circuitResult;
          }
        } else if (nextToken == Keyword::LAYER_RULE()) {
          FRLogger::warn("Network.read_net_scope: layer_rule not yet implemented at '" +
                         scanner->getScopeIdentifier() + "'");
          skipScope(scanner);
        } else {
          skipScope(scanner);
        }
      }
    }
  }
  if (subnetPinLists->isEmpty()) {
    if (pinOrderFound) {
      subnetPinLists = createOrderedSubnets(pinList);
    } else {
      subnetPinLists->add(pinList);
    }
  }
  for (jrt::Collection<Net_Pin*>* currentPinList : *subnetPinLists) {
    Net_Id* netId = new Net_Id(netName, subnetNumber);
    if (!netList->contains(netId)) {
      Net* newNet = netList->addNet(netId);
      bool containsPlane = layerStructure->containsPlane(netName);
      if (newNet != nullptr) {
        board->rules->nets->add(newNet->id->name, newNet->id->subnetNumber, containsPlane);
      }
    }
    Net* currentSubnet = netList->getNet(netId);
    if (currentSubnet == nullptr) {
      FRLogger::warn("Network.read_net_scope: net not found in netlist at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }
    currentSubnet->setPins(currentPinList);
    fr::Net* boardNet =
        board->rules->nets->get(currentSubnet->id->name, currentSubnet->id->subnetNumber);
    if (boardNet != nullptr && netCircuit != nullptr) {
      double max =
          (netCircuit->maxLength > 0) ? coordinateTransform->dsnToBoard(netCircuit->maxLength) : 0.0;
      double min =
          (netCircuit->minLength > 0) ? coordinateTransform->dsnToBoard(netCircuit->minLength) : 0.0;
      if (max > 0 || min > 0) {
        boardNet->setLengthConstraint(new fr::NetLengthConstraint(min, max));
      }
      if (netCircuit->hasMeanderConstraint()) {
        boardNet->setMeanderConstraint(netCircuit->toMeanderConstraint(
            fr::MeanderPatternType::SINGLE_TRACK, coordinateTransform));
      }
    }
    if (!netRules->isEmpty()) {
      // Evaluate the net rules.
      if (boardNet == nullptr) {
        FRLogger::warn("Network.read_net_scope: board net not found at '" +
                       scanner->getScopeIdentifier() + "'");
        return false;
      }
      for (Rule* currentObject : *netRules) {
        if (auto* rule = dynamic_cast<Rule_WidthRule*>(currentObject)) {
          fr::NetClass* defaultNetRule = board->rules->getDefaultNetClass();
          const double wireWidth = rule->value;
          int32_t traceHalfwidth =
              jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard(wireWidth) / 2));
          fr::NetClass* netRule = board->rules->netClasses->find(
              traceHalfwidth, defaultNetRule->getTraceClearanceClass(),
              defaultNetRule->getViaRule());
          if (netRule == nullptr) {
            // create a new net rule
            netRule = board->rules->getNewNetClass();
          }
          netRule->setTraceHalfWidth(traceHalfwidth);
          boardNet->setClass(netRule);
        } else if (auto* lengthRule = dynamic_cast<Rule_LengthRule*>(currentObject)) {
          double max = (lengthRule->maxLength > 0)
                           ? coordinateTransform->dsnToBoard(lengthRule->maxLength)
                           : 0.0;
          double min = (lengthRule->minLength > 0)
                           ? coordinateTransform->dsnToBoard(lengthRule->minLength)
                           : 0.0;
          if (max > 0 || min > 0) {
            boardNet->setLengthConstraint(new fr::NetLengthConstraint(min, max));
          }
        } else if (auto* meanderRule = dynamic_cast<Rule_MeanderRule*>(currentObject)) {
          boardNet->setMeanderConstraint(
              meanderRule->applyTo(boardNet->getMeanderConstraint(), coordinateTransform));
        } else {
          FRLogger::warn("Network.read_net_scope: Rule not yet implemented at '" +
                         scanner->getScopeIdentifier() + "'");
        }
      }
    }
    ++subnetNumber;
  }
  return true;
}

}  // namespace fr::dsn
