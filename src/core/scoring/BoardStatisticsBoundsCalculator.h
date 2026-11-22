#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

// Private records / helper class of BoardStatisticsBoundsCalculator (hoisted).

/** (Java private record BoardStatisticsBoundsCalculator.CachedBounds) */
class BoardStatisticsBoundsCalculator_CachedBounds : public virtual jrt::Object {
 public:
  BoardStatisticsBoundsCalculator_CachedBounds(AngleRestriction angleRestriction,
                                               BoardStatisticsBounds* bounds)
      : angleRestriction_(angleRestriction), bounds_(bounds) {}

  AngleRestriction angleRestriction() { return angleRestriction_; }
  BoardStatisticsBounds* bounds() { return bounds_; }

  // Record equals/hashCode/toString: in the .cpp (component types are incomplete here).
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  AngleRestriction angleRestriction_;
  BoardStatisticsBounds* bounds_;
};

/**
 * A pin, or a conduction area when area is set (x, y its centroid).
 * (Java private record BoardStatisticsBoundsCalculator.Terminal)
 */
class BoardStatisticsBoundsCalculator_Terminal : public virtual jrt::Object {
 public:
  BoardStatisticsBoundsCalculator_Terminal(double x, double y, jrt::Set<int32_t>* signalLayers,
                                           Area* area)
      : x_(x), y_(y), signalLayers_(signalLayers), area_(area) {}

  double x() { return x_; }
  double y() { return y_; }
  jrt::Set<int32_t>* signalLayers() { return signalLayers_; }
  Area* area() { return area_; }

  // Record equals/hashCode/toString: in the .cpp (component types are incomplete here).
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  double x_;
  double y_;
  jrt::Set<int32_t>* signalLayers_;
  Area* area_;
};

/** (Java private record BoardStatisticsBoundsCalculator.MstResult) */
class BoardStatisticsBoundsCalculator_MstResult : public virtual jrt::Object {
 public:
  BoardStatisticsBoundsCalculator_MstResult(double length, int32_t bendCount)
      : length_(length), bendCount_(bendCount) {}

  double length() { return length_; }
  int32_t bendCount() { return bendCount_; }

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<BoardStatisticsBoundsCalculator_MstResult>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf(length_, other->length_) && bendCount_ == other->bendCount_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(length_));
    h = jrt::hashCombine(h, jrt::hashOf(bendCount_));
    return h;
  }
  std::string toString() override {
    return "MstResult[length=" + jrt::str(length_) + ", bendCount=" + jrt::str(bendCount_) + "]";
  }

 private:
  double length_;
  int32_t bendCount_;
};

/** (Java private record BoardStatisticsBoundsCalculator.ViaSpan) */
class BoardStatisticsBoundsCalculator_ViaSpan : public virtual jrt::Object {
 public:
  BoardStatisticsBoundsCalculator_ViaSpan(int32_t firstLayer, int32_t lastLayer)
      : firstLayer_(firstLayer), lastLayer_(lastLayer) {}

  int32_t firstLayer() { return firstLayer_; }
  int32_t lastLayer() { return lastLayer_; }

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<BoardStatisticsBoundsCalculator_ViaSpan>(o);
    if (other == nullptr) return false;
    return firstLayer_ == other->firstLayer_ && lastLayer_ == other->lastLayer_;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, firstLayer_);
    h = jrt::hashCombine(h, lastLayer_);
    return h;
  }
  std::string toString() override {
    return "ViaSpan[firstLayer=" + jrt::str(firstLayer_) +
           ", lastLayer=" + jrt::str(lastLayer_) + "]";
  }

 private:
  int32_t firstLayer_;
  int32_t lastLayer_;
};

/** Union-find over layer numbers. (Java private static final class ...LayerGroups) */
class BoardStatisticsBoundsCalculator_LayerGroups : public virtual jrt::Object {
 public:
  BoardStatisticsBoundsCalculator_LayerGroups() = default;

  void addLayers(jrt::Collection<int32_t>* layers);

  int32_t count();

  jrt::Set<int32_t>* roots();

  jrt::Set<int32_t>* groupsCoveredBy(BoardStatisticsBoundsCalculator_ViaSpan* span);

 private:
  int32_t find(int32_t layer);

  void union_(int32_t firstLayer, int32_t secondLayer);

  jrt::Map<int32_t, int32_t>* parent = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();
};

/** Computes board-only lower bounds for optimizer scoring. */
class BoardStatisticsBoundsCalculator final : public virtual jrt::Object {
 private:
  /**
   * PORT NOTE: Java WeakHashMap -> jrt::HashMap (keys are kept alive; boards are cached for the
   * process lifetime). `synchronized` dropped (single-threaded port).
   */
  static jrt::Map<BasicBoard*, BoardStatisticsBoundsCalculator_CachedBounds*>* CACHE();

  BoardStatisticsBoundsCalculator() {}

 public:
  static BoardStatisticsBounds* calculate(BasicBoard* board);

 private:
  static BoardStatisticsBounds* calculateUncached(BasicBoard* board,
                                                  AngleRestriction angleRestriction);

  static jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* getTerminals(BasicBoard* board,
                                                                             Net* net);

  static BoardStatisticsBoundsCalculator_Terminal* toTerminal(BasicBoard* board, Item* item);

  static void addSignalLayer(BasicBoard* board, int32_t layer, jrt::Set<int32_t>* signalLayers);

  /**
   * Returns the length and bend count of a minimum spanning tree over the terminals of a net.
   * Pins are points; all conduction areas of the net form a single node at distance 0 from the
   * pins they cover. Distances follow the trace angle restriction.
   */
  static BoardStatisticsBoundsCalculator_MstResult* calculateMst(
      jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* terminals,
      AngleRestriction angleRestriction);

  /** PORT NOTE: Java switch has `case null` - angleRestriction is never null here. */
  static double distance(BoardStatisticsBoundsCalculator_Terminal* first,
                         BoardStatisticsBoundsCalculator_Terminal* second,
                         AngleRestriction angleRestriction);

  /** Euclidean distance from a pin to the nearest conduction area of its net, 0 inside one. */
  static double distanceToAreas(BoardStatisticsBoundsCalculator_Terminal* pin,
                                jrt::List<Area*>* areas);

  static int32_t calculateMinimumViaCount(
      Net* net, jrt::List<BoardStatisticsBoundsCalculator_Terminal*>* terminals);
};

}  // namespace fr
