#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Executes one autoroute connection, including necked retry and strict-DRC recovery. */
class AutorouteConnectionRouter : public virtual jrt::Object {
 private:
  BatchAutorouter* router;
  /** Set by route(..., planeOnly) when it aimed at the plane only. */
  bool aimedAtPlaneOnly = false;

 public:
  explicit AutorouteConnectionRouter(BatchAutorouter* router);

 private:
  /**
   * plane_as_priority: pads of the net already in the plane and at most plane_share_gap_um from
   * item, edge to edge, are accepted as destinations next to the plane.
   */
  void addSharablePads(Item* item, int32_t netNo, jrt::Set<Item*>* destinations);

 public:
  /**
   * Returns the items of items that are not fractured conduction areas, or items itself when
   * nothing else is left.
   */
  static jrt::Set<Item*>* withoutFracturedAreas(jrt::Set<Item*>* items);

 private:
  jrt::Set<Item*>* planeEscapeObstacles(
      Item* item, int32_t netNo, jrt::Set<Item*>* connectedSet, jrt::Set<Item*>* destinations);
  static double boxGap(IntBox* a, IntBox* b);

 public:
  /**
   * Routes one connection of item. A pad of a plane net is first routed to its planes on power
   * layers only, so that it drops into the plane through a via of its own.
   */
  virtual AutorouteAttemptResult* route(
      Item* item,
      int32_t routeNetNo,
      jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* ripupCosts,
      int32_t ripupPassNo);

 private:
  AutorouteAttemptResult* route(
      Item* item,
      int32_t routeNetNo,
      jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* ripupCosts,
      int32_t ripupPassNo,
      bool planeOnly);

  AutorouteAttemptResult* retryConnectionNecked(
      int32_t routeNetNo,
      AutorouteControl* originalControl,
      int32_t viaCosts,
      jrt::Set<Item*>* routeStartSet,
      jrt::Set<Item*>* routeDestSet,
      jrt::TreeSet<Item*>* rippedItemList,
      jrt::Map<Item*, int32_t>* ripupCosts,
      int32_t ripupPassNo,
      TimeLimit* timeLimit,
      bool planned,
      bool quiet);

  /**
   * Rejects the route when its new items carry clearance violations and restores the board from
   * the snapshot taken before it. Without a snapshot the route is kept.
   * PORT NOTE: the snapshot is whatever BasicBoard::serialize returns (jrt::Array<int8_t>* in
   * the mechanical mapping).
   */
  AutorouteAttemptResult* applyStrictDrcAfterRoute(
      int32_t routeNetNo,
      int32_t maxItemIdBefore,
      jrt::Array<int8_t>* boardSnapshotBeforeRoute,
      int32_t ripupPassNo);
};

}  // namespace fr
