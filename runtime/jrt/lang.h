// java.lang emulation for the freerouting C++ port.
// See PORTING.md for how Java constructs map onto these helpers.
#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace jrt {

void* alloc_uncollectable(std::size_t n);
void free_uncollectable(void* p);
std::size_t gc_heap_size();
void gc_collect();

// ---------------------------------------------------------------------------------------------
// Object: root of every ported class and interface (always inherited `public virtual`).
// Methods are deliberately non-const: Java has no const, and mixing const into a mechanical
// port causes endless friction.
// ---------------------------------------------------------------------------------------------
class Object {
 public:
  Object() = default;
  // A copy is a new object: it must not inherit the identity hash of its source.
  Object(const Object&) : identity_hash_(0) {}
  Object& operator=(const Object&) { return *this; }
  virtual ~Object() = default;
  // Java Object.equals: identity by default.
  virtual bool equals(Object* other) { return this == other; }
  // Java Object.hashCode: identity hash. Deterministic (sequential, assigned on first use)
  // instead of Java's random identity hash, so iteration orders are reproducible.
  virtual int32_t hashCode() { return identityHashCode(); }
  virtual std::string toString();
  int32_t identityHashCode();

 private:
  int32_t identity_hash_ = 0;
};

std::string className(Object* o);  // demangled dynamic class name, without namespaces

// Java `instanceof` / checked cast helpers.
template <class T, class U>
inline bool instanceof(U* p) {
  return dynamic_cast<T*>(p) != nullptr;
}
template <class T, class U>
inline T* as(U* p) {
  return dynamic_cast<T*>(p);
}

// ---------------------------------------------------------------------------------------------
// Exceptions. Message storage is uncollectable because the C++ runtime allocates exception
// objects with malloc, which the collector does not scan.
// ---------------------------------------------------------------------------------------------
class Throwable : public std::exception {
 public:
  Throwable() = default;
  explicit Throwable(std::string_view msg) { set(msg); }
  Throwable(const Throwable& o) { set(o.getMessage()); }
  Throwable& operator=(const Throwable& o) {
    if (this != &o) set(o.getMessage());
    return *this;
  }
  ~Throwable() override {
    if (msg_) free_uncollectable(msg_);
  }
  const char* what() const noexcept override { return msg_ ? msg_ : ""; }
  std::string getMessage() const { return msg_ ? std::string(msg_) : std::string(); }
  bool hasMessage() const { return msg_ != nullptr; }

 private:
  void set(std::string_view m) {
    if (msg_) free_uncollectable(msg_);
    msg_ = static_cast<char*>(alloc_uncollectable(m.size() + 1));
    std::memcpy(msg_, m.data(), m.size());
    msg_[m.size()] = '\0';
  }
  char* msg_ = nullptr;
};

#define JRT_EXCEPTION(Name, Base)              \
  class Name : public Base {                   \
   public:                                     \
    Name() = default;                          \
    explicit Name(std::string_view m) : Base(m) {} \
  };

JRT_EXCEPTION(Exception, Throwable)
JRT_EXCEPTION(Error, Throwable)
JRT_EXCEPTION(RuntimeException, Exception)
JRT_EXCEPTION(IOException, Exception)
JRT_EXCEPTION(IllegalArgumentException, RuntimeException)
JRT_EXCEPTION(NumberFormatException, IllegalArgumentException)
JRT_EXCEPTION(IllegalStateException, RuntimeException)
JRT_EXCEPTION(UnsupportedOperationException, RuntimeException)
JRT_EXCEPTION(NullPointerException, RuntimeException)
JRT_EXCEPTION(ArithmeticException, RuntimeException)
JRT_EXCEPTION(ClassCastException, RuntimeException)
JRT_EXCEPTION(IndexOutOfBoundsException, RuntimeException)
JRT_EXCEPTION(ArrayIndexOutOfBoundsException, IndexOutOfBoundsException)
JRT_EXCEPTION(NoSuchElementException, RuntimeException)
JRT_EXCEPTION(ConcurrentModificationException, RuntimeException)
JRT_EXCEPTION(InterruptedException, Exception)
JRT_EXCEPTION(StackOverflowError, Error)
JRT_EXCEPTION(OutOfMemoryError, Error)
JRT_EXCEPTION(AssertionError, Error)
#undef JRT_EXCEPTION

// ---------------------------------------------------------------------------------------------
// Numeric conversions with Java semantics. C++ casts from floating point to integer are
// undefined on overflow / NaN; Java saturates and maps NaN to 0. ALWAYS use these for
// Java `(int) someDouble` / `(long) someDouble`.
// ---------------------------------------------------------------------------------------------
inline int32_t d2i(double d) {
  if (std::isnan(d)) return 0;
  if (d >= 2147483647.0) return std::numeric_limits<int32_t>::max();
  if (d <= -2147483648.0) return std::numeric_limits<int32_t>::min();
  return static_cast<int32_t>(d);
}
inline int64_t d2l(double d) {
  if (std::isnan(d)) return 0;
  if (d >= 9223372036854775807.0) return std::numeric_limits<int64_t>::max();
  if (d <= -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return static_cast<int64_t>(d);
}
inline int32_t l2i(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v))); }
// Java `>>>`
inline int32_t ushr(int32_t v, int n) { return static_cast<int32_t>(static_cast<uint32_t>(v) >> (n & 31)); }
inline int64_t ushr(int64_t v, int n) { return static_cast<int64_t>(static_cast<uint64_t>(v) >> (n & 63)); }
// Java shifts mask the shift count.
inline int32_t shl(int32_t v, int n) { return static_cast<int32_t>(static_cast<uint32_t>(v) << (n & 31)); }
inline int64_t shl(int64_t v, int n) { return static_cast<int64_t>(static_cast<uint64_t>(v) << (n & 63)); }
inline int32_t shr(int32_t v, int n) { return v >> (n & 31); }
inline int64_t shr(int64_t v, int n) { return v >> (n & 63); }
// Java integer division / remainder (truncating, MIN/-1 does not trap).
inline int32_t idiv(int32_t a, int32_t b) {
  if (b == 0) throw ArithmeticException("/ by zero");
  if (b == -1) return static_cast<int32_t>(0u - static_cast<uint32_t>(a));
  return a / b;
}
inline int64_t ldiv(int64_t a, int64_t b) {
  if (b == 0) throw ArithmeticException("/ by zero");
  if (b == -1) return static_cast<int64_t>(0ull - static_cast<uint64_t>(a));
  return a / b;
}

// ---------------------------------------------------------------------------------------------
// java.lang.Math
// ---------------------------------------------------------------------------------------------
struct Math {
  static constexpr double PI = 3.141592653589793;
  static constexpr double E = 2.718281828459045;

  static int32_t abs(int32_t a) { return a < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(a)) : a; }
  static int64_t abs(int64_t a) { return a < 0 ? static_cast<int64_t>(0ull - static_cast<uint64_t>(a)) : a; }
  static double abs(double a) { return std::fabs(a); }
  static float abs(float a) { return std::fabs(a); }

  static int32_t max(int32_t a, int32_t b) { return a >= b ? a : b; }
  static int64_t max(int64_t a, int64_t b) { return a >= b ? a : b; }
  static int32_t min(int32_t a, int32_t b) { return a <= b ? a : b; }
  static int64_t min(int64_t a, int64_t b) { return a <= b ? a : b; }
  // Java max/min on floating point propagate NaN and order -0.0 < 0.0.
  static double max(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(a) && !std::signbit(b)) return b;
    return a >= b ? a : b;
  }
  static double min(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && std::signbit(b) && !std::signbit(a)) return b;
    return a <= b ? a : b;
  }
  static float max(float a, float b) { return static_cast<float>(max(static_cast<double>(a), static_cast<double>(b))); }
  static float min(float a, float b) { return static_cast<float>(min(static_cast<double>(a), static_cast<double>(b))); }

  // Math.round(double) -> long: round half up, NaN -> 0, saturating.
  static int64_t round(double a) {
    if (std::isnan(a)) return 0;
    double f = std::floor(a);
    double r = (a - f >= 0.5) ? f + 1.0 : f;
    return d2l(r);
  }
  // Math.round(float) -> int
  static int32_t round(float a) {
    if (std::isnan(a)) return 0;
    double f = std::floor(static_cast<double>(a));
    double r = (static_cast<double>(a) - f >= 0.5) ? f + 1.0 : f;
    return d2i(r);
  }
  static double floor(double a) { return std::floor(a); }
  static double ceil(double a) { return std::ceil(a); }
  static double rint(double a) { return std::nearbyint(a); }  // round half even (default FP env)
  static double sqrt(double a) { return std::sqrt(a); }
  static double cbrt(double a) { return std::cbrt(a); }
  static double hypot(double a, double b) { return std::hypot(a, b); }
  static double pow(double a, double b) { return std::pow(a, b); }
  static double exp(double a) { return std::exp(a); }
  static double log(double a) { return std::log(a); }
  static double log10(double a) { return std::log10(a); }
  static double sin(double a) { return std::sin(a); }
  static double cos(double a) { return std::cos(a); }
  static double tan(double a) { return std::tan(a); }
  static double asin(double a) { return std::asin(a); }
  static double acos(double a) { return std::acos(a); }
  static double atan(double a) { return std::atan(a); }
  static double atan2(double y, double x) { return std::atan2(y, x); }
  static double toRadians(double deg) { return deg / 180.0 * PI; }
  static double toDegrees(double rad) { return rad * 180.0 / PI; }
  static double signum(double d) { return (d == 0.0 || d != d) ? d : (d > 0 ? 1.0 : -1.0); }
  static double ulp(double d) {
    if (std::isnan(d)) return d;
    if (std::isinf(d)) return std::numeric_limits<double>::infinity();
    d = std::fabs(d);
    if (d == std::numeric_limits<double>::max()) return std::pow(2.0, 971);
    return std::nextafter(d, std::numeric_limits<double>::infinity()) - d;
  }
  static int32_t floorDiv(int32_t x, int32_t y) {
    int32_t q = idiv(x, y);
    if ((x % y != 0) && ((x ^ y) < 0)) --q;
    return q;
  }
  static int64_t floorDiv(int64_t x, int64_t y) {
    int64_t q = ldiv(x, y);
    if ((x % y != 0) && ((x ^ y) < 0)) --q;
    return q;
  }
  static int32_t floorMod(int32_t x, int32_t y) {
    if (y == -1) return 0;
    int32_t m = x % y;
    if ((m ^ y) < 0 && m != 0) m += y;
    return m;
  }
  static int64_t floorMod(int64_t x, int64_t y) {
    if (y == -1) return 0;
    int64_t m = x % y;
    if ((m ^ y) < 0 && m != 0) m += y;
    return m;
  }
  static int32_t clamp(int64_t v, int32_t lo, int32_t hi) {
    if (lo > hi) throw IllegalArgumentException("clamp: min > max");
    return static_cast<int32_t>(v < lo ? lo : (v > hi ? hi : v));
  }
  static int64_t clamp(int64_t v, int64_t lo, int64_t hi) {
    if (lo > hi) throw IllegalArgumentException("clamp: min > max");
    return v < lo ? lo : (v > hi ? hi : v);
  }
  static double clamp(double v, double lo, double hi) { return min(hi, max(v, lo)); }
  static int32_t addExact(int32_t a, int32_t b) {
    int32_t r;
    if (__builtin_add_overflow(a, b, &r)) throw ArithmeticException("integer overflow");
    return r;
  }
  static int64_t addExact(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) throw ArithmeticException("long overflow");
    return r;
  }
  static int64_t multiplyExact(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r)) throw ArithmeticException("long overflow");
    return r;
  }
  static int32_t toIntExact(int64_t v) {
    if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max())
      throw ArithmeticException("integer overflow");
    return static_cast<int32_t>(v);
  }
};
using StrictMath = Math;

// ---------------------------------------------------------------------------------------------
// Boxed-type statics
// ---------------------------------------------------------------------------------------------
struct Integer {
  static constexpr int32_t MAX_VALUE = std::numeric_limits<int32_t>::max();
  static constexpr int32_t MIN_VALUE = std::numeric_limits<int32_t>::min();
  static int32_t compare(int32_t a, int32_t b) { return a < b ? -1 : (a == b ? 0 : 1); }
  static int32_t signum(int32_t a) { return a > 0 ? 1 : (a < 0 ? -1 : 0); }
  static int32_t hashCode(int32_t v) { return v; }
  static int32_t parseInt(const std::string& s, int radix = 10);
  static std::string toString(int32_t v) { return std::to_string(v); }
  static std::string toHexString(int32_t v);
  static int32_t bitCount(int32_t v) { return __builtin_popcount(static_cast<uint32_t>(v)); }
  static int32_t numberOfLeadingZeros(int32_t v) { return v == 0 ? 32 : __builtin_clz(static_cast<uint32_t>(v)); }
  static int32_t numberOfTrailingZeros(int32_t v) { return v == 0 ? 32 : __builtin_ctz(static_cast<uint32_t>(v)); }
  static int32_t highestOneBit(int32_t v) {
    return v == 0 ? 0 : static_cast<int32_t>(0x80000000u >> numberOfLeadingZeros(v));
  }
};
struct Long {
  static constexpr int64_t MAX_VALUE = std::numeric_limits<int64_t>::max();
  static constexpr int64_t MIN_VALUE = std::numeric_limits<int64_t>::min();
  static int32_t compare(int64_t a, int64_t b) { return a < b ? -1 : (a == b ? 0 : 1); }
  static int32_t signum(int64_t a) { return a > 0 ? 1 : (a < 0 ? -1 : 0); }
  static int32_t hashCode(int64_t v) { return static_cast<int32_t>(static_cast<uint64_t>(v) ^ (static_cast<uint64_t>(v) >> 32)); }
  static int64_t parseLong(const std::string& s, int radix = 10);
  static std::string toString(int64_t v) { return std::to_string(v); }
};
struct Double {
  static constexpr double MAX_VALUE = std::numeric_limits<double>::max();
  static constexpr double MIN_VALUE = std::numeric_limits<double>::denorm_min();
  static constexpr double POSITIVE_INFINITY = std::numeric_limits<double>::infinity();
  static constexpr double NEGATIVE_INFINITY = -std::numeric_limits<double>::infinity();
  static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
  static bool isNaN(double d) { return std::isnan(d); }
  static bool isInfinite(double d) { return std::isinf(d); }
  static bool isFinite(double d) { return std::isfinite(d); }
  static int64_t doubleToLongBits(double d) {
    if (std::isnan(d)) return 0x7ff8000000000000LL;
    int64_t b;
    std::memcpy(&b, &d, sizeof b);
    return b;
  }
  static int64_t doubleToRawLongBits(double d) {
    int64_t b;
    std::memcpy(&b, &d, sizeof b);
    return b;
  }
  static double longBitsToDouble(int64_t b) {
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
  }
  static int32_t hashCode(double d) { return Long::hashCode(doubleToLongBits(d)); }
  // Java Double.compare: total order, -0.0 < 0.0, NaN greatest.
  static int32_t compare(double a, double b) {
    if (a < b) return -1;
    if (a > b) return 1;
    int64_t x = doubleToLongBits(a), y = doubleToLongBits(b);
    return x == y ? 0 : (x < y ? -1 : 1);
  }
  static double parseDouble(const std::string& s);
  static std::string toString(double d);
};
struct Float {
  static constexpr float MAX_VALUE = std::numeric_limits<float>::max();
  static constexpr float MIN_VALUE = std::numeric_limits<float>::denorm_min();
  static constexpr float POSITIVE_INFINITY = std::numeric_limits<float>::infinity();
  static constexpr float NEGATIVE_INFINITY = -std::numeric_limits<float>::infinity();
  static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
  static bool isNaN(float f) { return std::isnan(f); }
  static bool isInfinite(float f) { return std::isinf(f); }
  static bool isFinite(float f) { return std::isfinite(f); }
  static int32_t floatToIntBits(float f) {
    if (std::isnan(f)) return 0x7fc00000;
    int32_t b;
    std::memcpy(&b, &f, sizeof b);
    return b;
  }
  static int32_t hashCode(float f) { return floatToIntBits(f); }
  static int32_t compare(float a, float b) {
    if (a < b) return -1;
    if (a > b) return 1;
    int32_t x = floatToIntBits(a), y = floatToIntBits(b);
    return x == y ? 0 : (x < y ? -1 : 1);
  }
  static float parseFloat(const std::string& s);
  static std::string toString(float f);
};
struct Boolean {
  static int32_t hashCode(bool b) { return b ? 1231 : 1237; }
  static int32_t compare(bool a, bool b) { return a == b ? 0 : (a ? 1 : -1); }
  static bool parseBoolean(const std::string& s);
  static std::string toString(bool b) { return b ? "true" : "false"; }
};
struct Character {
  static bool isDigit(int32_t c) { return c >= '0' && c <= '9'; }
  static bool isLetter(int32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 127; }
  static bool isLetterOrDigit(int32_t c) { return isDigit(c) || isLetter(c); }
  static bool isWhitespace(int32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b || (c >= 0x1c && c <= 0x1f); }
  static int32_t toUpperCase(int32_t c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
  static int32_t toLowerCase(int32_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
};

// ---------------------------------------------------------------------------------------------
// String helpers. Java String -> std::string (UTF-8). Java `"x" + v` must go through str(v)
// so numbers print exactly as Java prints them (Double.toString format!).
// ---------------------------------------------------------------------------------------------
int32_t stringHashCode(std::string_view s);  // Java String.hashCode over UTF-16 code units

inline std::string str(const std::string& s) { return s; }
inline std::string str(std::string_view s) { return std::string(s); }
inline std::string str(const char* s) { return s ? std::string(s) : std::string("null"); }
inline std::string str(char c) { return std::string(1, c); }
inline std::string str(bool b) { return b ? "true" : "false"; }
inline std::string str(int32_t v) { return std::to_string(v); }
inline std::string str(int64_t v) { return std::to_string(v); }
inline std::string str(int16_t v) { return std::to_string(v); }
inline std::string str(int8_t v) { return std::to_string(v); }
inline std::string str(double v) { return Double::toString(v); }
inline std::string str(float v) { return Float::toString(v); }
inline std::string str(Object* o) { return o ? o->toString() : std::string("null"); }
template <class T>
  requires(std::is_base_of_v<Object, T> && !std::is_same_v<T, Object>)
inline std::string str(T* o) {
  return o ? static_cast<Object*>(o)->toString() : std::string("null");
}
template <class T>
inline std::string str(const std::optional<T>& o) {
  return o ? str(*o) : std::string("null");
}

// printf-style formatting (Java String.format with Locale.ROOT / US). Supports the usual
// %d %s %f %.Nf %x %e %g %%, width and flags. Pass std::string args via .c_str().
std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool contains(std::string_view s, std::string_view needle);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
int32_t compareStrings(std::string_view a, std::string_view b);  // Java String.compareTo
int32_t compareStringsIgnoreCase(std::string_view a, std::string_view b);
std::string toLowerCase(std::string_view s);
std::string toUpperCase(std::string_view s);
std::string trim(std::string_view s);    // Java trim(): strips chars <= ' '
std::string strip(std::string_view s);   // Java strip(): strips whitespace
int32_t indexOf(std::string_view s, std::string_view needle, int32_t from = 0);
int32_t lastIndexOf(std::string_view s, std::string_view needle);
std::string substring(std::string_view s, int32_t begin);
std::string substring(std::string_view s, int32_t begin, int32_t end);
std::string replace(std::string_view s, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view s, char sep);
std::string join(std::string_view sep, const std::vector<std::string>& parts);
std::string repeat(std::string_view s, int32_t n);

// ---------------------------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------------------------
struct System {
  static int64_t currentTimeMillis();
  static int64_t nanoTime();
  static std::optional<std::string> getenv(const std::string& name);
  static std::optional<std::string> getProperty(const std::string& name);
  static void setProperty(const std::string& name, const std::string& value);
  static std::string lineSeparator() { return "\n"; }
  static int32_t identityHashCode(Object* o) { return o ? o->identityHashCode() : 0; }
};

// ---------------------------------------------------------------------------------------------
// java.util.Random (bit-exact LCG so seeded sequences match Java).
// ---------------------------------------------------------------------------------------------
class Random : public virtual Object {
 public:
  Random();
  explicit Random(int64_t seed) { setSeed(seed); }
  void setSeed(int64_t seed) { seed_ = (seed ^ 0x5DEECE66DLL) & ((1LL << 48) - 1); have_gaussian_ = false; }
  int32_t nextInt();
  int32_t nextInt(int32_t bound);
  int64_t nextLong();
  bool nextBoolean() { return next(1) != 0; }
  double nextDouble();
  float nextFloat() { return next(24) / static_cast<float>(1 << 24); }
  double nextGaussian();

 protected:
  int32_t next(int bits);

 private:
  int64_t seed_ = 0;
  double next_gaussian_ = 0;
  bool have_gaussian_ = false;
};

// Java Objects.hash / Arrays.hashCode style combination.
inline int32_t hashCombine(int32_t h, int32_t v) { return static_cast<int32_t>(31u * static_cast<uint32_t>(h) + static_cast<uint32_t>(v)); }

}  // namespace jrt
