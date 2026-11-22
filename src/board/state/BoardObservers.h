// Port of app.freerouting.board.state.BoardObservers
#pragma once

#include "datastructures/Observers.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Observers of board changes (Java: extends Observers<Item>). */
class BoardObservers : public virtual Observers<Item*> {
 public:
  /** Enable the observers to synchronize the moved component. */
  virtual void notifyMoved(Component* component) = 0;
};

}  // namespace fr
