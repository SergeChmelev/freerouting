#include "core/facade/FreeroutingHeadlessEngine.h"

#include <sstream>

#include "autoroute/events/BoardUpdatedEvent.h"
#include "autoroute/events/BoardUpdatedEventListener.h"
#include "autoroute/events/TaskStateChangedEvent.h"
#include "autoroute/events/TaskStateChangedEventListener.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/BatchOptimizer.h"
#include "autoroute/pipeline/RoutingPipeline.h"
#include "autoroute/pipeline/TaskState.h"
#include "board/facade/RoutingBoard.h"
#include "core/RoutingJob.h"
#include "core/RoutingJobState.h"
#include "core/RoutingStage.h"
#include "core/facade/CancellationToken.h"
#include "core/facade/HeadlessRoutingProgress.h"
#include "core/facade/HeadlessRoutingProgressListener.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "core/facade/HeadlessStoppableThread.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsConnections.h"
#include "drc/ClearanceBaseline.h"
#include "drc/ClearanceViolation.h"
#include "drc/DesignRulesChecker.h"
#include "io/BoardReadResult.h"
#include "io/specctra/DsnReader.h"
#include "io/specctra/RulesReader.h"
#include "io/specctra/SesWriter.h"
#include "logger/FRLogger.h"
#include "rules/DifferentialPairDetector.h"
#include "settings/RouterSettings.h"
#include "settings/SettingsMerger.h"
#include "settings/sources/ApiSettings.h"
#include "settings/sources/DefaultSettings.h"
#include "settings/sources/DsnFileSettings.h"
#include "settings/sources/RulesFileSettings.h"

namespace fr {

namespace {

// std::istringstream over a byte array (Java: new ByteArrayInputStream(bytes)).
std::string bytesToString(jrt::Array<int8_t>* bytes) {
  return std::string(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::size_t>(bytes->length));
}

// Java `(RoutingBoard) basicBoard`.
RoutingBoard* toRoutingBoard(BasicBoard* board) {
  if (board == nullptr) {
    return nullptr;
  }
  auto* result = jrt::fast_cast<RoutingBoard>(board);
  if (result == nullptr) {
    throw jrt::ClassCastException("BasicBoard cannot be cast to RoutingBoard");
  }
  return result;
}

// Java auto-unboxing of `stats.connections.incompleteCount` (Integer -> int).
int32_t unboxIncompleteCount(BoardStatistics* stats) {
  if (!stats->connections->incompleteCount.has_value()) {
    throw jrt::NullPointerException("incompleteCount");
  }
  return *stats->connections->incompleteCount;
}

}  // namespace

HeadlessRoutingResult* FreeroutingHeadlessEngine::route(HeadlessRoutingRequest* request) {
  if (request == nullptr) {
    throw jrt::IllegalArgumentException("request must not be null");
  }

  int64_t startTime = jrt::System::currentTimeMillis();

  if (request->cancellationToken()->isCancelled()) {
    return HeadlessRoutingResult::cancelled(nullptr, nullptr,
                                            jrt::System::currentTimeMillis() - startTime);
  }

  // 1. Parse DSN into RoutingBoard (purely in-memory, no temporary files)
  jrt::Array<int8_t>* dsnBytes = request->dsnBytes();
  io::BoardReadResult* readResult =
      dsn::DsnReader::readBoard(dsnBytes, nullptr, nullptr, request->designName());

  RoutingBoard* board;
  if (auto* s = jrt::fast_cast<io::BoardReadResult_Success>(readResult)) {
    board = toRoutingBoard(s->board());
  } else if (auto* o = jrt::fast_cast<io::BoardReadResult_OutlineMissing>(readResult)) {
    board = toRoutingBoard(o->board());
  } else if (auto* p = jrt::fast_cast<io::BoardReadResult_ParseError>(readResult)) {
    return HeadlessRoutingResult::failure(
        "DSN parse error at '" + p->location() + "': " + p->detail(),
        jrt::System::currentTimeMillis() - startTime);
  } else if (auto* io = jrt::fast_cast<io::BoardReadResult_IoError>(readResult)) {
    return HeadlessRoutingResult::failure("DSN IO error: " + io->cause().getMessage(),
                                          jrt::System::currentTimeMillis() - startTime);
  } else {
    return HeadlessRoutingResult::failure("Unknown DSN board read result",
                                          jrt::System::currentTimeMillis() - startTime);
  }

  if (board == nullptr) {
    return HeadlessRoutingResult::failure("DSN did not contain a valid board",
                                          jrt::System::currentTimeMillis() - startTime);
  }

  if (request->cancellationToken()->isCancelled()) {
    return HeadlessRoutingResult::cancelled(board,
                                            new BoardStatistics(board, std::nullopt, false),
                                            jrt::System::currentTimeMillis() - startTime);
  }

  // 2. Resolve settings using SettingsMerger priority ladder and apply .rules to the board
  SettingsMerger* settingsMerger;
  {
    std::istringstream dsnStream(bytesToString(dsnBytes));
    settingsMerger = new SettingsMerger(
        {new DefaultSettings(), new DsnFileSettings(dsnStream, request->designName())});
  }

  jrt::Array<int8_t>* rulesBytes = request->rulesBytes();
  if (rulesBytes != nullptr && rulesBytes->length > 0) {
    std::string rulesName =
        request->rulesName().has_value() ? *request->rulesName() : std::string("rules.rules");
    bool rulesApplied;
    {
      std::istringstream rulesStream(bytesToString(rulesBytes));
      rulesApplied = dsn::RulesReader::read(rulesStream, rulesName, board);
    }
    if (!rulesApplied) {
      return HeadlessRoutingResult::failure(
          "Failed to apply rules file '" + rulesName + "' to board",
          jrt::System::currentTimeMillis() - startTime);
    }
    std::istringstream rulesStream(bytesToString(rulesBytes));
    settingsMerger->addOrReplaceSources(new RulesFileSettings(rulesStream, rulesName));
  }

  if (request->settings() != nullptr) {
    settingsMerger->addOrReplaceSources(new ApiSettings(request->settings()));
  }

  RouterSettings* routerSettings = settingsMerger->merge();
  int32_t boardLayerCount = board->getLayerCount();
  if (routerSettings->getLayerCount() != boardLayerCount) {
    routerSettings->setLayerCount(boardLayerCount);
  }
  routerSettings->applyBoardSpecificOptimizations(board);
  routerSettings->applyNetClassExclusions(board);
  applyBoardSettingsOverrides(board, routerSettings);
  prepareBoardConnectivity(board, routerSettings);
  captureInitialClearanceBaseline(board);

  // 3. Prepare RoutingJob
  auto* job = new RoutingJob();
  job->board = board;
  job->routerSettings = routerSettings;
  job->name = request->designName();
  std::optional<int64_t> deadlineMillis =
      parseDeadlineMillis(startTime, routerSettings->jobTimeoutString);
  auto* thread = new HeadlessStoppableThread(request->cancellationToken(), deadlineMillis);
  job->thread = thread;
  job->state = RoutingJobState::RUNNING;

  // 4. Configure pipeline and listeners
  RoutingPipeline* pipeline = RoutingPipeline::createForHeadless(job);

  if (request->progressListener() != nullptr) {
    pipeline->addTaskStateChangedEventListener(
        TaskStateChangedEventListener::of([request, job](TaskStateChangedEvent* event) {
          notifyProgress(request->progressListener(),
                         new HeadlessRoutingProgress(
                             job->stage, event->getPassNumber(), -1, nullptr,
                             "Task state: " + TaskState_toString(event->getTaskState())));
        }));

    pipeline->addBoardUpdatedEventListener(
        BoardUpdatedEventListener::of([request, job](BoardUpdatedEvent* event) {
          BoardStatistics* stats = event->getBoardStatistics();
          int32_t unrouted = stats != nullptr && stats->connections != nullptr
                                 ? unboxIncompleteCount(stats)
                                 : 0;
          notifyProgress(request->progressListener(),
                         new HeadlessRoutingProgress(job->stage, job->getCurrentPass(), unrouted,
                                                     stats, "Board updated"));
        }));

    pipeline->addStageListener(RoutingPipeline_StageListener::of(
        [request, job](BatchAutorouter* autorouter) {
          BoardStatistics* stats = job->board->getStatistics();
          notifyProgress(request->progressListener(),
                         new HeadlessRoutingProgress(
                             RoutingStage::ROUTING, job->getCurrentPass(),
                             stats->connections != nullptr ? unboxIncompleteCount(stats) : 0,
                             stats, "Routing stage finished"));
        },
        [request, job](BatchOptimizer* optimizer) {
          BoardStatistics* stats = job->board->getStatistics();
          int32_t unrouted = stats != nullptr && stats->connections != nullptr
                                 ? unboxIncompleteCount(stats)
                                 : 0;
          notifyProgress(request->progressListener(),
                         new HeadlessRoutingProgress(RoutingStage::OPTIMIZATION, 0, unrouted,
                                                     stats, "Optimization stage started"));
        },
        [request, job](BatchOptimizer* optimizer) {
          BoardStatistics* stats = job->board->getStatistics();
          int32_t unrouted = stats != nullptr && stats->connections != nullptr
                                 ? unboxIncompleteCount(stats)
                                 : 0;
          notifyProgress(request->progressListener(),
                         new HeadlessRoutingProgress(RoutingStage::OPTIMIZATION, 0, unrouted,
                                                     stats, "Optimization stage finished"));
        }));
  }

  // 5. Execute routing pipeline
  try {
    pipeline->run();
  } catch (const jrt::Exception& e) {
    FRLogger::error("Error during in-memory routing pipeline execution", &e);
    return HeadlessRoutingResult::failure(
        "Routing pipeline execution failed: " + e.getMessage(),
        jrt::System::currentTimeMillis() - startTime);
  }

  // Refresh board reference in case the autorouter or optimizer installed a snapshot/candidate
  board = job->board;

  bool cancelled = request->cancellationToken()->isCancelled();
  bool timedOut = !cancelled && thread->isTimedOut();
  if (cancelled) {
    job->state = RoutingJobState::CANCELLED;
  } else if (timedOut) {
    job->state = RoutingJobState::TIMED_OUT;
  } else {
    job->state = RoutingJobState::COMPLETED;
  }

  // 6. Gather results & statistics without duplicating the clearance check
  auto* stats = new BoardStatistics(board, std::nullopt, false);
  int32_t unrouted = stats->connections != nullptr ? unboxIncompleteCount(stats) : 0;

  std::optional<std::string> errorMessage;
  if (cancelled) {
    errorMessage = "Routing was cancelled";
  } else if (timedOut) {
    errorMessage = "Routing timed out";
  }

  // 7. Perform authoritative DRC once if requested
  jrt::Collection<ClearanceViolation*>* violations = new jrt::ArrayList<ClearanceViolation*>();
  if (request->runDrc() && !cancelled) {
    try {
      violations = (new DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
    } catch (const jrt::Exception& e) {
      FRLogger::warn("DRC check encountered an error: " + e.getMessage());
      if (!errorMessage.has_value()) {
        errorMessage = "DRC check failed: " + e.getMessage();
      }
    }
  }

  // 8. Serialize to SES if requested
  jrt::Array<int8_t>* sesBytes = new jrt::Array<int8_t>(0);
  if (request->computeSes() && !cancelled) {
    try {
      std::ostringstream baos;
      dsn::SesWriter::write(board, baos, request->designName());
      std::string out = baos.str();
      sesBytes = new jrt::Array<int8_t>(static_cast<int32_t>(out.size()));
      for (std::size_t i = 0; i < out.size(); i++) {
        (*sesBytes)[static_cast<int32_t>(i)] = static_cast<int8_t>(out[i]);
      }
    } catch (const jrt::Exception& e) {
      FRLogger::error("Failed to serialize SES output", &e);
      if (!errorMessage.has_value()) {
        errorMessage = "SES serialization failed: " + e.getMessage();
      }
    }
  }

  int64_t executionTimeMillis = jrt::System::currentTimeMillis() - startTime;
  bool completed =
      !cancelled && !errorMessage.has_value() && unrouted == 0 && violations->isEmpty();

  return new HeadlessRoutingResult(completed, cancelled, unrouted, violations, stats, sesBytes,
                                   executionTimeMillis, errorMessage, board);
}

HeadlessRoutingResult* FreeroutingHeadlessEngine::route(jrt::Array<int8_t>* dsnBytes) {
  return route(HeadlessRoutingRequest::of(dsnBytes));
}

HeadlessRoutingResult* FreeroutingHeadlessEngine::route(const std::string& dsnContent) {
  return route(HeadlessRoutingRequest::of(dsnContent));
}

HeadlessRoutingResult* FreeroutingHeadlessEngine::route(std::istream& dsnStream) {
  return route(HeadlessRoutingRequest::of(dsnStream));
}

void FreeroutingHeadlessEngine::notifyProgress(HeadlessRoutingProgressListener* listener,
                                               HeadlessRoutingProgress* progress) {
  if (listener != nullptr) {
    try {
      listener->onProgress(progress);
    } catch (const jrt::RuntimeException& e) {
      FRLogger::warn("Progress listener threw exception: " + e.getMessage());
    }
  }
}

jrt::Collection<ClearanceViolation*>* FreeroutingHeadlessEngine::checkDrc(RoutingBoard* board) {
  if (board == nullptr) {
    return new jrt::ArrayList<ClearanceViolation*>();
  }
  return (new DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
}

jrt::Collection<ClearanceViolation*>* FreeroutingHeadlessEngine::checkDrc(
    jrt::Array<int8_t>* dsnBytes) {
  if (dsnBytes == nullptr || dsnBytes->length == 0) {
    throw jrt::IllegalArgumentException("dsnBytes must not be null or empty");
  }
  io::BoardReadResult* result =
      dsn::DsnReader::readBoard(dsnBytes, nullptr, nullptr, std::string("drc_check.dsn"));
  if (auto* s = jrt::fast_cast<io::BoardReadResult_Success>(result)) {
    return checkDrc(toRoutingBoard(s->board()));
  } else if (auto* o = jrt::fast_cast<io::BoardReadResult_OutlineMissing>(result)) {
    return checkDrc(toRoutingBoard(o->board()));
  } else if (auto* p = jrt::fast_cast<io::BoardReadResult_ParseError>(result)) {
    throw jrt::IllegalArgumentException("DSN parse error at '" + p->location() +
                                        "': " + p->detail());
  } else if (auto* io = jrt::fast_cast<io::BoardReadResult_IoError>(result)) {
    // PORT NOTE: UncheckedIOException -> jrt::IOException.
    throw jrt::IOException("DSN IO error: " + io->cause().getMessage());
  }
  throw jrt::IllegalArgumentException("Unknown DSN board read result");
}

void FreeroutingHeadlessEngine::applyBoardSettingsOverrides(RoutingBoard* board,
                                                            RouterSettings* routerSettings) {
  if (board == nullptr || routerSettings == nullptr) {
    return;
  }
  if (routerSettings->planeAsObstacle.has_value()) {
    board->changePlaneAsObstacle(*routerSettings->planeAsObstacle);
  }
}

void FreeroutingHeadlessEngine::prepareBoardConnectivity(RoutingBoard* board,
                                                         RouterSettings* routerSettings) {
  if (board == nullptr) {
    return;
  }
  board->expandBoundingBoxToIncludeAllItems();
  board->reduceNetsOfRouteItems();
  bool fixKicadFootprints = routerSettings != nullptr &&
                            routerSettings->fixKicadFootprints.has_value() &&
                            *routerSettings->fixKicadFootprints;
  board->connectNetlessPads(false, fixKicadFootprints, new jrt::Array<int32_t>(1));
  board->clusterNetTies();
  DifferentialPairDetector::detectAndRegister(board->rules);
}

void FreeroutingHeadlessEngine::captureInitialClearanceBaseline(RoutingBoard* board) {
  if (board == nullptr) {
    return;
  }
  jrt::Collection<ClearanceViolation*>* initialViolations =
      (new DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
  board->preExistingClearanceViolationsCount = initialViolations->size();
  int32_t unfixable = 0;
  for (ClearanceViolation* v : *initialViolations) {
    if (v->isUnfixable()) {
      unfixable++;
    }
  }
  board->unfixableClearanceViolationsCount = unfixable;
  board->clearanceBaseline = new ClearanceBaseline(initialViolations);
}

std::optional<int64_t> FreeroutingHeadlessEngine::parseDeadlineMillis(
    int64_t startTimeMillis, const std::optional<std::string>& timeoutString) {
  if (!timeoutString.has_value() || jrt::strip(*timeoutString).empty()) {
    return std::nullopt;
  }
  std::vector<std::string> parts = jrt::split(jrt::trim(*timeoutString), ':');
  if (parts.size() != 3) {
    return std::nullopt;
  }
  try {
    int64_t hours = jrt::Long::parseLong(parts[0]);
    int64_t minutes = jrt::Long::parseLong(parts[1]);
    int64_t seconds = jrt::Long::parseLong(parts[2]);
    int64_t totalMillis = ((hours * 3600LL) + (minutes * 60LL) + seconds) * 1000LL;
    return totalMillis > 0 ? std::optional<int64_t>(startTimeMillis + totalMillis)
                           : std::nullopt;
  } catch (const jrt::NumberFormatException& e) {
    return std::nullopt;
  }
}

}  // namespace fr
