#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Java private record Entry(int ringKey, int position, Item item). */
class BallEscapeOrder_Entry : public virtual jrt::Object {
  friend class BallEscapeOrder;

 private:
  int32_t ringKey_;
  int32_t position_;
  Item* item_;

 public:
  BallEscapeOrder_Entry(int32_t ringKey, int32_t position, Item* item)
      : ringKey_(ringKey), position_(position), item_(item) {}
  int32_t ringKey() { return ringKey_; }
  int32_t position() { return position_; }
  Item* item() { return item_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Erik Gavriluk (freerouting.net) (router.autorouter.ball_escape_order): the order the first pass
 * routes the balls of a ball grid part in, by ring: ring 0 is the grid's outer row and column on
 * each side, ring 1 the balls one pitch further in, and so on.
 */
class BallEscapeOrder : public virtual jrt::Object {
 public:
  /** Java: Set.of("auto", "off", "outer_first", "inner_first"). */
  static jrt::Set<std::string>* MODES();

 private:
  jrt::Map<int32_t, int32_t>* ringByPin;
  bool outerFirst;

  BallEscapeOrder(jrt::Map<int32_t, int32_t>* ringByPin, bool outerFirst)
      : ringByPin(ringByPin), outerFirst(outerFirst) {}

 public:
  /** The number of balls ordered. */
  virtual int32_t getBalls();
  /** The ring of the ball with this pin id, or empty when it is not a ball of a ball grid part. */
  virtual std::optional<int32_t> getRing(int32_t pinId);
  /** The setting's value (auto when unset), checked. */
  static std::string checkMode(const std::optional<std::string>& mode);
  /** The order for board's ball grid parts, or nullptr when the mode is off or there are none. */
  static BallEscapeOrder* of(BasicBoard* board, const std::optional<std::string>& mode);
  /**
   * Each ball's ring on an axis-parallel grid: its distance to the nearest outermost row or column
   * of balls, in pitches, rounded.
   */
  static jrt::Array<int32_t>* rings(jrt::List<FloatPoint*>* centres);
  /**
   * Moves the balls among items to the front, ring by ring in the mode's order, each ring in the
   * items' own order; the other items follow in theirs. Returns the number of balls moved.
   */
  virtual int32_t moveToFront(jrt::List<Item*>* items);
};

}  // namespace fr
