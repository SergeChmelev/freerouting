#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Radius, contender cap, and whether any nearby net counts. Attempt numbers start at 1.
 * Java record Reach(double radiusMm, int contenders, boolean anyNet) - fully implemented.
 */
class RetryNeighbourhood_Reach : public virtual jrt::Object {
 private:
  double radiusMm_;
  int32_t contenders_;
  bool anyNet_;

 public:
  RetryNeighbourhood_Reach(double radiusMm, int32_t contenders, bool anyNet)
      : radiusMm_(radiusMm), contenders_(contenders), anyNet_(anyNet) {}
  double radiusMm() { return radiusMm_; }
  int32_t contenders() { return contenders_; }
  bool anyNet() { return anyNet_; }

  /** Defined inline below RetryNeighbourhood (needs its constants). */
  static RetryNeighbourhood_Reach* of(int32_t attempt);

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = dynamic_cast<RetryNeighbourhood_Reach*>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf(radiusMm_, other->radiusMm_) && contenders_ == other->contenders_ &&
           anyNet_ == other->anyNet_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(radiusMm_));
    h = jrt::hashCombine(h, jrt::hashOf(contenders_));
    h = jrt::hashCombine(h, jrt::hashOf(anyNet_));
    return h;
  }
  std::string toString() override {
    return "Reach[radiusMm=" + jrt::str(radiusMm_) + ", contenders=" + jrt::str(contenders_) +
           ", anyNet=" + jrt::str(anyNet_) + "]";
  }
};

/**
 * Later exploration tries rip a wider neighbourhood around open pins. The first try does not rip
 * on radius alone; each later try widens the radius and removes other nets' traces and vias
 * inside it. Ported from freerouting.net (Erik Gavriluk, Experimental Television Center).
 */
class RetryNeighbourhood : public virtual jrt::Object {
 public:
  static constexpr double RADIUS_MM = 1.5;
  static constexpr double MAX_RADIUS_MM = 12;
  static constexpr int32_t MAX_CONTENDERS = 3;
  static constexpr int32_t MAX_WIDE_CONTENDERS = 12;

 private:
  RetryNeighbourhood() = default;

 public:
  /**
   * Removes other nets' traces and vias near pins that still have an open connection. Returns how
   * many items were removed. The first try removes nothing.
   */
  static int32_t ripNearOpenPins(BasicBoard* board, int32_t attempt);
  static int32_t radius(BasicBoard* board, double mm);
};

inline RetryNeighbourhood_Reach* RetryNeighbourhood_Reach::of(int32_t attempt) {
  if (attempt <= 1) {
    return new RetryNeighbourhood_Reach(RetryNeighbourhood::RADIUS_MM, RetryNeighbourhood::MAX_CONTENDERS, false);
  }
  return new RetryNeighbourhood_Reach(
      jrt::Math::min(RetryNeighbourhood::RADIUS_MM * attempt, RetryNeighbourhood::MAX_RADIUS_MM),
      jrt::Math::min(RetryNeighbourhood::MAX_CONTENDERS * attempt, RetryNeighbourhood::MAX_WIDE_CONTENDERS),
      true);
}

}  // namespace fr
