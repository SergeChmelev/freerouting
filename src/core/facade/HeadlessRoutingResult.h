#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Immutable outcome of a headless routing execution performed by FreeroutingHeadlessEngine (Java
 * record).
 *
 * completed: routing succeeded without cancellation, fatal errors, unrouted nets, or DRC clearance
 * violations; cancelled: stopped by a CancellationToken; unroutedConnections: unrouted connections
 * remaining (-1 on failure); clearanceViolations: authoritative DRC violations; statistics: final
 * board statistics or null; sesBytes: generated SES bytes (empty if disabled or aborted);
 * executionTimeMillis: elapsed wall-clock time; errorMessage: error description or null; board:
 * the in-memory RoutingBoard or null if parsing failed.
 */
class HeadlessRoutingResult : public virtual jrt::Object {
 public:
  /** Defensively copies the violations (List.copyOf; null -> empty) and the SES bytes. */
  HeadlessRoutingResult(bool completed, bool cancelled, int32_t unroutedConnections,
                        jrt::Collection<ClearanceViolation*>* clearanceViolations,
                        BoardStatistics* statistics, jrt::Array<int8_t>* sesBytes,
                        int64_t executionTimeMillis,
                        const std::optional<std::string>& errorMessage, RoutingBoard* board);

  bool completed() { return completed_; }
  bool cancelled() { return cancelled_; }
  int32_t unroutedConnections() { return unroutedConnections_; }
  jrt::Collection<ClearanceViolation*>* clearanceViolations() { return clearanceViolations_; }
  BoardStatistics* statistics() { return statistics_; }
  /** Returns a copy of the SES bytes. */
  jrt::Array<int8_t>* sesBytes();
  int64_t executionTimeMillis() { return executionTimeMillis_; }
  std::optional<std::string> errorMessage() { return errorMessage_; }
  RoutingBoard* board() { return board_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

  /** Returns true if the request completed without a fatal error or cancellation. */
  bool isSuccess() { return !errorMessage_.has_value() && !cancelled_; }
  /** Returns true if all nets were routed and 0 DRC violations exist. */
  bool isCompleted() { return completed_; }
  /** Returns true if the execution was stopped by cancellation. */
  bool isCancelled() { return cancelled_; }
  /** Returns true if there are zero DRC clearance violations. */
  bool isDrcClean() { return clearanceViolations_->isEmpty(); }
  /** Returns the generated Specctra SES file content as a UTF-8 string. */
  std::string sesContent();

  /** Creates a failed result with an error message and timing information. */
  static HeadlessRoutingResult* failure(const std::string& errorMessage,
                                        int64_t executionTimeMillis);
  /** Creates a cancelled result with the partial board state and timing information. */
  static HeadlessRoutingResult* cancelled(RoutingBoard* board, BoardStatistics* stats,
                                          int64_t executionTimeMillis);

 private:
  bool completed_;
  bool cancelled_;
  int32_t unroutedConnections_;
  jrt::Collection<ClearanceViolation*>* clearanceViolations_;
  BoardStatistics* statistics_;
  jrt::Array<int8_t>* sesBytes_;
  int64_t executionTimeMillis_;
  std::optional<std::string> errorMessage_;
  RoutingBoard* board_;
};

}  // namespace fr
