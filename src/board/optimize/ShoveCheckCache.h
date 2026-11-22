#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Inputs of one shove check, held strictly by value with no object references kept. */
class ShoveCheckCache_Key final : public virtual jrt::Object {
 private:
  int32_t kind;  // 1 octagon, 2 box, 3 simplex
  int32_t lineCount;
  jrt::Array<int32_t>* coords;
  int32_t sideNo;
  int64_t sideX;
  int64_t sideY;
  bool sideHasPoint;
  int32_t dirX;
  int32_t dirY;
  int32_t netCount;
  int32_t net0;
  int32_t net1;
  int32_t layer;
  int32_t clearanceClass;
  int32_t depth;
  int32_t viaDepth;
  int32_t springOverDepth;
  bool timed;
  int32_t hash;

 public:
  /** fromSide, dir and nets may be null. */
  ShoveCheckCache_Key(TileShape* shape, ShapeEntrySide* fromSide, Direction* dir, int32_t layer,
                      jrt::Array<int32_t>* nets, int32_t clearanceClass, int32_t depth,
                      int32_t viaDepth, int32_t springOverDepth, bool timed);

  bool equals(jrt::Object* obj) override;

  int32_t hashCode() override { return hash; }
};

/** A check's outcome and recorded side-effects on shared board state. */
class ShoveCheckCache_Entry final : public virtual jrt::Object {
 public:
  bool result;
  int32_t ids;
  bool wroteObstacle;
  int32_t obstacle;
  bool wroteLayer;
  int32_t layer;

  ShoveCheckCache_Entry(bool result, int32_t ids, bool wroteObstacle, int32_t obstacle,
                        bool wroteLayer, int32_t layer)
      : result(result),
        ids(ids),
        wroteObstacle(wroteObstacle),
        obstacle(obstacle),
        wroteLayer(wroteLayer),
        layer(layer) {}
};

/**
 * Exact memo of the trace shove check in TraceShover.
 *
 * The maze search and optimizer ask the same shove question many times on an unchanged board.
 * Entries hold while the board's content version is unchanged. A hit replays the answer and all
 * shared-state mutations: temporary trace IDs from the ID generator, and writes to the board's
 * shove-failure record.
 */
class ShoveCheckCache final : public virtual jrt::Object {
 public:
  /** The entry count at which the cache starts over, bounding its memory. */
  static constexpr int32_t MAX_ENTRIES = 1 << 16;

  /** The most border lines a simplex key holds; a trace segment's tile has at most eight. */
  static constexpr int32_t MAX_LINES = 8;

 private:
  /** PORT NOTE: Java ThreadLocal -> single process-wide instance (single-threaded port). */
  static ShoveCheckCache*& CURRENT();

  jrt::Map<ShoveCheckCache_Key*, ShoveCheckCache_Entry*>* entries =
      new (jrt::kBoard) jrt::HashMap<ShoveCheckCache_Key*, ShoveCheckCache_Entry*>();
  /** PORT NOTE: Java WeakReference<RoutingBoard> -> plain pointer (only compared by identity). */
  RoutingBoard* owner = nullptr;
  int64_t stamp = jrt::Long::MIN_VALUE;
  int64_t hits = 0;
  int64_t misses = 0;

 public:
  /** Returns this thread's shove check cache instance. */
  static ShoveCheckCache* forThread();

  int64_t getHits() { return hits; }

  int64_t getMisses() { return misses; }

  // Java package-private.
  void countHit() { hits++; }

  void countMiss() { misses++; }

  /** Looks up an entry for key on board at content version contentStamp (nullptr if none). */
  ShoveCheckCache_Entry* tryGet(RoutingBoard* board, int64_t contentStamp,
                                ShoveCheckCache_Key* key);

  /** Puts an entry for key on board at content version contentStamp. */
  void put(RoutingBoard* board, int64_t contentStamp, ShoveCheckCache_Key* key,
           ShoveCheckCache_Entry* entry);

  /** The index of obstacle in the board's obstacle table, adding it when new. */
  static int32_t obstacleIndex(RoutingBoard* board, Item* obstacle);

 private:
  void reset(RoutingBoard* board, int64_t contentStamp);

 public:
  /** True for a check whose key can be held strictly by value. dir and nets may be null. */
  static bool supported(TileShape* shape, Direction* dir, jrt::Array<int32_t>* nets);

 private:
  /** PORT NOTE: `shape.getClass() != Simplex.class` -> typeid(*shape) != typeid(Simplex). */
  static bool integerSimplex(TileShape* shape);

  /** Returns {x / g, y / g} with g = gcd(|x|, |y|) (or {0, 0}). Accessed by Key's constructor. */
  static jrt::Array<int32_t>* primitive(int32_t x, int32_t y);

  static int64_t gcd(int64_t a, int64_t b);

  friend class ShoveCheckCache_Key;
};

}  // namespace fr
