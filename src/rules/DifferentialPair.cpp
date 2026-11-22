// Port of app.freerouting.rules.DifferentialPair
#include "rules/DifferentialPair.h"

#include "rules/Net.h"

namespace fr {

DifferentialPair::DifferentialPair(Net* positiveNet, Net* negativeNet,
                                   const std::string& baseName, double coupledSpacing,
                                   double maxSkew) {
  if (positiveNet == nullptr) {
    throw jrt::NullPointerException("positiveNet cannot be null");
  }
  if (negativeNet == nullptr) {
    throw jrt::NullPointerException("negativeNet cannot be null");
  }
  if (positiveNet->equals(negativeNet)) {
    throw jrt::IllegalArgumentException("positiveNet and negativeNet cannot be the same net: " +
                                        positiveNet->name);
  }
  if (coupledSpacing < 0.0) {
    coupledSpacing = 0.0;
  }
  if (maxSkew < 0.0) {
    maxSkew = 0.0;
  }
  this->positiveNet_ = positiveNet;
  this->negativeNet_ = negativeNet;
  this->baseName_ = baseName;
  this->coupledSpacing_ = coupledSpacing;
  this->maxSkew_ = maxSkew;
}

bool DifferentialPair::contains(Net* net) {
  if (net == nullptr) {
    return false;
  }
  return this->positiveNet_->equals(net) || this->negativeNet_->equals(net) ||
         this->positiveNet_->name == net->name || this->negativeNet_->name == net->name;
}

Net* DifferentialPair::getPartner(Net* net) {
  if (net == nullptr) {
    return nullptr;
  }
  if (this->positiveNet_->equals(net) || this->positiveNet_->name == net->name) {
    return this->negativeNet_;
  }
  if (this->negativeNet_->equals(net) || this->negativeNet_->name == net->name) {
    return this->positiveNet_;
  }
  return nullptr;
}

bool DifferentialPair::isPositive(Net* net) {
  return net != nullptr && (this->positiveNet_->equals(net) || this->positiveNet_->name == net->name);
}

bool DifferentialPair::isNegative(Net* net) {
  return net != nullptr && (this->negativeNet_->equals(net) || this->negativeNet_->name == net->name);
}

bool DifferentialPair::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<DifferentialPair>(o);
  return other != nullptr && jrt::equalsOf(positiveNet_, other->positiveNet_) &&
         jrt::equalsOf(negativeNet_, other->negativeNet_) && baseName_ == other->baseName_ &&
         jrt::equalsOf(coupledSpacing_, other->coupledSpacing_) &&
         jrt::equalsOf(maxSkew_, other->maxSkew_);
}

int32_t DifferentialPair::hashCode() {
  int32_t h = jrt::hashOf(positiveNet_);
  h = jrt::hashCombine(h, jrt::hashOf(negativeNet_));
  h = jrt::hashCombine(h, jrt::hashOf(baseName_));
  h = jrt::hashCombine(h, jrt::hashOf(coupledSpacing_));
  return jrt::hashCombine(h, jrt::hashOf(maxSkew_));
}

std::string DifferentialPair::toString() {
  return "DifferentialPair[positiveNet=" + jrt::str(positiveNet_) +
         ", negativeNet=" + jrt::str(negativeNet_) + ", baseName=" + baseName_ +
         ", coupledSpacing=" + jrt::str(coupledSpacing_) + ", maxSkew=" + jrt::str(maxSkew_) +
         "]";
}

}  // namespace fr
