#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A field: the layer and the box (board units) of the balls' centres grown by one pitch.
 * Java record Field(int layer, double x0, double y0, double x1, double y1) - fully implemented.
 */
class BallFields_Field : public virtual jrt::Object {
 private:
  int32_t layer_;
  double x0_;
  double y0_;
  double x1_;
  double y1_;

 public:
  BallFields_Field(int32_t layer, double x0, double y0, double x1, double y1)
      : layer_(layer), x0_(x0), y0_(y0), x1_(x1), y1_(y1) {}
  int32_t layer() { return layer_; }
  double x0() { return x0_; }
  double y0() { return y0_; }
  double x1() { return x1_; }
  double y1() { return y1_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = jrt::fast_cast<BallFields_Field>(o);
    if (other == nullptr) return false;
    return layer_ == other->layer_ && jrt::equalsOf(x0_, other->x0_) && jrt::equalsOf(y0_, other->y0_) &&
           jrt::equalsOf(x1_, other->x1_) && jrt::equalsOf(y1_, other->y1_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(layer_));
    h = jrt::hashCombine(h, jrt::hashOf(x0_));
    h = jrt::hashCombine(h, jrt::hashOf(y0_));
    h = jrt::hashCombine(h, jrt::hashOf(x1_));
    h = jrt::hashCombine(h, jrt::hashOf(y1_));
    return h;
  }
  std::string toString() override {
    return "Field[layer=" + jrt::str(layer_) + ", x0=" + jrt::str(x0_) + ", y0=" + jrt::str(y0_) +
           ", x1=" + jrt::str(x1_) + ", y1=" + jrt::str(y1_) + "]";
  }
};

/**
 * Erik Gavriluk (freerouting.net) (router.autorouter.ball_field_cost, experiment, off by default):
 * the ball fields of a board's area-array parts (BGAs), each grown by one pitch, on the balls'
 * layer. A trace pays cost times its length inside a field.
 */
class BallFields : public virtual jrt::Object {
 public:
  /** The fewest balls a part needs: no QFN, no small array. */
  static constexpr int32_t MIN_BALLS = 16;

 private:
  jrt::List<BallFields_Field*>* fields;
  double cost;

 public:
  /** Copies fields (Java: new ArrayList<>(fields)). */
  BallFields(jrt::List<BallFields_Field*>* fields, double cost);

  virtual double getCost() { return cost; }
  virtual jrt::List<BallFields_Field*>* getFields() { return fields; }
  /** The ball fields of board's parts, or nullptr when it has none or the cost is off. */
  static BallFields* of(BasicBoard* board, double cost);
  /**
   * The field of a part's pins: at least MIN_BALLS single-layer pins on one layer, on an
   * axis-parallel grid of at least three rows and three columns that they fill at least half of.
   * Returns nullptr otherwise.
   */
  static BallFields_Field* fieldOf(jrt::Collection<Pin*>* pinCollection);
  /** fieldOf on the balls' centres (board units). */
  static BallFields_Field* fieldOf(jrt::List<FloatPoint*>* centres, int32_t layer);
  /** The number of distinct values, two values within tolerance counting as one. */
  static int32_t distinct(jrt::List<double>* values, double tolerance);
  /** What a trace from (fromX, fromY) to (toX, toY) on layer pays inside the fields. */
  virtual double pathCost(int32_t layer, double fromX, double fromY, double toX, double toY);
  /** The length of the segment inside the field's box (Liang-Barsky clipping). */
  static double insideLength(BallFields_Field* field, double fromX, double fromY, double toX, double toY);

 private:
  static bool clip(double p, double q, jrt::Array<double>* t);
};

}  // namespace fr
