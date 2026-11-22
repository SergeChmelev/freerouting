// Port of app.freerouting.rules.DifferentialPairDetector
#pragma once

#include <regex>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Logical polarity of a differential net. */
enum class DifferentialPairDetector_Polarity : int32_t { POSITIVE, NEGATIVE };

inline std::string DifferentialPairDetector_Polarity_name(DifferentialPairDetector_Polarity e) {
  return e == DifferentialPairDetector_Polarity::POSITIVE ? "POSITIVE" : "NEGATIVE";
}
inline std::string DifferentialPairDetector_Polarity_toString(DifferentialPairDetector_Polarity e) {
  return DifferentialPairDetector_Polarity_name(e);
}
inline jrt::Array<DifferentialPairDetector_Polarity>* DifferentialPairDetector_Polarity_values() {
  return jrt::Array<DifferentialPairDetector_Polarity>::of(
      {DifferentialPairDetector_Polarity::POSITIVE, DifferentialPairDetector_Polarity::NEGATIVE});
}
inline DifferentialPairDetector_Polarity DifferentialPairDetector_Polarity_valueOf(
    const std::string& name) {
  for (auto e : *DifferentialPairDetector_Polarity_values()) {
    if (DifferentialPairDetector_Polarity_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant Polarity." + name);
}

/** Style of differential pair naming suffix. The ordinal is the preference order. */
enum class DifferentialPairDetector_SuffixStyle : int32_t {
  P_N,
  DP_DM,
  D_PLUS_MINUS,
  PLUS_MINUS,
  POS_NEG,
  BARE_D_PLUS_MINUS,
  /** P/N right after the base name (ETH_TXP/ETH_TXN); only used by detect(). */
  P_N_UNDELIMITED
};

inline std::string DifferentialPairDetector_SuffixStyle_name(
    DifferentialPairDetector_SuffixStyle e) {
  using E = DifferentialPairDetector_SuffixStyle;
  switch (e) {
    case E::P_N:
      return "P_N";
    case E::DP_DM:
      return "DP_DM";
    case E::D_PLUS_MINUS:
      return "D_PLUS_MINUS";
    case E::PLUS_MINUS:
      return "PLUS_MINUS";
    case E::POS_NEG:
      return "POS_NEG";
    case E::BARE_D_PLUS_MINUS:
      return "BARE_D_PLUS_MINUS";
    case E::P_N_UNDELIMITED:
      return "P_N_UNDELIMITED";
  }
  return "";
}
inline std::string DifferentialPairDetector_SuffixStyle_toString(
    DifferentialPairDetector_SuffixStyle e) {
  return DifferentialPairDetector_SuffixStyle_name(e);
}
inline jrt::Array<DifferentialPairDetector_SuffixStyle>* DifferentialPairDetector_SuffixStyle_values() {
  using E = DifferentialPairDetector_SuffixStyle;
  return jrt::Array<E>::of({E::P_N, E::DP_DM, E::D_PLUS_MINUS, E::PLUS_MINUS, E::POS_NEG,
                            E::BARE_D_PLUS_MINUS, E::P_N_UNDELIMITED});
}
inline DifferentialPairDetector_SuffixStyle DifferentialPairDetector_SuffixStyle_valueOf(
    const std::string& name) {
  for (auto e : *DifferentialPairDetector_SuffixStyle_values()) {
    if (DifferentialPairDetector_SuffixStyle_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant SuffixStyle." + name);
}

/** Result of analyzing a net name for differential pair suffix patterns (Java record). */
class DifferentialPairDetector_ExtractedNetInfo : public virtual jrt::Object {
 public:
  DifferentialPairDetector_ExtractedNetInfo(const std::string& baseName,
                                            DifferentialPairDetector_Polarity polarity,
                                            DifferentialPairDetector_SuffixStyle style)
      : baseName_(baseName), polarity_(polarity), style_(style) {}

  std::string baseName() { return baseName_; }
  DifferentialPairDetector_Polarity polarity() { return polarity_; }
  DifferentialPairDetector_SuffixStyle style() { return style_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = dynamic_cast<DifferentialPairDetector_ExtractedNetInfo*>(o);
    return other != nullptr && baseName_ == other->baseName_ && polarity_ == other->polarity_ &&
           style_ == other->style_;
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(baseName_);
    h = jrt::hashCombine(h, jrt::hashOf(polarity_));
    return jrt::hashCombine(h, jrt::hashOf(style_));
  }
  std::string toString() override {
    return "ExtractedNetInfo[baseName=" + baseName_ +
           ", polarity=" + DifferentialPairDetector_Polarity_toString(polarity_) +
           ", style=" + DifferentialPairDetector_SuffixStyle_toString(style_) + "]";
  }

 private:
  std::string baseName_;
  DifferentialPairDetector_Polarity polarity_;
  DifferentialPairDetector_SuffixStyle style_;
};

/** A delimited suffix token pair (private Java record). */
class DifferentialPairDetector_SuffixTokenPattern : public virtual jrt::Object {
 public:
  DifferentialPairDetector_SuffixTokenPattern(const std::string& posToken,
                                              const std::string& negToken,
                                              DifferentialPairDetector_SuffixStyle style)
      : posToken_(posToken), negToken_(negToken), style_(style) {}

  std::string posToken() { return posToken_; }
  std::string negToken() { return negToken_; }
  DifferentialPairDetector_SuffixStyle style() { return style_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = dynamic_cast<DifferentialPairDetector_SuffixTokenPattern*>(o);
    return other != nullptr && posToken_ == other->posToken_ && negToken_ == other->negToken_ &&
           style_ == other->style_;
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(posToken_);
    h = jrt::hashCombine(h, jrt::hashOf(negToken_));
    return jrt::hashCombine(h, jrt::hashOf(style_));
  }
  std::string toString() override {
    return "SuffixTokenPattern[posToken=" + posToken_ + ", negToken=" + negToken_ +
           ", style=" + DifferentialPairDetector_SuffixStyle_toString(style_) + "]";
  }

  // Record component fields (accessed directly as pattern.posToken in Java).
  std::string posToken_;
  std::string negToken_;
  DifferentialPairDetector_SuffixStyle style_;
};

/** Key of a detection candidate (private Java record, used as LinkedHashMap key). */
class DifferentialPairDetector_CandidateKey : public virtual jrt::Object {
 public:
  DifferentialPairDetector_CandidateKey(const std::string& baseNameKey,
                                        DifferentialPairDetector_SuffixStyle style)
      : baseNameKey_(baseNameKey), style_(style) {}

  std::string baseNameKey() { return baseNameKey_; }
  DifferentialPairDetector_SuffixStyle style() { return style_; }

  bool equals(jrt::Object* o) override {
    if (this == o) return true;
    auto* other = dynamic_cast<DifferentialPairDetector_CandidateKey*>(o);
    return other != nullptr && baseNameKey_ == other->baseNameKey_ && style_ == other->style_;
  }
  int32_t hashCode() override {
    int32_t h = jrt::hashOf(baseNameKey_);
    return jrt::hashCombine(h, jrt::hashOf(style_));
  }
  std::string toString() override {
    return "CandidateKey[baseNameKey=" + baseNameKey_ +
           ", style=" + DifferentialPairDetector_SuffixStyle_toString(style_) + "]";
  }

 private:
  std::string baseNameKey_;
  DifferentialPairDetector_SuffixStyle style_;
};

/** A detection candidate (private static final class in Java). */
class DifferentialPairDetector_CandidateEntry final : public virtual jrt::Object {
 public:
  DifferentialPairDetector_CandidateEntry(const std::string& baseName,
                                          DifferentialPairDetector_SuffixStyle style)
      : baseName(baseName), style(style) {}

  // private in Java, accessed by the enclosing class
  const std::string baseName;
  const DifferentialPairDetector_SuffixStyle style;
  Net* positiveNet = nullptr;
  Net* negativeNet = nullptr;
  bool ambiguous = false;
};

/**
 * Utility for parsing and identifying differential signal pair naming conventions. Supports KiCad
 * standard styles (+/- and _P/_N), hierarchical paths, and guards against false positives on
 * power and ground rails.
 */
class DifferentialPairDetector final {
 public:
  /** Whether the net name is a power, ground, or non-signal net excluded from detection. */
  static bool isExcludedNet(const std::optional<std::string>& netName);

  /**
   * Attempts to extract differential pair polarity and base name from the given net name. Returns
   * null if no recognized differential suffix matches.
   */
  static DifferentialPairDetector_ExtractedNetInfo* extract(
      const std::optional<std::string>& netName);

  /**
   * Scans the nets on the board, auto-detects complementary differential pairs based on standard
   * naming conventions, and returns them without modifying the board rules.
   */
  static jrt::List<DifferentialPair*>* detect(BoardRules* boardRules);

  /** Detects differential pairs and registers the new ones into the board rules. */
  static jrt::List<DifferentialPair*>* detectAndRegister(BoardRules* boardRules);

 private:
  DifferentialPairDetector() = delete;

  /** Java: Pattern "^[+-]?\\d+([.vV]\\d*)?[vV]?$", CASE_INSENSITIVE. */
  static const std::regex& POWER_RAIL_PATTERN();

  static jrt::Array<DifferentialPairDetector_SuffixTokenPattern*>* DELIMITED_PATTERNS();

  /** P/N right after the base name (ETH_TXP/ETH_TXN); only paired by detect(). */
  static DifferentialPairDetector_ExtractedNetInfo* extractUndelimited(
      const std::optional<std::string>& netName);

  static DifferentialPairDetector_ExtractedNetInfo* tryExtractDelimited(
      const std::string& trimmed, const std::string& upper, const std::string& posToken,
      const std::string& negToken, DifferentialPairDetector_SuffixStyle style);
};

}  // namespace fr
