// Port of app.freerouting.autoroute.pipeline.NamedAlgorithm
#include "autoroute/pipeline/NamedAlgorithm.h"

#include "autoroute/events/BoardSnapshotEvent.h"
#include "autoroute/events/BoardSnapshotEventListener.h"
#include "autoroute/events/BoardUpdatedEvent.h"
#include "autoroute/events/BoardUpdatedEventListener.h"
#include "autoroute/events/TaskStateChangedEvent.h"
#include "autoroute/events/TaskStateChangedEventListener.h"
#include "autoroute/pipeline/TaskState.h"

namespace fr {

NamedAlgorithm::NamedAlgorithm(StoppableThread* thread, RoutingBoard* board, RouterSettings* settings)
    : thread(thread),
      boardSnapshotEventListeners(new jrt::ArrayList<BoardSnapshotEventListener*>()),
      boardUpdatedEventListeners(new jrt::ArrayList<BoardUpdatedEventListener*>()),
      taskStateChangedEventListeners(new jrt::ArrayList<TaskStateChangedEventListener*>()),
      settings(settings),
      board(board) {}

void NamedAlgorithm::addBoardSnapshotEventListener(BoardSnapshotEventListener* listener) {
  boardSnapshotEventListeners->add(listener);
}

void NamedAlgorithm::fireBoardSnapshotEvent(RoutingBoard* board) {
  BoardSnapshotEvent* event = new BoardSnapshotEvent(this, board);
  for (BoardSnapshotEventListener* listener : *boardSnapshotEventListeners) {
    listener->onBoardSnapshotEvent(event);
  }
}

void NamedAlgorithm::copyListenersFrom(NamedAlgorithm* source) {
  this->boardSnapshotEventListeners->addAll(source->boardSnapshotEventListeners);
  this->boardUpdatedEventListeners->addAll(source->boardUpdatedEventListeners);
  this->taskStateChangedEventListeners->addAll(source->taskStateChangedEventListeners);
}

void NamedAlgorithm::addBoardUpdatedEventListener(BoardUpdatedEventListener* listener) {
  boardUpdatedEventListeners->add(listener);
}

bool NamedAlgorithm::hasBoardUpdatedEventListeners() {
  return !boardUpdatedEventListeners->isEmpty();
}

bool NamedAlgorithm::hasBoardStatisticsReaders() {
  for (BoardUpdatedEventListener* listener : *boardUpdatedEventListeners) {
    if (listener->readsBoardStatistics()) {
      return true;
    }
  }
  return false;
}

void NamedAlgorithm::fireBoardUpdatedEvent(
    BoardStatistics* boardStatistics, RouterCounters* routerCounters, RoutingBoard* board) {
  BoardUpdatedEvent* event = new BoardUpdatedEvent(this, boardStatistics, routerCounters, board);
  for (BoardUpdatedEventListener* listener : *boardUpdatedEventListeners) {
    listener->onBoardUpdatedEvent(event);
  }
}

void NamedAlgorithm::addTaskStateChangedEventListener(TaskStateChangedEventListener* listener) {
  taskStateChangedEventListeners->add(listener);
}

void NamedAlgorithm::fireTaskStateChangedEvent(TaskStateChangedEvent* event) {
  for (TaskStateChangedEventListener* listener : *taskStateChangedEventListeners) {
    listener->onTaskStateChangedEvent(event);
  }
}

}  // namespace fr
