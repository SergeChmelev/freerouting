// Port of app.freerouting.rules.MeanderPatternType
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Type of length-tuning pattern supported for trace length matching. */
enum class MeanderPatternType : int32_t { SINGLE_TRACK, DIFF_PAIR, DIFF_PAIR_SKEW };

inline std::string MeanderPatternType_name(MeanderPatternType e) {
  switch (e) {
    case MeanderPatternType::SINGLE_TRACK:
      return "SINGLE_TRACK";
    case MeanderPatternType::DIFF_PAIR:
      return "DIFF_PAIR";
    case MeanderPatternType::DIFF_PAIR_SKEW:
      return "DIFF_PAIR_SKEW";
  }
  return "";
}

/** Java toString(): the lower-case name. */
inline std::string MeanderPatternType_toString(MeanderPatternType e) {
  return jrt::toLowerCase(MeanderPatternType_name(e));
}

inline jrt::Array<MeanderPatternType>* MeanderPatternType_values() {
  return jrt::Array<MeanderPatternType>::of({MeanderPatternType::SINGLE_TRACK,
                                             MeanderPatternType::DIFF_PAIR,
                                             MeanderPatternType::DIFF_PAIR_SKEW});
}

inline MeanderPatternType MeanderPatternType_valueOf(const std::string& name) {
  for (MeanderPatternType e : *MeanderPatternType_values()) {
    if (MeanderPatternType_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant MeanderPatternType." + name);
}

/**
 * Parses a meander pattern type from a string in a case-insensitive manner. Returns SINGLE_TRACK
 * if null, blank or unrecognised.
 */
inline MeanderPatternType MeanderPatternType_parse(const std::optional<std::string>& name) {
  if (!name.has_value() || jrt::strip(*name).empty()) {
    return MeanderPatternType::SINGLE_TRACK;
  }
  std::string normalized = jrt::replace(jrt::toUpperCase(jrt::trim(*name)), "-", "_");
  if (normalized == "DIFF_PAIR" || normalized == "DIFFPAIR" || normalized == "DIFFERENTIAL_PAIR" ||
      normalized == "DIFFERENTIAL") {
    return MeanderPatternType::DIFF_PAIR;
  }
  if (normalized == "DIFF_PAIR_SKEW" || normalized == "DIFFPAIR_SKEW" || normalized == "SKEW" ||
      normalized == "DIFFERENTIAL_PAIR_SKEW") {
    return MeanderPatternType::DIFF_PAIR_SKEW;
  }
  return MeanderPatternType::SINGLE_TRACK;
}

}  // namespace fr
