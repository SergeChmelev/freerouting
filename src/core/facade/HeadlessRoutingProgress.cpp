#include "core/facade/HeadlessRoutingProgress.h"

#include "core/RoutingStage.h"
#include "core/scoring/BoardStatistics.h"

namespace fr {

bool HeadlessRoutingProgress::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<HeadlessRoutingProgress>(o);
  if (other == nullptr) return false;
  return stage_ == other->stage_ && passNumber_ == other->passNumber_ &&
         unroutedCount_ == other->unroutedCount_ &&
         jrt::equalsOf<BoardStatistics*>(statistics_, other->statistics_) &&
         message_ == other->message_;
}

int32_t HeadlessRoutingProgress::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(stage_));
  h = jrt::hashCombine(h, jrt::hashOf(passNumber_));
  h = jrt::hashCombine(h, jrt::hashOf(unroutedCount_));
  h = jrt::hashCombine(h, jrt::hashOf<BoardStatistics*>(statistics_));
  h = jrt::hashCombine(h, jrt::stringHashCode(message_));
  return h;
}

std::string HeadlessRoutingProgress::toString() {
  return "HeadlessRoutingProgress[stage=" + RoutingStage_toString(stage_) +
         ", passNumber=" + jrt::str(passNumber_) + ", unroutedCount=" + jrt::str(unroutedCount_) +
         ", statistics=" + jrt::str(static_cast<jrt::Object*>(statistics_)) +
         ", message=" + message_ + "]";
}

}  // namespace fr
