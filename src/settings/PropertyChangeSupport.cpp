#include "settings/PropertyChangeSupport.h"

#include "settings/PropertyChangeEvent.h"

namespace fr {

PropertyChangeSupport::PropertyChangeSupport(jrt::Object* sourceBean) {
  if (sourceBean == nullptr) {
    throw jrt::NullPointerException("sourceBean");
  }
  this->sourceBean = sourceBean;
}

void PropertyChangeSupport::addPropertyChangeListener(PropertyChangeListener* listener) {
  if (listener != nullptr) {
    listeners->add(listener);
  }
}

void PropertyChangeSupport::addPropertyChangeListener(
    const std::optional<std::string>& propertyName, PropertyChangeListener* listener) {
  if (propertyName.has_value() && listener != nullptr) {
    namedListeners
        ->computeIfAbsent(*propertyName,
                          [](const std::string&) {
                            return new (jrt::kOp) jrt::ArrayList<PropertyChangeListener*>();
                          })
        ->add(listener);
  }
}

void PropertyChangeSupport::removePropertyChangeListener(PropertyChangeListener* listener) {
  if (listener != nullptr) {
    listeners->remove(listener);
  }
}

void PropertyChangeSupport::removePropertyChangeListener(
    const std::optional<std::string>& propertyName, PropertyChangeListener* listener) {
  if (propertyName.has_value() && listener != nullptr) {
    jrt::ArrayList<PropertyChangeListener*>* list = namedListeners->get(*propertyName);
    if (list != nullptr) {
      list->remove(listener);
    }
  }
}

void PropertyChangeSupport::firePropertyChange(const std::optional<std::string>& propertyName,
                                               const std::optional<std::string>& oldValue,
                                               const std::optional<std::string>& newValue) {
  if (oldValue == newValue) {
    return;
  }
  firePropertyChange(new (jrt::kBoard) PropertyChangeEvent(sourceBean, propertyName, oldValue, newValue));
}

void PropertyChangeSupport::firePropertyChange(const std::optional<std::string>& propertyName,
                                               int32_t oldValue, int32_t newValue) {
  if (oldValue == newValue) {
    return;
  }
  firePropertyChange(new (jrt::kBoard) PropertyChangeEvent(sourceBean, propertyName, jrt::str(oldValue),
                                             jrt::str(newValue)));
}

void PropertyChangeSupport::firePropertyChange(const std::optional<std::string>& propertyName,
                                               bool oldValue, bool newValue) {
  if (oldValue == newValue) {
    return;
  }
  firePropertyChange(new (jrt::kBoard) PropertyChangeEvent(sourceBean, propertyName, jrt::str(oldValue),
                                             jrt::str(newValue)));
}

void PropertyChangeSupport::firePropertyChange(PropertyChangeEvent* event) {
  if (event == nullptr || event->getOldValue() == event->getNewValue()) {
    return;
  }
  // CopyOnWriteArrayList iterates a snapshot.
  auto* listenersSnapshot = new (jrt::kOp) jrt::ArrayList<PropertyChangeListener*>(listeners);
  for (PropertyChangeListener* listener : *listenersSnapshot) {
    listener->propertyChange(event);
  }
  std::optional<std::string> propertyName = event->getPropertyName();
  if (propertyName.has_value()) {
    jrt::ArrayList<PropertyChangeListener*>* specific = namedListeners->get(*propertyName);
    if (specific != nullptr) {
      auto* specificSnapshot = new (jrt::kOp) jrt::ArrayList<PropertyChangeListener*>(specific);
      for (PropertyChangeListener* listener : *specificSnapshot) {
        listener->propertyChange(event);
      }
    }
  }
}

}  // namespace fr
