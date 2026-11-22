#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Accumulates and reads meander pattern parameters across circuit and rule scopes.
 * All fields are nullable in Java (boxed / enum references) -> std::optional.
 */
class Circuit_MeanderDescriptorBuilder : public virtual jrt::Object {
 public:
  std::optional<double> minAmplitude;
  std::optional<double> maxAmplitude;
  std::optional<double> spacing;
  std::optional<fr::CornerStyle> cornerStyle;
  std::optional<double> radiusPercentage;
  std::optional<bool> singleSided;
  std::optional<fr::MeanderPatternType> patternType;

  /**
   * Attempts to read a meander descriptor matching the given token from the scanner. Returns true
   * if token matched a meander descriptor, false otherwise.
   */
  virtual bool readDescriptor(jrt::Object* token, IJFlexScanner* scanner);

  /** Returns true if any meander parameter has been populated. */
  virtual bool hasAttributes() {
    return maxAmplitude.has_value() || minAmplitude.has_value() || spacing.has_value() ||
           cornerStyle.has_value() || radiusPercentage.has_value() || singleSided.has_value() ||
           patternType.has_value();
  }

  /** Converts the accumulated descriptors into a MeanderRule. */
  virtual Rule_MeanderRule* toMeanderRule();
};

/** Result of reading a circuit scope. */
class Circuit_ReadScopeResult : public virtual jrt::Object {
 public:
  double maxLength;
  double minLength;
  jrt::Collection<std::string>* useVia;
  jrt::Collection<std::string>* useLayer;
  std::optional<double> minAmplitude;
  std::optional<double> maxAmplitude;
  std::optional<double> spacing;
  std::optional<fr::CornerStyle> cornerStyle;
  std::optional<double> radiusPercentage;
  std::optional<bool> singleSided;
  std::optional<fr::MeanderPatternType> patternType;

  Circuit_ReadScopeResult(double maxLength, double minLength, jrt::Collection<std::string>* useVia,
                          jrt::Collection<std::string>* useLayer)
      : Circuit_ReadScopeResult(maxLength, minLength, useVia, useLayer, std::nullopt,
                                std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                std::nullopt, std::nullopt) {}

  Circuit_ReadScopeResult(double maxLength, double minLength, jrt::Collection<std::string>* useVia,
                          jrt::Collection<std::string>* useLayer,
                          std::optional<double> minAmplitude, std::optional<double> maxAmplitude,
                          std::optional<double> spacing,
                          std::optional<fr::CornerStyle> cornerStyle,
                          std::optional<double> radiusPercentage, std::optional<bool> singleSided,
                          std::optional<fr::MeanderPatternType> patternType)
      : maxLength(maxLength),
        minLength(minLength),
        useVia(useVia),
        useLayer(useLayer),
        minAmplitude(minAmplitude),
        maxAmplitude(maxAmplitude),
        spacing(spacing),
        cornerStyle(cornerStyle),
        radiusPercentage(radiusPercentage),
        singleSided(singleSided),
        patternType(patternType) {}

  /** Returns true if any meander parameter is defined in this circuit scope. */
  virtual bool hasMeanderConstraint() {
    return (this->maxAmplitude.has_value() && *this->maxAmplitude > 0) ||
           (this->spacing.has_value() && *this->spacing > 0) || this->cornerStyle.has_value() ||
           this->singleSided.has_value();
  }

  /**
   * Converts the parsed circuit meander attributes into a NetMeanderConstraint in board units, or
   * null if unconstrained. defaultType is used if no pattern type is specified.
   */
  virtual fr::NetMeanderConstraint* toMeanderConstraint(fr::MeanderPatternType defaultType,
                                                        fr::io::CoordinateTransform* transform);

  /** As above with MeanderPatternType::SINGLE_TRACK as fallback. */
  virtual fr::NetMeanderConstraint* toMeanderConstraint(fr::io::CoordinateTransform* transform);
};

/** A maxLength of -1 indicates that no maximum length is defined. */
class Circuit_LengthMatchingRule : public virtual jrt::Object {
 public:
  double maxLength;
  double minLength;

  Circuit_LengthMatchingRule(double maxLength, double minLength)
      : maxLength(maxLength), minLength(minLength) {}
};

class Circuit_AmplitudeRule : public virtual jrt::Object {
 public:
  double maxAmplitude;
  double minAmplitude;

  Circuit_AmplitudeRule(double maxAmplitude, double minAmplitude)
      : maxAmplitude(maxAmplitude), minAmplitude(minAmplitude) {}
};

/** Reads circuit scopes (length matching and meander parameters) of a class or net. */
class Circuit final : public virtual jrt::Object {
 public:
  /** Reads a circuit scope; null on error. */
  static Circuit_ReadScopeResult* readScope(IJFlexScanner* scanner);

  static Circuit_LengthMatchingRule* readLengthScope(IJFlexScanner* scanner);

  /** Converts a Double/Integer/String token to a double; nullopt if not a number. */
  static std::optional<double> parseDouble(jrt::Object* token);

  static Circuit_AmplitudeRule* readLengthAmplitudeScope(IJFlexScanner* scanner);

  static std::optional<double> readNumberScope(IJFlexScanner* scanner);

  /** Reads a string or keyword name followed by the closing bracket; nullopt otherwise. */
  static std::optional<std::string> readStringScope(IJFlexScanner* scanner);

  static std::optional<bool> readBooleanScope(IJFlexScanner* scanner);

  static void readMeanderPatternScopeInto(IJFlexScanner* scanner,
                                          Circuit_MeanderDescriptorBuilder* builder);

  static void skipToClosedBracket(IJFlexScanner* scanner);

 private:
  Circuit() = default;
};

}  // namespace fr::dsn
