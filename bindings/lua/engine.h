// Routing engine host for front ends (used by the Lua module frcore).
//
// Threading model
//   * The engine owns ONE worker thread ("engine thread"), created and joined in C++. All routing
//     core code (DSN parsing, routing pipeline, SES writing, render-snapshot extraction) runs on
//     it. The core is single-threaded by design, so this is the only thread that touches ported
//     objects; the Lua interpreter is never involved in computation and holds no lock while the
//     engine works.
//   * The UI thread only enqueues commands and reads plain data: Status, log lines and immutable
//     render Snapshots, each exchanged under a short mutex.
//   * Only one Engine may exist per process (the core keeps process-wide static state).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "jrt/job_thread.h"

namespace fr {
class BasicBoard;
class RoutingBoard;
class InteractiveSession;
}
namespace jrt {
template <class T>
class Array;
}

namespace frui {

/** Routing options coming from the UI (plain data, no core objects). */
struct RouteOptions {
  std::optional<int32_t> maxPasses;
  std::optional<bool> optimize;
  std::optional<bool> vias;
  std::optional<std::string> timeout;  // Java Duration syntax, e.g. "PT10M"
  bool drc = true;
};

/** Geometry of one layer, as float32 xy triangle lists (binary strings). */
struct LayerGeometry {
  std::string name;
  bool signal = true;
  std::string traces;    // triangles
  std::string pads;      // triangles
  std::string areas;     // triangles (conduction areas / planes)
  std::string keepouts;  // triangles
};

/** Immutable render snapshot of a board. */
struct Snapshot {
  uint64_t version = 0;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // bounding box of the drawn geometry
  std::vector<LayerGeometry> layers;
  std::string vias;      // triangles (first layer of each via)
  std::string outline;   // line segments (xy xy)
  std::string airlines;  // line segments of incomplete connections
  int32_t items = 0, traces = 0, viaCount = 0, pins = 0, incompletes = 0;
  std::string unit = "um";  // DSN unit of the design (um, mm, mil, inch)
  int32_t resolution = 1;   // board units per DSN unit
};

struct Status {
  std::string state = "idle";  // idle | loading | ready | routing | saving | error
  std::string stage;           // core RoutingStage name while routing
  std::string message;
  std::string design;          // loaded design name
  std::string error;
  int32_t pass = 0;
  int32_t unrouted = -1;
  int32_t violations = -1;
  int64_t startedMs = 0;  // routing start (epoch ms), 0 if not routing
  int64_t elapsedMs = 0;  // duration of the last finished command
  uint64_t snapshotVersion = 0;
  bool busy = false;
  bool boardLoaded = false;
  bool routed = false;
  // interactive editing (see InteractiveSession)
  std::string editMode = "select";  // select | route | drag
  std::string editMessage;
  int32_t editLayer = 0;
  bool editRouting = false;         // a route is being drawn
  bool editDragging = false;
  bool hasCorner = false, hasTarget = false;
  double cornerX = 0, cornerY = 0;  // last corner of the route being drawn
  double targetX = 0, targetY = 0;  // nearest target point of that route
  int32_t targetLayer = -1;
  int32_t penHalfWidth = 0;
};

/** Interactive editing events (plain data). */
struct EditEvent {
  enum Kind { Mode, Press, Drag, Release, Move, Click, Complete, Cancel, Layer, Undo, Redo } kind;
  double x = 0, y = 0;
  int32_t value = 0;  // Mode: 0 select, 1 route, 2 drag; Layer: layer index
};

struct LogLine {
  int32_t level;
  std::string text;
};

class Engine {
 public:
  Engine();
  ~Engine();  // cancels and joins the engine thread
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Commands (non-blocking; executed in order on the engine thread).
  void load(const std::string& path);
  void route(const RouteOptions& options);
  void save(const std::string& path);
  void cancel();  // cancels the running routing job and drops queued commands
  /** Queues an interactive edit event (consecutive Move/Drag events are coalesced). */
  void edit(const EditEvent& event);

  // Plain-data accessors (thread-safe).
  Status status();
  /** Latest snapshot if its version differs from haveVersion, else nullptr. */
  const Snapshot* snapshotIfNewer(uint64_t haveVersion);
  /** Log lines with index >= from; returns the index after the last returned line. */
  uint64_t logs(uint64_t from, std::vector<LogLine>& out);

 private:
  struct Command {
    enum Kind { Load, Route, Save, Edit, Quit } kind;
    std::string path;
    RouteOptions options;
    EditEvent edit{EditEvent::Mode};
  };

  void threadMain();
  void doLoad(const std::string& path);
  void doRoute(const RouteOptions& options);
  void doSave(const std::string& path);
  void doEdit(const EditEvent& e, bool morePending);
  void updateEditStatus();
  fr::RoutingBoard* routingBoard();
  void publish(fr::BasicBoard* board, bool withAirlines);
  void setStatus(const std::function<void(Status&)>& update);
  void appendLog(int32_t level, const std::string& text);

  jrt::JobThread thread_;
  std::mutex mutex_;  // guards queue_, status_, snapshot_
  std::condition_variable cv_;
  std::deque<Command> queue_;
  Status status_;
  const Snapshot* snapshot_ = nullptr;  // immutable once published; old ones are GC'd
  uint64_t nextVersion_ = 1;

  std::mutex logMutex_;
  std::deque<LogLine> logLines_;
  uint64_t logBase_ = 0;  // index of logLines_.front()

  std::atomic<bool> cancelRequested_{false};

  // Engine-thread-only state (core objects).
  fr::BasicBoard* board_ = nullptr;  // loaded or routed board
  void dropBoard(fr::BasicBoard* next, bool lastOfFamily);  // frees board_ unless it is `next`
  jrt::Array<int8_t>* dsnBytes_ = nullptr;
  fr::InteractiveSession* session_ = nullptr;  // interactive editing of board_
  std::string designName_;
  std::string dsnPath_;
  int64_t lastPublishMs_ = 0;
  int64_t lastAirlinesMs_ = 0;
};

}  // namespace frui
