// Port of app.freerouting.core.library.PadOval
#include "core/library/PadOval.h"

#include "geometry/planar/FloatPoint.h"

namespace fr {

bool PadOval::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* other = dynamic_cast<PadOval*>(o);
  if (other == nullptr) {
    return false;
  }
  return jrt::equalsOf<FloatPoint*>(start_, other->start_) &&
         jrt::equalsOf<FloatPoint*>(end_, other->end_) &&
         jrt::equalsOf<double>(radius_, other->radius_);
}

int32_t PadOval::hashCode() {
  int32_t h = jrt::hashOf<FloatPoint*>(start_);
  h = jrt::hashCombine(h, jrt::hashOf<FloatPoint*>(end_));
  return jrt::hashCombine(h, jrt::Double::hashCode(radius_));
}

std::string PadOval::toString() {
  return "PadOval[start=" + jrt::str(start_) + ", end=" + jrt::str(end_) +
         ", radius=" + jrt::str(radius_) + "]";
}

}  // namespace fr
