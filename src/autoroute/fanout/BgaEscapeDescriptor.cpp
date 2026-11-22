#include "autoroute/fanout/BgaEscapeDescriptor.h"

#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// BgaEscapeDescriptor_BgaPinDescriptor (Java record)
// ---------------------------------------------------------------------------------------------

bool BgaEscapeDescriptor_BgaPinDescriptor::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeDescriptor_BgaPinDescriptor>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<Pin*>(pin_, other->pin_) && jrt::equalsOf<double>(localU_, other->localU_) &&
         jrt::equalsOf<double>(localV_, other->localV_) && ring_ == other->ring_ &&
         canonicalIndex_ == other->canonicalIndex_ && disposition_ == other->disposition_ &&
         netNo_ == other->netNo_ && clearanceClassIdx_ == other->clearanceClassIdx_ &&
         targetLayer_ == other->targetLayer_ && breakoutSector_ == other->breakoutSector_;
}

int32_t BgaEscapeDescriptor_BgaPinDescriptor::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<Pin*>(pin_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(localU_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(localV_));
  h = jrt::hashCombine(h, ring_);
  h = jrt::hashCombine(h, canonicalIndex_);
  h = jrt::hashCombine(h, jrt::hashOf<BgaEscapeDescriptor_PinDisposition>(disposition_));
  h = jrt::hashCombine(h, netNo_);
  h = jrt::hashCombine(h, clearanceClassIdx_);
  h = jrt::hashCombine(h, targetLayer_);
  h = jrt::hashCombine(h, breakoutSector_);
  return h;
}

std::string BgaEscapeDescriptor_BgaPinDescriptor::toString() {
  return "BgaPinDescriptor[pin=" + jrt::str(pin_) + ", localU=" + jrt::str(localU_) +
         ", localV=" + jrt::str(localV_) + ", ring=" + jrt::str(ring_) +
         ", canonicalIndex=" + jrt::str(canonicalIndex_) +
         ", disposition=" + BgaEscapeDescriptor_PinDisposition_toString(disposition_) +
         ", netNo=" + jrt::str(netNo_) + ", clearanceClassIdx=" + jrt::str(clearanceClassIdx_) +
         ", targetLayer=" + jrt::str(targetLayer_) +
         ", breakoutSector=" + jrt::str(breakoutSector_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaEscapeDescriptor (Java record)
// ---------------------------------------------------------------------------------------------

bool BgaEscapeDescriptor::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeDescriptor>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<Component*>(component_, other->component_) &&
         jrt::equalsOf<double>(rotationDeg_, other->rotationDeg_) &&
         jrt::equalsOf<double>(pitchUm_, other->pitchUm_) &&
         jrt::equalsOf<double>(pitchBoardUnits_, other->pitchBoardUnits_) &&
         latticeClass_ == other->latticeClass_ &&
         jrt::equalsOf<jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>*>(pins_, other->pins_) &&
         jrt::equalsOf<double>(minX_, other->minX_) && jrt::equalsOf<double>(maxX_, other->maxX_) &&
         jrt::equalsOf<double>(minY_, other->minY_) && jrt::equalsOf<double>(maxY_, other->maxY_) &&
         maxRing_ == other->maxRing_;
}

int32_t BgaEscapeDescriptor::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<Component*>(component_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(rotationDeg_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(pitchUm_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(pitchBoardUnits_));
  h = jrt::hashCombine(h, jrt::hashOf<BgaEscapeDescriptor_LatticeClass>(latticeClass_));
  h = jrt::hashCombine(h, jrt::hashOf<jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>*>(pins_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(minX_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(maxX_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(minY_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(maxY_));
  h = jrt::hashCombine(h, maxRing_);
  return h;
}

std::string BgaEscapeDescriptor::toString() {
  return "BgaEscapeDescriptor[component=" + jrt::str(component_) +
         ", rotationDeg=" + jrt::str(rotationDeg_) + ", pitchUm=" + jrt::str(pitchUm_) +
         ", pitchBoardUnits=" + jrt::str(pitchBoardUnits_) +
         ", latticeClass=" + BgaEscapeDescriptor_LatticeClass_toString(latticeClass_) +
         ", pins=" + jrt::str(pins_) + ", minX=" + jrt::str(minX_) + ", maxX=" + jrt::str(maxX_) +
         ", minY=" + jrt::str(minY_) + ", maxY=" + jrt::str(maxY_) +
         ", maxRing=" + jrt::str(maxRing_) + "]";
}

}  // namespace fr
