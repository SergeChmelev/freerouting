#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Java private record FixedCopper(int id, FixedState state). */
class DirectPinRouting_FixedCopper : public virtual jrt::Object {
  friend class DirectPinRouting;

 private:
  int32_t id_;
  FixedState state_;

 public:
  DirectPinRouting_FixedCopper(int32_t id, FixedState state) : id_(id), state_(state) {}
  int32_t id() { return id_; }
  FixedState state() { return state_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Same-layer pin connections before any escape via. Existing traces and vias stay fixed for the
 * pass, vias and rip-up are off, and a failed pin is left for the autorouter.
 * Ported from freerouting.net (Erik Gavriluk, Experimental Television Center).
 */
class DirectPinRouting : public virtual jrt::Object {
 private:
  DirectPinRouting() = default;

 public:
  /** A null or non-positive fanout.max_items is unlimited, as in BatchFanout. */
  static int32_t fanoutItemLimit(RouterSettings* settings);
  static BatchFanout_FanoutRunSummary* run(RoutingBoard* board, RouterSettings* settings, StoppableThread* thread);

 private:
  static AutorouteAttemptResult* routeOnLayer(
      RoutingBoard* board,
      RouterSettings* settings,
      StoppableThread* thread,
      Pin* pin,
      int32_t net,
      int32_t layer,
      bool neck);
  static int32_t neckHalfWidth(RoutingBoard* board, RouterSettings* settings);
  static bool touchesPlane(jrt::Set<Item*>* source);
  static bool onLayer(jrt::Set<Item*>* destinations, int32_t layer);
  static bool stopped(StoppableThread* thread);
};

}  // namespace fr
