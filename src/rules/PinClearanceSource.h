// Port of app.freerouting.rules.PinClearanceSource
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Erik Gavriluk (freerouting.net): records a pin's clearance source before DRC or tolerance
 * copies. (Java record; localClearance is a nullable Integer.)
 */
class PinClearanceSource : public virtual jrt::Object {
 public:
  PinClearanceSource(int32_t originalClass, int32_t maskClearance,
                     std::optional<int32_t> localClearance)
      : originalClass_(originalClass),
        maskClearance_(maskClearance),
        localClearance_(localClearance) {}

  int32_t originalClass() { return originalClass_; }
  int32_t maskClearance() { return maskClearance_; }
  std::optional<int32_t> localClearance() { return localClearance_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = jrt::fast_cast<PinClearanceSource>(o);
    return other != nullptr && originalClass_ == other->originalClass_ &&
           maskClearance_ == other->maskClearance_ && localClearance_ == other->localClearance_;
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(originalClass_);
    h = jrt::hashCombine(h, jrt::hashOf(maskClearance_));
    return jrt::hashCombine(h, jrt::hashOf(localClearance_));
  }
  std::string toString() override {
    return "PinClearanceSource[originalClass=" + jrt::str(originalClass_) +
           ", maskClearance=" + jrt::str(maskClearance_) +
           ", localClearance=" + jrt::str(localClearance_) + "]";
  }

 private:
  int32_t originalClass_;
  int32_t maskClearance_;
  std::optional<int32_t> localClearance_;
};

}  // namespace fr
