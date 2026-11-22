#include "io/BoardMetadata.h"

#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Unit.h"
#include "settings/RouterSettings.h"

namespace fr::io {

bool BoardMetadata::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BoardMetadata>(o);
  if (other == nullptr) return false;
  return hostCad_ == other->hostCad_ && hostVersion_ == other->hostVersion_ &&
         layerCount_ == other->layerCount_ && unit_ == other->unit_ &&
         resolution_ == other->resolution_ && snapAngle_ == other->snapAngle_ &&
         jrt::equalsOf(routerSettings_, other->routerSettings_);
}

int32_t BoardMetadata::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(hostCad_));
  h = jrt::hashCombine(h, jrt::hashOf(hostVersion_));
  h = jrt::hashCombine(h, layerCount_);
  h = jrt::hashCombine(h, jrt::hashOf(unit_));
  h = jrt::hashCombine(h, resolution_);
  h = jrt::hashCombine(h, jrt::hashOf(snapAngle_));
  h = jrt::hashCombine(h, jrt::hashOf(routerSettings_));
  return h;
}

std::string BoardMetadata::toString() {
  return "BoardMetadata[hostCad=" + jrt::str(hostCad_) + ", hostVersion=" + jrt::str(hostVersion_) +
         ", layerCount=" + jrt::str(layerCount_) + ", unit=" + Unit_toString(unit_) +
         ", resolution=" + jrt::str(resolution_) + ", snapAngle=" + AngleRestriction_toString(snapAngle_) +
         ", routerSettings=" + jrt::str(routerSettings_) + "]";
}

}  // namespace fr::io
