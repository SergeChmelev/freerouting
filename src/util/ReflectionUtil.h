// Port of app.freerouting.util.ReflectionUtil
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Utility methods for reflective field access and copying between settings objects.
 *
 * PORT NOTE: C++ has no reflection (and Gson @SerializedName is out of scope). Both methods throw
 * jrt::UnsupportedOperationException. Callers (RouterSettings.applyNewValuesFrom,
 * GlobalSettings, CliSettings, EnvironmentVariablesSource) must implement the copy explicitly per
 * field with the same rule as copyFields: copy a public non-static field only if the source value
 * is non-null (nullable fields) / not the Java default value (primitive fields), recursing into
 * nested settings objects, and count the copied fields.
 */
class ReflectionUtil final {
 public:
  /** Sets a nested field value on an object using a dot-separated property path. */
  [[noreturn]] static void setFieldValue(jrt::Object* obj, const std::string& propertyName,
                                         jrt::Object* newValue) {
    throw jrt::UnsupportedOperationException("ReflectionUtil.setFieldValue (no reflection in C++): " +
                                             propertyName);
  }

  /** Copy all non-null, and non-default fields from one object to another recursively. */
  [[noreturn]] static int32_t copyFields(jrt::Object* source, jrt::Object* target) {
    throw jrt::UnsupportedOperationException("ReflectionUtil.copyFields (no reflection in C++)");
  }

 private:
  ReflectionUtil() = delete;
};

}  // namespace fr
