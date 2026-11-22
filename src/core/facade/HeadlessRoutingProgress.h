#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Snapshot of routing progress emitted to HeadlessRoutingProgressListener (Java record).
 *
 * stage: the current routing stage; passNumber: current pass within the stage (1-based);
 * unroutedCount: unrouted connections remaining; statistics: latest board statistics or null;
 * message: informative status of the latest event.
 */
class HeadlessRoutingProgress : public virtual jrt::Object {
 public:
  HeadlessRoutingProgress(RoutingStage stage, int32_t passNumber, int32_t unroutedCount,
                          BoardStatistics* statistics, const std::string& message)
      : stage_(stage),
        passNumber_(passNumber),
        unroutedCount_(unroutedCount),
        statistics_(statistics),
        message_(message) {}

  RoutingStage stage() { return stage_; }
  int32_t passNumber() { return passNumber_; }
  int32_t unroutedCount() { return unroutedCount_; }
  BoardStatistics* statistics() { return statistics_; }
  std::string message() { return message_; }

  // Record semantics: component-wise equals, hash = 31*h + hash(c), Java record toString.
  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  RoutingStage stage_;
  int32_t passNumber_;
  int32_t unroutedCount_;
  BoardStatistics* statistics_;
  std::string message_;
};

}  // namespace fr
