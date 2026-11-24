#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * What the translation did (Java record): the clearance rules applied to the board, the number of
 * clearance classes added, for each split net class the names of its parts (in insertion order),
 * and what is not translated, or translated more strictly than written.
 */
class KiCadDesignRulesTranslator_Result : public virtual jrt::Object {
 public:
  KiCadDesignRulesTranslator_Result(
      int32_t translatedRules, int32_t addedClearanceClasses,
      const std::vector<std::pair<std::string, std::vector<std::string>>>& splitNetClasses,
      jrt::List<std::string>* warnings)
      : translatedRules_(translatedRules),
        addedClearanceClasses_(addedClearanceClasses),
        splitNetClasses_(splitNetClasses),
        warnings_(warnings) {}

  int32_t translatedRules() { return translatedRules_; }
  int32_t addedClearanceClasses() { return addedClearanceClasses_; }
  /** Java: Map<String, List<String>> (a LinkedHashMap): the entries in insertion order. */
  const std::vector<std::pair<std::string, std::vector<std::string>>>& splitNetClasses() {
    return splitNetClasses_;
  }
  jrt::List<std::string>* warnings() { return warnings_; }

 private:
  int32_t translatedRules_;
  int32_t addedClearanceClasses_;
  std::vector<std::pair<std::string, std::vector<std::string>>> splitNetClasses_;
  jrt::List<std::string>* warnings_;
};

enum class KiCadDesignRulesTranslator_Kind : int32_t { TRACE, VIA, PIN, AREA };

/** A set of copper items with the same kind, net, clearance class and KiCad attributes (record). */
class KiCadDesignRulesTranslator_EntityKey {
 public:
  KiCadDesignRulesTranslator_Kind kind;
  int32_t net;
  int32_t clearanceClass;
  int32_t pinItemId;

  bool operator<(const KiCadDesignRulesTranslator_EntityKey& o) const {
    if (kind != o.kind) return kind < o.kind;
    if (net != o.net) return net < o.net;
    if (clearanceClass != o.clearanceClass) return clearanceClass < o.clearanceClass;
    return pinItemId < o.pinItemId;
  }
};

/** Java record Entity(EntityKey key, KiCadRuleCondition.Item item). */
class KiCadDesignRulesTranslator_Entity {
 public:
  KiCadDesignRulesTranslator_EntityKey key;
  KiCadRuleCondition_Item* item;
};

/** Java record ClearanceRule(String name, boolean[] layers, KiCadRuleCondition condition, int value). */
class KiCadDesignRulesTranslator_ClearanceRule {
 public:
  std::string name;
  std::vector<bool> layers;
  KiCadRuleCondition* condition;
  int32_t value;
};

/**
 * Applies the clearance rules of a KiCad {@code .kicad_dru} file to a board loaded from a Specctra
 * design that KiCad exported without them.
 *
 * <p>The copper items of the board (pins, and per net its traces, vias and conduction areas) are
 * sorted into groups that every rule condition treats alike. For each pair of groups and each
 * layer, the rules are applied as KiCad applies them: the last matching rule sets the clearance,
 * and without a matching rule the clearance of the design (net classes) stays. A group whose
 * clearances change gets its own clearance class, a copy of the class it had; a net class whose
 * nets end up in different groups is split, with copies of its via rule. Groups that behave the
 * same share one class.
 *
 * <p>Where a condition cannot be evaluated (see KiCadRuleCondition), the rule is taken as matching
 * wherever it may match and the largest of the possible clearances is used: the board is never
 * less strict than KiCad, only more. Everything that is not translated is reported in
 * Result::warnings().
 */
// PORT NOTE: the private records (EntityKey, Entity, ClearanceRule) are plain value types, and the
// Java LinkedHashMaps keyed by them are insertion-ordered vectors with a std::map index; Java's
// HashMaps among them are only looked up, never iterated in an order that matters.
class KiCadDesignRulesTranslator final : public virtual jrt::Object {
 public:
  using Result = KiCadDesignRulesTranslator_Result;
  using Kind = KiCadDesignRulesTranslator_Kind;
  using EntityKey = KiCadDesignRulesTranslator_EntityKey;
  using Entity = KiCadDesignRulesTranslator_Entity;
  using ClearanceRule = KiCadDesignRulesTranslator_ClearanceRule;

  /** Name suffix of the clearance classes, via infos and via rules made by the translation. */
  static inline const std::string SUFFIX = "#dru";

  /** Applies the clearance rules to the board. */
  static Result* apply(BasicBoard* board, KiCadDesignRules* designRules);

  /**
   * The KiCad library identifier of a component, null if unknown: the name of its Specctra image,
   * less the suffix ({@code ::1}, {@code ::2}...) the KiCad export gives to footprint variants.
   */
  static std::optional<std::string> libraryId(Component* component);

  /** A short description of the result, for the log. */
  static std::string summary(Result* result, const std::string& sourceName);

 private:
  explicit KiCadDesignRulesTranslator(BasicBoard* board);

  Result* run(KiCadDesignRules* designRules);
  std::vector<ClearanceRule> clearanceRules(KiCadDesignRules* designRules);
  void resetSmdToSmdClearance();
  void warnImageKeepouts();
  void warnDuplicateReferences();
  void warnNamesMatchingNothing(const std::vector<ClearanceRule>& rules,
                                const std::vector<Entity>& entities);
  static std::string kicadClassName(NetClass* netClass);
  void collectEntities(std::vector<Entity>& entities, std::map<EntityKey, int32_t>& entityIndex,
                       std::vector<std::pair<Item*, EntityKey>>& items);
  EntityKey addEntity(std::vector<Entity>& entities, std::map<EntityKey, int32_t>& entityIndex,
                      Kind kind, Net* net, int32_t clearanceClass, Item* pinItem);
  KiCadRuleCondition_Item* describe(Kind kind, Net* net, Item* pinItem);
  /** Empty (Java: null) where the clearance does not differ from the design; -1 per layer. */
  std::optional<std::vector<int32_t>> customClearance(const std::vector<ClearanceRule>& rules,
                                                      const Entity& a, const Entity& b);
  int32_t copyClass(int32_t source);
  std::vector<std::pair<std::string, std::vector<std::string>>> updateNetClasses(
      const std::map<EntityKey, int32_t>& newClass);
  static EntityKey key(Kind kind, Net* net, int32_t clearanceClass);
  void applyClasses(NetClass* original, NetClass* target, const std::vector<int32_t>& classes);

  BasicBoard* board;
  ClearanceMatrix* matrix;
  int32_t layerCount;
  jrt::List<std::string>* warnings;
};

}  // namespace fr
