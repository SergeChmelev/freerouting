#include "core/facade/HeadlessRoutingResult.h"

#include "board/facade/RoutingBoard.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "drc/ClearanceViolation.h"

namespace fr {

HeadlessRoutingResult::HeadlessRoutingResult(
    bool completed, bool cancelled, int32_t unroutedConnections,
    jrt::Collection<ClearanceViolation*>* clearanceViolations, BoardStatistics* statistics,
    jrt::Array<int8_t>* sesBytes, int64_t executionTimeMillis,
    const std::optional<std::string>& errorMessage, RoutingBoard* board)
    : completed_(completed),
      cancelled_(cancelled),
      unroutedConnections_(unroutedConnections),
      statistics_(statistics),
      executionTimeMillis_(executionTimeMillis),
      errorMessage_(errorMessage),
      board_(board) {
  // PORT NOTE: List.copyOf / List.of (immutable) -> a fresh ArrayList copy.
  this->clearanceViolations_ =
      clearanceViolations != nullptr
          ? static_cast<jrt::Collection<ClearanceViolation*>*>(
                new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>(clearanceViolations))
          : static_cast<jrt::Collection<ClearanceViolation*>*>(
                new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>());
  this->sesBytes_ = sesBytes != nullptr ? sesBytes->clone() : new (jrt::kOp) jrt::Array<int8_t>(0);
}

jrt::Array<int8_t>* HeadlessRoutingResult::sesBytes() { return sesBytes_->clone(); }

bool HeadlessRoutingResult::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* that = jrt::fast_cast<HeadlessRoutingResult>(o);
  if (that == nullptr) {
    return false;
  }
  return completed_ == that->completed_ && cancelled_ == that->cancelled_ &&
         unroutedConnections_ == that->unroutedConnections_ &&
         executionTimeMillis_ == that->executionTimeMillis_ &&
         jrt::equalsOf<jrt::Collection<ClearanceViolation*>*>(clearanceViolations_,
                                                              that->clearanceViolations_) &&
         jrt::equalsOf<BoardStatistics*>(statistics_, that->statistics_) &&
         jrt::Arrays::equals(sesBytes_, that->sesBytes_) &&
         errorMessage_ == that->errorMessage_ &&
         jrt::equalsOf<RoutingBoard*>(board_, that->board_);
}

int32_t HeadlessRoutingResult::hashCode() {
  int32_t result =
      jrt::hashAll(completed_, cancelled_, unroutedConnections_, clearanceViolations_,
                   statistics_, executionTimeMillis_, errorMessage_, board_);
  result = jrt::hashCombine(result, jrt::Arrays::hashCode(sesBytes_));
  return result;
}

std::string HeadlessRoutingResult::toString() {
  return "HeadlessRoutingResult[completed=" + jrt::str(completed_) +
         ", cancelled=" + jrt::str(cancelled_) +
         ", unroutedConnections=" + jrt::str(unroutedConnections_) +
         ", clearanceViolations=" + jrt::str(static_cast<jrt::Object*>(clearanceViolations_)) +
         ", statistics=" + jrt::str(static_cast<jrt::Object*>(statistics_)) +
         ", sesBytes=" + jrt::str(static_cast<jrt::Object*>(sesBytes_)) +
         ", executionTimeMillis=" + jrt::str(executionTimeMillis_) +
         ", errorMessage=" + jrt::str(errorMessage_) +
         ", board=" + jrt::str(static_cast<jrt::Object*>(board_)) + "]";
}

std::string HeadlessRoutingResult::sesContent() {
  return std::string(reinterpret_cast<const char*>(sesBytes_->data()),
                     static_cast<std::size_t>(sesBytes_->length));
}

HeadlessRoutingResult* HeadlessRoutingResult::failure(const std::string& errorMessage,
                                                      int64_t executionTimeMillis) {
  return new (jrt::kBoard) HeadlessRoutingResult(false, false, -1, new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>(),
                                   nullptr, new (jrt::kOp) jrt::Array<int8_t>(0), executionTimeMillis,
                                   errorMessage, nullptr);
}

HeadlessRoutingResult* HeadlessRoutingResult::cancelled(RoutingBoard* board,
                                                        BoardStatistics* stats,
                                                        int64_t executionTimeMillis) {
  int32_t unrouted = -1;
  if (stats != nullptr && stats->connections != nullptr) {
    // Java unboxes the Integer (NullPointerException when absent).
    if (!stats->connections->incompleteCount.has_value()) {
      throw jrt::NullPointerException("incompleteCount");
    }
    unrouted = *stats->connections->incompleteCount;
  }
  return new (jrt::kBoard) HeadlessRoutingResult(false, true, unrouted, new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>(),
                                   stats, new (jrt::kOp) jrt::Array<int8_t>(0), executionTimeMillis,
                                   std::string("Routing was cancelled"), board);
}

}  // namespace fr
