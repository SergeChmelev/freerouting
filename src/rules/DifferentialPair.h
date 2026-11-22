// Port of app.freerouting.rules.DifferentialPair
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Immutable representation of a differential signal pair consisting of a positive and a negative
 * net (Java record), e.g. USB_P / USB_N, routed together with a coupled spacing.
 *
 * positiveNet / negativeNet: the two nets; baseName: the common base name without polarity
 * suffixes; coupledSpacing: target spacing in board units, or <= 0 if unconstrained; maxSkew:
 * maximum allowable length difference in board units, or <= 0 if unconstrained.
 */
class DifferentialPair : public virtual jrt::Object {
 public:
  /**
   * Canonical constructor (Java compact constructor): requires non-null nets and base name and
   * different nets (else NullPointerException / IllegalArgumentException); negative spacing and
   * skew become 0.
   */
  DifferentialPair(Net* positiveNet, Net* negativeNet, const std::string& baseName,
                   double coupledSpacing, double maxSkew);

  /** Constructs an unconstrained DifferentialPair with the given nets and base name. */
  DifferentialPair(Net* positiveNet, Net* negativeNet, const std::string& baseName)
      : DifferentialPair(positiveNet, negativeNet, baseName, 0.0, 0.0) {}

  Net* positiveNet() { return positiveNet_; }
  Net* negativeNet() { return negativeNet_; }
  std::string baseName() { return baseName_; }
  double coupledSpacing() { return coupledSpacing_; }
  double maxSkew() { return maxSkew_; }

  /** Returns true if the given net is a member of this differential pair. */
  bool contains(Net* net);

  /** Returns the partner net of the given net within this pair, or null. */
  Net* getPartner(Net* net);

  /** Returns true if the given net is the positive member of this pair. */
  bool isPositive(Net* net);

  /** Returns true if the given net is the negative member of this pair. */
  bool isNegative(Net* net);

  /** Returns true if a specific coupled spacing constraint is configured for this pair. */
  bool hasCoupledSpacing() { return this->coupledSpacing_ > 0.0; }

  /** Returns true if a maximum allowable intra-pair skew constraint is configured. */
  bool hasMaxSkew() { return this->maxSkew_ > 0.0; }

  /** Returns a copy of this differential pair with updated coupled spacing. */
  DifferentialPair* withCoupledSpacing(double newSpacing) {
    return new (jrt::kBoard) DifferentialPair(positiveNet_, negativeNet_, baseName_, newSpacing, maxSkew_);
  }

  /** Returns a copy of this differential pair with updated maximum skew. */
  DifferentialPair* withMaxSkew(double newMaxSkew) {
    return new (jrt::kBoard) DifferentialPair(positiveNet_, negativeNet_, baseName_, coupledSpacing_,
                                newMaxSkew);
  }

  /** Record semantics over (positiveNet, negativeNet, baseName, coupledSpacing, maxSkew). */
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  Net* positiveNet_;
  Net* negativeNet_;
  std::string baseName_;
  double coupledSpacing_;
  double maxSkew_;
};

}  // namespace fr
