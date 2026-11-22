#include "autoroute/pipeline/RoutingFallback.h"

#include "autoroute/DesignBlocks.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "board/facade/RoutingBoard.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "drc/AirLine.h"
#include "drc/ClearanceBaseline.h"
#include "drc/ClearanceViolation.h"
#include "drc/DesignRulesChecker.h"
#include "settings/AutorouterSettings.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

// ---- RoutingFallback_Outcome ----

int32_t RoutingFallback_Outcome::newOrChangedCount(int32_t violationCount, ClearanceBaseline* baseline,
                                                   jrt::Collection<ClearanceViolation*>* violations) {
  if (violationCount == 0) {
    return 0;
  }
  if (baseline == nullptr) {
    return violationCount;
  }
  return baseline->countNewOrChanged(violations);
}

RoutingFallback_Outcome* RoutingFallback_Outcome::of(const std::string& strategy, RoutingBoard* board,
                                                     RouterSettings* scoring) {
  return of(strategy, board, scoring, nullptr);
}

RoutingFallback_Outcome* RoutingFallback_Outcome::of(const std::string& strategy, RoutingBoard* board,
                                                     RouterSettings* scoring, ClearanceBaseline* incumbent) {
  BoardStatistics* statistics = new BoardStatistics(board);
  int32_t incomplete =
      statistics->connections->incompleteCount.has_value() ? *statistics->connections->incompleteCount : 0;
  int32_t blocked = 0;
  if (incomplete > 0) {
    jrt::Array<AirLine*>* airlines = (new DesignRulesChecker(board, nullptr))->getAllAirlines();
    blocked = DesignBlocks::blockedAirlines(
                  new jrt::ArrayList<AirLine*>(std::vector<AirLine*>(airlines->begin(), airlines->end())))
                  ->size();
    statistics->connections->designBlockedCount = blocked > 0 ? std::optional<int32_t>(blocked) : std::nullopt;
  }
  int32_t violationCount = statistics->clearanceViolations->totalCount.has_value()
                               ? *statistics->clearanceViolations->totalCount
                               : 0;
  int32_t changed = newOrChangedCount(violationCount, exemptionBaseline(incumbent, board->clearanceBaseline),
                                      RoutingFallback::violationsOf(board));
  statistics->clearanceViolations->newOrChangedCount = changed;
  return new RoutingFallback_Outcome(strategy, incomplete, changed, violationCount,
                                     statistics->getRouterScore(scoring), blocked);
}

bool RoutingFallback_Outcome::canReplace(RoutingFallback_Outcome* candidate, RoutingFallback_Outcome* incumbent,
                                         int32_t changedSinceIncumbent) {
  return changedSinceIncumbent == 0 && candidate->newOrChanged <= incumbent->newOrChanged &&
         compare(candidate, incumbent) < 0;
}

int32_t RoutingFallback_Outcome::compare(RoutingFallback_Outcome* a, RoutingFallback_Outcome* b) {
  int32_t order = jrt::Integer::compare(a->unrouted, b->unrouted);
  if (order == 0) {
    order = jrt::Integer::compare(a->newOrChanged, b->newOrChanged);
  }
  if (order == 0) {
    order = jrt::Integer::compare(a->violations, b->violations);
  }
  if (order == 0) {
    order = jrt::Float::compare(b->score, a->score);
  }
  return order;
}

std::string RoutingFallback_Outcome::toString() {
  std::string blocked =
      this->designBlocked > 0 ? " (" + jrt::str(this->designBlocked) + " blocked by the design)" : "";
  return jrt::str(this->unrouted) + " unrouted" + blocked + ", " + jrt::str(this->newOrChanged) +
         " new violation" + (this->newOrChanged == 1 ? "" : "s") + " (" + jrt::str(this->violations) +
         " total), score " + jrt::format("%.2f", static_cast<double>(this->score));
}

// ---- RoutingFallback ----

jrt::List<RoutingFallback_Strategy*>* RoutingFallback::KNOWN() {
  static jrt::List<RoutingFallback_Strategy*>* v = new jrt::ArrayList<RoutingFallback_Strategy*>({
      new RoutingFallback_Strategy(
          "no-neck", false, [](RouterSettings* settings) { settings->setNeckWidthUm(0); }, false, false),
      new RoutingFallback_Strategy(
          "continue-auto-neck", false, [](RouterSettings* settings) { settings->setNeckWidthUm(150); }, false,
          false),
      new RoutingFallback_Strategy(
          "continue-fine-neck", false,
          [](RouterSettings* settings) {
            settings->setNeckWidthUm(jrt::Math::min(settings->getNeckWidthUm(), static_cast<double>(100)));
          },
          true, false),
      new RoutingFallback_Strategy(
          "fanout-retry", true, [](RouterSettings* settings) { settings->fanout->retryWithoutEscapeWindow = true; },
          false, true),
      new RoutingFallback_Strategy(
          "short-escape", true, [](RouterSettings* settings) { settings->fanout->minEscapeLengthMm = 0.0; }, false,
          true),
      new RoutingFallback_Strategy(
          "open-escape", true,
          [](RouterSettings* settings) {
            settings->fanout->minEscapeLengthMm = 0.0;
            settings->fanout->maxEscapeLengthMm = 1000.0;
            settings->autorouter->smdNetViaCostFactor = 1.0;
          },
          false, false),
      new RoutingFallback_Strategy(
          "direct-first", true, [](RouterSettings* settings) { settings->fanout->strategy = "direct_first"; }, false,
          false),
      new RoutingFallback_Strategy(
          "no-fanout", true, [](RouterSettings* settings) { settings->setFanoutEnabled(false); }, false, false),
      new RoutingFallback_Strategy("continue", false, [](RouterSettings*) {}, false, false),
      new RoutingFallback_Strategy(
          "continue-no-neck", false, [](RouterSettings* settings) { settings->setNeckWidthUm(0); }, false, false),
  });
  return v;
}

jrt::List<RoutingFallback_Strategy*>* RoutingFallback::applicable(jrt::List<RoutingFallback_Strategy*>* strategies,
                                                                  RouterSettings* settings, bool fanoutRuns) {
  bool necking = settings->getNeckWidthUm() > 0;
  jrt::List<RoutingFallback_Strategy*>* result = new jrt::ArrayList<RoutingFallback_Strategy*>();
  for (RoutingFallback_Strategy* strategy : *strategies) {
    if (strategy->needsFanout && !fanoutRuns) {
      continue;
    }
    if (std::string("no-neck") == strategy->name && !necking) {
      continue;
    }
    if (std::string("continue-fine-neck") == strategy->name && settings->getNeckWidthUm() <= 100) {
      continue;
    }
    if (std::string("continue-no-neck") == strategy->name && !necking) {
      continue;
    }
    if (std::string("continue-auto-neck") == strategy->name && necking) {
      continue;
    }
    result->add(strategy);
  }
  return result;
}

jrt::List<RoutingFallback_Strategy*>* RoutingFallback::parse(RouterSettings* settings,
                                                             std::function<void(const std::string&)> unknown) {
  jrt::List<RoutingFallback_Strategy*>* result = new jrt::ArrayList<RoutingFallback_Strategy*>();
  std::optional<std::string> spec =
      settings->autorouter != nullptr ? settings->autorouter->fallbackStrategies : std::nullopt;
  // Java isBlank(): empty or only whitespace -> strip() is empty
  if (!spec.has_value() || jrt::strip(*spec).empty()) {
    return result;
  }
  // PORT NOTE: Java String.split(",") drops trailing empty strings; empty names are skipped below
  // anyway, so jrt::split gives the same result.
  for (const std::string& raw : jrt::split(*spec, ',')) {
    std::string name = jrt::trim(raw);
    if (name.empty()) {
      continue;
    }
    if (jrt::equalsIgnoreCase("none", name)) {
      return new jrt::ArrayList<RoutingFallback_Strategy*>();
    }
    RoutingFallback_Strategy* known = find(name);
    if (known == nullptr) {
      if (unknown) {
        unknown(name);
      }
    } else if (!result->contains(known)) {
      result->add(known);
    }
  }
  return result;
}

RoutingFallback_Strategy* RoutingFallback::find(const std::string& name) {
  for (RoutingFallback_Strategy* strategy : *KNOWN()) {
    if (jrt::equalsIgnoreCase(strategy->name, name)) {
      return strategy;
    }
  }
  return nullptr;
}

bool RoutingFallback::isRetryable(std::optional<BatchAutorouter_StopReason> reason) {
  return reason == BatchAutorouter_StopReason::STAGNATION || reason == BatchAutorouter_StopReason::NO_IMPROVEMENT ||
         reason == BatchAutorouter_StopReason::COMPLETED;
}

std::string RoutingFallback::describe(std::optional<BatchAutorouter_StopReason> reason) {
  if (!reason.has_value()) {
    return "an unknown stop";
  }
  switch (*reason) {
    case BatchAutorouter_StopReason::COMPLETED: return "completed";
    case BatchAutorouter_StopReason::MAX_PASSES: return "pass limit";
    case BatchAutorouter_StopReason::STAGNATION: return "stagnation";
    case BatchAutorouter_StopReason::NO_IMPROVEMENT: return "no improvement";
    case BatchAutorouter_StopReason::TIMED_OUT: return "timeout";
    case BatchAutorouter_StopReason::CANCELLED: return "cancelled";
    case BatchAutorouter_StopReason::REPEATED: return "a repeat of an earlier pass loop";
  }
  return "";
}

ClearanceBaseline* RoutingFallback::captureClearance(RoutingBoard* board, int32_t violations) {
  if (violations == 0) {
    return nullptr;
  }
  return new ClearanceBaseline(violationsOf(board));
}

int32_t RoutingFallback::newOrChangedClearance(RoutingBoard* candidate, int32_t violations,
                                               ClearanceBaseline* incumbent) {
  if (violations == 0) {
    return 0;
  }
  if (incumbent == nullptr) {
    return violations;
  }
  return incumbent->countNewOrChanged(violationsOf(candidate));
}

jrt::Collection<ClearanceViolation*>* RoutingFallback::violationsOf(RoutingBoard* board) {
  return (new DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
}

}  // namespace fr
