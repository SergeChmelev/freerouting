#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Configuration controlling design-rule-check reporting. */
// PORT NOTE: Serializable / gson annotations dropped; fully implemented here (no .cpp).
class DesignRulesCheckerSettings : public virtual jrt::Object {
 public:
  /** Whether the checker is enabled. */
  bool enabled = false;
  /** Whether warning-level violations are included in reports. */
  bool includeWarnings = true;
  /** Whether error-level violations are included in reports. */
  bool includeErrors = true;
  /**
   * Clearance violation shortfall tolerance in micrometers. Shortfalls less than or equal to this
   * threshold are treated as rounding/discretization noise rather than violations.
   */
  double clearanceToleranceUm = 1.0;

  /** Creates a copy of this design-rule-checker configuration. */
  virtual DesignRulesCheckerSettings* clone() {
    auto* clone = new DesignRulesCheckerSettings();
    clone->enabled = this->enabled;
    clone->includeWarnings = this->includeWarnings;
    clone->includeErrors = this->includeErrors;
    clone->clearanceToleranceUm = this->clearanceToleranceUm;
    return clone;
  }
};

}  // namespace fr
