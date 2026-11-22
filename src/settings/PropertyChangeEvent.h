#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Pure-Java property change event emitted by settings models when a property value changes.
 * Replaces java.beans.PropertyChangeEvent.
 */
// PORT NOTE: Java carries the old/new values as Object (boxed Integer/Boolean/String/String[]).
// The port carries them as their string representation: std::nullopt for null, jrt::str(value)
// for boxed scalars/strings, and Object::toString() (identity-based) for arrays, so that
// "Objects.equals(old, new)" keeps Java's result for every value RouterSettings fires. The only
// consumers are GUI listeners (no-ops in the headless port). Use PropertyChangeSupport::valueOf.
class PropertyChangeEvent : public virtual jrt::Object {
 public:
  /**
   * Constructs a new PropertyChangeEvent.
   *
   * @param source the bean that fired the event (must not be null)
   * @param propertyName the programmatic name of the property that was changed
   */
  PropertyChangeEvent(jrt::Object* source, const std::optional<std::string>& propertyName,
                      const std::optional<std::string>& oldValue,
                      const std::optional<std::string>& newValue)
      : source(source), propertyName(propertyName), oldValue(oldValue), newValue(newValue) {
    if (source == nullptr) {
      throw jrt::IllegalArgumentException("null source");
    }
  }

  /** Returns the object on which the event initially occurred. */
  jrt::Object* getSource() { return source; }
  /** Returns the programmatic name of the property that was changed. */
  std::optional<std::string> getPropertyName() { return propertyName; }
  /** Returns the old value of the property, or null if unknown. */
  std::optional<std::string> getOldValue() { return oldValue; }
  /** Returns the new value of the property, or null if unknown. */
  std::optional<std::string> getNewValue() { return newValue; }

 private:
  jrt::Object* source;
  std::optional<std::string> propertyName;
  std::optional<std::string> oldValue;
  std::optional<std::string> newValue;
};

}  // namespace fr
