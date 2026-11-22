// Port of app.freerouting.rules.HoleSpacing
#pragma once

#include <functional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Drilled item and its edge-to-edge clearance gap. */
class HoleSpacing_TooCloseItem : public virtual jrt::Object {
 public:
  /** Creates a new TooCloseItem. */
  HoleSpacing_TooCloseItem(DrillItem* item, double gap) : item(item), gap(gap) {}

  DrillItem* item;   // final
  const double gap;
};

/**
 * KiCad's hole-to-hole rule. KiCad keeps no copper clearance within a net, but between drills of
 * any nets it keeps the board's hole-to-hole minimum (BoardRules::getMinHoleToHole()), measured
 * edge to edge. The router checks it where it places or moves a via and the judge reports it, so
 * both use the same rule. Only pairs with a via are checked.
 */
class HoleSpacing final {
 public:
  /** Whether a padstack has a drilled hole: it spans layers or is a bare hole. */
  static bool hasHole(Padstack* padstack);

  /**
   * The edge-to-edge gap between the holes of radii radius1 and radius2 at the two centres,
   * negative where they overlap.
   */
  static double gap(FloatPoint* center1, double radius1, FloatPoint* center2, double radius2);

  /**
   * Whether a via of padstack at location keeps the hole-to-hole minimum from every drilled item
   * on the board.
   */
  static bool clear(BasicBoard* board, Padstack* padstack, Point* location) {
    return clear(board, padstack, location, nullptr, nullptr);
  }

  /** As clear(board, padstack, location), except self. */
  static bool clear(BasicBoard* board, Padstack* padstack, Point* location, Item* self) {
    return clear(board, padstack, location, self, nullptr);
  }

  /**
   * Whether a via of padstack at location keeps the hole-to-hole minimum from every drilled item
   * on the board except self (the via itself, when it moves) and ignore.
   */
  static bool clear(BasicBoard* board, Padstack* padstack, Point* location, Item* self,
                    jrt::Collection<Item*>* ignore);

  /**
   * The drilled items whose holes are closer than minimum to a hole of radius at center through
   * layers fromLayer to toLayer, with their gaps, in the default tree's order.
   */
  static jrt::List<HoleSpacing_TooCloseItem*>* tooClose(BasicBoard* board, FloatPoint* center,
                                                       double radius, int32_t fromLayer,
                                                       int32_t toLayer, double minimum,
                                                       Item* self);

 private:
  HoleSpacing() = delete;

  static DrillItem* firstTooClose(BasicBoard* board, FloatPoint* center, double radius,
                                  int32_t fromLayer, int32_t toLayer, double minimum, Item* self,
                                  jrt::Collection<Item*>* ignore);

  /** report: Java BiPredicate<DrillItem, Double>; returning false stops the visit. */
  static void visit(BasicBoard* board, FloatPoint* center, double radius, int32_t fromLayer,
                    int32_t toLayer, double minimum, Item* self, jrt::Collection<Item*>* ignore,
                    std::function<bool(DrillItem*, double)> report);
};

}  // namespace fr
