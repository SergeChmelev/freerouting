#include "autoroute/ItemRouteResult.h"

namespace fr {

ItemRouteResult::ItemRouteResult(int32_t itemId) : ItemRouteResult(itemId, 0, 0, 0, 0, 0, 1) {
  this->improved_ = false;
}

ItemRouteResult::ItemRouteResult(int32_t itemId, int32_t viaCountBefore, int32_t viaCountAfter,
                                 double traceLengthBefore, double traceLengthAfter,
                                 int32_t incompleteCountBefore, int32_t incompleteCountAfter) {
  this->itemId_ = itemId;
  this->viaCountBefore = viaCountBefore;
  this->viaCountAfter = viaCountAfter;
  this->traceLengthBefore = traceLengthBefore;
  this->traceLengthAfter = traceLengthAfter;
  this->incompleteCountBefore_ = incompleteCountBefore;
  this->incompleteCountAfter = incompleteCountAfter;

  if (incompleteCountAfter < incompleteCountBefore) {
    improved_ = true;
  } else if (incompleteCountAfter > incompleteCountBefore) {
    improved_ = false;
  } else {  // incompleteCountAfter == incompleteCountBefore
    if (viaCountAfter < viaCountBefore) {
      improved_ = true;
    } else if (viaCountAfter > viaCountBefore) {
      improved_ = false;
    } else {  // viaCountAfter == viaCountBefore
      if (traceLengthAfter < traceLengthBefore) {
        improved_ = true;
      } else if (traceLengthAfter > traceLengthBefore) {
        improved_ = false;
      } else {
        improved_ = false;
      }
    }
  }

  improvementPercentage_ = static_cast<float>(
      viaCountBefore != 0 && traceLengthBefore != 0
          ? 1.0 - (((static_cast<double>(viaCountAfter) / viaCountBefore) + (traceLengthAfter / traceLengthBefore)) / 2)
          : 0);
}

int32_t ItemRouteResult::compareTo(ItemRouteResult* r) {
  if (incompleteCountAfter < r->incompleteCountAfter) {
    return -1;
  } else if (incompleteCountAfter > r->incompleteCountAfter) {
    return 1;
  } else {  // incompleteCountAfter == r.incompleteCountAfter
    if (viaCountAfter < r->viaCountAfter) {
      return -1;
    } else if (viaCountAfter > r->viaCountAfter) {
      return 1;
    } else {  // viaCountAfter == r.viaCountAfter
      if (traceLengthAfter < r->traceLengthAfter) {
        return -1;
      } else if (traceLengthAfter > r->traceLengthAfter) {
        return 1;
      } else {
        return jrt::Integer::compare(this->itemId_, r->itemId_);
      }
    }
  }
}

}  // namespace fr
