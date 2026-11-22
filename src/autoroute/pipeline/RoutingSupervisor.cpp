#include "autoroute/pipeline/RoutingSupervisor.h"

#include "autoroute/pipeline/RoutingFallback.h"
#include "board/facade/BasicBoard.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "rules/BoardRules.h"
#include "settings/AutorouterSettings.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

bool RoutingSupervisor::explores(RouterSettings* settings) {
  std::optional<int32_t> patience =
      settings->autorouter != nullptr ? settings->autorouter->explorePatience : std::nullopt;
  return patience.has_value() && *patience > 0;
}

bool RoutingSupervisor::explorationExhausted(RouterSettings* settings) {
  int32_t patience = settings->autorouter != nullptr && settings->autorouter->explorePatience.has_value()
                         ? *settings->autorouter->explorePatience
                         : 0;
  return this->attemptsWithoutGain >= patience;
}

int32_t RoutingSupervisor::viaFactor(int32_t attempt) {
  int32_t clamped = jrt::Math::min(jrt::Math::max(attempt, 1), 3);
  return 1 << clamped;
}

std::optional<std::string> RoutingSupervisor::explorationFallback(RouterSettings* jobSettings) {
  std::optional<std::string> keep =
      jobSettings->autorouter != nullptr ? jobSettings->autorouter->exploreFallbackStrategies : std::nullopt;
  if (!keep.has_value() || jrt::strip(*keep).empty()) {
    return jobSettings->autorouter != nullptr ? jobSettings->autorouter->fallbackStrategies : std::nullopt;
  }
  jrt::List<std::string>* names = new jrt::ArrayList<std::string>();
  for (const std::string& raw : jrt::split(*keep, ',')) {
    std::string name = jrt::trim(raw);
    if (!name.empty()) {
      names->add(name);
    }
  }
  std::vector<std::string> kept;
  for (RoutingFallback_Strategy* strategy : *RoutingFallback::parse(jobSettings, nullptr)) {
    for (const std::string& name : *names) {
      if (jrt::equalsIgnoreCase(strategy->name, name)) {
        kept.push_back(strategy->name);
        break;
      }
    }
  }
  return kept.empty() ? std::string("none") : jrt::join(",", kept);
}

RoutingSupervisor_Attempt* RoutingSupervisor::nextAttempt(int32_t round, RouterSettings* jobSettings,
                                                          BasicBoard* board) {
  jrt::List<std::string>* applicable = applicableKinds(jobSettings, board);
  // PORT NOTE: Java `String choice = null`; it is always assigned (applicable is never empty).
  std::optional<std::string> choice;
  for (const std::string& kind : *applicable) {
    if (!this->attemptStats->containsKey(kind)) {
      choice = kind;
      break;
    }
  }
  if (!choice.has_value()) {
    int32_t total = 0;
    for (RoutingSupervisor_Stats* stats : *this->attemptStats->values()) {
      total += stats->tries_;
    }
    int32_t totalTries = jrt::Math::max(2, total);
    double bestScore = jrt::Double::NEGATIVE_INFINITY;
    for (const std::string& kind : *applicable) {
      RoutingSupervisor_Stats* stats = this->attemptStats->get(kind);
      double score = (stats->closed_ + 0.5) / stats->tries_ +
                     jrt::Math::sqrt(2 * jrt::Math::log(totalTries) / stats->tries_);
      if (score > bestScore) {
        bestScore = score;
        choice = kind;
      }
    }
  }
  std::string choiceStr = choice.value_or("null");
  int32_t attempt = this->attemptStats->getOrDefault(choiceStr, new RoutingSupervisor_Stats(0, 0))->tries_ + 1;
  std::string name =
      choiceStr + " (seed " + jrt::str(round) + (attempt > 1 ? ", try " + jrt::str(attempt) : std::string("")) + ")";
  return new RoutingSupervisor_Attempt(choiceStr, name, round, attempt);
}

void RoutingSupervisor::apply(RoutingSupervisor_Attempt* attempt, RouterSettings* jobSettings,
                              RouterSettings* settings) {
  if (settings->autorouter != nullptr) {
    settings->autorouter->itemOrderSeed = attempt->seed();
    settings->autorouter->explorePatience = 0;
    settings->autorouter->fallbackStrategies = explorationFallback(jobSettings);
  }
  std::string kind = attempt->kind();
  if (kind == "seed") {
    if (settings->fanout != nullptr) {
      bool fanout = attempt->seed() % 2 == 1 && jobSettings->isFanoutEnabled();
      settings->fanout->enabled = fanout;
    }
  } else if (kind == "neighbourhood") {
    // The rip is applied to the board copy by the caller, using the try number.
  } else if (kind == "neck-100um") {
    settings->setNeckWidthUm(100);
  } else if (kind == "vias-cheaper") {
    settings->setViaCosts(jrt::Math::max(1, jobSettings->getViaCosts() / viaFactor(attempt->tryNumber())));
  } else if (kind == "vias-dearer") {
    settings->setViaCosts(jobSettings->getViaCosts() * viaFactor(attempt->tryNumber()));
  } else {
    // retry-plan keeps the job settings plus the item-order seed.
  }
}

void RoutingSupervisor::attemptDone(RoutingSupervisor_Attempt* attempt, int32_t openBefore, int32_t openAfter,
                                    bool kept) {
  RoutingSupervisor_Stats* stats = this->attemptStats->getOrDefault(attempt->kind(), new RoutingSupervisor_Stats(0, 0));
  int32_t gain = kept ? jrt::Math::max(0, openBefore - openAfter) : 0;
  this->attemptStats->put(attempt->kind(), new RoutingSupervisor_Stats(stats->tries_ + 1, stats->closed_ + gain));
  this->attemptsWithoutGain = gain > 0 ? 0 : this->attemptsWithoutGain + 1;
}

bool RoutingSupervisor::residual(RouterSettings* settings, int32_t open) {
  int32_t maxOpen = settings->autorouter != nullptr && settings->autorouter->residualRepairMaxOpen.has_value()
                        ? *settings->autorouter->residualRepairMaxOpen
                        : 0;
  return open > 0 && open <= maxOpen;
}

jrt::List<std::string>* RoutingSupervisor::applicableKinds(RouterSettings* jobSettings, BasicBoard* board) {
  jrt::List<std::string>* kinds = new jrt::ArrayList<std::string>();
  kinds->add("seed");
  kinds->add("retry-plan");
  kinds->add("neighbourhood");
  if (neckApplies(jobSettings, board)) {
    kinds->add("neck-100um");
  }
  kinds->add("vias-cheaper");
  kinds->add("vias-dearer");
  return kinds;
}

bool RoutingSupervisor::neckApplies(RouterSettings* jobSettings, BasicBoard* board) {
  if (jobSettings->getNeckWidthUm() <= 100 || board == nullptr) {
    return false;
  }
  double halfUm = board->rules->getMinAllowedTraceHalfWidth();
  double resolution = board->communication->getResolution(Unit::UM);
  return halfUm <= 50.0 * resolution;
}

std::string RoutingSupervisor::toString() {
  return jrt::format("attemptsWithoutGain=%d", this->attemptsWithoutGain);
}

}  // namespace fr
