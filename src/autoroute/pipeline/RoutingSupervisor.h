#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * One exploration attempt. tryNumber starts at 1.
 * Java record Attempt(String kind, String name, int seed, int tryNumber) - fully implemented.
 */
class RoutingSupervisor_Attempt : public virtual jrt::Object {
 private:
  std::string kind_;
  std::string name_;
  int32_t seed_;
  int32_t tryNumber_;

 public:
  RoutingSupervisor_Attempt(const std::string& kind, const std::string& name, int32_t seed, int32_t tryNumber)
      : kind_(kind), name_(name), seed_(seed), tryNumber_(tryNumber) {}
  std::string kind() { return kind_; }
  std::string name() { return name_; }
  int32_t seed() { return seed_; }
  int32_t tryNumber() { return tryNumber_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<RoutingSupervisor_Attempt>(o);
    if (other == nullptr) return false;
    return kind_ == other->kind_ && name_ == other->name_ && seed_ == other->seed_ && tryNumber_ == other->tryNumber_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(kind_));
    h = jrt::hashCombine(h, jrt::hashOf(name_));
    h = jrt::hashCombine(h, jrt::hashOf(seed_));
    h = jrt::hashCombine(h, jrt::hashOf(tryNumber_));
    return h;
  }
  std::string toString() override {
    return "Attempt[kind=" + kind_ + ", name=" + name_ + ", seed=" + jrt::str(seed_) +
           ", tryNumber=" + jrt::str(tryNumber_) + "]";
  }
};

/** Java private record Stats(int tries, int closed) - fully implemented. */
class RoutingSupervisor_Stats : public virtual jrt::Object {
  friend class RoutingSupervisor;

 private:
  int32_t tries_;
  int32_t closed_;

 public:
  RoutingSupervisor_Stats(int32_t tries, int32_t closed) : tries_(tries), closed_(closed) {}
  int32_t tries() { return tries_; }
  int32_t closed() { return closed_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<RoutingSupervisor_Stats>(o);
    if (other == nullptr) return false;
    return tries_ == other->tries_ && closed_ == other->closed_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(tries_));
    h = jrt::hashCombine(h, jrt::hashOf(closed_));
    return h;
  }
  std::string toString() override {
    return "Stats[tries=" + jrt::str(tries_) + ", closed=" + jrt::str(closed_) + "]";
  }
};

/**
 * Chooses exploration attempts after the opening fallback. Each kind is tried once, then the kind
 * that has closed the most opens per try. Later tries of a via kind multiply the via cost, and
 * later neighbourhood tries widen the radius.
 */
class RoutingSupervisor : public virtual jrt::Object {
 private:
  jrt::Map<std::string, RoutingSupervisor_Stats*>* attemptStats =
      new (jrt::kOp) jrt::LinkedHashMap<std::string, RoutingSupervisor_Stats*>();
  int32_t attemptsWithoutGain = 0;

 public:
  RoutingSupervisor() = default;

  static bool explores(RouterSettings* settings);
  virtual bool explorationExhausted(RouterSettings* settings);
  /** Via-cost multiplier of a kind's nth try: 2, then 4, then 8. */
  static int32_t viaFactor(int32_t attempt);
  /**
   * Fallback strategies an exploration attempt runs: the job's strategies that
   * explore_fallback_strategies names, in the job's order. An empty or blank filter keeps the
   * job's list (which may be null -> empty optional). A filter that names none of them gives "none".
   */
  static std::optional<std::string> explorationFallback(RouterSettings* jobSettings);
  /** The next attempt for this board. round is the attempt seed. */
  virtual RoutingSupervisor_Attempt* nextAttempt(int32_t round, RouterSettings* jobSettings, BasicBoard* board);
  /** Applies the attempt to a clone of the job settings. Does not rip the board. */
  static void apply(RoutingSupervisor_Attempt* attempt, RouterSettings* jobSettings, RouterSettings* settings);
  virtual void attemptDone(RoutingSupervisor_Attempt* attempt, int32_t openBefore, int32_t openAfter, bool kept);
  static bool residual(RouterSettings* settings, int32_t open);

 private:
  static jrt::List<std::string>* applicableKinds(RouterSettings* jobSettings, BasicBoard* board);
  static bool neckApplies(RouterSettings* jobSettings, BasicBoard* board);

 public:
  std::string toString() override;
};

}  // namespace fr
