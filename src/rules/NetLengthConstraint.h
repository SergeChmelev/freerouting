// Port of app.freerouting.rules.NetLengthConstraint
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Immutable trace length constraint (Java record) in board units; values <= 0 mean no limit.
 */
class NetLengthConstraint : public virtual jrt::Object {
 public:
  NetLengthConstraint(double minLength, double maxLength)
      : minLength_(minLength), maxLength_(maxLength) {}

  /** A constraint without minimum and maximum. */
  static NetLengthConstraint* UNCONSTRAINED() {
    static NetLengthConstraint* v = jrt::onHeap([&] { return new (jrt::kBoard) NetLengthConstraint(0.0, 0.0); });
    return v;
  }

  double minLength() { return minLength_; }
  double maxLength() { return maxLength_; }

  /** Returns true if a minimum length is set. */
  bool hasMin() { return this->minLength_ > 0.0; }

  /** Returns true if a maximum length is set. */
  bool hasMax() { return this->maxLength_ > 0.0; }

  /** Returns true if a minimum or maximum is set. */
  bool isConstrained() { return hasMin() || hasMax(); }

  /** The target length: the middle of min and max, or the one that is set, or 0. */
  double targetLength() {
    if (hasMin() && hasMax()) {
      return (this->minLength_ + this->maxLength_) / 2.0;
    }
    if (hasMin()) {
      return this->minLength_;
    }
    if (hasMax()) {
      return this->maxLength_;
    }
    return 0.0;
  }

  /** Returns true if currentLength satisfies the constraint. */
  bool isSatisfied(double currentLength) {
    if (hasMin() && currentLength < this->minLength_) {
      return false;
    }
    if (hasMax() && currentLength > this->maxLength_) {
      return false;
    }
    return true;
  }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = jrt::fast_cast<NetLengthConstraint>(o);
    return other != nullptr && jrt::equalsOf(minLength_, other->minLength_) &&
           jrt::equalsOf(maxLength_, other->maxLength_);
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(minLength_);
    return jrt::hashCombine(h, jrt::hashOf(maxLength_));
  }
  std::string toString() override {
    return "NetLengthConstraint[minLength=" + jrt::str(minLength_) +
           ", maxLength=" + jrt::str(maxLength_) + "]";
  }

 private:
  double minLength_;
  double maxLength_;
};

}  // namespace fr
