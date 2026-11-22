#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics of the clearance violations of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped. Fully implemented here.
class BoardStatisticsClearanceViolations : public virtual jrt::Object {
 public:
  std::optional<int32_t> totalCount;
  std::optional<int32_t> preExistingCount = 0;
  std::optional<int32_t> unfixableCount = 0;
  std::optional<int32_t> routerIntroducedCount = 0;
  std::optional<int32_t> newOrChangedCount;
  std::optional<double> totalViolationUm;
  /** Summed clearance shortfall of the violations counted by fixableCount(). */
  std::optional<double> fixableViolationUm;
  std::optional<double> minViolationUm;
  std::optional<double> maxViolationUm;
  std::optional<double> avgViolationUm;

  /**
   * Returns the number of violations that routing can resolve: all except the unfixable ones
   * between two items the router never moves.
   */
  virtual int32_t fixableCount() {
    int32_t total = totalCount.has_value() ? *totalCount : 0;
    int32_t unfixable = unfixableCount.has_value() ? *unfixableCount : 0;
    return jrt::Math::max(0, total - unfixable);
  }

  /**
   * Returns the summed shortfall of the fixable violations in um, or the total shortfall for
   * statistics recorded without that split.
   */
  virtual double fixableShortfallUm() {
    if (fixableCount() == 0) {
      return 0.0;
    }
    std::optional<double> shortfall =
        fixableViolationUm.has_value() ? fixableViolationUm : totalViolationUm;
    return shortfall.has_value() ? jrt::Math::max(0.0, *shortfall) : 0.0;
  }
};

}  // namespace fr
