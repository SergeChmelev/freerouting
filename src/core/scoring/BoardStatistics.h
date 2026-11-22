#pragma once

#include <optional>
#include <string>

#include "core/scoring/BoardStatisticsBends.h"
#include "core/scoring/BoardStatisticsBoard.h"
#include "core/scoring/BoardStatisticsBounds.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "core/scoring/BoardStatisticsComponents.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "core/scoring/BoardStatisticsDifficulty.h"
#include "core/scoring/BoardStatisticsItems.h"
#include "core/scoring/BoardStatisticsLayers.h"
#include "core/scoring/BoardStatisticsNets.h"
#include "core/scoring/BoardStatisticsPads.h"
#include "core/scoring/BoardStatisticsTraces.h"
#include "core/scoring/BoardStatisticsVias.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Statistics for surface-mount pin fanout. (Java: BoardStatistics.BoardStatisticsFanout) */
class BoardStatistics_BoardStatisticsFanout : public virtual jrt::Object {
 public:
  int32_t totalSmdPins = 0;
  int32_t pinsToEscape = 0;
  int32_t escapedCount = 0;
};

/**
 * Lightweight metrics for optimizer evaluation without building full BoardStatistics.
 * (Java record BoardStatistics.OptimizerMetrics)
 */
class BoardStatistics_OptimizerMetrics : public virtual jrt::Object {
 public:
  BoardStatistics_OptimizerMetrics(int32_t viaCount, float totalLength, float totalWeightedLength)
      : viaCount_(viaCount), totalLength_(totalLength), totalWeightedLength_(totalWeightedLength) {}

  int32_t viaCount() { return viaCount_; }
  float totalLength() { return totalLength_; }
  float totalWeightedLength() { return totalWeightedLength_; }

  static BoardStatistics_OptimizerMetrics* of(BasicBoard* board);

  bool equals(jrt::Object* o) override {
    auto* other = jrt::fast_cast<BoardStatistics_OptimizerMetrics>(o);
    if (other == nullptr) return false;
    return viaCount_ == other->viaCount_ &&
           jrt::Float::compare(totalLength_, other->totalLength_) == 0 &&
           jrt::Float::compare(totalWeightedLength_, other->totalWeightedLength_) == 0;
  }
  int32_t hashCode() override {
    int32_t h = jrt::Integer::hashCode(viaCount_);
    h = jrt::hashCombine(h, jrt::Float::hashCode(totalLength_));
    h = jrt::hashCombine(h, jrt::Float::hashCode(totalWeightedLength_));
    return h;
  }
  std::string toString() override {
    return "OptimizerMetrics[viaCount=" + jrt::str(viaCount_) +
           ", totalLength=" + jrt::str(totalLength_) +
           ", totalWeightedLength=" + jrt::str(totalWeightedLength_) + "]";
  }

 private:
  int32_t viaCount_;
  float totalLength_;
  float totalWeightedLength_;
};

/** Statistics of a board. */
// PORT NOTE: Serializable and gson @SerializedName annotations dropped.
class BoardStatistics : public virtual jrt::Object {
 public:
  std::optional<std::string> host;
  std::optional<std::string> unit;
  BoardStatisticsBoard* board = new (jrt::kOp) BoardStatisticsBoard();
  BoardStatisticsLayers* layers = new (jrt::kOp) BoardStatisticsLayers();
  BoardStatisticsItems* items = new (jrt::kOp) BoardStatisticsItems();
  BoardStatisticsComponents* components = new (jrt::kOp) BoardStatisticsComponents();
  BoardStatisticsPads* pads = new (jrt::kOp) BoardStatisticsPads();
  BoardStatisticsNets* nets = new (jrt::kOp) BoardStatisticsNets();
  BoardStatisticsConnections* connections = new (jrt::kOp) BoardStatisticsConnections();
  BoardStatisticsTraces* traces = new (jrt::kOp) BoardStatisticsTraces();
  BoardStatisticsBends* bends = new (jrt::kOp) BoardStatisticsBends();
  BoardStatisticsVias* vias = new (jrt::kOp) BoardStatisticsVias();
  BoardStatisticsClearanceViolations* clearanceViolations =
      new (jrt::kOp) BoardStatisticsClearanceViolations();
  BoardStatisticsDifficulty* difficulty = new (jrt::kOp) BoardStatisticsDifficulty();
  BoardStatisticsBounds* bounds = new (jrt::kOp) BoardStatisticsBounds();
  BoardStatistics_BoardStatisticsFanout* fanout = new (jrt::kOp) BoardStatistics_BoardStatisticsFanout();

  /** Creates empty board statistics. */
  BoardStatistics() {}

  /** Creates a new BoardFileStatistics object from a RoutingBoard object. */
  explicit BoardStatistics(BasicBoard* board);

  /** ... and defines the preferred unit for the statistics (nullopt = Java null = MM). */
  BoardStatistics(BasicBoard* board, std::optional<Unit> unit);

  /** ... and allows skipping the clearance checks. */
  BoardStatistics(BasicBoard* board, std::optional<Unit> unit, bool includeClearanceViolations);

  /**
   * Creates board statistics with optional clearance and connection (incomplete) analysis.
   * includeConnections false skips the incomplete-connection analysis.
   */
  BoardStatistics(BasicBoard* board, std::optional<Unit> unit, bool includeClearanceViolations,
                  bool includeConnections);

  /**
   * Creates a new BoardFileStatistics object from a file (SES / DSN only).
   * PORT NOTE: the KICAD_DESIGN_JSON branch (gson) is dropped - KiCad input treated as absent.
   */
  BoardStatistics(jrt::Array<int8_t>* data, std::optional<io::FileFormat> format);

 private:
  /** Same as pin.getUnconnectedSet(netNumber).isEmpty() for netNumber > 0, with caches. */
  static bool isConnectedToWholeNet(BasicBoard* board, Pin* pin, int32_t netNumber,
                                    jrt::Map<Item*, jrt::Collection<Item*>*>* contacts,
                                    jrt::Map<int32_t, jrt::Collection<Item*>*>* connectableItems);

 public:
  /** Returns whether a pin has a valid trace or via escape. */
  static bool isPinEscaped(Pin* pin);

 private:
  static double shortfallUm(ClearanceViolation* violation, double boardUnitToUmFactor);

  static int32_t countOccurrences(const std::string& text, const std::string& target);

 public:
  /**
   * Returns a JSON representation of this object.
   * PORT NOTE: Java uses gson; phase 2 must hand-write the JSON (or a plain summary).
   */
  std::string toString() override;

  /** Calculates the score/cost of the board. Higher score means better board. */
  virtual float calculateScore(RoutingCostSettings* scoringSettings);

  /** Returns the maximum score for the supplied scoring settings. */
  virtual float getMaximumScore(RoutingCostSettings* scoringSettings);

 private:
  /** Returns the legacy score normalized to a range from zero to one thousand. */
  float getLegacyNormalizedScore(RoutingCostSettings* scoringSettings);

 public:
  /** Returns the router score normalized to a range from zero to one thousand. */
  virtual float getRouterScore(RoutingCostSettings* scoringSettings);

  /** Returns the configured router score normalized to a range from zero to one thousand. */
  virtual float getRouterScore(RouterSettings* routerSettings);

 private:
  float getV2RouterScore(RouterScoreSettings* settings);

 public:
  /** Ensures that difficulty metrics (pin count, signal layer count, C and D) are computed. */
  virtual void ensureDifficulty();

 private:
  static RoutingCostSettings* legacyScoringOrDefault(RouterSettings* routerSettings);

  static float valueOrDefault(std::optional<float> value, float defaultValue) {
    return value.has_value() ? *value : defaultValue;
  }

 public:
  /** Returns the legacy optimizer score normalized to a range from zero to one thousand. */
  virtual float getOptimizerScore(RoutingCostSettings* scoringSettings);

  /** Returns the configured optimizer score normalized to a range from zero to one thousand. */
  virtual float getOptimizerScore(RouterSettings* routerSettings);

  /** Returns the configured optimizer score without its floor at zero (used for ranking). */
  virtual float getOptimizerRank(RouterSettings* routerSettings);

 private:
  /** Calculates the V2 optimizer score from board-only lower bounds and actual route metrics. */
  float getV2OptimizerScore(OptimizerScoreSettings* settings);

  /** 1,000 less the V2 optimizer score's penalties; below zero when they outweigh it. */
  double v2OptimizerScoreUnbounded(OptimizerScoreSettings* settings);

 public:
  /** @deprecated Use getRouterScore(RoutingCostSettings*). */
  virtual float getNormalizedScore(RoutingCostSettings* scoringSettings);
};

}  // namespace fr
