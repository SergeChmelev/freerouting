#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** How a relation ended after a routing attempt of the net it keeps away. */
enum class RipupMemory_Outcome : int32_t {
  /** Nothing changed: no relation, or a failure below the limit. */
  NONE,
  /** The kept-away net routed: the relation is done. */
  RESOLVED,
  /** The kept-away net failed too often: the other net is now kept away from it instead. */
  FLIPPED,
  /** The flipped relation failed too: the pair is free again for good. */
  RETIRED
};

inline std::string RipupMemory_Outcome_name(RipupMemory_Outcome e) {
  switch (e) {
    case RipupMemory_Outcome::NONE: return "NONE";
    case RipupMemory_Outcome::RESOLVED: return "RESOLVED";
    case RipupMemory_Outcome::FLIPPED: return "FLIPPED";
    case RipupMemory_Outcome::RETIRED: return "RETIRED";
  }
  return "";
}
inline std::string RipupMemory_Outcome_toString(RipupMemory_Outcome e) { return RipupMemory_Outcome_name(e); }
inline jrt::Array<RipupMemory_Outcome>* RipupMemory_Outcome_values() {
  auto* a = new (jrt::kOp) jrt::Array<RipupMemory_Outcome>(4);
  for (int32_t i = 0; i < 4; ++i) (*a)[i] = static_cast<RipupMemory_Outcome>(i);
  return a;
}
inline RipupMemory_Outcome RipupMemory_Outcome_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 4; ++i) {
    auto e = static_cast<RipupMemory_Outcome>(i);
    if (RipupMemory_Outcome_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant RipupMemory.Outcome." + s);
}

/** One relation: keptAwayNet may not rip keptFromNet. */
class RipupMemory_Entry : public virtual jrt::Object {
 public:
  int32_t keptFromNet;
  int32_t keptAwayNet;
  int32_t firstPass;
  bool flipped;
  int32_t lastPass;
  int32_t ripupCount = 0;
  int32_t failuresWhileKeptAway = 0;
  int32_t lastFailurePass = -1;

  RipupMemory_Entry(int32_t keptFromNet, int32_t keptAwayNet, int32_t pass, bool flipped)
      : keptFromNet(keptFromNet), keptAwayNet(keptAwayNet), firstPass(pass), flipped(flipped), lastPass(pass) {}
};

/**
 * Remembers which net removed which other net's traces and vias during the autoroute passes, and
 * why: the net that was being routed when the rip-up happened. Without this memory two nets that
 * want the same space can take it from each other pass after pass.
 */
class RipupMemory : public virtual jrt::Object {
 public:
  /**
   * Passes in which a net failed while kept away from another one, before the relation ends. A net
   * with several open pins can fail more than once in a pass; that counts once.
   */
  static constexpr int32_t MAX_FAILURES_WHILE_KEPT_AWAY = 2;

 private:
  /** Active relations: kept-away net -> kept-from net -> entry . */
  jrt::Map<int32_t, jrt::Map<int32_t, RipupMemory_Entry*>*>* active =
      new (jrt::kOp) jrt::TreeMap<int32_t, jrt::Map<int32_t, RipupMemory_Entry*>*>();
  /** Rip-ups seen so far, active or not: ripper net -> ripped net -> pass of the last one. */
  jrt::Map<int32_t, jrt::Map<int32_t, int32_t>*>* seen = new (jrt::kOp) jrt::TreeMap<int32_t, jrt::Map<int32_t, int32_t>*>();
  /** Unordered pairs (lower net number first) whose relation has flipped once already. */
  jrt::Set<int64_t>* flippedPairs = new (jrt::kOp) jrt::TreeSet<int64_t>();
  /** Unordered pairs that may not be restricted again. */
  jrt::Set<int64_t>* retiredPairs = new (jrt::kOp) jrt::TreeSet<int64_t>();

 public:
  RipupMemory() = default;

  /**
   * Records the items ripperNet removed while it was routed in pass `pass`. A net that ripperNet
   * already removed in an earlier pass is kept away from it from now on. Returns the nets for which
   * a new relation started.
   */
  virtual jrt::Set<int32_t>* recordRipup(int32_t ripperNet, jrt::Collection<Item*>* rippedItems, int32_t pass);
  /** As recordRipup, with the nets of the removed items already collected. */
  virtual jrt::Set<int32_t>* recordRipupOfNets(int32_t ripperNet, jrt::Collection<int32_t>* rippedNets, int32_t pass);
  /** Nets whose traces and vias net may not rip, in net order. */
  virtual jrt::Set<int32_t>* netsNotToRip(int32_t net);
  /** True if net is kept away from at least one other net. */
  virtual bool isKeptAway(int32_t net);
  /**
   * Records the outcome of routing net while it was kept away from keptFromNet, in pass `pass`.
   * A routed net has found its own way and the relation is done. A failure counts against the
   * relation, at most once per pass.
   */
  virtual RipupMemory_Outcome recordOutcome(int32_t net, int32_t keptFromNet, bool routed, int32_t pass);
  virtual bool isRetired(int32_t netA, int32_t netB);
  virtual int32_t activeCount();

 private:
  void remove(int32_t keptAwayNet, int32_t keptFromNet);
  static int64_t pairKey(int32_t netA, int32_t netB);
};

}  // namespace fr
