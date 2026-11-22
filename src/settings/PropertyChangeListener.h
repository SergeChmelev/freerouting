#pragma once

#include <functional>
#include <utility>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Listener notified when a bound settings property changes. Replaces
 * java.beans.PropertyChangeListener.
 */
class PropertyChangeListener : public virtual jrt::Object {
 public:
  /** Called when a bound property is changed. */
  virtual void propertyChange(PropertyChangeEvent* evt) = 0;

  /** PORT NOTE: adapter for Java lambdas passed as PropertyChangeListener. */
  static PropertyChangeListener* of(std::function<void(PropertyChangeEvent*)> f);
};

namespace detail {
class LambdaPropertyChangeListener : public PropertyChangeListener {
 public:
  explicit LambdaPropertyChangeListener(std::function<void(PropertyChangeEvent*)> f)
      : f_(std::move(f)) {}
  void propertyChange(PropertyChangeEvent* evt) override { f_(evt); }

 private:
  std::function<void(PropertyChangeEvent*)> f_;
};
}  // namespace detail

inline PropertyChangeListener* PropertyChangeListener::of(
    std::function<void(PropertyChangeEvent*)> f) {
  return new detail::LambdaPropertyChangeListener(std::move(f));
}

}  // namespace fr
