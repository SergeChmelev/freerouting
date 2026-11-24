#include "io/kicad/KiCadDesignRulesTranslator.h"

#include <unordered_map>

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/state/Communication.h"
#include "core/library/Package.h"
#include "io/kicad/KiCadDesignRules.h"
#include "io/kicad/KiCadRuleCondition.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/NetClasses.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "rules/ViaRule.h"

namespace fr {

namespace {

using Tri = KiCadRuleCondition_Tri;
using ItemType = KiCadRuleCondition_ItemType;
using ItemClass = DefaultItemClearanceClasses_ItemClass;

/** Arrays.toString(int[]). */
std::string arrayToString(const std::vector<int32_t>& values) {
  std::string result = "[";
  for (std::size_t i = 0; i < values.size(); i++) {
    if (i > 0) result += ", ";
    result += jrt::str(values[i]);
  }
  return result + "]";
}

/** List.toString() of strings. */
std::string listToString(const std::vector<std::string>& values) {
  std::string result = "[";
  for (std::size_t i = 0; i < values.size(); i++) {
    if (i > 0) result += ", ";
    result += values[i];
  }
  return result + "]";
}

}  // namespace

KiCadDesignRulesTranslator::KiCadDesignRulesTranslator(BasicBoard* board)
    : board(board),
      matrix(board->rules->clearanceMatrix),
      layerCount(board->rules->clearanceMatrix->getLayerCount()),
      warnings(new (jrt::kOp) jrt::ArrayList<std::string>()) {}

KiCadDesignRulesTranslator_Result* KiCadDesignRulesTranslator::apply(BasicBoard* board,
                                                                     KiCadDesignRules* designRules) {
  return (new (jrt::kOp) KiCadDesignRulesTranslator(board))->run(designRules);
}

KiCadDesignRulesTranslator_Result* KiCadDesignRulesTranslator::run(KiCadDesignRules* designRules) {
  std::string source = designRules->sourceName();
  for (const std::string& warning : *designRules->warnings()) {
    warnings->add(source + ": " + warning);
  }
  std::vector<ClearanceRule> rules = clearanceRules(designRules);
  resetSmdToSmdClearance();
  warnImageKeepouts();
  warnDuplicateReferences();
  if (rules.empty()) {
    return new (jrt::kOp) Result(0, 0, {}, warnings);
  }

  // 1. The copper items, by entity
  std::vector<Entity> entities;  // Java: LinkedHashMap<EntityKey, Entity>
  std::map<EntityKey, int32_t> entityIndex;
  std::vector<std::pair<Item*, EntityKey>> itemEntities;  // Java: LinkedHashMap<Item, EntityKey>
  collectEntities(entities, entityIndex, itemEntities);
  warnNamesMatchingNothing(rules, entities);

  // 2. Cells: entities that every condition treats alike, with the same clearance class
  const std::vector<Entity>& entityList = entities;
  std::unordered_map<std::string, int32_t> cellOfSignature;
  std::vector<Entity> cellRepresentatives;
  std::map<EntityKey, int32_t> cellOfEntity;
  for (const Entity& entity : entityList) {
    std::string signature = jrt::str(entity.key.clearanceClass);
    for (const ClearanceRule& rule : rules) {
      signature += '|';
      if (rule.condition != nullptr) {
        signature += rule.condition->signature(entity.item);
      }
    }
    auto found = cellOfSignature.find(signature);
    int32_t cell;
    if (found == cellOfSignature.end()) {
      cellRepresentatives.push_back(entity);
      cell = static_cast<int32_t>(cellRepresentatives.size()) - 1;
      cellOfSignature.emplace(signature, cell);
    } else {
      cell = found->second;
    }
    cellOfEntity[entity.key] = cell;
  }

  // 3. The clearance KiCad requires between two cells, where it differs from the design
  int32_t cells = static_cast<int32_t>(cellRepresentatives.size());
  std::vector<std::vector<std::optional<std::vector<int32_t>>>> custom(
      cells, std::vector<std::optional<std::vector<int32_t>>>(cells));
  for (int32_t i = 0; i < cells; i++) {
    for (int32_t j = i; j < cells; j++) {
      std::optional<std::vector<int32_t>> values =
          customClearance(rules, cellRepresentatives[i], cellRepresentatives[j]);
      custom[i][j] = values;
      custom[j][i] = values;
    }
  }

  // 4. Groups of cells with the same clearance class and the same clearances to every cell
  std::unordered_map<std::string, int32_t> groupOfSignature;
  std::vector<int32_t> groupOfCell(cells);
  std::vector<int32_t> groupFirstCell;
  for (int32_t i = 0; i < cells; i++) {
    std::string signature = jrt::str(cellRepresentatives[i].key.clearanceClass);
    for (int32_t j = 0; j < cells; j++) {
      signature += '|';
      signature += !custom[i][j].has_value() ? std::string() : arrayToString(*custom[i][j]);
    }
    auto found = groupOfSignature.find(signature);
    if (found == groupOfSignature.end()) {
      groupFirstCell.push_back(i);
      groupOfCell[i] = static_cast<int32_t>(groupFirstCell.size()) - 1;
      groupOfSignature.emplace(signature, groupOfCell[i]);
    } else {
      groupOfCell[i] = found->second;
    }
  }

  // 5. A new clearance class for each group whose clearances change
  int32_t groups = static_cast<int32_t>(groupFirstCell.size());
  std::vector<int32_t> classOfGroup(groups);
  int32_t added = 0;
  for (int32_t g = 0; g < groups; g++) {
    int32_t cell = groupFirstCell[g];
    int32_t base = cellRepresentatives[cell].key.clearanceClass;
    bool changed = false;
    for (int32_t j = 0; j < cells && !changed; j++) {
      changed = custom[cell][j].has_value();
    }
    classOfGroup[g] = changed ? copyClass(base) : base;
    added += changed ? 1 : 0;
  }
  for (int32_t g = 0; g < groups; g++) {
    for (int32_t h = g; h < groups; h++) {
      const std::optional<std::vector<int32_t>>& values = custom[groupFirstCell[g]][groupFirstCell[h]];
      if (!values.has_value()) {
        continue;
      }
      for (int32_t layer = 0; layer < layerCount; layer++) {
        if ((*values)[layer] >= 0) {
          matrix->setValue(classOfGroup[g], classOfGroup[h], layer, (*values)[layer]);
          matrix->setValue(classOfGroup[h], classOfGroup[g], layer, (*values)[layer]);
        }
      }
    }
  }
  std::map<EntityKey, int32_t> newClass;
  for (const auto& entry : cellOfEntity) {
    newClass[entry.first] = classOfGroup[groupOfCell[entry.second]];
  }

  // 6. The items, and the net classes for the items the router will add
  for (const auto& entry : itemEntities) {
    int32_t target = newClass.at(entry.second);
    if (entry.first->clearanceClassIndex() != target) {
      entry.first->setClearanceClassIndex(target);
    }
  }
  std::vector<std::pair<std::string, std::vector<std::string>>> split = updateNetClasses(newClass);
  if (added > 0) {
    board->searchTreeManager->reinsertTreeItems();
  }
  return new (jrt::kOp) Result(static_cast<int32_t>(rules.size()), added, split, warnings);
}

// ------------------------------------------------------------------------------------------

std::vector<KiCadDesignRulesTranslator_ClearanceRule> KiCadDesignRulesTranslator::clearanceRules(
    KiCadDesignRules* designRules) {
  std::vector<ClearanceRule> result;
  for (KiCadDesignRules_Rule* rule : *designRules->rules()) {
    std::string prefix = "rule '" + rule->name() + "': ";
    for (const std::string& warning : *rule->warnings()) {
      warnings->add(prefix + warning);
    }
    for (const std::string& constraint : *rule->otherConstraints()) {
      warnings->add(prefix + "constraint " + constraint +
                    " not translated (only minimum clearances and creepage are translated)");
    }
    std::optional<double> effectiveClearanceUm = rule->clearanceUm();
    if (rule->creepageUm().has_value()) {
      effectiveClearanceUm = !effectiveClearanceUm.has_value()
                                 ? *rule->creepageUm()
                                 : jrt::Math::max(*effectiveClearanceUm, *rule->creepageUm());
    }
    if (!effectiveClearanceUm.has_value()) {
      continue;
    }
    if (rule->ignored()) {
      warnings->add(prefix + "severity ignore: the clearance is not relaxed, the rule is skipped");
      continue;
    }
    if (rule->creepageUm().has_value()) {
      warnings->add(prefix + "constraint creepage " + KiCadDesignRules::formatMm(*rule->creepageUm()) +
                    " applied conservatively as routing clearance" +
                    " (slots and milling cutouts are not modelled)");
    }
    std::vector<bool> layers(layerCount, false);
    if (!rule->layer().has_value()) {
      std::fill(layers.begin(), layers.end(), true);
    } else if (jrt::equalsIgnoreCase("outer", *rule->layer())) {
      layers[0] = true;
      layers[layerCount - 1] = true;
    } else if (jrt::equalsIgnoreCase("inner", *rule->layer())) {
      for (int32_t layer = 1; layer < layerCount - 1; layer++) {
        layers[layer] = true;
      }
    } else {
      bool any = false;
      for (int32_t layer = 0; layer < layerCount; layer++) {
        layers[layer] = KiCadRuleCondition::wildcardMatches(
            *rule->layer(), (*board->layerStructure->layers)[layer]->name, false);
        any |= layers[layer];
      }
      if (!any) {
        warnings->add(prefix + "layer '" + *rule->layer() +
                      "' is not a copper layer of the design, skipped");
        continue;
      }
    }
    if (rule->condition() != nullptr && !rule->condition()->unsupportedParts()->isEmpty()) {
      std::vector<std::string> parts;
      for (const std::string& part : *rule->condition()->unsupportedParts()) {
        parts.push_back(part);
      }
      warnings->add(prefix + jrt::join(", ", parts) +
                    " cannot be evaluated: the rule is applied wherever it may match");
    }
    int32_t value =
        jrt::d2i(jrt::Math::ceil(board->communication->physicalToBoard(*effectiveClearanceUm, Unit::UM)));
    result.push_back(ClearanceRule{rule->name(), layers, rule->condition(), value});
  }
  return result;
}

void KiCadDesignRulesTranslator::resetSmdToSmdClearance() {
  int32_t smdClass = matrix->getNo("smd");
  int32_t defaultClass = matrix->getNo("default");
  if (smdClass <= 0 || defaultClass <= 0) {
    return;
  }
  for (int32_t layer = 0; layer < layerCount; layer++) {
    int32_t defaultValue = matrix->getValue(defaultClass, defaultClass, layer, false);
    if (matrix->getValue(smdClass, smdClass, layer, false) < defaultValue) {
      matrix->setValue(smdClass, smdClass, layer, defaultValue);
    }
  }
}

void KiCadDesignRulesTranslator::warnImageKeepouts() {
  int32_t keepoutCount = 0;
  for (Item* item : *board->getItems()) {
    if (dynamic_cast<ObstacleArea*>(item) != nullptr && item->getComponentId() > 0) {
      keepoutCount++;
    }
  }
  if (keepoutCount > 0) {
    warnings->add("board has " + jrt::str(keepoutCount) +
                  " footprint image keepout(s) (such as pads without a net);" +
                  " .kicad_dru clearance rules are not applied to them");
  }
}

void KiCadDesignRulesTranslator::warnDuplicateReferences() {
  auto* names = new (jrt::kOp) jrt::HashSet<std::string>();
  for (int32_t i = 1; i <= board->components->count(); i++) {
    Component* c = board->components->get(i);
    if (c != nullptr) {
      names->add(c->name);
    }
  }
  for (const std::string& name : *names) {
    int32_t underscore = jrt::lastIndexOf(name, "_");
    if (underscore > 0 && underscore < static_cast<int32_t>(name.size()) - 1) {
      std::string base = jrt::substring(name, 0, underscore);
      std::string suffix = jrt::substring(name, underscore + 1);
      bool digits = true;
      for (char ch : suffix) {
        digits &= jrt::Character::isDigit(static_cast<unsigned char>(ch));
      }
      if (names->contains(base) && digits) {
        warnings->add("component '" + name + "' appears to be a KiCad-renamed duplicate of '" +
                      base + "'; memberOfFootprint rules for '" + base + "' may not match it");
      }
    }
  }
}

void KiCadDesignRulesTranslator::warnNamesMatchingNothing(const std::vector<ClearanceRule>& rules,
                                                          const std::vector<Entity>& entities) {
  auto* items = new (jrt::kOp) jrt::ArrayList<KiCadRuleCondition_Item*>();
  for (const Entity& entity : entities) {
    items->add(entity.item);
  }
  for (const ClearanceRule& rule : rules) {
    if (rule.condition == nullptr) {
      continue;
    }
    for (const std::string& part : *rule.condition->namesMatchingNothing(items)) {
      warnings->add("rule '" + rule.name + "': " + part +
                    " matches no net class, net or footprint of the design (misspelt or renamed?)");
    }
  }
}

std::string KiCadDesignRulesTranslator::kicadClassName(NetClass* netClass) {
  if (netClass == nullptr || netClass->getName() == "kicad_default") {
    return "Default";
  }
  return netClass->getName();
}

void KiCadDesignRulesTranslator::collectEntities(std::vector<Entity>& entities,
                                                 std::map<EntityKey, int32_t>& entityIndex,
                                                 std::vector<std::pair<Item*, EntityKey>>& items) {
  // The traces and vias the router may still add: the classes of the net classes
  for (int32_t netNo = 1; netNo <= board->rules->nets->maxNetNumber(); netNo++) {
    Net* net = board->rules->nets->get(netNo);
    if (net == nullptr || net->getNetClass() == nullptr) {
      continue;
    }
    NetClass* netClass = net->getNetClass();
    addEntity(entities, entityIndex, Kind::TRACE, net, netClass->getTraceClearanceClass(), nullptr);
    addEntity(entities, entityIndex, Kind::VIA, net,
              netClass->defaultItemClearanceClasses->get(ItemClass::VIA), nullptr);
    ViaRule* viaRule = netClass->getViaRule();
    for (int32_t i = 0; viaRule != nullptr && i < viaRule->viaCount(); i++) {
      addEntity(entities, entityIndex, Kind::VIA, net, viaRule->getVia(i)->getClearanceClassIndex(),
                nullptr);
    }
  }
  // The items of the board
  for (Item* item : *board->getItems()) {
    Kind kind;
    if (dynamic_cast<Pin*>(item) != nullptr) {
      kind = Kind::PIN;
    } else if (dynamic_cast<Trace*>(item) != nullptr) {
      kind = Kind::TRACE;
    } else if (dynamic_cast<Via*>(item) != nullptr) {
      kind = Kind::VIA;
    } else if (dynamic_cast<ConductionArea*>(item) != nullptr && item->netCount() > 0) {
      kind = Kind::AREA;
    } else {
      continue;
    }
    Net* net = item->netCount() > 0 ? board->rules->nets->get(item->getNetNumber(0)) : nullptr;
    if (kind != Kind::PIN && net == nullptr) {
      continue;
    }
    items.emplace_back(item, addEntity(entities, entityIndex, kind, net,
                                       item->clearanceClassIndex(), item));
  }
}

KiCadDesignRulesTranslator_EntityKey KiCadDesignRulesTranslator::addEntity(
    std::vector<Entity>& entities, std::map<EntityKey, int32_t>& entityIndex, Kind kind, Net* net,
    int32_t clearanceClass, Item* pinItem) {
  int32_t netNo = net == nullptr ? 0 : net->netNumber;
  int32_t pinId = kind == Kind::PIN ? pinItem->getId() : 0;
  EntityKey key{kind, netNo, clearanceClass, pinId};
  if (entityIndex.find(key) == entityIndex.end()) {
    entityIndex.emplace(key, static_cast<int32_t>(entities.size()));
    entities.push_back(Entity{key, describe(kind, net, pinItem)});
  }
  return key;
}

KiCadRuleCondition_Item* KiCadDesignRulesTranslator::describe(Kind kind, Net* net, Item* pinItem) {
  std::string netName = net == nullptr ? "" : net->name;
  std::string netClass = kicadClassName(net == nullptr ? nullptr : net->getNetClass());
  if (kind != Kind::PIN) {
    ItemType type = kind == Kind::TRACE ? ItemType::TRACK : kind == Kind::VIA ? ItemType::VIA : ItemType::ZONE;
    return new (jrt::kOp) KiCadRuleCondition_Item(type, netName, netClass, std::nullopt, std::nullopt, false);
  }
  auto* pin = jrt::fast_cast<Pin>(pinItem);
  Component* component = board->components->get(pin->getComponentId());
  std::optional<std::string> padNumber = pin->name();
  if (padNumber.has_value() && jrt::indexOf(*padNumber, "@") > 0) {
    padNumber = jrt::substring(*padNumber, 0, jrt::indexOf(*padNumber, "@"));  // duplicated pad numbers
  }
  return new (jrt::kOp) KiCadRuleCondition_Item(
      ItemType::PAD, netName, netClass,
      component == nullptr ? std::nullopt : std::optional<std::string>(component->name), padNumber,
      pin->firstLayer() == pin->lastLayer(), libraryId(component));
}

std::optional<std::string> KiCadDesignRulesTranslator::libraryId(Component* component) {
  if (component == nullptr || component->getPackage() == nullptr) {
    return std::nullopt;
  }
  std::string name = component->getPackage()->name;
  int32_t variant = jrt::lastIndexOf(name, "::");
  if (variant > 0 && variant + 2 < static_cast<int32_t>(name.size())) {
    bool digits = true;
    for (int32_t i = variant + 2; i < static_cast<int32_t>(name.size()); i++) {
      digits &= jrt::Character::isDigit(static_cast<unsigned char>(name[i]));
    }
    if (digits) {
      name = jrt::substring(name, 0, variant);
    }
  }
  return name;
}

std::optional<std::vector<int32_t>> KiCadDesignRulesTranslator::customClearance(
    const std::vector<ClearanceRule>& rules, const Entity& a, const Entity& b) {
  int32_t classA = a.key.clearanceClass;
  int32_t classB = b.key.clearanceClass;
  std::optional<std::vector<int32_t>> result;
  std::vector<Tri> matches(rules.size());
  for (std::size_t r = 0; r < rules.size(); r++) {
    const ClearanceRule& rule = rules[r];
    matches[r] = rule.condition == nullptr ? Tri::TRUE_ : rule.condition->evaluate(a.item, b.item);
  }
  for (int32_t layer = 0; layer < layerCount; layer++) {
    int32_t base = matrix->getValue(classA, classB, layer, false);
    // The possible outcomes of the rules on this layer: the clearance of the design, unless a
    // rule surely matches, and the value of every rule that may match after it.
    bool designPossible = true;
    int32_t best = -1;
    for (std::size_t r = 0; r < rules.size(); r++) {
      const ClearanceRule& rule = rules[r];
      if (!rule.layers[layer]) {
        continue;
      }
      Tri match = matches[r];
      if (match == Tri::TRUE_) {
        designPossible = false;
        best = rule.value;
      } else if (match == Tri::UNKNOWN) {
        best = jrt::Math::max(best, rule.value);
      }
    }
    if (best < 0) {
      continue;
    }
    int32_t value = designPossible ? jrt::Math::max(best, base) : best;
    // the matrix stores even values, rounded up
    if ((value + 1) / 2 * 2 == base) {
      continue;
    }
    if (!result.has_value()) {
      result = std::vector<int32_t>(layerCount, -1);
    }
    (*result)[layer] = value;
  }
  return result;
}

int32_t KiCadDesignRulesTranslator::copyClass(int32_t source) {
  std::string baseName = matrix->getName(source) + SUFFIX;
  int32_t index = 1;
  while (matrix->getNo(baseName + jrt::str(index)) >= 0) {
    index++;
  }
  std::string name = baseName + jrt::str(index);
  matrix->appendClass(name);
  int32_t result = matrix->getNo(name);
  for (int32_t layer = 0; layer < layerCount; layer++) {
    for (int32_t other = 1; other < result; other++) {
      int32_t value = matrix->getValue(source, other, layer, false);
      matrix->setValue(result, other, layer, value);
      matrix->setValue(other, result, layer, value);
    }
    matrix->setValue(result, result, layer, matrix->getValue(source, source, layer, false));
  }
  return result;
}

std::vector<std::pair<std::string, std::vector<std::string>>>
KiCadDesignRulesTranslator::updateNetClasses(const std::map<EntityKey, int32_t>& newClass) {
  std::vector<std::pair<std::string, std::vector<std::string>>> split;
  // Java: LinkedHashMap<NetClass, LinkedHashMap<List<Integer>, List<Net>>>
  using Parts = std::vector<std::pair<std::vector<int32_t>, std::vector<Net*>>>;
  std::vector<std::pair<NetClass*, Parts>> partsOfClass;
  for (int32_t netNo = 1; netNo <= board->rules->nets->maxNetNumber(); netNo++) {
    Net* net = board->rules->nets->get(netNo);
    if (net == nullptr || net->getNetClass() == nullptr) {
      continue;
    }
    NetClass* netClass = net->getNetClass();
    std::vector<int32_t> classes;
    classes.push_back(newClass.at(key(Kind::TRACE, net, netClass->getTraceClearanceClass())));
    classes.push_back(
        newClass.at(key(Kind::VIA, net, netClass->defaultItemClearanceClasses->get(ItemClass::VIA))));
    ViaRule* viaRule = netClass->getViaRule();
    for (int32_t i = 0; viaRule != nullptr && i < viaRule->viaCount(); i++) {
      classes.push_back(newClass.at(key(Kind::VIA, net, viaRule->getVia(i)->getClearanceClassIndex())));
    }
    Parts* parts = nullptr;
    for (auto& entry : partsOfClass) {
      if (entry.first == netClass) {
        parts = &entry.second;
        break;
      }
    }
    if (parts == nullptr) {
      partsOfClass.emplace_back(netClass, Parts());
      parts = &partsOfClass.back().second;
    }
    std::vector<Net*>* nets = nullptr;
    for (auto& part : *parts) {
      if (part.first == classes) {
        nets = &part.second;
        break;
      }
    }
    if (nets == nullptr) {
      parts->emplace_back(classes, std::vector<Net*>());
      nets = &parts->back().second;
    }
    nets->push_back(net);
  }
  for (auto& entry : partsOfClass) {
    NetClass* original = entry.first;
    std::vector<std::string> names;
    int32_t part = 0;
    for (auto& netsOfPart : entry.second) {
      const std::vector<int32_t>& classes = netsOfPart.first;
      NetClass* target = original;
      if (part > 0) {
        std::string name = original->getName() + "_" + jrt::str(part + 1);
        while (board->rules->netClasses->get(name) != nullptr) {
          name += "_";
        }
        target = board->rules->netClasses->append(name, board->layerStructure, matrix,
                                                  original->isIgnoredByAutorouter);
        target->copySettingsFrom(original);
      }
      names.push_back(target->getName());
      applyClasses(original, target, classes);
      if (target != original) {
        for (Net* net : netsOfPart.second) {
          net->setClass(target);
        }
      }
      part++;
    }
    if (names.size() > 1) {
      split.emplace_back(original->getName(), names);
    }
  }
  return split;
}

KiCadDesignRulesTranslator_EntityKey KiCadDesignRulesTranslator::key(Kind kind, Net* net,
                                                                     int32_t clearanceClass) {
  return EntityKey{kind, net->netNumber, clearanceClass, 0};
}

void KiCadDesignRulesTranslator::applyClasses(NetClass* original, NetClass* target,
                                              const std::vector<int32_t>& classes) {
  target->setTraceClearanceClass(classes[0]);
  target->defaultItemClearanceClasses->set(ItemClass::TRACE, classes[0]);
  target->defaultItemClearanceClasses->set(ItemClass::VIA, classes[1]);
  ViaRule* viaRule = original->getViaRule();
  if (viaRule == nullptr) {
    return;
  }
  bool changed = false;
  for (int32_t i = 0; i < viaRule->viaCount(); i++) {
    changed |= viaRule->getVia(i)->getClearanceClassIndex() != classes[i + 2];
  }
  if (!changed) {
    target->setViaRule(viaRule);
    return;
  }
  std::string ruleName = viaRule->name + SUFFIX + "_" + target->getName();
  while (board->rules->getViaRule(ruleName) != nullptr) {
    ruleName += "_";
  }
  auto* newRule = new (jrt::kBoard) ViaRule(ruleName);
  for (int32_t i = 0; i < viaRule->viaCount(); i++) {
    ViaInfo* via = viaRule->getVia(i);
    int32_t clearanceClass = classes[i + 2];
    if (via->getClearanceClassIndex() != clearanceClass) {
      std::string viaName = via->getName() + SUFFIX + "_" + matrix->getName(clearanceClass);
      while (board->rules->viaInfos->nameExists(viaName)) {
        viaName += "_";
      }
      via = new (jrt::kBoard) ViaInfo(viaName, via->getPadstack(), clearanceClass,
                                      via->attachSmdAllowed(), board->rules);
      board->rules->viaInfos->add(via);
    }
    newRule->appendVia(via);
  }
  board->rules->viaRules->add(newRule);
  target->setViaRule(newRule);
}

std::string KiCadDesignRulesTranslator::summary(Result* result, const std::string& sourceName) {
  std::string text = jrt::format(
      "KiCad design rules '%s': %d clearance rule(s) applied, %d clearance class(es) added",
      sourceName.c_str(), result->translatedRules(), result->addedClearanceClasses());
  for (const auto& entry : result->splitNetClasses()) {
    text += ", net class '" + entry.first + "' split into " + listToString(entry.second);
  }
  if (!result->warnings()->isEmpty()) {
    text += ", " + jrt::str(result->warnings()->size()) + " warning(s)";
  }
  return text + ".";
}

}  // namespace fr
