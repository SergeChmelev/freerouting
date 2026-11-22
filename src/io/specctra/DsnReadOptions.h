#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Immutable import decisions supplied before the parser allocates board items (Java record). */
class DsnReadOptions : public virtual jrt::Object {
 public:
  explicit DsnReadOptions(bool canonicalPlacementOrder)
      : canonicalPlacementOrder_(canonicalPlacementOrder) {}

  bool canonicalPlacementOrder() { return canonicalPlacementOrder_; }

  /** Retains legacy source enumeration when explicitly requested. */
  static DsnReadOptions* INPUT_ORDER();

  /** Allocates uniquely named placements in exact reference-name order. */
  static DsnReadOptions* REFERENCE_NAME();

  /** Universal default allocating placements canonically by reference name (= REFERENCE_NAME). */
  static DsnReadOptions* DEFAULT();

  /** Selects reference-name allocation as the universal import policy for all routing jobs. */
  static DsnReadOptions* forSettings(fr::RouterSettings* settings) { return REFERENCE_NAME(); }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = jrt::fast_cast<DsnReadOptions>(o);
    return other != nullptr && canonicalPlacementOrder_ == other->canonicalPlacementOrder_;
  }
  int32_t hashCode() override { return jrt::Boolean::hashCode(canonicalPlacementOrder_); }
  std::string toString() override {
    return "DsnReadOptions[canonicalPlacementOrder=" + jrt::str(canonicalPlacementOrder_) + "]";
  }

 private:
  bool canonicalPlacementOrder_;
};

}  // namespace fr::dsn
