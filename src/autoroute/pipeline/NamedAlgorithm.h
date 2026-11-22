#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Interface for named algorithms, e.g. "Freerouting Classic Fast Autorouter v1.0" for Autorouter,
 * "Freerouting Classic Optimizer v1.0" for route optimization. (Java: sealed, permits
 * BatchAutorouter and BatchOptimizer.)
 *
 * PORT NOTE: Java `protected` members are also package-visible (AutorouteBatchLoop etc. read
 * router.board / router.thread / router.settings), so they are public here. Serializable and
 * `transient` are dropped.
 */
class NamedAlgorithm : public virtual jrt::Object {
 public:
  StoppableThread* thread;
  jrt::List<BoardSnapshotEventListener*>* boardSnapshotEventListeners;  // Java init: new ArrayList<>() (set in constructor)
  jrt::List<BoardUpdatedEventListener*>* boardUpdatedEventListeners;  // Java init: new ArrayList<>() (set in constructor)
  jrt::List<TaskStateChangedEventListener*>* taskStateChangedEventListeners;  // Java init: new ArrayList<>() (set in constructor)
  RouterSettings* settings;
  RoutingBoard* board;

 protected:
  NamedAlgorithm(StoppableThread* thread, RoutingBoard* board, RouterSettings* settings);

 public:
  /** Returns the id of the algorithm. */
  virtual std::string getId() = 0;
  /** Returns the name of the algorithm. */
  virtual std::string getName() = 0;
  /** Returns the version of the algorithm. */
  virtual std::string getVersion() = 0;
  /** Returns the description of the algorithm. */
  virtual std::string getDescription() = 0;
  /** Returns the type of the algorithm. */
  virtual NamedAlgorithmType getType() = 0;

  /** Adds a listener for board snapshot events. */
  virtual void addBoardSnapshotEventListener(BoardSnapshotEventListener* listener);
  /** Fires a board snapshot event. */
  virtual void fireBoardSnapshotEvent(RoutingBoard* board);
  /** Copies listeners onto another algorithm stage so a fallback attempt reports the same events. */
  virtual void copyListenersFrom(NamedAlgorithm* source);
  /** Adds a listener for board updated events. */
  virtual void addBoardUpdatedEventListener(BoardUpdatedEventListener* listener);
  /** Returns true when someone listens to board updated events. */
  virtual bool hasBoardUpdatedEventListeners();
  /** Returns true when a listener reads the board statistics of board updated events. */
  virtual bool hasBoardStatisticsReaders();
  /** Fires a board updated event (e.g. after a route has been added). */
  virtual void fireBoardUpdatedEvent(
      BoardStatistics* boardStatistics, RouterCounters* routerCounters, RoutingBoard* board);
  /** Adds a listener for task state changed events. */
  virtual void addTaskStateChangedEventListener(TaskStateChangedEventListener* listener);
  /** Fires a task state changed event. */
  virtual void fireTaskStateChangedEvent(TaskStateChangedEvent* event);
};

}  // namespace fr
