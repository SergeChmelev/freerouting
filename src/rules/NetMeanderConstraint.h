// Port of app.freerouting.rules.NetMeanderConstraint
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"
#include "rules/CornerStyle.h"
#include "rules/MeanderPatternType.h"

namespace fr {

/**
 * Immutable length-tuning meander constraint (Java record). Amplitudes and spacing in board units;
 * values <= 0 mean unconstrained. The canonical constructor clamps negative values to 0 and
 * radiusPercentage to [0, 100].
 */
class NetMeanderConstraint : public virtual jrt::Object {
 public:
  static constexpr double DEFAULT_MIN_AMPLITUDE_MM = 0.2;
  static constexpr double DEFAULT_MAX_AMPLITUDE_MM = 1.0;
  static constexpr double DEFAULT_SPACING_SINGLE_TRACK_MM = 0.6;
  static constexpr double DEFAULT_SPACING_DIFF_PAIR_MM = 1.0;
  static constexpr double DEFAULT_SPACING_DIFF_PAIR_SKEW_MM = 0.6;
  static constexpr CornerStyle DEFAULT_CORNER_STYLE = CornerStyle::FILLET;
  static constexpr double DEFAULT_RADIUS_PERCENTAGE = 80.0;
  static constexpr bool DEFAULT_SINGLE_SIDED = false;

  /** A constraint without amplitude and spacing. */
  static NetMeanderConstraint* UNCONSTRAINED() {
    static NetMeanderConstraint* v = new NetMeanderConstraint(
        0.0, 0.0, 0.0, DEFAULT_CORNER_STYLE, DEFAULT_RADIUS_PERCENTAGE, DEFAULT_SINGLE_SIDED);
    return v;
  }

  /** Canonical constructor with the clamping of the Java compact constructor. */
  NetMeanderConstraint(double minAmplitude, double maxAmplitude, double spacing,
                       CornerStyle cornerStyle, double radiusPercentage, bool singleSided) {
    if (minAmplitude < 0.0) {
      minAmplitude = 0.0;
    }
    if (maxAmplitude < 0.0) {
      maxAmplitude = 0.0;
    }
    if (spacing < 0.0) {
      spacing = 0.0;
    }
    if (radiusPercentage < 0.0) {
      radiusPercentage = 0.0;
    } else if (radiusPercentage > 100.0) {
      radiusPercentage = 100.0;
    }
    minAmplitude_ = minAmplitude;
    maxAmplitude_ = maxAmplitude;
    spacing_ = spacing;
    cornerStyle_ = cornerStyle;
    radiusPercentage_ = radiusPercentage;
    singleSided_ = singleSided;
  }

  double minAmplitude() { return minAmplitude_; }
  double maxAmplitude() { return maxAmplitude_; }
  double spacing() { return spacing_; }
  CornerStyle cornerStyle() { return cornerStyle_; }
  double radiusPercentage() { return radiusPercentage_; }
  bool singleSided() { return singleSided_; }

  /** Creates a constraint from millimetre values scaled to board units. */
  static NetMeanderConstraint* ofMm(double minAmpMm, double maxAmpMm, double spacingMm,
                                    CornerStyle cornerStyle, double radiusPercentage,
                                    bool singleSided, double mmToBoardScale) {
    double scale = (mmToBoardScale > 0) ? mmToBoardScale : 1.0;
    return new NetMeanderConstraint(minAmpMm * scale, maxAmpMm * scale, spacingMm * scale,
                                    cornerStyle, radiusPercentage, singleSided);
  }

  /** Creates the default preset for a pattern type. */
  static NetMeanderConstraint* createDefaultPreset(MeanderPatternType type, double mmToBoardScale) {
    double spacingMm = 0.0;
    switch (type) {
      case MeanderPatternType::DIFF_PAIR:
        spacingMm = DEFAULT_SPACING_DIFF_PAIR_MM;
        break;
      case MeanderPatternType::DIFF_PAIR_SKEW:
        spacingMm = DEFAULT_SPACING_DIFF_PAIR_SKEW_MM;
        break;
      case MeanderPatternType::SINGLE_TRACK:
        spacingMm = DEFAULT_SPACING_SINGLE_TRACK_MM;
        break;
    }
    return ofMm(DEFAULT_MIN_AMPLITUDE_MM, DEFAULT_MAX_AMPLITUDE_MM, spacingMm,
                DEFAULT_CORNER_STYLE, DEFAULT_RADIUS_PERCENTAGE, DEFAULT_SINGLE_SIDED,
                mmToBoardScale);
  }

  /**
   * Resolves a constraint from optional DSN values (DSN units, converted with transform if not
   * null); absent values are taken from fallback (or UNCONSTRAINED). min/max are swapped if
   * min > max > 0.
   */
  static NetMeanderConstraint* fromDsn(std::optional<double> minAmp, std::optional<double> maxAmp,
                                       std::optional<double> spacing,
                                       std::optional<CornerStyle> cornerStyle,
                                       std::optional<double> radiusPct,
                                       std::optional<bool> singleSided,
                                       io::CoordinateTransform* transform,
                                       NetMeanderConstraint* fallback);

  bool hasAmplitude() { return this->maxAmplitude_ > 0.0; }

  bool hasSpacing() { return this->spacing_ > 0.0; }

  bool isConstrained() { return hasAmplitude() || hasSpacing(); }

  bool isValid() { return !(this->maxAmplitude_ > 0.0 && this->minAmplitude_ > this->maxAmplitude_); }

  NetMeanderConstraint* withAmplitude(double newMin, double newMax) {
    return new NetMeanderConstraint(newMin, newMax, spacing_, cornerStyle_, radiusPercentage_,
                                    singleSided_);
  }

  NetMeanderConstraint* withSpacing(double newSpacing) {
    return new NetMeanderConstraint(minAmplitude_, maxAmplitude_, newSpacing, cornerStyle_,
                                    radiusPercentage_, singleSided_);
  }

  NetMeanderConstraint* withCornerStyle(CornerStyle newStyle) {
    return new NetMeanderConstraint(minAmplitude_, maxAmplitude_, spacing_, newStyle,
                                    radiusPercentage_, singleSided_);
  }

  NetMeanderConstraint* withRadiusPercentage(double newRadius) {
    return new NetMeanderConstraint(minAmplitude_, maxAmplitude_, spacing_, cornerStyle_,
                                    newRadius, singleSided_);
  }

  NetMeanderConstraint* withSingleSided(bool newSingleSided) {
    return new NetMeanderConstraint(minAmplitude_, maxAmplitude_, spacing_, cornerStyle_,
                                    radiusPercentage_, newSingleSided);
  }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = jrt::fast_cast<NetMeanderConstraint>(o);
    return other != nullptr && jrt::equalsOf(minAmplitude_, other->minAmplitude_) &&
           jrt::equalsOf(maxAmplitude_, other->maxAmplitude_) &&
           jrt::equalsOf(spacing_, other->spacing_) && cornerStyle_ == other->cornerStyle_ &&
           jrt::equalsOf(radiusPercentage_, other->radiusPercentage_) &&
           singleSided_ == other->singleSided_;
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(minAmplitude_);
    h = jrt::hashCombine(h, jrt::hashOf(maxAmplitude_));
    h = jrt::hashCombine(h, jrt::hashOf(spacing_));
    h = jrt::hashCombine(h, jrt::hashOf(cornerStyle_));
    h = jrt::hashCombine(h, jrt::hashOf(radiusPercentage_));
    return jrt::hashCombine(h, jrt::hashOf(singleSided_));
  }
  std::string toString() override {
    return "NetMeanderConstraint[minAmplitude=" + jrt::str(minAmplitude_) +
           ", maxAmplitude=" + jrt::str(maxAmplitude_) + ", spacing=" + jrt::str(spacing_) +
           ", cornerStyle=" + CornerStyle_toString(cornerStyle_) +
           ", radiusPercentage=" + jrt::str(radiusPercentage_) +
           ", singleSided=" + jrt::str(singleSided_) + "]";
  }

 private:
  double minAmplitude_;
  double maxAmplitude_;
  double spacing_;
  CornerStyle cornerStyle_;
  double radiusPercentage_;
  bool singleSided_;
};

}  // namespace fr
