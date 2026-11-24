#include "core/facade/FreeroutingHeadlessEngine.h"

#include <sstream>
#include <unordered_map>
#include <vector>

#include "autoroute/events/BoardUpdatedEvent.h"
#include "autoroute/events/BoardUpdatedEventListener.h"
#include "autoroute/events/TaskStateChangedEvent.h"
#include "autoroute/events/TaskStateChangedEventListener.h"
#include "autoroute/pipeline/BatchAutorouter.h"
#include "autoroute/pipeline/BatchOptimizer.h"
#include "autoroute/pipeline/RoutingPipeline.h"
#include "autoroute/pipeline/TaskState.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/PadPlaneConnection.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
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
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Point.h"
#include "io/BoardReadResult.h"
#include "io/CoordinateTransform.h"
#include "io/kicad/KiCadBoardZones.h"
#include "io/kicad/KiCadDesignRules.h"
#include "io/kicad/KiCadDesignRulesTranslator.h"
#include "io/kicad/KiCadProject.h"
#include "io/specctra/DsnReader.h"
#include "io/specctra/RulesReader.h"
#include "io/specctra/SesWriter.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/DifferentialPairDetector.h"
#include "rules/Net.h"
#include "rules/Nets.h"
#include "settings/RouterSettings.h"
#include "settings/SettingsMerger.h"
#include "settings/sources/ApiSettings.h"
#include "settings/sources/DefaultSettings.h"
#include "settings/sources/DsnFileSettings.h"
#include "settings/sources/KiCadProjectSettings.h"
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

  // PORT NOTE: the job is created before the board is read (Java's CLI creates it first too), so
  // that it can hold the KiCad inputs; nothing of it is used before step 3.
  auto* job = new (jrt::kBoard) RoutingJob();

  // PORT EXTENSION (Java: CliRunner, before the board is loaded): routing without the rules of a
  // .kicad_dru given with the design would produce a board that fails the KiCad DRC; without the
  // minimum track width of the .kicad_pro, the router may neck traces down below it; without the
  // fill settings of the zones of the .kicad_pcb, a plane that KiCad refills in several pieces
  // counts as one.
  if (!job->loadKiCadDesignRules(request->kicadDesignRulesFile)) {
    return HeadlessRoutingResult::failure(
        "Couldn't read the KiCad design rules file '" + *request->kicadDesignRulesFile + "'",
        jrt::System::currentTimeMillis() - startTime);
  }
  if (!job->loadKiCadProject(request->kicadProjectFile)) {
    return HeadlessRoutingResult::failure(
        "Couldn't read the KiCad project file '" + *request->kicadProjectFile + "'",
        jrt::System::currentTimeMillis() - startTime);
  }
  if (!job->loadKiCadBoardZones(request->kicadBoardFile)) {
    return HeadlessRoutingResult::failure(
        "Couldn't read the KiCad board file '" + *request->kicadBoardFile + "'",
        jrt::System::currentTimeMillis() - startTime);
  }

  // 1. Parse DSN into RoutingBoard (purely in-memory, no temporary files)
  jrt::Array<int8_t>* dsnBytes = request->dsnBytes();
  // PORT EXTENSION: a front end may route an already loaded (and possibly hand-edited) board;
  // the DSN bytes are then only used as settings source.
  io::BoardReadResult* readResult =
      request->presetBoard != nullptr
          ? nullptr
          : dsn::DsnReader::readBoard(dsnBytes, nullptr, nullptr, request->designName());

  RoutingBoard* board;
  if (request->presetBoard != nullptr) {
    board = request->presetBoard;
  } else if (auto* s = jrt::fast_cast<io::BoardReadResult_Success>(readResult)) {
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
  RoutingBoard* inputBoard = board;

  if (request->cancellationToken()->isCancelled()) {
    return HeadlessRoutingResult::cancelled(board,
                                            new (jrt::kOp) BoardStatistics(board, std::nullopt, false),
                                            jrt::System::currentTimeMillis() - startTime);
  }

  // 2. Resolve settings using SettingsMerger priority ladder and apply .rules to the board
  SettingsMerger* settingsMerger;
  {
    std::istringstream dsnStream(bytesToString(dsnBytes));
    settingsMerger = new (jrt::kBoard) SettingsMerger(
        {new (jrt::kBoard) DefaultSettings(), new (jrt::kBoard) DsnFileSettings(dsnStream, request->designName())});
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
    settingsMerger->addOrReplaceSources(new (jrt::kBoard) RulesFileSettings(rulesStream, rulesName));
  }

  // Java CliRunner.addKiCadProjectSettings: priority 25, above the DSN file, below every user
  // setting.
  if (job->kicadProject != nullptr) {
    settingsMerger->addOrReplaceSources(new (jrt::kBoard) KiCadProjectSettings(job->kicadProject));
  }

  if (request->settings() != nullptr) {
    settingsMerger->addOrReplaceSources(new (jrt::kBoard) ApiSettings(request->settings()));
  }

  RouterSettings* routerSettings = settingsMerger->merge();
  int32_t boardLayerCount = board->getLayerCount();
  if (routerSettings->getLayerCount() != boardLayerCount) {
    routerSettings->setLayerCount(boardLayerCount);
  }
  routerSettings->applyBoardSpecificOptimizations(board);
  routerSettings->applyNetClassExclusions(board);
  applyKiCadDesignRules(board, job);
  applyKiCadProjectMinTrackWidth(board, job);
  applyKiCadZoneFillRules(board, job);
  applyBoardSettingsOverrides(board, routerSettings);
  prepareBoardConnectivity(board, routerSettings);
  captureInitialClearanceBaseline(board);

  // 3. Prepare RoutingJob (created at the start, see above)
  job->board = board;
  job->routerSettings = routerSettings;
  job->name = request->designName();
  std::optional<int64_t> deadlineMillis =
      parseDeadlineMillis(startTime, routerSettings->jobTimeoutString);
  auto* thread = new (jrt::kBoard) HeadlessStoppableThread(request->cancellationToken(), deadlineMillis);
  job->thread = thread;
  job->state = RoutingJobState::RUNNING;

  // 4. Configure pipeline and listeners
  RoutingPipeline* pipeline = RoutingPipeline::createForHeadless(job);

  if (request->progressListener() != nullptr) {
    pipeline->addTaskStateChangedEventListener(
        TaskStateChangedEventListener::of([request, job](TaskStateChangedEvent* event) {
          notifyProgress(request->progressListener(),
                         new (jrt::kBoard) HeadlessRoutingProgress(
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
                         new (jrt::kBoard) HeadlessRoutingProgress(job->stage, job->getCurrentPass(), unrouted,
                                                     stats, "Board updated"));
        }));

    pipeline->addStageListener(RoutingPipeline_StageListener::of(
        [request, job](BatchAutorouter* autorouter) {
          BoardStatistics* stats = job->board->getStatistics();
          notifyProgress(request->progressListener(),
                         new (jrt::kBoard) HeadlessRoutingProgress(
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
                         new (jrt::kBoard) HeadlessRoutingProgress(RoutingStage::OPTIMIZATION, 0, unrouted,
                                                     stats, "Optimization stage started"));
        },
        [request, job](BatchOptimizer* optimizer) {
          BoardStatistics* stats = job->board->getStatistics();
          int32_t unrouted = stats != nullptr && stats->connections != nullptr
                                 ? unboxIncompleteCount(stats)
                                 : 0;
          notifyProgress(request->progressListener(),
                         new (jrt::kBoard) HeadlessRoutingProgress(RoutingStage::OPTIMIZATION, 0, unrouted,
                                                     stats, "Optimization stage finished"));
        }));
  }

  // PORT EXTENSION: live-board observer for front ends (see HeadlessRoutingRequest).
  if (request->boardObserver) {
    auto observer = request->boardObserver;
    observer(job->board, "routing started");
    pipeline->addBoardUpdatedEventListener(BoardUpdatedEventListener::of(
        [observer, job](BoardUpdatedEvent* event) { observer(job->board, "board updated"); }));
    pipeline->addStageListener(RoutingPipeline_StageListener::of(
        [observer, job](BatchAutorouter* autorouter) { observer(job->board, "routing finished"); },
        [observer, job](BatchOptimizer* optimizer) { observer(job->board, "optimization started"); },
        [observer, job](BatchOptimizer* optimizer) {
          observer(job->board, "optimization finished");
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
  auto* stats = new (jrt::kOp) BoardStatistics(board, std::nullopt, false);
  int32_t unrouted = stats->connections != nullptr ? unboxIncompleteCount(stats) : 0;

  std::optional<std::string> errorMessage;
  if (cancelled) {
    errorMessage = "Routing was cancelled";
  } else if (timedOut) {
    errorMessage = "Routing timed out";
  }

  // 7. Perform authoritative DRC once if requested
  jrt::Collection<ClearanceViolation*>* violations = new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>();
  if (request->runDrc() && !cancelled) {
    try {
      violations = (new (jrt::kOp) DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
    } catch (const jrt::Exception& e) {
      FRLogger::warn("DRC check encountered an error: " + e.getMessage());
      if (!errorMessage.has_value()) {
        errorMessage = "DRC check failed: " + e.getMessage();
      }
    }
  }

  // 8. Serialize to SES if requested
  jrt::Array<int8_t>* sesBytes = new (jrt::kOp) jrt::Array<int8_t>(0);
  if (request->computeSes() && !cancelled) {
    try {
      std::ostringstream baos;
      dsn::SesWriter::write(board, baos, request->designName());
      std::string out = baos.str();
      sesBytes = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(out.size()));
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

  // PORT NOTE (stage E): the board read from the DSN bytes belongs to this call; when the job
  // finished on another board (a snapshot / candidate), nothing uses it any more. A preset
  // board is the caller's.
  if (request->presetBoard == nullptr && inputBoard != board) {
    BasicBoard::discard(inputBoard, "input");
  }

  int64_t executionTimeMillis = jrt::System::currentTimeMillis() - startTime;
  bool completed =
      !cancelled && !errorMessage.has_value() && unrouted == 0 && violations->isEmpty();

  return new (jrt::kBoard) HeadlessRoutingResult(completed, cancelled, unrouted, violations, stats, sesBytes,
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
    return new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>();
  }
  return (new (jrt::kOp) DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
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

void FreeroutingHeadlessEngine::applyKiCadDesignRules(RoutingBoard* board, RoutingJob* routingJob) {
  KiCadDesignRules* designRules = routingJob->kicadDesignRules;
  if (designRules == nullptr || board == nullptr) {
    return;
  }
  KiCadDesignRulesTranslator_Result* result = KiCadDesignRulesTranslator::apply(board, designRules);
  for (const std::string& warning : *result->warnings()) {
    routingJob->logWarning("KiCad design rules: " + warning);
  }
  routingJob->logInfo(KiCadDesignRulesTranslator::summary(result, designRules->sourceName()));
}

void FreeroutingHeadlessEngine::applyKiCadProjectMinTrackWidth(RoutingBoard* board,
                                                               RoutingJob* routingJob) {
  KiCadProject* project = routingJob->kicadProject;
  if (project == nullptr || !project->minTrackWidthUm().has_value() || board == nullptr) {
    return;
  }
  double minWidth = board->communication->physicalToBoard(*project->minTrackWidthUm(), Unit::UM);
  int32_t minHalfWidth = jrt::d2i(jrt::Math::ceil(minWidth / 2));
  if (minHalfWidth > board->rules->getMinAllowedTraceHalfWidth()) {
    board->rules->setMinAllowedTraceHalfWidth(minHalfWidth);
    routingJob->logInfo("KiCad project: no automatic narrowing below the minimum track width of " +
                        jrt::str(*project->minTrackWidthUm()) + " um.");
  }
}

void FreeroutingHeadlessEngine::applyKiCadZoneFillRules(RoutingBoard* board, RoutingJob* routingJob) {
  KiCadBoardZones* boardZones = routingJob->kicadBoardZones;
  if (boardZones == nullptr || board == nullptr) {
    return;
  }
  Communication* communication = board->communication;
  int32_t matched = 0;
  // board order, so that the log is the same on every run
  auto* areas = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
  for (ConductionArea* area : *board->getConductionAreas()) {
    areas->add(area);
  }
  areas->sort([](ConductionArea* a, ConductionArea* b) {
    return jrt::Integer::compare(a->getId(), b->getId());
  });
  for (ConductionArea* area : *areas) {
    if (area->netCount() != 1 || board->rules->nets->get(area->getNetNumber(0)) == nullptr) {
      continue;
    }
    std::string netName = board->rules->nets->get(area->getNetNumber(0))->name;
    std::string layerName = (*board->layerStructure->layers)[area->getLayer()]->name;
    // the bounding box of the plane in micrometres, as KiCad writes it into the DSN
    jrt::Array<double>* box = communication->coordinateTransform->boardToDsn(area->boundingBox());
    for (int32_t i = 0; i < box->length; i++) {
      (*box)[i] = Unit_scale((*box)[i], communication->unit, Unit::UM);
    }
    KiCadBoardZones_Zone* best = nullptr;
    double bestDistance = jrt::Double::MAX_VALUE;
    for (KiCadBoardZones_Zone* zone : *boardZones->zones()) {
      if (zone->netName() != netName || !zone->isOnLayer(layerName)) {
        continue;
      }
      // the y axis of KiCad points down, the one of the DSN up
      double distance = jrt::Math::abs(zone->minXUm() - (*box)[0]) +
                        jrt::Math::abs(-zone->maxYUm() - (*box)[1]) +
                        jrt::Math::abs(zone->maxXUm() - (*box)[2]) +
                        jrt::Math::abs(-zone->minYUm() - (*box)[3]);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = zone;
      }
    }
    if (best == nullptr) {
      continue;
    }
    matched++;
    int32_t clearance =
        jrt::d2i(jrt::Math::ceil(communication->physicalToBoard(best->clearanceUm(), Unit::UM)));
    int32_t minWidth =
        jrt::d2i(jrt::Math::ceil(communication->physicalToBoard(best->minThicknessUm(), Unit::UM)));
    if (clearance != area->getFillClearance() || minWidth != area->getFillMinWidth()) {
      area->setFillRules(clearance, minWidth);
    }
    PadPlaneConnection_Mode padConnection =
        PadPlaneConnection_Mode_valueOf(KiCadBoardZones_PadConnection_name(best->padConnection()));
    int32_t thermalGap = static_cast<int32_t>(
        jrt::Math::round(communication->physicalToBoard(best->thermalGapUm(), Unit::UM)));
    int32_t spokeWidth = static_cast<int32_t>(
        jrt::Math::round(communication->physicalToBoard(best->thermalBridgeWidthUm(), Unit::UM)));
    if (std::optional<PadPlaneConnection_Mode>(padConnection) != area->getPadConnection() ||
        thermalGap != area->getThermalGap() || spokeWidth != area->getThermalSpokeWidth()) {
      area->setPadConnectionRules(padConnection, thermalGap, spokeWidth);
    }
    int32_t edgeClearance = static_cast<int32_t>(
        jrt::Math::round(communication->physicalToBoard(boardZones->edgeClearanceUm(), Unit::UM)));
    if (edgeClearance != area->getFillEdgeClearance()) {
      area->setFillEdgeClearance(edgeClearance);
    }
    bool fillets = best->cornerSmoothing() == KiCadBoardZones_CornerSmoothing::FILLET;
    int32_t corner =
        best->cornerSmoothing() == KiCadBoardZones_CornerSmoothing::NONE
            ? 0
            : static_cast<int32_t>(
                  jrt::Math::round(communication->physicalToBoard(best->cornerRadiusUm(), Unit::UM)));
    if (fillets != area->hasCornerFillets() || corner != area->getCornerSmoothing()) {
      area->setFillCornerSmoothing(fillets, corner);
    }
  }
  int32_t matchedPads = applyKiCadPadConnections(board, boardZones);
  routingJob->logInfo("KiCad board '" + boardZones->sourceName() +
                      "': zone clearance and minimum width applied to " + jrt::str(matched) +
                      " of " + jrt::str(areas->size()) + " planes, pad connection to " +
                      jrt::str(matchedPads) + " pads.");
}

int32_t FreeroutingHeadlessEngine::applyKiCadPadConnections(RoutingBoard* board,
                                                            KiCadBoardZones* boardZones) {
  Communication* communication = board->communication;
  // the pads by cells of 10 um, to find the pad of a pin without comparing each with each
  double cell = 10.0;
  // Java: HashMap<Long, List<Pad>>, only looked up
  std::unordered_map<int64_t, std::vector<KiCadBoardZones_Pad*>> padsByCell;
  for (KiCadBoardZones_Pad* pad : *boardZones->pads()) {
    int64_t key = cellKey(jrt::Math::round(pad->xUm() / cell), jrt::Math::round(-pad->yUm() / cell));
    padsByCell[key].push_back(pad);
  }
  int32_t matched = 0;
  auto* pins = new (jrt::kOp) jrt::ArrayList<Pin*>();
  for (Pin* pin : *board->getPins()) {
    pins->add(pin);
  }
  pins->sort([](Pin* a, Pin* b) { return jrt::Integer::compare(a->getId(), b->getId()); });
  for (Pin* pin : *pins) {
    // the centre of the pin in micrometres, the y axis pointing up as in the DSN
    jrt::Array<double>* centre =
        communication->coordinateTransform->boardToDsn(pin->getCenter()->toFloat());
    double x = Unit_scale((*centre)[0], communication->unit, Unit::UM);
    double y = Unit_scale((*centre)[1], communication->unit, Unit::UM);
    std::string firstLayer = (*board->layerStructure->layers)[pin->firstLayer()]->name;
    std::string lastLayer = (*board->layerStructure->layers)[pin->lastLayer()]->name;
    KiCadBoardZones_Pad* best = nullptr;
    double bestDistance = 5.0;
    int64_t cellX = jrt::Math::round(x / cell);
    int64_t cellY = jrt::Math::round(y / cell);
    for (int64_t dx = -1; dx <= 1; dx++) {
      for (int64_t dy = -1; dy <= 1; dy++) {
        auto candidates = padsByCell.find(cellKey(cellX + dx, cellY + dy));
        if (candidates == padsByCell.end()) {
          continue;
        }
        for (KiCadBoardZones_Pad* pad : candidates->second) {
          double distance = jrt::Math::hypot(pad->xUm() - x, -pad->yUm() - y);
          if (distance < bestDistance && (pad->isOnLayer(firstLayer) || pad->isOnLayer(lastLayer))) {
            bestDistance = distance;
            best = pad;
          }
        }
      }
    }
    if (best == nullptr) {
      continue;
    }
    matched++;
    auto* shapeTuple = new (jrt::kOp) jrt::Array<double>(2);
    (*shapeTuple)[0] = Unit_scale(best->shapeXUm(), Unit::UM, communication->unit);
    (*shapeTuple)[1] = Unit_scale(-best->shapeYUm(), Unit::UM, communication->unit);
    FloatPoint* shapeCentre = communication->coordinateTransform->dsnToBoard(shapeTuple);
    pin->setPlaneConnection(new (jrt::kBoard) PadPlaneConnection(
        PadPlaneConnection_Mode_valueOf(KiCadBoardZones_PadConnection_name(best->connection())),
        best->throughHole(), shapeCentre->x, shapeCentre->y,
        communication->physicalToBoard(best->sizeXUm() / 2.0, Unit::UM),
        communication->physicalToBoard(best->sizeYUm() / 2.0, Unit::UM), best->angleDeg(),
        best->spokeAngleDeg(), best->circular(),
        static_cast<int32_t>(
            jrt::Math::round(communication->physicalToBoard(best->thermalGapUm(), Unit::UM))),
        static_cast<int32_t>(
            jrt::Math::round(communication->physicalToBoard(best->thermalBridgeWidthUm(), Unit::UM)))));
  }
  return matched;
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
  board->connectNetlessPads(false, fixKicadFootprints, new (jrt::kOp) jrt::Array<int32_t>(1));
  board->clusterNetTies();
  DifferentialPairDetector::detectAndRegister(board->rules);
}

void FreeroutingHeadlessEngine::captureInitialClearanceBaseline(RoutingBoard* board) {
  if (board == nullptr) {
    return;
  }
  jrt::Collection<ClearanceViolation*>* initialViolations =
      (new (jrt::kOp) DesignRulesChecker(board, nullptr))->getAllClearanceViolations();
  board->preExistingClearanceViolationsCount = initialViolations->size();
  int32_t unfixable = 0;
  for (ClearanceViolation* v : *initialViolations) {
    if (v->isUnfixable()) {
      unfixable++;
    }
  }
  board->unfixableClearanceViolationsCount = unfixable;
  board->clearanceBaseline = new (jrt::kOp) ClearanceBaseline(initialViolations);
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
