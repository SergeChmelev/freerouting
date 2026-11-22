#pragma once

#include <istream>
#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Single-entry, in-memory headless routing facade: board parsing, routing pipeline sequencing
 * (fanout/autoroute + optimizer + DRC) and SES serialization, synchronously on the calling thread,
 * without disk I/O, schedulers, GUI managers, or telemetry. This is the entry point of the C++ CLI.
 *
 * <p>route() steps: (1) DsnReader::readBoard on the DSN bytes; (2) settings = SettingsMerger of
 * DefaultSettings + DsnFileSettings (+ RulesFileSettings after RulesReader::read applied the
 * rules to the board) (+ ApiSettings(request.settings())), merge(); setLayerCount to the board
 * layer count if different; applyBoardSpecificOptimizations; applyNetClassExclusions;
 * applyBoardSettingsOverrides; prepareBoardConnectivity; captureInitialClearanceBaseline;
 * (3) RoutingJob with a HeadlessStoppableThread (deadline from jobTimeoutString "HH:MM:SS");
 * (4) RoutingPipeline::createForHeadless + progress listeners; (5) pipeline->run(); (6) final
 * state/statistics; (7) DRC if requested; (8) SES if requested.
 */
// PORT NOTE: Java ByteArrayInputStream/ByteArrayOutputStream -> std::istringstream /
// std::ostringstream on the stack; byte[] -> jrt::Array<int8_t>*; Long -> optional<int64_t>.
class FreeroutingHeadlessEngine final : public virtual jrt::Object {
 private:
  FreeroutingHeadlessEngine() = default;

 public:
  /**
   * Routes a board using the specified in-memory request (must not be null: throws
   * IllegalArgumentException).
   */
  static HeadlessRoutingResult* route(HeadlessRoutingRequest* request);

  /** Convenience method: routes a board given raw DSN bytes with default settings. */
  static HeadlessRoutingResult* route(jrt::Array<int8_t>* dsnBytes);

  /** Convenience method: routes a board given DSN string content with default settings. */
  static HeadlessRoutingResult* route(const std::string& dsnContent);

  /** Convenience method: routes a board given a DSN input stream with default settings. */
  static HeadlessRoutingResult* route(std::istream& dsnStream);

 private:
  /** Calls the listener, logging (not propagating) RuntimeExceptions it throws. */
  static void notifyProgress(HeadlessRoutingProgressListener* listener,
                             HeadlessRoutingProgress* progress);

 public:
  /** Standalone DRC check for an in-memory routing board (null board -> empty list). */
  static jrt::Collection<ClearanceViolation*>* checkDrc(RoutingBoard* board);

  /**
   * Standalone DRC check for a board represented by DSN bytes. Throws IllegalArgumentException if
   * dsnBytes is null/empty or fails to parse, IOException (Java: UncheckedIOException) on IO error.
   */
  static jrt::Collection<ClearanceViolation*>* checkDrc(jrt::Array<int8_t>* dsnBytes);

 private:
  /** Applies plane_as_obstacle (board->changePlaneAsObstacle) when set. */
  static void applyBoardSettingsOverrides(RoutingBoard* board, RouterSettings* routerSettings);

  /**
   * expandBoundingBoxToIncludeAllItems, reduceNetsOfRouteItems, connectNetlessPads(false,
   * fixKicadFootprints, new int[1]), clusterNetTies, DifferentialPairDetector::detectAndRegister.
   */
  static void prepareBoardConnectivity(RoutingBoard* board, RouterSettings* routerSettings);

  /** Records pre-existing/unfixable clearance violations and the clearance baseline. */
  static void captureInitialClearanceBaseline(RoutingBoard* board);

  /** Parses "HH:MM:SS" into an absolute deadline (epoch millis), or null. */
  static std::optional<int64_t> parseDeadlineMillis(
      int64_t startTimeMillis, const std::optional<std::string>& timeoutString);
};

}  // namespace fr
