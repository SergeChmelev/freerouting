#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A truth value which may be unknown (Kleene logic). */
// PORT NOTE: TRUE / FALSE are common macros, so the enumerators get a trailing `_`; the methods
// and / or / not are keywords in C++ and become and_ / or_ / not_.
enum class KiCadRuleCondition_Tri : int32_t { TRUE_, FALSE_, UNKNOWN };

inline KiCadRuleCondition_Tri KiCadRuleCondition_Tri_of(bool value) {
  return value ? KiCadRuleCondition_Tri::TRUE_ : KiCadRuleCondition_Tri::FALSE_;
}

inline KiCadRuleCondition_Tri KiCadRuleCondition_Tri_and_(KiCadRuleCondition_Tri self,
                                                          KiCadRuleCondition_Tri other) {
  using Tri = KiCadRuleCondition_Tri;
  if (self == Tri::FALSE_ || other == Tri::FALSE_) {
    return Tri::FALSE_;
  }
  return self == Tri::TRUE_ && other == Tri::TRUE_ ? Tri::TRUE_ : Tri::UNKNOWN;
}

inline KiCadRuleCondition_Tri KiCadRuleCondition_Tri_or_(KiCadRuleCondition_Tri self,
                                                         KiCadRuleCondition_Tri other) {
  using Tri = KiCadRuleCondition_Tri;
  if (self == Tri::TRUE_ || other == Tri::TRUE_) {
    return Tri::TRUE_;
  }
  return self == Tri::FALSE_ && other == Tri::FALSE_ ? Tri::FALSE_ : Tri::UNKNOWN;
}

inline KiCadRuleCondition_Tri KiCadRuleCondition_Tri_not_(KiCadRuleCondition_Tri self) {
  using Tri = KiCadRuleCondition_Tri;
  return self == Tri::UNKNOWN ? Tri::UNKNOWN : KiCadRuleCondition_Tri_of(self == Tri::FALSE_);
}

/** The kind of a board item, as the KiCad {@code Type} property names it. */
enum class KiCadRuleCondition_ItemType : int32_t { PAD, TRACK, VIA, ZONE };

inline std::string KiCadRuleCondition_ItemType_kicadName(KiCadRuleCondition_ItemType e) {
  switch (e) {
    case KiCadRuleCondition_ItemType::PAD:
      return "Pad";
    case KiCadRuleCondition_ItemType::TRACK:
      return "Track";
    case KiCadRuleCondition_ItemType::VIA:
      return "Via";
    case KiCadRuleCondition_ItemType::ZONE:
      return "Zone";
  }
  return "";
}

/**
 * What a condition may ask about a copper item of the board (Java record).
 *
 * type: the kind of the item; netName: the name of its net, empty for an item without net;
 * netClass: the KiCad name of the class of its net, {@code Default} without net (a composite class
 * is named by its constituents joined with commas, {@code HV,Default}); footprint: the reference of
 * the footprint of a pad, else null; padNumber: the number of a pad, else null; smd: for a pad,
 * true if it has copper on a single layer; libraryId: the library identifier of the footprint of a
 * pad ({@code lib:name}), else null.
 */
class KiCadRuleCondition_Item : public virtual jrt::Object {
 public:
  KiCadRuleCondition_Item(KiCadRuleCondition_ItemType type, const std::string& netName,
                          const std::string& netClass, const std::optional<std::string>& footprint,
                          const std::optional<std::string>& padNumber, bool smd,
                          const std::optional<std::string>& libraryId)
      : type_(type),
        netName_(netName),
        netClass_(netClass),
        footprint_(footprint),
        padNumber_(padNumber),
        smd_(smd),
        libraryId_(libraryId) {}

  /** An item whose footprint, if any, has no known library identifier. */
  KiCadRuleCondition_Item(KiCadRuleCondition_ItemType type, const std::string& netName,
                          const std::string& netClass, const std::optional<std::string>& footprint,
                          const std::optional<std::string>& padNumber, bool smd)
      : KiCadRuleCondition_Item(type, netName, netClass, footprint, padNumber, smd,
                                std::nullopt) {}

  KiCadRuleCondition_ItemType type() { return type_; }
  std::string netName() { return netName_; }
  std::string netClass() { return netClass_; }
  const std::optional<std::string>& footprint() { return footprint_; }
  const std::optional<std::string>& padNumber() { return padNumber_; }
  bool smd() { return smd_; }
  const std::optional<std::string>& libraryId() { return libraryId_; }

 private:
  KiCadRuleCondition_ItemType type_;
  std::string netName_;
  std::string netClass_;
  std::optional<std::string> footprint_;
  std::optional<std::string> padNumber_;
  bool smd_;
  std::optional<std::string> libraryId_;
};

/**
 * A value of the expression (Java: sealed interface Value permits Bool, Str, Num, Prop,
 * Undefined, all records).
 */
// PORT NOTE: the five value records are one small value type here, distinguished by kind: they
// are created on every evaluation and have no identity.
class KiCadRuleCondition_Value {
 public:
  enum class Kind : int32_t { BOOL, STR, NUM, PROP, UNDEFINED };
  Kind kind = Kind::UNDEFINED;
  KiCadRuleCondition_Tri boolValue = KiCadRuleCondition_Tri::UNKNOWN;  // Bool
  std::string text;                    // Str: the string; Prop: the property name
  double num = 0;                      // Num
  KiCadRuleCondition_Item* item = nullptr;  // Prop

  static KiCadRuleCondition_Value Bool(KiCadRuleCondition_Tri value) {
    KiCadRuleCondition_Value v;
    v.kind = Kind::BOOL;
    v.boolValue = value;
    return v;
  }
  static KiCadRuleCondition_Value Str(const std::string& value) {
    KiCadRuleCondition_Value v;
    v.kind = Kind::STR;
    v.text = value;
    return v;
  }
  static KiCadRuleCondition_Value Num(double value) {
    KiCadRuleCondition_Value v;
    v.kind = Kind::NUM;
    v.num = value;
    return v;
  }
  static KiCadRuleCondition_Value Prop(const std::string& name, KiCadRuleCondition_Item* item) {
    KiCadRuleCondition_Value v;
    v.kind = Kind::PROP;
    v.text = name;
    v.item = item;
    return v;
  }
  static KiCadRuleCondition_Value Undefined() { return KiCadRuleCondition_Value(); }
};

/** A node of the syntax tree (Java: sealed interface Node). */
class KiCadRuleCondition_Node : public virtual jrt::Object {
 public:
  virtual KiCadRuleCondition_Value eval(KiCadRuleCondition_Item* a, KiCadRuleCondition_Item* b) = 0;
  virtual void collectUnsupported(jrt::List<std::string>* out) {}
  virtual void signature(KiCadRuleCondition_Item* item, std::string& out) {}
};

/**
 * The condition of a KiCad custom design rule ({@code (condition "A.NetClass == 'HV'")}), reduced
 * to what a Specctra board knows about its items.
 *
 * <p>The evaluation follows KiCad 10 ({@code pcbexpr_evaluator.cpp}, {@code libeval_compiler.cpp},
 * {@code pcbexpr_functions.cpp}): string comparisons ignore case and accept the {@code *} and
 * {@code ?} wildcards, while {@code memberOfFootprint()} and {@code hasNetclass()} match with
 * {@code wxString::Matches}, wildcards too but case-sensitive, {@code A.NetClass == 'X'} also
 * matches a composite class such as {@code X,Default}, a property the item does not have makes both
 * {@code ==} and {@code !=} false, and a condition holds if it holds for (A, B) or for (B, A).
 *
 * <p>The result has three values. A part of the condition that Freerouting cannot evaluate (a
 * geometric function, an unknown property, a numeric comparison of a property...) is UNKNOWN, and
 * unsupportedParts() names it, so that the caller can apply the rule wherever it may match and
 * warn about it.
 */
class KiCadRuleCondition final : public virtual jrt::Object {
 public:
  using Tri = KiCadRuleCondition_Tri;
  using Item = KiCadRuleCondition_Item;
  using Value = KiCadRuleCondition_Value;
  using Node = KiCadRuleCondition_Node;

  /** Parses a condition; a condition Freerouting cannot read evaluates to UNKNOWN. */
  static KiCadRuleCondition* parse(const std::optional<std::string>& expression);

  /** The text of the condition. */
  std::string expression() { return expression_; }

  /** The parts of the condition that always evaluate to UNKNOWN, for warnings. */
  jrt::List<std::string>* unsupportedParts();

  /** Evaluates the condition for a pair of items in both orders, as KiCad does. */
  Tri evaluate(Item* a, Item* b);

  /**
   * A key such that two items with the same key give the same result in every evaluation of this
   * condition, whatever the other item is.
   */
  std::string signature(Item* item);

  /**
   * The parts of the condition that name a net class, a net or a footprint ({@code NetClass} or
   * {@code NetName} compared with a text, {@code memberOfFootprint()}, {@code hasNetclass()}) and
   * that no item of {@code items} satisfies.
   */
  jrt::List<std::string>* namesMatchingNothing(jrt::Collection<Item*>* items);

  /**
   * A KiCad wildcard ({@code *} and {@code ?}) matched against a text (Java: wildcard(glob,
   * caseSensitive).matcher(text).matches()).
   */
  // PORT NOTE: Java compiles the wildcard into a java.util.regex.Pattern (DOTALL, and
  // CASE_INSENSITIVE unless caseSensitive) and caches it; the port matches the wildcard directly,
  // with the same result. '?' matches one character (one UTF-8 code point).
  static bool wildcardMatches(const std::string& glob, const std::string& text, bool caseSensitive);

  /** Parses a numeric value with a KiCad unit into micrometres; null if it is not one. */
  static std::optional<double> parseLength(const std::string& text, jrt::List<std::string>* warnings);

  /** The names of the supported properties and functions, for documentation and tests. */
  static jrt::LinkedHashSet<std::string>* supportedNames();

  // ---- helpers shared with the node classes (Java: private static members) ----
  static Tri truth(const Value& value);
  static std::optional<std::string> propertyText(const std::string& name, Item* item);
  static bool matches(const std::string& pattern, const std::string& text, bool caseSensitive = false);
  static jrt::List<std::string>* constituents(const std::string& netClass);
  static Tri compare(const std::string& op, const Value& left, const Value& right);
  static bool isProperty(const std::string& name);
  static bool isFunction(const std::string& name);

 private:
  explicit KiCadRuleCondition(const std::string& expression);

  static void collectNameTests(Node* node, std::vector<Node*>& out);
  static Tri nameTest(Node* test, Item* item);
  /** {@code property == literal}, or null if the item does not have the property. */
  static std::optional<bool> propertyEquals(const Value& prop, const std::string& literal);

  std::string expression_;
  Node* root;
  jrt::List<std::string>* unsupportedParts_;
};

}  // namespace fr
