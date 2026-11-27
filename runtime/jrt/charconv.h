// std::from_chars for double and float where the C++ library lacks it.
//
// libstdc++ (Linux, MinGW) has the floating-point overloads; libc++ (macOS) gained them only
// recently. Without them, jrt::fromChars parses the same text with strtod_l / strtof_l in the C
// locale. Both are correctly rounded, so the value is identical; the accepted text is restricted
// to what std::from_chars (chars_format::general) accepts as a decimal number, and range errors
// are reported the same way (result_out_of_range, value untouched). The words inf / nan, which
// std::from_chars also accepts, are rejected (the port's callers handle Java's own spellings
// before calling it). JRT_FORCE_FROM_CHARS_FALLBACK selects the fallback on any system (tests).
#pragma once

#include <charconv>
#include <system_error>

#if !defined(JRT_FORCE_FROM_CHARS_FALLBACK) && defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define JRT_STD_FLOAT_FROM_CHARS 1
#else
#include <locale.h>
#include <stdlib.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#endif

namespace jrt {

#if defined(JRT_STD_FLOAT_FROM_CHARS)

inline std::from_chars_result fromChars(const char* first, const char* last, double& value) {
  return std::from_chars(first, last, value);
}
inline std::from_chars_result fromChars(const char* first, const char* last, float& value) {
  return std::from_chars(first, last, value);
}

#else

namespace charconv_detail {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Length of the longest prefix of [first, last) of the form
// [-] digits [. digits] [(e|E) [+|-] digits] with at least one mantissa digit; 0 if none.
inline std::size_t decimalPrefix(const char* first, const char* last) {
  const char* p = first;
  if (p < last && *p == '-') ++p;
  bool digits = false;
  while (p < last && isDigit(*p)) {
    ++p;
    digits = true;
  }
  if (p < last && *p == '.') {
    ++p;
    while (p < last && isDigit(*p)) {
      ++p;
      digits = true;
    }
  }
  if (!digits) return 0;
  if (p < last && (*p == 'e' || *p == 'E')) {
    const char* q = p + 1;
    if (q < last && (*q == '+' || *q == '-')) ++q;
    const char* exponentDigits = q;
    while (q < last && isDigit(*q)) ++q;
    if (q > exponentDigits) p = q;
  }
  return static_cast<std::size_t>(p - first);
}

inline locale_t cLocale() {
  static locale_t c = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(0));
  return c;
}

template <class T>
std::from_chars_result fromChars(const char* first, const char* last, T& value) {
  std::size_t n = decimalPrefix(first, last);
  if (n == 0) return {first, std::errc::invalid_argument};
  std::string text(first, n);
  T result;
  if constexpr (std::is_same_v<T, float>) {
    result = strtof_l(text.c_str(), nullptr, cLocale());
  } else {
    result = strtod_l(text.c_str(), nullptr, cLocale());
  }
  const char* end = first + n;
  if (std::isinf(result)) return {end, std::errc::result_out_of_range};
  if (result == 0) {
    // Zero from a non-zero mantissa: underflow.
    for (char c : text) {
      if (c == 'e' || c == 'E') break;
      if (c >= '1' && c <= '9') return {end, std::errc::result_out_of_range};
    }
  }
  value = result;
  return {end, std::errc()};
}

}  // namespace charconv_detail

inline std::from_chars_result fromChars(const char* first, const char* last, double& value) {
  return charconv_detail::fromChars(first, last, value);
}
inline std::from_chars_result fromChars(const char* first, const char* last, float& value) {
  return charconv_detail::fromChars(first, last, value);
}

#endif

}  // namespace jrt
