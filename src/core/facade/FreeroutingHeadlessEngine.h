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

  // PORT EXTENSION: the KiCad inputs of the job, applied as Java's HeadlessBoardManager does in
  // applyRouterSettingsForLoadedBoard (same methods, same order: after applyNetClassExclusions,
  // before the other board overrides).

  /**
   * Translates the KiCad custom design rules of the job into clearance classes. Runs before the
   * other clearance overrides, so that they also reach the classes it adds, and after the net class
   * exclusions, which the split net classes inherit.
   */
  static void applyKiCadDesignRules(RoutingBoard* board, RoutingJob* routingJob);

  /**
   * Makes the minimum track width of the KiCad project of the job the narrowest trace the board
   * allows; every automatic narrowing stops there.
   */
  static void applyKiCadProjectMinTrackWidth(RoutingBoard* board, RoutingJob* routingJob);

  /**
   * Gives each plane the clearance and the minimum width of its zone in the KiCad board of the job.
   * A plane is matched to the zone of its net and layer; if the net has several zones on the layer,
   * to the one whose outline has the nearest bounding box.
   */
  static void applyKiCadZoneFillRules(RoutingBoard* board, RoutingJob* routingJob);

  /**
   * Gives each pin the zone connection of its pad in the KiCad board: the pad at the position of
   * the pin, on a layer of the pin. Returns the number of pins that found their pad.
   */
  static int32_t applyKiCadPadConnections(RoutingBoard* board, KiCadBoardZones* boardZones);

  static int64_t cellKey(int64_t x, int64_t y) { return x * 4000037LL + y; }

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
