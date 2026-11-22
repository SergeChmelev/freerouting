// Port of app.freerouting.board.state.ChangedArea
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An octagon with double coordinates that can be enlarged in place. */
class ChangedArea_MutableOctagon : public virtual jrt::Object {
 public:
  double lx = 0;
  double ly = 0;
  double rx = 0;
  double uy = 0;
  double ulx = 0;
  double lrx = 0;
  double llx = 0;
  double urx = 0;

  void setEmpty() {
    lx = jrt::Integer::MAX_VALUE;
    ly = jrt::Integer::MAX_VALUE;
    rx = jrt::Integer::MIN_VALUE;
    uy = jrt::Integer::MIN_VALUE;
    ulx = jrt::Integer::MAX_VALUE;
    lrx = jrt::Integer::MIN_VALUE;
    llx = jrt::Integer::MAX_VALUE;
    urx = jrt::Integer::MIN_VALUE;
  }

  /** Calculates the smallest IntOctagon containing this octagon (IntOctagon::EMPTY if empty). */
  IntOctagon* toInt();
};

/**
 * Linear area of an octagon for each layer, which is changed by an algorithm, so that for example
 * the search trees are updated only there.
 */
class ChangedArea : public virtual jrt::Object {
 public:
  /** Creates a new instance of ChangedArea with all octagons empty. */
  explicit ChangedArea(int32_t layerCount);

  /** Enlarges the octagon on layer, so that it contains point. */
  void join(FloatPoint* point, int32_t layer);

  /** Enlarges the octagon on layer, so that it contains shape. */
  void join(TileShape* shape, int32_t layer);

  /** Get the changed area of layer as IntOctagon. */
  IntOctagon* getArea(int32_t layer);

  /** Get the surrounding box of the changed area on all layers. */
  IntBox* surroundingBox();

  /** Sets the changed area on layer to empty. */
  void setEmpty(int32_t layer) { (*arr)[layer]->setEmpty(); }

  // package-private in Java
  const int32_t layerCount;
  jrt::Array<ChangedArea_MutableOctagon*>* arr;
};

}  // namespace fr
