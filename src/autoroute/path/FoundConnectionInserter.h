#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A terminal of the inserted connection (Java: private record TerminalAnchor). */
class FoundConnectionInserter_TerminalAnchor : public virtual jrt::Object {
 public:
  FoundConnectionInserter_TerminalAnchor(jrt::Set<Item*>* component, Point* point, int32_t layer,
                                         bool fanoutEnd)
      : component_(component), point_(point), layer_(layer), fanoutEnd_(fanoutEnd) {}

  jrt::Set<Item*>* component() { return component_; }
  Point* point() { return point_; }
  int32_t layer() { return layer_; }
  bool fanoutEnd() { return fanoutEnd_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  jrt::Set<Item*>* component_;
  Point* point_;
  int32_t layer_;
  bool fanoutEnd_;
};

/** Inserts the traces and vias of the connection found by the autoroute algorithm. */
class FoundConnectionInserter final : public virtual jrt::Object {
 private:
  RoutingBoard* board;
  AutorouteControl* ctrl;
  IntPoint* lastCorner = nullptr;
  IntPoint* firstCorner = nullptr;

  /** Creates a new instance of FoundConnectionInserter. */
  FoundConnectionInserter(RoutingBoard* board, AutorouteControl* ctrl)
      : board(board), ctrl(ctrl) {}

 public:
  /** Creates a new instance of FoundConnectionInserter. Returns null if the insertion did not
   * succeed. */
  static FoundConnectionInserter* getInstance(FoundConnectionLocator* connection,
                                              RoutingBoard* board, AutorouteControl* ctrl);

  /** Inserts the segments of a planned connection. Returns null if the insertion did not
   * succeed. */
  static FoundConnectionInserter* insertSegments(RoutingBoard* board, AutorouteControl* ctrl,
                                                 Item* startItem, Item* targetItem,
                                                 int32_t startLayer, int32_t targetLayer,
                                                 jrt::Array<PlannedConnection_Segment*>* segments);

 private:
  static std::string formatPoint(Point* point);

  /** Inserts the trace by shoving aside obstacle traces and vias. Returns false, if that was not
   * possible for the whole trace. */
  bool insertTrace(PlannedConnection_Segment* trace);

  void removeTraceStubs(PlannedConnection_Segment* trace);

  static bool isContinuousPlan(jrt::Array<PlannedConnection_Segment*>* segments,
                               int32_t layerCount);

  FoundConnectionInserter_TerminalAnchor* captureTerminal(Item* item, IntPoint* endpoint,
                                                          int32_t layer, bool fanoutEnd);

  jrt::Set<Item*>* resolveTerminal(FoundConnectionInserter_TerminalAnchor* anchor);

  bool connectsTerminals(FoundConnectionInserter_TerminalAnchor* start,
                         FoundConnectionInserter_TerminalAnchor* target);

  jrt::Set<Item*>* terminalComponent(Item* start);

  bool insertFanoutMicroNeckdown(Point* okPoint, Point* targetPoint, int32_t layer,
                                 jrt::Array<int32_t>* netNumbers, Pin* startPin, Pin* endPin);

  bool reachesNeckdownZone(Point* a, Point* b, int32_t layer, Pin* pin);

  double pinNeckDownDistance(Pin* pin, int32_t layer);

 public:
  /** (Java: package-private) */
  bool insertNeckdown(Point* fromCorner, Point* toCorner, int32_t layer, Pin* startPin,
                      Pin* endPin);

 private:
  Point* tryNeckDown(Point* fromCorner, Point* toCorner, int32_t layer, Pin* pin, bool atStart);

  bool insertVia(Point* location, int32_t inputFromLayer, int32_t inputToLayer);

  bool shouldTraceFanoutDiagnostics();

  void traceFanoutDiagnostic(const std::string& event, const std::string& message);
};

}  // namespace fr
