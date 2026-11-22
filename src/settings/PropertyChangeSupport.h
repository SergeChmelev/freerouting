#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "settings/PropertyChangeListener.h"  // complete type needed by the inline ArrayList

namespace fr {

/**
 * Pure-Java property change notification helper (replaces java.beans.PropertyChangeSupport),
 * preserving Objects.equals short-circuiting and named-property listener semantics.
 */
// PORT NOTE: single-threaded port: CopyOnWriteArrayList -> jrt::ArrayList (Java iterates a
// snapshot; firePropertyChange must iterate a copy if a listener can (un)register while firing),
// ConcurrentHashMap -> jrt::ConcurrentHashMap. Event values are carried as optional strings (see
// PropertyChangeEvent); build them with valueOf().
class PropertyChangeSupport : public virtual jrt::Object {
 public:
  /** Constructs a PropertyChangeSupport object; throws NullPointerException for null. */
  explicit PropertyChangeSupport(jrt::Object* sourceBean);

  /** Adds a listener for all properties; ignored if null. */
  void addPropertyChangeListener(PropertyChangeListener* listener);
  /** Adds a listener for a specific property; ignored if either argument is null. */
  void addPropertyChangeListener(const std::optional<std::string>& propertyName,
                                 PropertyChangeListener* listener);
  /** Removes a listener from the all-properties list; ignored if null. */
  void removePropertyChangeListener(PropertyChangeListener* listener);
  /** Removes a listener for a specific property. */
  void removePropertyChangeListener(const std::optional<std::string>& propertyName,
                                    PropertyChangeListener* listener);

  /**
   * Reports a bound property update. No event is fired if Objects.equals(oldValue, newValue).
   * (Java signature: firePropertyChange(String, Object, Object).)
   */
  void firePropertyChange(const std::optional<std::string>& propertyName,
                          const std::optional<std::string>& oldValue,
                          const std::optional<std::string>& newValue);
  /** Reports an int bound property update if oldValue != newValue. */
  void firePropertyChange(const std::optional<std::string>& propertyName, int32_t oldValue,
                          int32_t newValue);
  /** Reports a boolean bound property update if oldValue != newValue. */
  void firePropertyChange(const std::optional<std::string>& propertyName, bool oldValue,
                          bool newValue);
  /** Fires a property change event to all matching listeners. */
  void firePropertyChange(PropertyChangeEvent* event);

  // PORT NOTE: helpers that turn a Java boxed value into the event value representation.
  template <class T>
  static std::optional<std::string> valueOf(const std::optional<T>& v) {
    if (!v.has_value()) return std::nullopt;
    return jrt::str(*v);
  }
  /** Reference-typed values (arrays): null or the identity-based Object::toString(). */
  static std::optional<std::string> valueOf(jrt::Object* o) {
    if (o == nullptr) return std::nullopt;
    return o->toString();
  }

 private:
  jrt::Object* sourceBean;
  jrt::ArrayList<PropertyChangeListener*>* listeners =
      new (jrt::kBoard) jrt::ArrayList<PropertyChangeListener*>();
  jrt::ConcurrentHashMap<std::string, jrt::ArrayList<PropertyChangeListener*>*>* namedListeners =
      new (jrt::kBoard) jrt::ConcurrentHashMap<std::string, jrt::ArrayList<PropertyChangeListener*>*>();
};

}  // namespace fr
