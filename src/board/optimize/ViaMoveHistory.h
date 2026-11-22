#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** (Java private static final class ViaMoveHistory.Track; members public for the outer class) */
class ViaMoveHistory_Track final : public virtual jrt::Object {
 public:
  /** Initialised in the constructor (.cpp): jrt::ArrayList<FloatPoint*> needs FloatPoint complete. */
  jrt::List<FloatPoint*>* locations;
  int32_t returns = 0;

  ViaMoveHistory_Track();

  /**
   * A move returns if it is longer than accuracy and ends within accuracy of a location held
   * before, other than from. Short moves never count: a via that creeps along in small steps is
   * not cycling.
   */
  bool isReturn(FloatPoint* from, FloatPoint* to, int32_t accuracy);
};

/**
 * The locations each via has taken during one fixpoint loop of TraceTightener::optChangedArea,
 * used to stop a via that moves back and forth.
 *
 * A move "returns" when it brings the via back within accuracy of a location it has already held
 * in this loop, other than the one it leaves. After two returns the via is proven to cycle: from
 * then on a move to a location it has already held is refused. As long as no via returns twice,
 * the history refuses nothing. (Java package-private class)
 */
class ViaMoveHistory final : public virtual jrt::Object {
 private:
  /** Number of returns that proves a cycle. */
  static constexpr int32_t RETURNS_FOR_CYCLE = 2;

  jrt::Map<int32_t, ViaMoveHistory_Track*>* tracks =
      new jrt::HashMap<int32_t, ViaMoveHistory_Track*>();

 public:
  /**
   * Returns false if moving the via from from to to would continue a proven cycle, that is if
   * the via has already returned twice and to is a location it has held.
   */
  bool allowsMove(Via* via, Point* from, Point* to, int32_t accuracy);

  /** Records that the via has moved from from to to. */
  void recordMove(Via* via, Point* from, Point* to, int32_t accuracy);
};

}  // namespace fr
