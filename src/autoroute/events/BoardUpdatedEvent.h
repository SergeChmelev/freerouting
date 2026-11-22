#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * BoardUpdatedEvent.
 * PORT NOTE: java.util.EventObject base is folded in (field `source`, getSource()).
 */
class BoardUpdatedEvent : public virtual jrt::Object {
 protected:
  jrt::Object* source;

 private:
  BoardStatistics* boardStatistics;
  RouterCounters* routerCounters;
  RoutingBoard* board;

 public:
  BoardUpdatedEvent(jrt::Object* source, BoardStatistics* statistics,
                    RouterCounters* routerCounters, RoutingBoard* board)
      : source(source), boardStatistics(statistics), routerCounters(routerCounters), board(board) {}

  virtual jrt::Object* getSource() { return source; }

  virtual BoardStatistics* getBoardStatistics() { return boardStatistics; }

  virtual RouterCounters* getRouterCounters() { return routerCounters; }

  virtual RoutingBoard* getBoard() { return board; }
};

}  // namespace fr
