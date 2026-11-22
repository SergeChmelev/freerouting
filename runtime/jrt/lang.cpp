#include "jrt/lang.h"

#include <cxxabi.h>

#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace jrt {

// ---------------------------------------------------------------------------------------------
// Object
// ---------------------------------------------------------------------------------------------
namespace {
int32_t g_next_identity_hash = 0x1000;
}

int32_t Object::identityHashCode() {
  if (identity_hash_ == 0) {
    // Deterministic, well-spread sequence (Weyl sequence with a golden-ratio step).
    g_next_identity_hash = static_cast<int32_t>(static_cast<uint32_t>(g_next_identity_hash) + 0x61c88647u);
    identity_hash_ = g_next_identity_hash == 0 ? 1 : g_next_identity_hash;
  }
  return identity_hash_;
}

std::string className(Object* o) {
  if (o == nullptr) return "null";
  const char* mangled = typeid(*o).name();
  int status = 0;
  char* dem = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
  std::string name = (status == 0 && dem) ? std::string(dem) : std::string(mangled);
  std::free(dem);
  auto pos = name.rfind("::");
  if (pos != std::string::npos) name = name.substr(pos + 2);
  return name;
}

std::string Object::toString() { return className(this) + "@" + Integer::toHexString(hashCode()); }

// ---------------------------------------------------------------------------------------------
// Number parsing / printing
// ---------------------------------------------------------------------------------------------
int32_t Integer::parseInt(const std::string& s, int radix) {
  int64_t v = Long::parseLong(s, radix);
  if (v < MIN_VALUE || v > MAX_VALUE) throw NumberFormatException("For input string: \"" + s + "\"");
  return static_cast<int32_t>(v);
}

std::string Integer::toHexString(int32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%x", static_cast<uint32_t>(v));
  return buf;
}

int64_t Long::parseLong(const std::string& s, int radix) {
  std::string_view sv(s);
  if (sv.empty()) throw NumberFormatException("For input string: \"\"");
  bool neg = false;
  if (sv[0] == '+' || sv[0] == '-') {
    neg = sv[0] == '-';
    sv.remove_prefix(1);
    if (sv.empty()) throw NumberFormatException("For input string: \"" + s + "\"");
  }
  uint64_t acc = 0;
  auto res = std::from_chars(sv.data(), sv.data() + sv.size(), acc, radix);
  if (res.ec != std::errc() || res.ptr != sv.data() + sv.size())
    throw NumberFormatException("For input string: \"" + s + "\"");
  if (neg) {
    if (acc > 9223372036854775808ull) throw NumberFormatException("For input string: \"" + s + "\"");
    return static_cast<int64_t>(0ull - acc);
  }
  if (acc > 9223372036854775807ull) throw NumberFormatException("For input string: \"" + s + "\"");
  return static_cast<int64_t>(acc);
}

double Double::parseDouble(const std::string& s0) {
  std::string s = trim(s0);
  if (s.empty()) throw NumberFormatException("empty String");
  std::string_view sv(s);
  // Java accepts trailing d/D/f/F type suffixes and a leading '+'.
  if (!sv.empty() && (sv.back() == 'd' || sv.back() == 'D' || sv.back() == 'f' || sv.back() == 'F'))
    sv.remove_suffix(1);
  if (!sv.empty() && sv[0] == '+') sv.remove_prefix(1);
  if (sv == "NaN") return NaN;
  if (sv == "Infinity") return POSITIVE_INFINITY;
  if (sv == "-Infinity") return NEGATIVE_INFINITY;
  double d = 0;
  auto res = std::from_chars(sv.data(), sv.data() + sv.size(), d);
  if (res.ec == std::errc::result_out_of_range) {
    // from_chars reports range errors; Java returns +-Infinity or +-0.
    return std::strtod(std::string(sv).c_str(), nullptr);
  }
  if (res.ec != std::errc() || res.ptr != sv.data() + sv.size())
    throw NumberFormatException("For input string: \"" + s0 + "\"");
  return d;
}

float Float::parseFloat(const std::string& s) { return static_cast<float>(Double::parseDouble(s)); }

bool Boolean::parseBoolean(const std::string& s) { return equalsIgnoreCase(s, "true"); }

namespace {
// Formats a shortest round-trip digit string the way Java's Double/Float.toString does.
template <class F>
std::string javaFloatingToString(F v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
  if (v == 0) return std::signbit(v) ? "-0.0" : "0.0";
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
  std::string sci(buf, res.ptr);
  bool neg = sci[0] == '-';
  if (neg) sci.erase(0, 1);
  auto epos = sci.find('e');
  std::string mant = sci.substr(0, epos);
  int exp10 = std::atoi(sci.c_str() + epos + 1);
  std::string digits;
  for (char c : mant)
    if (c != '.') digits += c;
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  if (digits.size() == 1) {
    // Java spec: when the shortest decimal has one digit, pick the closest decimal among those
    // with length 1 or 2. The correctly rounded 2-digit decimal is that one if it round-trips.
    char b2[64];
    auto r2 = std::to_chars(b2, b2 + sizeof b2, v < 0 ? -v : v, std::chars_format::scientific, 1);
    std::string s2(b2, r2.ptr);
    F back{};
    std::from_chars(b2, r2.ptr, back);
    if (back == (v < 0 ? -v : v)) {
      auto e2 = s2.find('e');
      std::string d2;
      for (char c : s2.substr(0, e2))
        if (c != '.') d2 += c;
      while (d2.size() > 1 && d2.back() == '0') d2.pop_back();
      digits = d2;
      exp10 = std::atoi(s2.c_str() + e2 + 1);
    }
  }
  std::string out = neg ? "-" : "";
  F a = v < 0 ? -v : v;
  if (a >= static_cast<F>(1e-3) && a < static_cast<F>(1e7)) {
    if (exp10 >= 0) {
      std::string ip, fp;
      if (static_cast<int>(digits.size()) <= exp10 + 1) {
        ip = digits + std::string(exp10 + 1 - digits.size(), '0');
        fp = "0";
      } else {
        ip = digits.substr(0, exp10 + 1);
        fp = digits.substr(exp10 + 1);
      }
      out += ip + "." + fp;
    } else {
      out += "0." + std::string(-exp10 - 1, '0') + digits;
    }
  } else {
    out += digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : std::string("0")) + "E" +
           std::to_string(exp10);
  }
  return out;
}
}  // namespace

std::string Double::toString(double d) { return javaFloatingToString(d); }
std::string Float::toString(float f) { return javaFloatingToString(f); }

// ---------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------
int32_t stringHashCode(std::string_view s) {
  // Decode UTF-8 and hash UTF-16 code units exactly like java.lang.String.hashCode.
  uint32_t h = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    int len;
    if (c < 0x80) {
      cp = c;
      len = 1;
    } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
      cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f);
      len = 2;
    } else if ((c >> 4) == 0xe && i + 2 < s.size()) {
      cp = ((c & 0x0f) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3f);
      len = 3;
    } else if ((c >> 3) == 0x1e && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3f);
      len = 4;
    } else {
      cp = c;  // invalid byte: hash raw
      len = 1;
    }
    if (cp >= 0x10000) {
      uint32_t v = cp - 0x10000;
      h = 31 * h + (0xD800 + (v >> 10));
      h = 31 * h + (0xDC00 + (v & 0x3ff));
    } else {
      h = 31 * h + cp;
    }
    i += len;
  }
  return static_cast<int32_t>(h);
}

std::string format(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = std::vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  std::string out(n > 0 ? n : 0, '\0');
  if (n > 0) std::vsnprintf(out.data(), n + 1, fmt, ap2);
  va_end(ap2);
  return out;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool contains(std::string_view s, std::string_view n) { return s.find(n) != std::string_view::npos; }
bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (Character::toLowerCase(static_cast<unsigned char>(a[i])) != Character::toLowerCase(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
int32_t compareStrings(std::string_view a, std::string_view b) {
  std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    int d = static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
    if (d != 0) return d;
  }
  return static_cast<int32_t>(a.size()) - static_cast<int32_t>(b.size());
}
int32_t compareStringsIgnoreCase(std::string_view a, std::string_view b) {
  std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    int d = Character::toLowerCase(static_cast<unsigned char>(a[i])) - Character::toLowerCase(static_cast<unsigned char>(b[i]));
    if (d != 0) return d;
  }
  return static_cast<int32_t>(a.size()) - static_cast<int32_t>(b.size());
}
std::string toLowerCase(std::string_view s) {
  std::string r(s);
  for (auto& c : r) c = static_cast<char>(Character::toLowerCase(static_cast<unsigned char>(c)));
  return r;
}
std::string toUpperCase(std::string_view s) {
  std::string r(s);
  for (auto& c : r) c = static_cast<char>(Character::toUpperCase(static_cast<unsigned char>(c)));
  return r;
}
std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
  while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
  return std::string(s.substr(b, e - b));
}
std::string strip(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && Character::isWhitespace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && Character::isWhitespace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}
int32_t indexOf(std::string_view s, std::string_view n, int32_t from) {
  if (from < 0) from = 0;
  auto p = s.find(n, static_cast<std::size_t>(from));
  return p == std::string_view::npos ? -1 : static_cast<int32_t>(p);
}
int32_t lastIndexOf(std::string_view s, std::string_view n) {
  auto p = s.rfind(n);
  return p == std::string_view::npos ? -1 : static_cast<int32_t>(p);
}
std::string substring(std::string_view s, int32_t b) {
  if (b < 0 || b > static_cast<int32_t>(s.size())) throw IndexOutOfBoundsException("substring");
  return std::string(s.substr(b));
}
std::string substring(std::string_view s, int32_t b, int32_t e) {
  if (b < 0 || e > static_cast<int32_t>(s.size()) || b > e) throw IndexOutOfBoundsException("substring");
  return std::string(s.substr(b, e - b));
}
std::string replace(std::string_view s, std::string_view from, std::string_view to) {
  if (from.empty()) return std::string(s);
  std::string r;
  std::size_t pos = 0;
  while (true) {
    auto p = s.find(from, pos);
    if (p == std::string_view::npos) break;
    r.append(s.substr(pos, p - pos));
    r.append(to);
    pos = p + from.size();
  }
  r.append(s.substr(pos));
  return r;
}
std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (true) {
    auto p = s.find(sep, pos);
    if (p == std::string_view::npos) {
      out.emplace_back(s.substr(pos));
      break;
    }
    out.emplace_back(s.substr(pos, p - pos));
    pos = p + 1;
  }
  // Java String.split drops trailing empty strings.
  while (!out.empty() && out.back().empty()) out.pop_back();
  return out;
}
std::string join(std::string_view sep, const std::vector<std::string>& parts) {
  std::string r;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) r.append(sep);
    r.append(parts[i]);
  }
  return r;
}
std::string repeat(std::string_view s, int32_t n) {
  std::string r;
  for (int32_t i = 0; i < n; ++i) r.append(s);
  return r;
}

// ---------------------------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------------------------
int64_t System::currentTimeMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
int64_t System::nanoTime() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
std::optional<std::string> System::getenv(const std::string& name) {
  const char* v = std::getenv(name.c_str());
  if (!v) return std::nullopt;
  return std::string(v);
}
namespace {
std::map<std::string, std::string>& properties() {
  static auto* p = new std::map<std::string, std::string>();
  return *p;
}
}  // namespace
std::optional<std::string> System::getProperty(const std::string& name) {
  auto it = properties().find(name);
  if (it == properties().end()) return std::nullopt;
  return it->second;
}
void System::setProperty(const std::string& name, const std::string& value) { properties()[name] = value; }

// ---------------------------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------------------------
Random::Random() {
  static int64_t uniquifier = 8682522807148012LL;
  uniquifier *= 1181783497276652981LL;
  setSeed(uniquifier ^ System::nanoTime());
}
int32_t Random::next(int bits) {
  seed_ = (seed_ * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
  return static_cast<int32_t>(ushr(seed_, 48 - bits));
}
int32_t Random::nextInt() { return next(32); }
int32_t Random::nextInt(int32_t bound) {
  if (bound <= 0) throw IllegalArgumentException("bound must be positive");
  int32_t r = next(31);
  int32_t m = bound - 1;
  if ((bound & m) == 0) {
    r = static_cast<int32_t>((static_cast<int64_t>(bound) * static_cast<int64_t>(r)) >> 31);
  } else {
    for (int32_t u = r; static_cast<int32_t>(static_cast<uint32_t>(u) - static_cast<uint32_t>(r = u % bound) + static_cast<uint32_t>(m)) < 0;
         u = next(31)) {
    }
  }
  return r;
}
int64_t Random::nextLong() {
  return static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>(next(32))) << 32) +
                              static_cast<uint64_t>(static_cast<int64_t>(next(32))));
}
double Random::nextDouble() {
  return static_cast<double>((static_cast<int64_t>(next(26)) << 27) + next(27)) * 0x1.0p-53;
}
double Random::nextGaussian() {
  if (have_gaussian_) {
    have_gaussian_ = false;
    return next_gaussian_;
  }
  double v1, v2, s;
  do {
    v1 = 2 * nextDouble() - 1;
    v2 = 2 * nextDouble() - 1;
    s = v1 * v1 + v2 * v2;
  } while (s >= 1 || s == 0);
  double multiplier = std::sqrt(-2 * std::log(s) / s);
  next_gaussian_ = v2 * multiplier;
  have_gaussian_ = true;
  return v1 * multiplier;
}

}  // namespace jrt
