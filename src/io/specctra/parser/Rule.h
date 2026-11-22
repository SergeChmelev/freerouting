#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing rule scopes from dsn-files. */
class Rule : public virtual jrt::Object {
 public:
  /** Returns a collection of objects of class Rule (null on error). */
  static jrt::Collection<Rule*>* readScope(IJFlexScanner* scanner);

  /** Reads a LayerRule from dsn-file (null on error). */
  static Rule_LayerRule* readLayerRuleScope(IJFlexScanner* scanner);

  static Rule_WidthRule* readWidthRule(IJFlexScanner* scanner);

  /** Writes the rules of a board net class. Throws jrt::IOException. */
  static void writeScope(fr::NetClass* netClass, WriteScopeParameter* scopeParameter);

  /** Writes the default rule as a scope to an output dsn-file. */
  static void writeDefaultRule(WriteScopeParameter* scopeParameter, int32_t layer);

  static Rule_ClearanceRule* readClearanceRule(IJFlexScanner* scanner);

  static void writeItemClearanceClass(const std::string& name, fr::IndentFileWriter* file,
                                      fr::IdentifierType* identifierType);

 protected:
  Rule() = default;

 private:
  static void writeLayerRule(fr::NetClass* netClass, int32_t layerIndex,
                             WriteScopeParameter* scopeParameter);

  /** Write the clearance rules, which are different from the default clearance. */
  static void writeNonDefaultClearanceRules(WriteScopeParameter* scopeParameter, int32_t layer,
                                            int32_t defaultClearance);

  /** Write the clearance rules for the named classes in the clearance matrix. */
  static void writeNamedClearanceRules(WriteScopeParameter* scopeParameter, int32_t layer);
};

class Rule_WidthRule : public Rule {
 public:
  double value;

  explicit Rule_WidthRule(double value) : value(value) {}
};

class Rule_ClearanceRule : public Rule {
 public:
  double value;
  jrt::Collection<std::string>* clearanceClassPairs;

  Rule_ClearanceRule(double value, jrt::Collection<std::string>* classPairs)
      : value(value), clearanceClassPairs(classPairs) {}
};

class Rule_LengthRule : public Rule {
 public:
  double maxLength;
  double minLength;

  Rule_LengthRule(double maxLength, double minLength) : maxLength(maxLength), minLength(minLength) {}
};

/** Meander parameters of a rule scope; all fields nullable (Java boxed / enum references). */
class Rule_MeanderRule : public Rule {
 public:
  std::optional<double> minAmplitude;
  std::optional<double> maxAmplitude;
  std::optional<double> spacing;
  std::optional<fr::CornerStyle> cornerStyle;
  std::optional<double> radiusPercentage;
  std::optional<bool> singleSided;
  std::optional<fr::MeanderPatternType> patternType;

  Rule_MeanderRule(std::optional<double> minAmplitude, std::optional<double> maxAmplitude,
                   std::optional<double> spacing, std::optional<fr::CornerStyle> cornerStyle,
                   std::optional<double> radiusPercentage, std::optional<bool> singleSided,
                   std::optional<fr::MeanderPatternType> patternType)
      : minAmplitude(minAmplitude),
        maxAmplitude(maxAmplitude),
        spacing(spacing),
        cornerStyle(cornerStyle),
        radiusPercentage(radiusPercentage),
        singleSided(singleSided),
        patternType(patternType) {}

  /** Applies the parameters on top of existing (nullable; a default preset is used if null). */
  virtual fr::NetMeanderConstraint* applyTo(fr::NetMeanderConstraint* existing,
                                            fr::io::CoordinateTransform* transform);

  virtual fr::NetMeanderConstraint* toMeanderConstraint(fr::MeanderPatternType defaultType,
                                                        fr::io::CoordinateTransform* transform);

  /** Uses MeanderPatternType::SINGLE_TRACK as default type. */
  virtual fr::NetMeanderConstraint* toMeanderConstraint(fr::io::CoordinateTransform* transform);
};

/** Rules restricted to the named layers (not a Rule itself). */
class Rule_LayerRule : public virtual jrt::Object {
 public:
  jrt::Collection<std::string>* layerNames;
  jrt::Collection<Rule*>* rules;

  Rule_LayerRule(jrt::Collection<std::string>* layerNames, jrt::Collection<Rule*>* rules)
      : layerNames(layerNames), rules(rules) {}
};

}  // namespace fr::dsn
