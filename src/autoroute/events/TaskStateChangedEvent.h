#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * TaskStateChangedEvent.
 * PORT NOTE: java.util.EventObject base is folded in (field `source`, getSource()).
 */
class TaskStateChangedEvent : public virtual jrt::Object {
 protected:
  jrt::Object* source;

 private:
  TaskState tastState;
  int32_t passNumber;
  std::string boardHash;

 public:
  TaskStateChangedEvent(jrt::Object* source, TaskState taskState, int32_t passNumber,
                        const std::string& boardHash)
      : source(source), tastState(taskState), passNumber(passNumber), boardHash(boardHash) {}

  virtual jrt::Object* getSource() { return source; }

  virtual TaskState getTaskState() { return tastState; }

  virtual int32_t getPassNumber() { return passNumber; }

  virtual std::string getBoardHash() { return boardHash; }
};

}  // namespace fr
