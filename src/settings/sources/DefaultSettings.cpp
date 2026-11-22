#include "settings/sources/DefaultSettings.h"

#include "autoroute/BoardUpdateStrategy.h"
#include "autoroute/ItemSelectionStrategy.h"
#include "settings/RouterSettings.h"

namespace fr {

RouterSettings* DefaultSettings::getSettings() {
  // Create a RouterSettings object with all default values.
  // Layer-count-dependent arrays (layers, preferredDirectionTraceCost,
  // undesiredDirectionTraceCost) are intentionally left null here. Their actual sizes must come
  // from the board file (via DsnFileSettings) and are finalised by
  // RouterSettings::applyBoardSpecificOptimizations() once the board is loaded.
  RouterSettings* settings = new RouterSettings();

  settings->autorouter->enabled = true;
  settings->autorouter->algorithm = RouterSettings::ALGORITHM_CURRENT;
  settings->jobTimeoutString = std::string("12:00:00");
  settings->autorouter->maxPasses = 0;
  settings->autorouter->stagnationPassLimit = DEFAULT_STAGNATION_PASS_LIMIT;
  settings->autorouter->maxItems = jrt::Integer::MAX_VALUE;
  settings->tracePullTightAccuracy = 500;
  settings->viasAllowed = true;
  settings->automaticNeckdown = true;
  settings->autorouter->saveIntermediateStages = false;
  settings->autorouter->ignoreNetClasses = new jrt::Array<std::string>(0);
  settings->autorouter->startNecked = true;
  settings->autorouter->fallbackStrategies = DEFAULT_FALLBACK_STRATEGIES;
  settings->autorouter->exploreFallbackStrategies = DEFAULT_EXPLORE_FALLBACK_STRATEGIES;
  settings->autorouter->smdNetViaCostFactor = DEFAULT_SMD_NET_VIA_COST_FACTOR;
  settings->autorouter->residualRepairMaxOpen = 4;
  settings->autorouter->explorePatience = 0;
  settings->autorouter->fanoutViaPadGap = 0.0;
  settings->autorouter->ballEscapeOrder = std::string("auto");
  settings->autorouter->exitStubs = true;
  settings->maxThreads = std::nullopt;
  settings->autorouter->maxThreads = std::nullopt;
  settings->copperToEdgeClearanceUm = DEFAULT_COPPER_TO_EDGE_CLEARANCE_UM;
  settings->holeClearanceUm = DEFAULT_HOLE_CLEARANCE_UM;
  settings->holeToHoleUm = DEFAULT_HOLE_TO_HOLE_UM;
  settings->clearanceToleranceUm = DEFAULT_CLEARANCE_TOLERANCE_UM;
  settings->meanderMinAmplitudeMm = DEFAULT_MEANDER_MIN_AMPLITUDE_MM;
  settings->meanderMaxAmplitudeMm = DEFAULT_MEANDER_MAX_AMPLITUDE_MM;
  settings->meanderSpacingMm = DEFAULT_MEANDER_SPACING_MM;
  settings->meanderCornerStyle = DEFAULT_MEANDER_CORNER_STYLE;
  settings->meanderRadiusPercentage = DEFAULT_MEANDER_RADIUS_PERCENTAGE;
  settings->meanderSingleSided = DEFAULT_MEANDER_SINGLE_SIDED;
  settings->planeNets = new jrt::Array<std::string>(0);
  settings->planeAsObstacle = false;
  settings->neckWidthUm = DEFAULT_NECK_WIDTH_UM;
  settings->exactCopper = true;
  settings->strictDrc = false;
  settings->fixKicadFootprints = false;

  // layers is left null intentionally: populated by DsnFileSettings (from the DSN layer count)
  // and then overwritten by applyBoardSpecificOptimizations().

  // Fanout pre-pass defaults
  settings->fanout->enabled = true;
  settings->fanout->maxPasses = 20;
  settings->fanout->maxMillisecondsPerPin = int64_t{10000};
  settings->fanout->ripupAllowed = true;
  settings->fanout->minEscapeLengthMm = 2.5;
  settings->fanout->maxEscapeLengthMm = 4.5;
  settings->fanout->startViaDiameterMm = 0.250;
  settings->fanout->endViaDiameterMm = 0.250;
  settings->fanout->pinSortingOrder = std::string("outer_first");
  settings->fanout->maxItems = jrt::Integer::MAX_VALUE;
  settings->fanout->fallbackToBoardVias = true;
  settings->fanout->retryWithoutEscapeWindow = false;
  settings->fanout->strategy = std::string("eager");
  settings->fanout->twoLayerBga = std::string("no_fanout");
  settings->fanout->bgaEscapeEnabled = true;
  settings->fanout->bgaEscapeMinPins = 9;

  // Optimizer defaults
  settings->optimizer->enabled = true;
  settings->optimizer->algorithm = std::string("freerouting-optimizer");
  settings->optimizer->maxPasses = 100;
  settings->optimizer->maxItems = jrt::Integer::MAX_VALUE;
  settings->optimizer->maxThreads = std::nullopt;
  settings->optimizer->optimizationImprovementThreshold = DEFAULT_OPTIMIZER_IMPROVEMENT_THRESHOLD;
  settings->optimizer->boardUpdateStrategy = BoardUpdateStrategy::GLOBAL_OPTIMAL;
  settings->optimizer->itemSelectionStrategy = ItemSelectionStrategy::SEQUENTIAL;
  settings->optimizer->additionalRipupCostFactorAtStart = 10;
  settings->optimizer->keepIncreasedRipupCosts = false;
  settings->optimizer->viaNudgeEnabled = false;
  settings->optimizer->stepSlideEnabled = true;
  settings->optimizer->traceRipupCostFactor = 0.6f;
  settings->optimizer->maxAutoroutePasses = 6;
  settings->optimizer->enablePreflightGuards = true;
  settings->optimizer->maxConsecutiveFailures = 50;
  settings->optimizer->maxConsecutiveFailuresPass1 = 12;

  // Scalar trace-cost defaults (layer-specific arrays are omitted for the same reason as the
  // layer arrays above: their sizes depend on the board).
  settings->scoring->defaultPreferredDirectionTraceCost = DEFAULT_PREFERRED_DIRECTION_TRACE_COST;
  settings->scoring->defaultUndesiredDirectionTraceCost = DEFAULT_UNDESIRED_DIRECTION_TRACE_COST;

  settings->scoring->viaCosts = DEFAULT_VIA_COSTS;
  settings->scoring->planeViaCosts = DEFAULT_PLANE_VIA_COSTS;
  settings->scoring->startRipupCosts = DEFAULT_START_RIPUP_COSTS;
  settings->scoring->unroutedNetPenalty = DEFAULT_UNROUTED_NET_PENALTY;
  settings->scoring->clearanceViolationPenalty = DEFAULT_CLEARANCE_VIOLATION_PENALTY;
  settings->scoring->bendPenalty = DEFAULT_BEND_PENALTY;
  settings->scoring->defaultBendCost = 0.0;

  settings->routerScoring->version = DEFAULT_ROUTER_SCORING_VERSION;
  settings->routerScoring->unroutedConnectionWeight = DEFAULT_ROUTER_UNROUTED_CONNECTION_WEIGHT;
  settings->routerScoring->unroutedFreeFraction = DEFAULT_ROUTER_UNROUTED_FREE_FRACTION;
  settings->routerScoring->unroutedFirstHalfWeight = DEFAULT_ROUTER_UNROUTED_FIRST_HALF_WEIGHT;
  settings->routerScoring->unroutedSecondHalfWeight = DEFAULT_ROUTER_UNROUTED_SECOND_HALF_WEIGHT;
  settings->routerScoring->clearanceViolationCountWeight = DEFAULT_ROUTER_CLEARANCE_COUNT_WEIGHT;
  settings->routerScoring->clearanceViolationDepthWeight = DEFAULT_ROUTER_CLEARANCE_DEPTH_WEIGHT;
  settings->routerScoring->clearanceViolationDepthScale = DEFAULT_ROUTER_CLEARANCE_DEPTH_SCALE_UM;

  settings->optimizerScoring->version = DEFAULT_OPTIMIZER_SCORING_VERSION;
  settings->optimizerScoring->excessWireLengthWeight = DEFAULT_OPTIMIZER_EXCESS_LENGTH_WEIGHT;
  settings->optimizerScoring->excessViaWeight = DEFAULT_OPTIMIZER_EXCESS_VIA_WEIGHT;
  settings->optimizerScoring->excessBendWeight = DEFAULT_OPTIMIZER_EXCESS_BEND_WEIGHT;
  settings->optimizerScoring->lengthFloor = DEFAULT_OPTIMIZER_LENGTH_FLOOR;
  settings->optimizerScoring->difficultyScaleFloor = DEFAULT_OPTIMIZER_DIFFICULTY_SCALE_FLOOR;

  return settings;
}

}  // namespace fr
