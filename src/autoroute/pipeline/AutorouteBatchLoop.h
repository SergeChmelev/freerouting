#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Owns fanout, autoroute-pass, stagnation, and final-board lifecycle decisions.
 *
 * PORT NOTE: java.time.Instant locals (stage start times) become int64_t epoch milliseconds
 * (jrt::System::currentTimeMillis()); Duration.between(a, Instant.now()).toMillis() becomes the
 * difference of the two.
 */
class AutorouteBatchLoop : public virtual jrt::Object {
 private:
  BatchAutorouter* router;

 public:
  explicit AutorouteBatchLoop(BatchAutorouter* router);

  virtual bool run();

 private:
  int32_t calculateIncompleteCount(RoutingBoard* board);
  void removeTails(Item_StopConnectionOption option);
  bool autoroutePass(int32_t passNo);
  std::string buildUnroutedConnectionsReport();
  void fireBoardSnapshotEvent(RoutingBoard* board);
  void stopFor(BatchAutorouter_StopReason reason);
};

}  // namespace fr
