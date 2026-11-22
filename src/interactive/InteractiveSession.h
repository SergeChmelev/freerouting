#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

class Route;
class MoveComponent;

/**
 * Interactive editing of a RoutingBoard without Swing: a compact equivalent of the Java GUI states
 * RouteMenuState / RouteState / DynamicRouteState / StitchRouteState and DragMenuState / DragState /
 * DragItemState / MakeSpaceState (gui/interactive), driven by mouse events in board coordinates.
 * Settings default to the Java WorkspaceSettings defaults.
 *
 * Not thread-safe: like the rest of the core, use it from one thread (the engine thread).
 */
class InteractiveSession : public virtual jrt::Object {
 public:
  enum class Mode : int32_t { Select, Route, Drag };

  explicit InteractiveSession(RoutingBoard* board);

  // --- WorkspaceSettings defaults
  bool pushEnabled = true;
  bool dragComponentsEnabled = true;
  bool selectOnAllVisibleLayers = true;
  bool isStitchRoute = false;
  bool viaSnapToSmdCenter = true;
  bool automaticNeckdown = true;
  bool highlightRoutingObstacle = false;
  int32_t tracePullTightRegionWidth = jrt::Integer::MAX_VALUE;
  int32_t tracePullTightAccuracy = 500;

  RoutingBoard* board() { return board_; }
  Mode mode() { return mode_; }
  void setMode(Mode mode);            // cancels a running route/drag
  int32_t layer() { return layer_; }  // current (routing) layer
  /** Java InteractiveState.changeLayerAction / RouteState.changeLayerAction. */
  bool changeLayer(int32_t newLayer);

  // --- mouse events (board coordinates). Each returns true if the board may have changed.
  bool mousePressed(double x, double y);   // drag mode: start dragging
  bool mouseDragged(double x, double y);   // drag mode: move
  bool mouseReleased();                    // drag mode: finish
  bool mouseMoved(double x, double y);     // dynamic route: follow the mouse
  bool leftClicked(double x, double y);    // route mode: start / finish (stitch: add corner)
  bool complete();                         // Enter / space: finish the current action
  bool cancel();                           // Esc: abort the current route

  bool undo();
  bool redo();

  // --- overlay / status for front ends
  bool routing() { return route_ != nullptr; }
  bool dragging() { return dragItem_ != nullptr || makeSpace_ != nullptr; }
  Route* route() { return route_; }
  std::string statusMessage() { return message_; }

 private:
  // RouteState.getInstance
  bool startRoute(FloatPoint* floatLocation);
  Item* startOk(IntPoint* location);
  static jrt::Array<int32_t>* getRouteNetNumbersAtTiePin(Pin* pin, int32_t layer);
  bool addCorner(FloatPoint* location);  // RouteState.addCorner
  void endRoute();
  // DragState
  bool startDrag(FloatPoint* location);
  bool dragItemMoveTo(FloatPoint* toLocation);   // DragItemState.moveTo
  bool makeSpaceMoveTo(FloatPoint* toLocation);  // MakeSpaceState.moveTo
  void endDrag();

  // GuiBoardRoutingSettings (manualRuleSelection = false)
  int32_t getTraceHalfwidth(int32_t netNumber, int32_t layer);
  bool isActiveRoutingLayer(int32_t netNumber, int32_t layer);
  int32_t getTraceClearanceClass(int32_t netNumber);
  ViaRule* getViaRule(int32_t netNumber);

  RoutingBoard* board_;
  Mode mode_ = Mode::Select;
  int32_t layer_ = 0;
  std::string message_;

  // route state
  Route* route_ = nullptr;
  bool stitchRoute_ = false;
  // drag state
  Item* dragItem_ = nullptr;     // DragItemState.itemToMove
  Route* makeSpace_ = nullptr;   // MakeSpaceState.route
  FloatPoint* previousLocation_ = nullptr;
  bool somethingDragged_ = false;
};

}  // namespace fr
