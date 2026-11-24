#include "engine.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "core/RoutingStage.h"
#include "core/facade/CancellationToken.h"
#include "core/facade/FreeroutingHeadlessEngine.h"
#include "core/facade/HeadlessRoutingProgress.h"
#include "core/facade/HeadlessRoutingProgressListener.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "core/scoring/BoardStatistics.h"
#include "core/scoring/BoardStatisticsClearanceViolations.h"
#include "drc/AirLine.h"
#include "drc/DesignRulesChecker.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/TileShape.h"
#include "interactive/InteractiveSession.h"
#include "interactive/Route.h"
#include "geometry/planar/Point.h"
#include "io/BoardReadResult.h"
#include "io/specctra/DsnReader.h"
#include "io/specctra/SesWriter.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace frui {

namespace {

constexpr size_t kMaxLogLines = 5000;
constexpr int64_t kPublishIntervalMs = 200;
constexpr int64_t kAirlinesIntervalMs = 1000;

std::atomic<bool> g_engineExists{false};

std::string baseName(const std::string& path) {
  auto pos = path.find_last_of("/\\");
  std::string b = pos == std::string::npos ? path : path.substr(pos + 1);
  auto dot = b.find_last_of('.');
  return dot == std::string::npos ? b : b.substr(0, dot);
}

// ---------------------------------------------------------------- geometry extraction -------

struct Bounds {
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  void add(double x, double y) {
    if (x < x0) x0 = x;
    if (y < y0) y0 = y;
    if (x > x1) x1 = x;
    if (y > y1) y1 = y;
  }
};

void putXY(std::string& out, double x, double y, Bounds& b) {
  float v[2] = {static_cast<float>(x), static_cast<float>(y)};
  out.append(reinterpret_cast<const char*>(v), sizeof v);
  b.add(x, y);
}

/** Fan-triangulates a convex shape given by its approximate corners. */
void appendConvex(std::string& out, fr::TileShape* shape, Bounds& b) {
  if (shape == nullptr) return;
  jrt::Array<fr::FloatPoint*>* c = shape->cornerApproxArr();
  if (c == nullptr || c->length < 3) return;
  fr::FloatPoint* p0 = (*c)[0];
  for (int32_t k = 1; k + 1 < c->length; ++k) {
    putXY(out, p0->x, p0->y, b);
    putXY(out, (*c)[k]->x, (*c)[k]->y, b);
    putXY(out, (*c)[k + 1]->x, (*c)[k + 1]->y, b);
  }
}

void appendLoop(std::string& out, jrt::Array<fr::FloatPoint*>* c, Bounds& b) {
  if (c == nullptr || c->length < 2) return;
  for (int32_t k = 0; k < c->length; ++k) {
    fr::FloatPoint* p = (*c)[k];
    fr::FloatPoint* q = (*c)[(k + 1) % c->length];
    putXY(out, p->x, p->y, b);
    putXY(out, q->x, q->y, b);
  }
}

Snapshot* buildSnapshot(fr::BasicBoard* board, bool withAirlines, const Snapshot* previous) {
  auto* snap = new (jrt::kOp) Snapshot();
  Bounds bounds;
  snap->unit = fr::Unit_toString(board->communication->unit);
  snap->resolution = board->communication->resolution;
  fr::LayerStructure* ls = board->layerStructure;
  int32_t layerCount = board->getLayerCount();
  snap->layers.resize(layerCount);
  for (int32_t i = 0; i < layerCount; ++i) {
    fr::Layer* layer = (*ls->layers)[i];
    snap->layers[i].name = layer->name;
    snap->layers[i].signal = layer->isSignal;
  }
  auto layerAt = [&](int32_t l) -> LayerGeometry* {
    return (l >= 0 && l < layerCount) ? &snap->layers[l] : nullptr;
  };

  for (fr::Item* item : *board->getItems()) {
    snap->items++;
    if (auto* outline = jrt::fast_cast<fr::BoardOutline>(item)) {
      for (int32_t s = 0; s < outline->shapeCount(); ++s)
        appendLoop(snap->outline, outline->getShape(s)->cornerApproxArr(), bounds);
      continue;
    }
    if (jrt::fast_cast<fr::ComponentOutline>(item)) continue;  // courtyards: not drawn yet
    auto* via = jrt::fast_cast<fr::Via>(item);
    std::string LayerGeometry::*bucket = nullptr;
    if (via) {
      snap->viaCount++;
    } else if (jrt::fast_cast<fr::Trace>(item)) {
      bucket = &LayerGeometry::traces;
      snap->traces++;
    } else if (jrt::fast_cast<fr::Pin>(item)) {
      bucket = &LayerGeometry::pads;
      snap->pins++;
    } else if (jrt::fast_cast<fr::ConductionArea>(item)) {
      bucket = &LayerGeometry::areas;
    } else if (jrt::fast_cast<fr::ObstacleArea>(item)) {
      bucket = &LayerGeometry::keepouts;
    } else {
      continue;
    }
    int32_t n = item->tileShapeCount();
    for (int32_t i = 0; i < n; ++i) {
      int32_t l = item->shapeLayer(i);
      if (via) {
        if (l == item->firstLayer()) appendConvex(snap->vias, item->getTileShape(i), bounds);
      } else if (LayerGeometry* g = layerAt(l)) {
        appendConvex(g->*bucket, item->getTileShape(i), bounds);
      }
    }
  }

  if (withAirlines) {
    jrt::Array<fr::AirLine*>* airlines = (new (jrt::kOp) fr::DesignRulesChecker(board, nullptr))->getAllAirlines();
    if (airlines != nullptr) {
      snap->incompletes = airlines->length;
      for (fr::AirLine* a : *airlines) {
        putXY(snap->airlines, a->fromCorner->x, a->fromCorner->y, bounds);
        putXY(snap->airlines, a->toCorner->x, a->toCorner->y, bounds);
      }
    }
  } else if (previous != nullptr) {
    // Keep the last computed airlines between the (throttled) airline updates.
    snap->airlines = previous->airlines;
    snap->incompletes = previous->incompletes;
  }

  if (bounds.x0 <= bounds.x1) {
    snap->x0 = bounds.x0;
    snap->y0 = bounds.y0;
    snap->x1 = bounds.x1;
    snap->y1 = bounds.y1;
  }
  return snap;
}

}  // namespace

// -------------------------------------------------------------------------- Engine ----------

Engine::Engine() {
  if (g_engineExists.exchange(true)) {
    throw jrt::IllegalStateException("only one routing engine may exist per process");
  }
  fr::FRLogger::consoleOutput = false;
  fr::FRLogger::sink = [this](int32_t level, const std::string& text) { appendLog(level, text); };
  thread_ = std::thread([this] { threadMain(); });
}

Engine::~Engine() {
  cancelRequested_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    queue_.push_back(Command{Command::Quit, {}, {}});
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  fr::FRLogger::sink = nullptr;
  fr::FRLogger::consoleOutput = true;
  g_engineExists = false;
}

void Engine::load(const std::string& path) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(Command{Command::Load, path, {}});
    status_.busy = true;
  }
  cv_.notify_all();
}

void Engine::route(const RouteOptions& options) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(Command{Command::Route, {}, options});
    status_.busy = true;
  }
  cv_.notify_all();
}

void Engine::save(const std::string& path) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(Command{Command::Save, path, {}});
    status_.busy = true;
  }
  cv_.notify_all();
}

void Engine::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = queue_.begin(); it != queue_.end();) {
    it = (it->kind == Command::Quit) ? std::next(it) : queue_.erase(it);
  }
  cancelRequested_ = true;
}

void Engine::edit(const EditEvent& event) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool coalescable = event.kind == EditEvent::Move || event.kind == EditEvent::Drag;
    if (coalescable && !queue_.empty() && queue_.back().kind == Command::Edit &&
        queue_.back().edit.kind == event.kind) {
      queue_.back().edit = event;  // only the latest mouse position matters
    } else {
      Command cmd{Command::Edit, {}, {}};
      cmd.edit = event;
      queue_.push_back(cmd);
    }
  }
  cv_.notify_all();
}

Status Engine::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  Status s = status_;
  if (s.startedMs != 0) s.elapsedMs = jrt::System::currentTimeMillis() - s.startedMs;
  return s;
}

const Snapshot* Engine::snapshotIfNewer(uint64_t haveVersion) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (snapshot_ == nullptr || snapshot_->version == haveVersion) return nullptr;
  return snapshot_;
}

uint64_t Engine::logs(uint64_t from, std::vector<LogLine>& out) {
  std::lock_guard<std::mutex> lock(logMutex_);
  if (from < logBase_) from = logBase_;
  for (uint64_t i = from - logBase_; i < logLines_.size(); ++i) out.push_back(logLines_[i]);
  return logBase_ + logLines_.size();
}

void Engine::appendLog(int32_t level, const std::string& text) {
  std::lock_guard<std::mutex> lock(logMutex_);
  logLines_.push_back(LogLine{level, text});
  while (logLines_.size() > kMaxLogLines) {
    logLines_.pop_front();
    ++logBase_;
  }
}

void Engine::setStatus(const std::function<void(Status&)>& update) {
  std::lock_guard<std::mutex> lock(mutex_);
  update(status_);
}

void Engine::dropBoard(fr::BasicBoard* next, bool lastOfFamily) {
  // Stage E: the engine owns its current board; a replaced one is freed (published views are
  // plain data, the edit session is recreated on the new board).
  if (board_ != nullptr && board_ != next) {
    if (lastOfFamily) {
      fr::BasicBoard::discardLast(board_);
    } else {
      fr::BasicBoard::discard(board_, "engine");
    }
  }
  if (board_ != next) board_ = nullptr;
}

void Engine::threadMain() {
  // This thread runs core code: geometry and temporaries no owner holds belong to the engine's
  // job region, freed when the engine stops (MEMORY_MODEL.md, E9).
  jrt::JobRegion job;
  for (;;) {
    Command cmd;
    bool moreEdits = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return !queue_.empty(); });
      cmd = queue_.front();
      queue_.pop_front();
      moreEdits = !queue_.empty() && queue_.front().kind == Command::Edit;
    }
    if (cmd.kind == Command::Quit) {
      dropBoard(nullptr, true);
      return;
    }
    int64_t start = jrt::System::currentTimeMillis();
    try {
      switch (cmd.kind) {
        case Command::Load: doLoad(cmd.path); break;
        case Command::Route: doRoute(cmd.options); break;
        case Command::Save: doSave(cmd.path); break;
        case Command::Edit: doEdit(cmd.edit, moreEdits); continue;  // no busy/elapsed bookkeeping
        default: break;
      }
    } catch (const jrt::Throwable& e) {
      setStatus([&](Status& s) {
        s.state = "error";
        s.error = e.getMessage();
      });
      appendLog(fr::FRLogger::LEVEL_ERROR, "engine: " + e.getMessage());
    } catch (const std::exception& e) {
      setStatus([&](Status& s) {
        s.state = "error";
        s.error = e.what();
      });
      appendLog(fr::FRLogger::LEVEL_ERROR, std::string("engine: ") + e.what());
    }
    int64_t elapsed = jrt::System::currentTimeMillis() - start;
    std::lock_guard<std::mutex> lock(mutex_);
    status_.elapsedMs = elapsed;
    status_.startedMs = 0;
    status_.busy = !queue_.empty();
  }
}

void Engine::publish(fr::BasicBoard* board, bool withAirlines) {
  const Snapshot* previous;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    previous = snapshot_;
  }
  Snapshot* snap = buildSnapshot(board, withAirlines, previous);
  std::lock_guard<std::mutex> lock(mutex_);
  snap->version = nextVersion_++;
  snapshot_ = snap;
  status_.snapshotVersion = snap->version;
  if (withAirlines) status_.unrouted = snap->incompletes;
  lastPublishMs_ = jrt::System::currentTimeMillis();
  if (withAirlines) lastAirlinesMs_ = lastPublishMs_;
}

void Engine::doLoad(const std::string& path) {
  setStatus([&](Status& s) {
    s.state = "loading";
    s.message = "Loading " + path;
    s.error.clear();
  });
  std::ifstream in(path, std::ios::binary);
  if (!in) throw jrt::IOException("cannot read '" + path + "'");
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto* bytes = new (jrt::kOp) jrt::Array<int8_t>(static_cast<int32_t>(data.size()));
  std::memcpy(bytes->data(), data.data(), data.size());
  std::string name = baseName(path);

  fr::io::BoardReadResult* result = fr::dsn::DsnReader::readBoard(bytes, nullptr, nullptr, name);
  fr::BasicBoard* board = nullptr;
  if (auto* ok = jrt::fast_cast<fr::io::BoardReadResult_Success>(result)) board = ok->board();
  else if (auto* om = jrt::fast_cast<fr::io::BoardReadResult_OutlineMissing>(result)) board = om->board();
  else if (auto* pe = jrt::fast_cast<fr::io::BoardReadResult_ParseError>(result))
    throw jrt::IOException("DSN parse error at '" + pe->location() + "': " + pe->detail());
  if (board == nullptr) throw jrt::IOException("DSN did not contain a valid board");

  dropBoard(board, true);  // a new board family (the old one is gone with its last board)
  board_ = board;
  session_ = nullptr;
  dsnBytes_ = bytes;
  designName_ = name;
  dsnPath_ = path;
  publish(board, true);
  setStatus([&](Status& s) {
    s.state = "ready";
    s.design = name;
    s.boardLoaded = true;
    s.routed = false;
    s.stage.clear();
    s.pass = 0;
    s.violations = -1;
    s.message = "Loaded " + name;
  });
}

void Engine::doRoute(const RouteOptions& o) {
  if (dsnBytes_ == nullptr) throw jrt::IllegalStateException("no design loaded");
  if (session_ != nullptr) {  // finish a half-drawn route / drag before autorouting
    session_->cancel();
    updateEditStatus();
  }
  cancelRequested_ = false;
  setStatus([&](Status& s) {
    s.state = "routing";
    s.stage = "STARTING";
    s.pass = 0;
    s.message = "Routing " + designName_;
    s.error.clear();
    s.startedMs = jrt::System::currentTimeMillis();
  });

  fr::RouterSettings* settings = nullptr;
  if (o.maxPasses || o.optimize || o.vias || o.timeout) {
    settings = new (jrt::kBoard) fr::RouterSettings();
    if (o.maxPasses) settings->setMaxPasses(o.maxPasses);
    if (o.optimize) settings->setOptimizerEnabled(o.optimize);
    if (o.vias) settings->setViasAllowed(o.vias);
    if (o.timeout) settings->setJobTimeoutString(o.timeout);
  }

  auto* request =
      fr::HeadlessRoutingRequest::builder()
          ->dsnBytes(dsnBytes_)
          ->presetBoard(routingBoard())  // route the current (possibly hand-edited) board
          ->designName(designName_)
          ->settings(settings)
          ->runDrc(o.drc)
          ->computeSes(false)
          ->cancellationToken(fr::CancellationToken::of([this] { return cancelRequested_.load(); }))
          ->progressListener(fr::HeadlessRoutingProgressListener::of([this](fr::HeadlessRoutingProgress* p) {
            std::optional<int32_t> violations;
            if (p->statistics() != nullptr && p->statistics()->clearanceViolations != nullptr)
              violations = p->statistics()->clearanceViolations->totalCount;
            setStatus([&](Status& s) {
              s.stage = fr::RoutingStage_toString(p->stage());
              if (p->passNumber() > 0) s.pass = p->passNumber();
              if (p->unroutedCount() >= 0) s.unrouted = p->unroutedCount();
              if (violations) s.violations = *violations;
              s.message = p->message();
            });
          }))
          ->boardObserver([this](fr::RoutingBoard* board, const std::string& reason) {
            int64_t now = jrt::System::currentTimeMillis();
            bool important = reason != "board updated";
            if (!important && now - lastPublishMs_ < kPublishIntervalMs) return;
            publish(board, important || now - lastAirlinesMs_ >= kAirlinesIntervalMs);
          })
          ->build();

  fr::HeadlessRoutingResult* result = fr::FreeroutingHeadlessEngine::route(request);
  if (result->board() != nullptr) {
    dropBoard(result->board(), false);  // the routed board replaces the input board (ours)
    board_ = result->board();
  }
  session_ = nullptr;  // recreated on the next edit event, on the routed board
  if (board_ != nullptr) publish(board_, true);

  int32_t violations = result->clearanceViolations() ? result->clearanceViolations()->size() : 0;
  setStatus([&](Status& s) {
    s.routed = !result->errorMessage().has_value();
    s.unrouted = result->unroutedConnections();
    if (o.drc) s.violations = violations;
    if (result->errorMessage()) {
      s.state = result->isCancelled() ? "ready" : "error";
      s.error = *result->errorMessage();
      s.message = result->isCancelled() ? "Routing cancelled" : "Routing failed";
    } else {
      s.state = "ready";
      s.message = "Routing finished: " + std::to_string(result->unroutedConnections()) + " unrouted, " +
                  std::to_string(violations) + " violations";
    }
    s.stage.clear();
  });
}

void Engine::doSave(const std::string& path) {
  if (board_ == nullptr) throw jrt::IllegalStateException("no board to save");
  setStatus([&](Status& s) {
    s.state = "saving";
    s.message = "Saving " + path;
  });
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw jrt::IOException("cannot write '" + path + "'");
    fr::dsn::SesWriter::write(board_, out, designName_);
  }
  setStatus([&](Status& s) {
    s.state = "ready";
    s.message = "Saved " + path;
  });
}

}  // namespace frui

namespace frui {

fr::RoutingBoard* Engine::routingBoard() { return jrt::fast_cast<fr::RoutingBoard>(board_); }

void Engine::updateEditStatus() {
  fr::InteractiveSession* s = session_;
  setStatus([&](Status& st) {
    if (s == nullptr) {
      st.editRouting = st.editDragging = st.hasCorner = st.hasTarget = false;
      return;
    }
    static const char* const kModes[] = {"select", "route", "drag"};
    st.editMode = kModes[static_cast<int>(s->mode())];
    st.editMessage = s->statusMessage();
    st.editLayer = s->layer();
    st.editRouting = s->routing();
    st.editDragging = s->dragging();
    st.hasCorner = st.hasTarget = false;
    if (fr::Route* r = s->route()) {
      fr::FloatPoint* c = r->getLastCorner()->toFloat();
      st.hasCorner = true;
      st.cornerX = c->x;
      st.cornerY = c->y;
      st.penHalfWidth = r->getPenHalfWidth(r->getLayer());
      st.editLayer = r->getLayer();
      if (fr::FloatPoint* t = r->getNearestTargetPoint()) {
        st.hasTarget = true;
        st.targetX = t->x;
        st.targetY = t->y;
        st.targetLayer = r->nearestTargetLayer();
      }
    }
  });
}

void Engine::doEdit(const EditEvent& e, bool morePending) {
  fr::RoutingBoard* board = routingBoard();
  if (board == nullptr) return;
  if (session_ == nullptr || session_->board() != board) {
    auto* s = new (jrt::kBoard) fr::InteractiveSession(board);
    // keep the mode and layer chosen in the UI across board replacements
    Status st = status();
    s->changeLayer(st.editLayer);
    if (st.editMode == "route") s->setMode(fr::InteractiveSession::Mode::Route);
    else if (st.editMode == "drag") s->setMode(fr::InteractiveSession::Mode::Drag);
    session_ = s;
  }
  fr::InteractiveSession* s = session_;
  bool changed = false;
  bool finished = false;  // end of an action: recompute incompletes too
  switch (e.kind) {
    case EditEvent::Mode:
      s->setMode(e.value == 1 ? fr::InteractiveSession::Mode::Route
                 : e.value == 2 ? fr::InteractiveSession::Mode::Drag
                                : fr::InteractiveSession::Mode::Select);
      changed = finished = true;
      break;
    case EditEvent::Press: changed = s->mousePressed(e.x, e.y); break;
    case EditEvent::Drag: changed = s->mouseDragged(e.x, e.y); break;
    case EditEvent::Release: changed = finished = s->mouseReleased(); break;
    case EditEvent::Move: {
      bool wasRouting = s->routing();
      changed = s->mouseMoved(e.x, e.y);
      finished = wasRouting && !s->routing();  // connection completed
      break;
    }
    case EditEvent::Click: {
      changed = s->leftClicked(e.x, e.y);
      finished = !s->routing();
      changed = changed || finished;
      break;
    }
    case EditEvent::Complete: s->complete(); changed = finished = true; break;
    case EditEvent::Cancel: changed = finished = s->cancel() || true; break;
    case EditEvent::Layer: changed = s->changeLayer(e.value); finished = !s->routing(); break;
    case EditEvent::Undo: changed = finished = s->undo(); break;
    case EditEvent::Redo: changed = finished = s->redo(); break;
  }
  updateEditStatus();
  if (changed && (!morePending || finished)) publish(board, finished);
}

}  // namespace frui
