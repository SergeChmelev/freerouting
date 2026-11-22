// Port of app.freerouting.board.state.ChangedArea
#include "board/state/ChangedArea.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/TileShape.h"

namespace fr {

IntOctagon* ChangedArea_MutableOctagon::toInt() {
  if (rx < lx || uy < ly || lrx < ulx || urx < llx) {
    return IntOctagon::EMPTY();
  }
  return new (jrt::kGeom) IntOctagon(jrt::d2i(jrt::Math::floor(lx)), jrt::d2i(jrt::Math::floor(ly)),
                        jrt::d2i(jrt::Math::ceil(rx)), jrt::d2i(jrt::Math::ceil(uy)),
                        jrt::d2i(jrt::Math::floor(ulx)), jrt::d2i(jrt::Math::ceil(lrx)),
                        jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::ceil(urx)));
}

ChangedArea::ChangedArea(int32_t layerCount) : layerCount(layerCount) {
  arr = new (jrt::kOp) jrt::Array<ChangedArea_MutableOctagon*>(layerCount);
  // initialise all octagons to empty
  for (int32_t i = 0; i < layerCount; i++) {
    (*arr)[i] = new (jrt::kOp) ChangedArea_MutableOctagon();
    (*arr)[i]->setEmpty();
  }
}

void ChangedArea::join(FloatPoint* point, int32_t layer) {
  ChangedArea_MutableOctagon* current = (*arr)[layer];
  current->lx = jrt::Math::min(point->x, current->lx);
  current->ly = jrt::Math::min(point->y, current->ly);
  current->rx = jrt::Math::max(current->rx, point->x);
  current->uy = jrt::Math::max(current->uy, point->y);

  double tmp = point->x - point->y;
  current->ulx = jrt::Math::min(current->ulx, tmp);
  current->lrx = jrt::Math::max(current->lrx, tmp);

  tmp = point->x + point->y;
  current->llx = jrt::Math::min(current->llx, tmp);
  current->urx = jrt::Math::max(current->urx, tmp);
}

void ChangedArea::join(TileShape* shape, int32_t layer) {
  if (shape == nullptr) {
    return;
  }
  int32_t cornerCount = shape->borderLineCount();
  for (int32_t i = 0; i < cornerCount; i++) {
    join(shape->cornerApprox(i), layer);
  }
}

IntOctagon* ChangedArea::getArea(int32_t layer) { return (*arr)[layer]->toInt(); }

IntBox* ChangedArea::surroundingBox() {
  int32_t llx = jrt::Integer::MAX_VALUE;
  int32_t lly = jrt::Integer::MAX_VALUE;
  int32_t urx = jrt::Integer::MIN_VALUE;
  int32_t ury = jrt::Integer::MIN_VALUE;
  for (int32_t i = 0; i < layerCount; i++) {
    ChangedArea_MutableOctagon* current = (*arr)[i];
    llx = jrt::Math::min(llx, jrt::d2i(jrt::Math::floor(current->lx)));
    lly = jrt::Math::min(lly, jrt::d2i(jrt::Math::floor(current->ly)));
    urx = jrt::Math::max(urx, jrt::d2i(jrt::Math::ceil(current->rx)));
    ury = jrt::Math::max(ury, jrt::d2i(jrt::Math::ceil(current->uy)));
  }
  if (llx > urx || lly > ury) {
    return IntBox::EMPTY();
  }
  return new (jrt::kGeom) IntBox(llx, lly, urx, ury);
}

}  // namespace fr
