// Port of app.freerouting.datastructures.Observers
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Interface to observe changes on objects for synchronisation purposes. T is the full C++ element
 * type (a pointer, e.g. Observers<Item*>).
 */
template <class T>
class Observers : public virtual jrt::Object {
 public:
  /** Tell the observers about the deletion of object. */
  virtual void notifyDeleted(T object) = 0;

  /** Notify the observers, that they can synchronize the changes on object. */
  virtual void notifyChanged(T object) = 0;

  /** Enable the observers to synchronize the new created item. */
  virtual void notifyNew(T object) = 0;

  /** Starts notifying the observers. */
  virtual void activate() = 0;

  /** Ends notifying the observers. */
  virtual void deactivate() = 0;

  /** Returns, if the observer is activated. */
  virtual bool isActive() = 0;
};

}  // namespace fr
