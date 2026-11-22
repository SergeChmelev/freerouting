#pragma once

// PORT NOTE: not a Java type. Helpers that reproduce, field kind by field kind, what
// app.freerouting.util.ReflectionUtil.copyFields(source, target) does for the public fields of
// the settings classes (RouterSettings and its nested settings objects). Java does this through
// reflection; the C++ port spells it out in each class's copyNewValuesFrom(). Only the count of
// changed fields may differ from Java (Java compares boxed values by reference); the count is only
// used for debug logging and for RoutingJob::setSettings' "changed" flag.

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::settings_copy {

// Boxed scalar / String field (Integer, Long, Float, Double, Boolean, String): copied when the
// source value is non-null; counted when it differs from the target value.
template <class T>
inline int32_t value(std::optional<T>& target, const std::optional<T>& source) {
  if (!source.has_value()) return 0;
  if (target.has_value() && *target == *source) return 0;
  target = source;
  return 1;
}

// Enum field: copied (and counted) whenever the source value is non-null.
template <class E>
inline int32_t enumValue(std::optional<E>& target, const std::optional<E>& source) {
  if (!source.has_value()) return 0;
  target = source;
  return 1;
}

// Array of primitives or Strings: the source array reference is assigned (shared, like Java)
// whenever it is non-null, and counted.
template <class T>
inline int32_t array(jrt::Array<T>*& target, jrt::Array<T>* source) {
  if (source == nullptr) return 0;
  target = source;
  return 1;
}

// Collection field (List): a new ArrayList copy of the source when it is non-null, counted.
template <class T>
inline int32_t list(jrt::List<T>*& target, jrt::List<T>* source) {
  if (source == nullptr) return 0;
  target = new (jrt::kOp) jrt::ArrayList<T>(static_cast<jrt::Collection<T>*>(source));
  return 1;
}

// Nested settings object: created in the target when missing, then merged recursively.
template <class S>
inline int32_t nested(S*& target, S* source) {
  if (source == nullptr) return 0;
  if (target == nullptr) target = new (jrt::kOp) S();
  return target->copyNewValuesFrom(source);
}

// Array of settings objects (LayerSettings[]): if the target array is at least as long, the
// source elements are merged into the existing target elements (missing ones created);
// otherwise a new target array of the source length is allocated with fresh merged elements.
// Counts the source array length in both cases.
template <class S>
inline int32_t objectArray(jrt::Array<S*>*& target, jrt::Array<S*>* source) {
  if (source == nullptr) return 0;
  int32_t targetLength = target != nullptr ? target->length : 0;
  if (targetLength >= source->length) {
    for (int32_t i = 0; i < source->length; i++) {
      if ((*source)[i] != nullptr) {
        if ((*target)[i] == nullptr) (*target)[i] = new (jrt::kOp) S();
        (*target)[i]->copyNewValuesFrom((*source)[i]);
      }
    }
  } else {
    auto* targetArray = new (jrt::kOp) jrt::Array<S*>(source->length);
    for (int32_t i = 0; i < source->length; i++) {
      if ((*source)[i] != nullptr) {
        S* targetElement = new (jrt::kOp) S();
        targetElement->copyNewValuesFrom((*source)[i]);
        (*targetArray)[i] = targetElement;
      }
    }
    target = targetArray;
  }
  return source->length;
}

}  // namespace fr::settings_copy
