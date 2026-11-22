#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * BoardSnapshotEvent.
 * PORT NOTE: java.util.EventObject base is folded in (field `source`, getSource()).
 */
class BoardSnapshotEvent : public virtual jrt::Object {
 protected:
  jrt::Object* source;

 private:
  RoutingBoard* board;

 public:
  BoardSnapshotEvent(jrt::Object* source, RoutingBoard* board) : source(source), board(board) {}

  virtual jrt::Object* getSource() { return source; }

  virtual RoutingBoard* getBoard() { return board; }
};

}  // namespace fr
