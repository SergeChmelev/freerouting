#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Types of named algorithms supported by Freerouting. */
enum class NamedAlgorithmType : int32_t { ROUTER, OPTIMIZER };

inline std::string NamedAlgorithmType_name(NamedAlgorithmType e) {
  switch (e) {
    case NamedAlgorithmType::ROUTER: return "ROUTER";
    case NamedAlgorithmType::OPTIMIZER: return "OPTIMIZER";
  }
  return "";
}
inline std::string NamedAlgorithmType_toString(NamedAlgorithmType e) { return NamedAlgorithmType_name(e); }
inline jrt::Array<NamedAlgorithmType>* NamedAlgorithmType_values() {
  auto* a = new (jrt::kOp) jrt::Array<NamedAlgorithmType>(2);
  for (int32_t i = 0; i < 2; ++i) (*a)[i] = static_cast<NamedAlgorithmType>(i);
  return a;
}
inline NamedAlgorithmType NamedAlgorithmType_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 2; ++i) {
    auto e = static_cast<NamedAlgorithmType>(i);
    if (NamedAlgorithmType_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant NamedAlgorithmType." + s);
}

}  // namespace fr
