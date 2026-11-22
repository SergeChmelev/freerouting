// Port of app.freerouting.board.state.BoardObserverAdaptor
#pragma once

#include "board/state/BoardObservers.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Empty adaptor implementing the BoardObservers interface. */
class BoardObserverAdaptor : public virtual BoardObservers {
 public:
  /** Tell the observers the deletion of item. */
  void notifyDeleted(Item* item) override {}

  /** Notify the observers, that they can synchronize the changes on item. */
  void notifyChanged(Item* item) override {}

  /** Enable the observers to synchronize the new created item. */
  void notifyNew(Item* item) override {}

  /** Enable the observers to synchronize the moved component. */
  void notifyMoved(Component* component) override {}

  /** Starts notifying the observers. */
  void activate() override { active = true; }

  /** Ends notifying the observers. */
  void deactivate() override { active = false; }

  /** Returns, if the observer is activated. */
  bool isActive() override { return active; }

 private:
  bool active = false;
};

}  // namespace fr
