// Port of app.freerouting.rules.NetMeanderConstraint
#include "rules/NetMeanderConstraint.h"

#include "io/CoordinateTransform.h"

namespace fr {

NetMeanderConstraint* NetMeanderConstraint::fromDsn(
    std::optional<double> minAmp, std::optional<double> maxAmp, std::optional<double> spacing,
    std::optional<CornerStyle> cornerStyle, std::optional<double> radiusPct,
    std::optional<bool> singleSided, io::CoordinateTransform* transform,
    NetMeanderConstraint* fallback) {
  NetMeanderConstraint* base = (fallback != nullptr) ? fallback : UNCONSTRAINED();

  double resolvedMin;
  double resolvedMax;

  if (maxAmp.has_value() && *maxAmp >= 0) {
    resolvedMax = (transform != nullptr) ? transform->dsnToBoard(*maxAmp) : *maxAmp;
  } else {
    resolvedMax = base->maxAmplitude();
  }

  if (minAmp.has_value() && *minAmp >= 0) {
    resolvedMin = (transform != nullptr) ? transform->dsnToBoard(*minAmp) : *minAmp;
  } else {
    resolvedMin = base->minAmplitude();
  }

  if (resolvedMax > 0 && resolvedMin > resolvedMax) {
    double temp = resolvedMin;
    resolvedMin = resolvedMax;
    resolvedMax = temp;
  }

  double resolvedSpacing =
      (spacing.has_value() && *spacing >= 0)
          ? ((transform != nullptr) ? transform->dsnToBoard(*spacing) : *spacing)
          : base->spacing();

  CornerStyle resolvedCorner = cornerStyle.has_value() ? *cornerStyle : base->cornerStyle();
  double resolvedRadius =
      (radiusPct.has_value() && *radiusPct >= 0) ? *radiusPct : base->radiusPercentage();
  bool resolvedSingleSided = singleSided.has_value() ? *singleSided : base->singleSided();

  return new (jrt::kBoard) NetMeanderConstraint(resolvedMin, resolvedMax, resolvedSpacing, resolvedCorner,
                                  resolvedRadius, resolvedSingleSided);
}

}  // namespace fr
