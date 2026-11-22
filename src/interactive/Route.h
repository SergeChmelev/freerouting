#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

class Route_TargetPoint : public virtual jrt::Object {
 public:
  Route_TargetPoint(FloatPoint* location, Item* item) : location(location), item(item) {}
  FloatPoint* location;
  Item* item;
};

/**
 * Interactive routing of one connection with push-and-shove (port of gui.interactive.Route,
 * without the Swing drawing: draw() is replaced by the overlay accessors below).
 */
class Route : public virtual jrt::Object {
 public:
  Route(Point* startCorner, int32_t layerIndex, jrt::Array<int32_t>* penHalfWidthArr,
        jrt::Array<bool>* layerActiveArr, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
        ViaRule* viaRuleValue, bool pushEnabled, int32_t traceTidyWidthValue,
        int32_t pullTightAccuracyValue, Item* startItem, jrt::Set<Item*>* targetSetValue,
        RoutingBoard* routingBoard, bool stitchMode, bool neckdown, bool snapToSmdCenter,
        bool highlightShoveFailingObstacleValue);

  /** Appends a corner (shoving obstacles). Returns true if the route reached its target. */
  bool nextCorner(FloatPoint* corner);
  /** Changes to toLayer by inserting a via at the last corner. */
  bool changeLayer(int32_t toLayer);
  /** The layer of the nearest target item closest to the current layer. */
  int32_t nearestTargetLayer();
  Point* getLastCorner() { return prevCorner; }
  bool isLayerActive(int32_t layerIndex);
  void calcNearestTargetPoint(FloatPoint* fromPoint);

  // PORT EXTENSION: overlay data for front ends (replaces Route.draw).
  FloatPoint* getNearestTargetPoint() { return nearestTargetPoint; }
  Item* getNearestTargetItem() { return nearestTargetItem; }
  Item* getShoveFailingObstacle() { return shoveFailingObstacle; }
  int32_t getLayer() { return layer; }
  int32_t getPenHalfWidth(int32_t layerIndex) { return (*penHalfWidthArr)[layerIndex]; }

  jrt::Array<int32_t>* netNumbers;

 private:
  static constexpr int32_t CHECK_FORCED_TRACE_TIME_LIMIT = 3000;
  static constexpr int32_t PULL_TIGHT_TIME_LIMIT = 2000;

  bool snapToSmdCenter(int32_t layerIndex);
  bool connectToTarget(IntPoint* fromPoint);
  bool connect(Point* fromPoint, IntPoint* toPoint);
  jrt::Array<Point*>* angledConnection(Point* fromPoint, Point* toPoint);
  void calculateTargetPointsAndAreas();
  void setShoveFailingObstacle(Item* item);
  Point* tryNeckdownAtStart(IntPoint* toCorner);
  Point* tryNeckdownAtEnd(Point* fromCorner, Point* toCorner);

  Item* startItem;
  jrt::Set<Item*>* targetSet;
  jrt::Array<int32_t>* penHalfWidthArr;
  jrt::Array<bool>* layerActive;
  int32_t clearanceClass;
  ViaRule* viaRule;
  int32_t maxShoveTraceRecursionDepth;
  int32_t maxShoveViaRecursionDepth;
  int32_t maxSpringOverRecursionDepth;
  int32_t traceTidyWidth;
  int32_t pullTightAccuracy;
  RoutingBoard* board;
  bool isStitchMode;
  bool withNeckdown;
  bool viaSnapToSmdCenter;
  bool highlightShoveFailingObstacle;
  int32_t pullTightTimeLimit;
  Point* prevCorner;
  int32_t layer;
  jrt::Collection<Route_TargetPoint*>* targetPoints = nullptr;
  jrt::Collection<Item*>* targetTracesAndAreas = nullptr;
  FloatPoint* nearestTargetPoint = nullptr;
  Item* nearestTargetItem = nullptr;
  Item* shoveFailingObstacle = nullptr;
};

}  // namespace fr
