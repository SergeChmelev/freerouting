// java.util emulation for the freerouting C++ port.
//
// All collections are garbage-collected heap objects used through pointers, exactly like in
// Java (`new jrt::ArrayList<Item*>()`), so aliasing semantics are preserved. Iteration orders
// match Java: HashMap/HashSet reproduce Java's bucket layout and resize behaviour, TreeMap/Set
// are comparator ordered, PriorityQueue reproduces Java's binary heap, and sorts are stable
// (TimSort is stable). See PORTING.md for the Java -> jrt mapping table.
#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <initializer_list>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "jrt/alloc.h"
#include "jrt/lang.h"

#ifndef JRT_CHECKED
#define JRT_CHECKED 1
#endif

namespace jrt {

// ---------------------------------------------------------------------------------------------
// Hashing / equality / natural ordering with Java semantics.
// ---------------------------------------------------------------------------------------------
template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <class T>
concept HasHashCodeMember = requires(T& t) {
  { t.hashCode() } -> std::convertible_to<int32_t>;
};
template <class T>
concept HasCompareToPtr = requires(T* a, T* b) {
  { a->compareTo(b) } -> std::convertible_to<int32_t>;
};
template <class T>
concept HasCompareToVal = requires(T& a, T& b) {
  { a.compareTo(b) } -> std::convertible_to<int32_t>;
};

template <class T>
int32_t hashOf(const T& v) {
  if constexpr (std::is_pointer_v<T>) {
    using P = std::remove_cv_t<std::remove_pointer_t<T>>;
    if constexpr (std::is_base_of_v<Object, P>) {
      return v ? static_cast<Object*>(const_cast<P*>(v))->hashCode() : 0;
    } else {
      auto u = reinterpret_cast<uintptr_t>(v);
      return static_cast<int32_t>(u ^ (u >> 32));
    }
  } else if constexpr (std::is_same_v<T, bool>) {
    return Boolean::hashCode(v);
  } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
    return Long::hashCode(static_cast<int64_t>(v));
  } else if constexpr (std::is_integral_v<T>) {
    return static_cast<int32_t>(v);
  } else if constexpr (std::is_enum_v<T>) {
    return static_cast<int32_t>(v);
  } else if constexpr (std::is_same_v<T, double>) {
    return Double::hashCode(v);
  } else if constexpr (std::is_same_v<T, float>) {
    return Float::hashCode(v);
  } else if constexpr (std::is_same_v<T, std::string>) {
    return stringHashCode(v);
  } else if constexpr (is_optional<T>::value) {
    return v ? hashOf(*v) : 0;
  } else if constexpr (HasHashCodeMember<T>) {
    return const_cast<T&>(v).hashCode();
  } else {
    static_assert(sizeof(T) == 0, "jrt::hashOf: unsupported key type");
  }
}

template <class T>
bool equalsOf(const T& a, const T& b) {
  if constexpr (std::is_pointer_v<T>) {
    using P = std::remove_cv_t<std::remove_pointer_t<T>>;
    if constexpr (std::is_base_of_v<Object, P>) {
      if (a == b) return true;
      if (!a || !b) return false;
      return static_cast<Object*>(const_cast<P*>(a))->equals(static_cast<Object*>(const_cast<P*>(b)));
    } else {
      return a == b;
    }
  } else if constexpr (std::is_same_v<T, double>) {
    return Double::doubleToLongBits(a) == Double::doubleToLongBits(b);
  } else if constexpr (std::is_same_v<T, float>) {
    return Float::floatToIntBits(a) == Float::floatToIntBits(b);
  } else if constexpr (requires(T& x, T& y) { x.equals(y); }) {
    return const_cast<T&>(a).equals(const_cast<T&>(b));
  } else {
    return a == b;
  }
}

template <class T>
int32_t compareOf(const T& a, const T& b) {
  if constexpr (std::is_pointer_v<T>) {
    using P = std::remove_cv_t<std::remove_pointer_t<T>>;
    if constexpr (HasCompareToPtr<P>) {
      return const_cast<P*>(a)->compareTo(const_cast<P*>(b));
    } else {
      // Java: natural ordering of a non-Comparable type fails at run time.
      throw ClassCastException("jrt::compareOf: type is not Comparable");
    }
  } else if constexpr (std::is_same_v<T, double>) {
    return Double::compare(a, b);
  } else if constexpr (std::is_same_v<T, float>) {
    return Float::compare(a, b);
  } else if constexpr (std::is_same_v<T, bool>) {
    return Boolean::compare(a, b);
  } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
    return a < b ? -1 : (b < a ? 1 : 0);
  } else if constexpr (std::is_same_v<T, std::string>) {
    return compareStrings(a, b);
  } else if constexpr (HasCompareToVal<T>) {
    return const_cast<T&>(a).compareTo(const_cast<T&>(b));
  } else {
    throw ClassCastException("jrt::compareOf: type has no natural ordering");
  }
}

// java.util.Comparator<T>
template <class T>
using Comparator = std::function<int32_t(T, T)>;

template <class T>
Comparator<T> naturalOrder() {
  return [](T a, T b) { return compareOf<T>(a, b); };
}
template <class T>
Comparator<T> reverseOrder(Comparator<T> c = nullptr) {
  if (!c) c = naturalOrder<T>();
  return [c](T a, T b) { return c(b, a); };
}
template <class T, class F>
Comparator<T> comparingInt(F key) {
  return [key](T a, T b) { return Integer::compare(key(a), key(b)); };
}
template <class T, class F>
Comparator<T> comparingDouble(F key) {
  return [key](T a, T b) { return Double::compare(key(a), key(b)); };
}
template <class T, class F>
Comparator<T> comparingLong(F key) {
  return [key](T a, T b) { return Long::compare(key(a), key(b)); };
}
template <class T>
Comparator<T> thenComparing(Comparator<T> first, Comparator<T> second) {
  return [first, second](T a, T b) {
    int32_t r = first(a, b);
    return r != 0 ? r : second(a, b);
  };
}

// ---------------------------------------------------------------------------------------------
// Arrays: Java arrays are reference objects -> jrt::Array<T>*
// ---------------------------------------------------------------------------------------------
template <class T>
class Array : public virtual Object {
 public:
  const int32_t length;

  explicit Array(int32_t n) : length(n), data_(allocData(n)) {
    if (n < 0) throw IllegalArgumentException("negative array size");
  }
  // PORT NOTE (stage E): an array owns its element storage. When the array object lives in the
  // current geometry region, so does the storage (released with the region); otherwise it is a
  // heap block freed by the destructor (jrt::dispose).
  ~Array() override {
    if (data_ != nullptr && is_heap_block(data_)) delete[] data_;
  }
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  Array(std::initializer_list<T> init) : Array(static_cast<int32_t>(init.size())) {
    std::copy(init.begin(), init.end(), data_);
  }
  explicit Array(const std::vector<T>& v) : Array(static_cast<int32_t>(v.size())) {
    std::copy(v.begin(), v.end(), data_);
  }
  static Array* of(std::initializer_list<T> init) { return new Array(init); }

  T& operator[](int32_t i) {
#if JRT_CHECKED
    if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(length))
      throw ArrayIndexOutOfBoundsException("Index " + std::to_string(i) + " out of bounds for length " + std::to_string(length));
#endif
    return data_[i];
  }
  T get(int32_t i) { return (*this)[i]; }
  void set(int32_t i, T v) { (*this)[i] = std::move(v); }
  T* data() { return data_; }
  T* begin() { return data_; }
  T* end() { return data_ + length; }
  Array* clone() {
    auto* a = new Array(length);
    std::copy(data_, data_ + length, a->data_);
    return a;
  }
  std::vector<T> toVector() { return std::vector<T>(data_, data_ + length); }

 private:
  T* allocData(int32_t n) {
    if (n <= 0) return nullptr;
    if constexpr (std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>) {
      // `this` was allocated just before, so it is in the head chunk of its arena. Arena memory
      // is zero-filled: value-initialised trivial elements. Arena objects are never destroyed,
      // so heap storage of an array in an arena would leak.
      std::size_t bytes = sizeof(T) * static_cast<std::size_t>(n);
      for (GeomRegion* r = current_geom_region(); r != nullptr; r = r->outer()) {
        if (r->inHeadChunk(this)) return static_cast<T*>(r->allocate(bytes));
      }
      Arena* a = current_search_arena();
      if (a != nullptr && a->inHeadChunk(this)) return static_cast<T*>(a->allocate(bytes));
      a = current_board_arena();
      if (a != nullptr && a->inHeadChunk(this)) return static_cast<T*>(a->allocate(bytes));
    }
    return new T[n]();
  }
  T* data_;
};

template <class T>
Array<Array<T>*>* newArray2(int32_t n, int32_t m) {
  auto* a = new Array<Array<T>*>(n);
  for (int32_t i = 0; i < n; ++i) (*a)[i] = new Array<T>(m);
  return a;
}

// ---------------------------------------------------------------------------------------------
// Iterator + range-for adapter over Java style iterators
// ---------------------------------------------------------------------------------------------
template <class T>
class Iterator : public virtual Object {
 public:
  virtual bool hasNext() = 0;
  virtual T next() = 0;
  virtual void remove() { throw UnsupportedOperationException("remove"); }
};

struct IterEnd {};
// Inline storage for the iterator object of a range-for over an abstract Collection: concrete
// collections construct their (unchanged) Java style iterator in this buffer instead of on the
// heap (Collection::iteratorIn). Same traversal semantics, no allocation.
inline constexpr std::size_t kIterBufSize = 64;
struct alignas(16) IterBuf {
  unsigned char bytes[kIterBufSize];
};

template <class T>
class IterAdapter {
 public:
  explicit IterAdapter(Iterator<T>* it) : it_(it) { advance(); }
  template <class C>
  IterAdapter(C* c, int) : it_(c->iteratorIn(&buf_)) {
    owned_ = it_ == reinterpret_cast<Iterator<T>*>(&buf_) ? it_ : nullptr;
    advance();
  }
  ~IterAdapter() {
    if (owned_ != nullptr) owned_->~Iterator<T>();
  }
  IterAdapter(const IterAdapter&) = delete;
  IterAdapter& operator=(const IterAdapter&) = delete;
  T operator*() const { return *cur_; }
  IterAdapter& operator++() {
    advance();
    return *this;
  }
  bool operator!=(IterEnd) const { return it_ != nullptr; }
  bool operator==(IterEnd) const { return it_ == nullptr; }

 private:
  void advance() {
    if (it_ && it_->hasNext()) {
      cur_ = it_->next();
    } else {
      cur_.reset();
      it_ = nullptr;
    }
  }
  Iterator<T>* it_;
  Iterator<T>* owned_ = nullptr;  // iterator constructed in buf_ (destroyed with the adapter)
  std::optional<T> cur_;
  IterBuf buf_;
};

// Constructs `It` (a collection's iterator class) in `buf` when it fits, else on the heap.
template <class It, class T, class C>
inline Iterator<T>* make_iter_in(IterBuf* buf, C* c) {
  if constexpr (sizeof(It) <= kIterBufSize && alignof(It) <= alignof(IterBuf)) {
    return new (static_cast<void*>(buf)) It(c);
  } else {
    return new It(c);
  }
}

template <class T>
class Collection;

// RAII Java style iterator over a collection, constructed in local storage when the collection
// supports it (Collection::iteratorIn): `for (ScopedIter<T> it(c); it->hasNext();) ...`.
template <class T>
class ScopedIter {
 public:
  explicit ScopedIter(Collection<T>* c) : it_(c->iteratorIn(&buf_)) {}
  ~ScopedIter() {
    if (it_ == reinterpret_cast<Iterator<T>*>(&buf_)) it_->~Iterator<T>();
  }
  ScopedIter(const ScopedIter&) = delete;
  ScopedIter& operator=(const ScopedIter&) = delete;
  Iterator<T>* operator->() const { return it_; }

 private:
  Iterator<T>* it_;
  IterBuf buf_;
};

template <class T>
class Collection;

// ---------------------------------------------------------------------------------------------
// Collection / List / Set / Queue / Deque interfaces
// ---------------------------------------------------------------------------------------------
template <class T>
class Collection : public virtual Object {
 public:
  using value_type = T;
  virtual int32_t size() = 0;
  bool isEmpty() { return size() == 0; }
  virtual bool add(T v) = 0;
  virtual bool contains(const T& v) {
    for (ScopedIter<T> it(this); it->hasNext();)
      if (equalsOf<T>(it->next(), v)) return true;
    return false;
  }
  // Java remove(Object)
  virtual bool remove(const T& v) {
    for (ScopedIter<T> it(this); it->hasNext();) {
      if (equalsOf<T>(it->next(), v)) {
        it->remove();
        return true;
      }
    }
    return false;
  }
  virtual void clear() = 0;
  virtual Iterator<T>* iterator() = 0;
  // iterator() constructed in caller storage when possible (the result is either `buf` or a
  // heap iterator). Used by the generic range-for; the caller destroys an in-buffer iterator.
  virtual Iterator<T>* iteratorIn(IterBuf* buf) { (void)buf; return iterator(); }

  bool addAll(Collection<T>* c) {
    if (static_cast<Collection<T>*>(this) == c) {
      auto snapshot = c->toVector();
      bool changed = false;
      for (auto& x : snapshot) changed |= add(x);
      return changed;
    }
    bool changed = false;
    for (ScopedIter<T> it(c); it->hasNext();) changed |= add(it->next());
    return changed;
  }
  template <class Range>
  bool addAllFrom(const Range& r) {
    bool changed = false;
    for (const auto& x : r) changed |= add(x);
    return changed;
  }
  bool containsAll(Collection<T>* c) {
    for (ScopedIter<T> it(c); it->hasNext();)
      if (!contains(it->next())) return false;
    return true;
  }
  bool removeAll(Collection<T>* c) {
    bool changed = false;
    for (ScopedIter<T> it(this); it->hasNext();) {
      if (c->contains(it->next())) {
        it->remove();
        changed = true;
      }
    }
    return changed;
  }
  bool retainAll(Collection<T>* c) {
    bool changed = false;
    for (ScopedIter<T> it(this); it->hasNext();) {
      if (!c->contains(it->next())) {
        it->remove();
        changed = true;
      }
    }
    return changed;
  }
  template <class P>
  bool removeIf(P pred) {
    bool changed = false;
    for (ScopedIter<T> it(this); it->hasNext();) {
      if (pred(it->next())) {
        it->remove();
        changed = true;
      }
    }
    return changed;
  }
  template <class F>
  void forEach(F f) {
    for (ScopedIter<T> it(this); it->hasNext();) f(it->next());
  }
  std::vector<T> toVector() {
    std::vector<T> v;
    v.reserve(size());
    for (ScopedIter<T> it(this); it->hasNext();) v.push_back(it->next());
    return v;
  }
  Array<T>* toArray() { return new Array<T>(toVector()); }

  // Generic (virtual) range-for support. Concrete classes provide faster begin()/end().
  IterAdapter<T> begin() { return IterAdapter<T>(this, 0); }
  IterEnd end() { return {}; }

  std::string toString() override {
    std::string s = "[";
    bool first = true;
    for (ScopedIter<T> it(this); it->hasNext();) {
      if (!first) s += ", ";
      first = false;
      T x = it->next();
      if constexpr (requires { str(x); }) s += str(x);
      else s += "?";
    }
    return s + "]";
  }
};

template <class T>
class List : public virtual Collection<T> {
 public:
  using Collection<T>::add;
  using Collection<T>::remove;
  virtual T get(int32_t i) = 0;
  virtual T set(int32_t i, T v) = 0;
  virtual void add(int32_t i, T v) = 0;
  virtual T removeAt(int32_t i) = 0;  // Java List.remove(int index)
  virtual int32_t indexOf(const T& v) {
    int32_t n = this->size();
    for (int32_t i = 0; i < n; ++i)
      if (equalsOf<T>(get(i), v)) return i;
    return -1;
  }
  virtual int32_t lastIndexOf(const T& v) {
    for (int32_t i = this->size() - 1; i >= 0; --i)
      if (equalsOf<T>(get(i), v)) return i;
    return -1;
  }
  T getFirst() {
    if (this->isEmpty()) throw NoSuchElementException();
    return get(0);
  }
  T getLast() {
    if (this->isEmpty()) throw NoSuchElementException();
    return get(this->size() - 1);
  }
  virtual void sort(Comparator<T> c) {
    auto v = this->toVector();
    if (!c) c = naturalOrder<T>();
    std::stable_sort(v.begin(), v.end(), [&](const T& a, const T& b) { return c(a, b) < 0; });
    for (int32_t i = 0; i < static_cast<int32_t>(v.size()); ++i) set(i, v[i]);
  }
  bool equals(Object* o) override {
    auto* other = dynamic_cast<List<T>*>(o);
    if (!other) return false;
    if (other == this) return true;
    int32_t n = this->size();
    if (other->size() != n) return false;
    for (int32_t i = 0; i < n; ++i)
      if (!equalsOf<T>(get(i), other->get(i))) return false;
    return true;
  }
  int32_t hashCode() override {
    int32_t h = 1;
    for (ScopedIter<T> it(this); it->hasNext();) h = hashCombine(h, hashOf<T>(it->next()));
    return h;
  }
};

template <class T>
class Set : public virtual Collection<T> {
 public:
  bool equals(Object* o) override {
    auto* other = dynamic_cast<Set<T>*>(o);
    if (!other) return false;
    if (other == this) return true;
    return other->size() == this->size() && this->containsAll(other);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    for (ScopedIter<T> it(this); it->hasNext();) h += hashOf<T>(it->next());
    return h;
  }
};

template <class T>
class Queue : public virtual Collection<T> {
 public:
  using Collection<T>::remove;
  virtual bool offer(T v) { return this->add(std::move(v)); }
  virtual std::optional<T> pollOpt() = 0;
  virtual std::optional<T> peekOpt() = 0;
  // For pointer element types poll()/peek() return nullptr when empty (as Java returns null).
  T poll() {
    auto r = pollOpt();
    return r ? *r : T{};
  }
  T peek() {
    auto r = peekOpt();
    return r ? *r : T{};
  }
  T element() {
    auto r = peekOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
  T remove() {
    auto r = pollOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
};

template <class T>
class Deque : public virtual Queue<T> {
 public:
  using Queue<T>::remove;
  virtual void addFirst(T v) = 0;
  virtual void addLast(T v) = 0;
  bool offerFirst(T v) {
    addFirst(std::move(v));
    return true;
  }
  bool offerLast(T v) {
    addLast(std::move(v));
    return true;
  }
  virtual std::optional<T> pollFirstOpt() = 0;
  virtual std::optional<T> pollLastOpt() = 0;
  virtual std::optional<T> peekFirstOpt() = 0;
  virtual std::optional<T> peekLastOpt() = 0;
  std::optional<T> pollOpt() override { return pollFirstOpt(); }
  std::optional<T> peekOpt() override { return peekFirstOpt(); }
  T pollFirst() {
    auto r = pollFirstOpt();
    return r ? *r : T{};
  }
  T pollLast() {
    auto r = pollLastOpt();
    return r ? *r : T{};
  }
  T peekFirst() {
    auto r = peekFirstOpt();
    return r ? *r : T{};
  }
  T peekLast() {
    auto r = peekLastOpt();
    return r ? *r : T{};
  }
  T removeFirst() {
    auto r = pollFirstOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
  T removeLast() {
    auto r = pollLastOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
  T getFirst() {
    auto r = peekFirstOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
  T getLast() {
    auto r = peekLastOpt();
    if (!r) throw NoSuchElementException();
    return *r;
  }
  void push(T v) { addFirst(std::move(v)); }
  T pop() { return removeFirst(); }
};

// ---------------------------------------------------------------------------------------------
// ArrayList (also used for java.util.Vector, List.of, Arrays.asList, ...)
// ---------------------------------------------------------------------------------------------
template <class T>
class ArrayList : public virtual List<T> {
 public:
  using Collection<T>::addAll;

  // PORT NOTE (stage C): removes [from, to). For pointer elements the vacated slots are nulled:
  // a shrunk std::vector keeps the old pointers in its capacity, where the conservative collector
  // (and FR_DISPOSE_CHECK) sees them as live references to removed objects.
  void eraseRange(std::size_t from, std::size_t to) {
    std::size_t oldSize = v_.size();
    v_.erase(v_.begin() + static_cast<std::ptrdiff_t>(from), v_.begin() + static_cast<std::ptrdiff_t>(to));
    if constexpr (std::is_pointer_v<T>) {
      std::size_t newSize = v_.size();
      if (oldSize > newSize) {
        v_.resize(oldSize, nullptr);  // within capacity: writes nulls, no reallocation
        v_.resize(newSize);
      }
    }
  }

  ArrayList() = default;
  explicit ArrayList(int32_t initial_capacity) { v_.reserve(initial_capacity > 0 ? initial_capacity : 0); }
  explicit ArrayList(Collection<T>* c) {
    if (c) {
      std::vector<T> src = c->toVector();
      v_.assign(src.begin(), src.end());
    }
  }
  ArrayList(std::initializer_list<T> init) { v_.assign(init.begin(), init.end()); }
  explicit ArrayList(const std::vector<T>& v) { v_.assign(v.begin(), v.end()); }
  static ArrayList* of(std::initializer_list<T> init) { return new ArrayList(init); }

  int32_t size() override { return static_cast<int32_t>(v_.size()); }
  bool add(T x) override {
    v_.push_back(std::move(x));
    return true;
  }
  void add(int32_t i, T x) override {
    check_pos(i);
    v_.insert(v_.begin() + i, std::move(x));
  }
  bool addAll(int32_t i, Collection<T>* c) {
    check_pos(i);
    auto src = c->toVector();
    v_.insert(v_.begin() + i, src.begin(), src.end());
    return !src.empty();
  }
  T get(int32_t i) override {
    check_index(i);
    return v_[i];
  }
  T set(int32_t i, T x) override {
    check_index(i);
    T old = v_[i];
    v_[i] = std::move(x);
    return old;
  }
  T removeAt(int32_t i) override {
    check_index(i);
    T old = v_[i];
    eraseRange(i, i + 1);
    return old;
  }
  bool remove(const T& x) override {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      if (equalsOf<T>(v_[i], x)) {
        eraseRange(i, i + 1);
        return true;
      }
    }
    return false;
  }
  // remove(x) for element types whose equals() is identity (no virtual call per element).
  bool removeIdentical(const T& x) {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      if (v_[i] == x) {
        eraseRange(i, i + 1);
        return true;
      }
    }
    return false;
  }
  bool contains(const T& x) override { return indexOf(x) >= 0; }
  int32_t indexOf(const T& x) override {
    for (std::size_t i = 0; i < v_.size(); ++i)
      if (equalsOf<T>(v_[i], x)) return static_cast<int32_t>(i);
    return -1;
  }
  void clear() override { eraseRange(0, v_.size()); }
  // Java list.subList(from, to).clear()
  void removeRange(int32_t from, int32_t to) { eraseRange(from, to); }
  // Java subList(from, to) as a COPY (Java returns a live view).
  ArrayList* subList(int32_t from, int32_t to) {
    return new ArrayList(std::vector<T>(v_.begin() + from, v_.begin() + to));
  }
  T removeFirst() {
    if (v_.empty()) throw NoSuchElementException();
    return removeAt(0);
  }
  T removeLast() {
    if (v_.empty()) throw NoSuchElementException();
    T x = v_.back();
    eraseRange(v_.size() - 1, v_.size());
    return x;
  }
  void addFirst(T x) { v_.insert(v_.begin(), std::move(x)); }
  void addLast(T x) { v_.push_back(std::move(x)); }
  void ensureCapacity(int32_t n) { v_.reserve(n); }
  void trimToSize() {}
  void sort(Comparator<T> c) override {
    if (!c) c = naturalOrder<T>();
    std::stable_sort(v_.begin(), v_.end(), [&](const T& a, const T& b) { return c(a, b) < 0; });
  }
  ArrayList* clone() { return new ArrayList(v_); }

  class It : public Iterator<T> {
   public:
    explicit It(ArrayList* l) : l_(l) {}
    bool hasNext() override { return cursor_ < static_cast<int32_t>(l_->v_.size()); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      last_ = cursor_;
      return l_->v_[cursor_++];
    }
    void remove() override {
      if (last_ < 0) throw IllegalStateException();
      l_->eraseRange(last_, last_ + 1);
      cursor_ = last_;
      last_ = -1;
    }

   private:
    ArrayList* l_;
    int32_t cursor_ = 0;
    int32_t last_ = -1;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }

  // Fast native iteration. Do not structurally modify the list inside such a loop
  // (Java would throw ConcurrentModificationException).
  auto begin() { return v_.begin(); }
  auto end() { return v_.end(); }
  // PORT NOTE (stage E): storage in the collection's domain (jrt::DomainAlloc).
  using Vec = std::vector<T, DomainAlloc<T>>;
  Vec& vec() { return v_; }

 private:
  void check_index(int32_t i) {
#if JRT_CHECKED
    if (static_cast<uint32_t>(i) >= v_.size())
      throw IndexOutOfBoundsException("Index " + std::to_string(i) + " out of bounds for length " + std::to_string(v_.size()));
#endif
  }
  void check_pos(int32_t i) {
    if (i < 0 || static_cast<std::size_t>(i) > v_.size()) throw IndexOutOfBoundsException("Index: " + std::to_string(i));
  }
  Vec v_{DomainAlloc<T>(domain_arena_of(this))};
};

template <class T>
using Vector = ArrayList<T>;

// ---------------------------------------------------------------------------------------------
// LinkedList (List + Deque, O(1) iterator removal)
// ---------------------------------------------------------------------------------------------
template <class T>
class LinkedList : public virtual List<T>, public virtual Deque<T> {
 public:
  using Collection<T>::addAll;
  using List<T>::add;
  using Deque<T>::getFirst;
  using Deque<T>::getLast;
  using Deque<T>::remove;
  using List<T>::remove;
  LinkedList() = default;
  explicit LinkedList(Collection<T>* c) {
    if (c)
      for (ScopedIter<T> it(c); it->hasNext();) l_.push_back(it->next());
  }
  LinkedList(std::initializer_list<T> init) { l_.assign(init.begin(), init.end()); }
  using ListT = std::list<T, DomainAlloc<T>>;  // storage in the collection's domain

  int32_t size() override { return static_cast<int32_t>(l_.size()); }
  bool add(T x) override {
    l_.push_back(std::move(x));
    return true;
  }
  void add(int32_t i, T x) override { l_.insert(pos(i, true), std::move(x)); }
  T get(int32_t i) override { return *pos(i, false); }
  T set(int32_t i, T x) override {
    auto p = pos(i, false);
    T old = *p;
    *p = std::move(x);
    return old;
  }
  T removeAt(int32_t i) override {
    auto p = pos(i, false);
    T old = *p;
    l_.erase(p);
    return old;
  }
  bool remove(const T& x) override {
    for (auto p = l_.begin(); p != l_.end(); ++p) {
      if (equalsOf<T>(*p, x)) {
        l_.erase(p);
        return true;
      }
    }
    return false;
  }
  bool contains(const T& x) override {
    for (auto& y : l_)
      if (equalsOf<T>(y, x)) return true;
    return false;
  }
  void clear() override { l_.clear(); }
  void addFirst(T x) override { l_.push_front(std::move(x)); }
  void addLast(T x) override { l_.push_back(std::move(x)); }
  std::optional<T> pollFirstOpt() override {
    if (l_.empty()) return std::nullopt;
    T x = l_.front();
    l_.pop_front();
    return x;
  }
  std::optional<T> pollLastOpt() override {
    if (l_.empty()) return std::nullopt;
    T x = l_.back();
    l_.pop_back();
    return x;
  }
  std::optional<T> peekFirstOpt() override {
    if (l_.empty()) return std::nullopt;
    return l_.front();
  }
  std::optional<T> peekLastOpt() override {
    if (l_.empty()) return std::nullopt;
    return l_.back();
  }
  void sort(Comparator<T> c) override {
    if (!c) c = naturalOrder<T>();
    l_.sort([&](const T& a, const T& b) { return c(a, b) < 0; });  // std::list::sort is stable
  }
  bool equals(Object* o) override { return List<T>::equals(o); }
  int32_t hashCode() override { return List<T>::hashCode(); }
  std::string toString() override { return Collection<T>::toString(); }

  class It : public Iterator<T> {
   public:
    explicit It(LinkedList* l) : l_(l), cur_(l->l_.begin()), last_(l->l_.end()) {}
    bool hasNext() override { return cur_ != l_->l_.end(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      last_ = cur_++;
      return *last_;
    }
    void remove() override {
      if (last_ == l_->l_.end()) throw IllegalStateException();
      l_->l_.erase(last_);
      last_ = l_->l_.end();
    }

   private:
    LinkedList* l_;
    typename ListT::iterator cur_, last_;
  };
  class DescIt : public Iterator<T> {
   public:
    explicit DescIt(LinkedList* l) : l_(l), cur_(l->l_.end()) {}
    bool hasNext() override { return cur_ != l_->l_.begin(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      --cur_;
      last_valid_ = true;
      return *cur_;
    }
    void remove() override {
      if (!last_valid_) throw IllegalStateException();
      cur_ = l_->l_.erase(cur_);
      last_valid_ = false;
    }

   private:
    LinkedList* l_;
    typename ListT::iterator cur_;
    bool last_valid_ = false;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }
  Iterator<T>* descendingIterator() { return new DescIt(this); }
  auto begin() { return l_.begin(); }
  auto end() { return l_.end(); }
  ListT& raw() { return l_; }

 private:
  typename ListT::iterator pos(int32_t i, bool allow_end) {
    int32_t n = size();
    if (i < 0 || i > n || (!allow_end && i == n)) throw IndexOutOfBoundsException("Index: " + std::to_string(i));
    if (i <= n / 2) {
      auto p = l_.begin();
      std::advance(p, i);
      return p;
    }
    auto p = l_.end();
    std::advance(p, -(n - i));
    return p;
  }
  ListT l_{DomainAlloc<T>(domain_arena_of(this))};
};

// ---------------------------------------------------------------------------------------------
// ArrayDeque
// ---------------------------------------------------------------------------------------------
template <class T>
class ArrayDeque : public virtual Deque<T> {
 public:
  using Deque<T>::remove;
  ArrayDeque() = default;
  explicit ArrayDeque(int32_t) {}
  explicit ArrayDeque(Collection<T>* c) {
    for (ScopedIter<T> it(c); it->hasNext();) d_.push_back(it->next());
  }
  int32_t size() override { return static_cast<int32_t>(d_.size()); }
  bool add(T x) override {
    d_.push_back(std::move(x));
    return true;
  }
  void clear() override { d_.clear(); }
  void addFirst(T x) override { d_.push_front(std::move(x)); }
  void addLast(T x) override { d_.push_back(std::move(x)); }
  std::optional<T> pollFirstOpt() override {
    if (d_.empty()) return std::nullopt;
    T x = d_.front();
    d_.pop_front();
    return x;
  }
  std::optional<T> pollLastOpt() override {
    if (d_.empty()) return std::nullopt;
    T x = d_.back();
    d_.pop_back();
    return x;
  }
  std::optional<T> peekFirstOpt() override {
    if (d_.empty()) return std::nullopt;
    return d_.front();
  }
  std::optional<T> peekLastOpt() override {
    if (d_.empty()) return std::nullopt;
    return d_.back();
  }
  class It : public Iterator<T> {
   public:
    explicit It(ArrayDeque* d) : d_(d) {}
    bool hasNext() override { return i_ < d_->d_.size(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      last_ = static_cast<int64_t>(i_);
      return d_->d_[i_++];
    }
    void remove() override {
      if (last_ < 0) throw IllegalStateException();
      d_->d_.erase(d_->d_.begin() + last_);
      i_ = static_cast<std::size_t>(last_);
      last_ = -1;
    }

   private:
    ArrayDeque* d_;
    std::size_t i_ = 0;
    int64_t last_ = -1;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }
  auto begin() { return d_.begin(); }
  auto end() { return d_.end(); }

 private:
  std::deque<T, DomainAlloc<T>> d_{DomainAlloc<T>(domain_arena_of(this))};
};

template <class T>
using Stack = ArrayDeque<T>;  // only push/pop/peek/isEmpty semantics are used

// ---------------------------------------------------------------------------------------------
// PriorityQueue: exact replica of java.util.PriorityQueue's binary heap so that ties and
// iteration order behave identically.
// ---------------------------------------------------------------------------------------------
template <class T>
class PriorityQueue : public virtual Queue<T> {
 public:
  using Queue<T>::remove;
  PriorityQueue() : cmp_(naturalOrder<T>()) {}
  explicit PriorityQueue(Comparator<T> c) : cmp_(c ? c : naturalOrder<T>()) {}
  PriorityQueue(int32_t, Comparator<T> c) : PriorityQueue(c) {}
  int32_t size() override { return static_cast<int32_t>(q_.size()); }
  bool add(T x) override { return offer(std::move(x)); }
  bool offer(T x) override {
    q_.push_back(x);
    siftUp(static_cast<int32_t>(q_.size()) - 1, x);
    return true;
  }
  std::optional<T> peekOpt() override {
    if (q_.empty()) return std::nullopt;
    return q_[0];
  }
  std::optional<T> pollOpt() override {
    if (q_.empty()) return std::nullopt;
    T result = q_[0];
    T x = q_.back();
    q_.pop_back();
    if (!q_.empty()) siftDown(0, x);
    return result;
  }
  bool remove(const T& o) override {
    for (std::size_t i = 0; i < q_.size(); ++i) {
      if (equalsOf<T>(q_[i], o)) {
        removeAt(static_cast<int32_t>(i));
        return true;
      }
    }
    return false;
  }
  bool contains(const T& o) override {
    for (auto& x : q_)
      if (equalsOf<T>(x, o)) return true;
    return false;
  }
  void clear() override { q_.clear(); }
  class It : public Iterator<T> {
   public:
    explicit It(PriorityQueue* q) : q_(q) {}
    bool hasNext() override { return i_ < q_->q_.size(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      return q_->q_[i_++];
    }

   private:
    PriorityQueue* q_;
    std::size_t i_ = 0;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }
  auto begin() { return q_.begin(); }
  auto end() { return q_.end(); }

 private:
  void removeAt(int32_t i) {
    int32_t s = static_cast<int32_t>(q_.size()) - 1;
    if (s == i) {
      q_.pop_back();
      return;
    }
    T moved = q_[s];
    q_.pop_back();
    siftDown(i, moved);
    bool same;
    if constexpr (std::is_pointer_v<T>) same = q_[i] == moved;  // Java identity check
    else same = equalsOf<T>(q_[i], moved);
    if (same) siftUp(i, moved);
  }
  void siftUp(int32_t k, T x) {
    while (k > 0) {
      int32_t parent = (k - 1) >> 1;
      T e = q_[parent];
      if (cmp_(x, e) >= 0) break;
      q_[k] = e;
      k = parent;
    }
    q_[k] = x;
  }
  void siftDown(int32_t k, T x) {
    int32_t n = static_cast<int32_t>(q_.size());
    int32_t half = n >> 1;
    while (k < half) {
      int32_t child = (k << 1) + 1;
      T c = q_[child];
      int32_t right = child + 1;
      if (right < n && cmp_(c, q_[right]) > 0) c = q_[child = right];
      if (cmp_(x, c) <= 0) break;
      q_[k] = c;
      k = child;
    }
    q_[k] = x;
  }
  std::vector<T, DomainAlloc<T>> q_{DomainAlloc<T>(domain_arena_of(this))};
  Comparator<T> cmp_;
};

// ---------------------------------------------------------------------------------------------
// Map interface + entries
// ---------------------------------------------------------------------------------------------
template <class K, class V>
class MapEntry : public virtual Object {
 public:
  MapEntry(K k, V v) : key(std::move(k)), value(std::move(v)) {}
  K getKey() { return key; }
  V getValue() { return value; }
  V setValue(V v) {
    V old = value;
    value = std::move(v);
    return old;
  }
  K key;
  V value;
};

template <class T>
class LinkedHashSet;

template <class K, class V>
class Map : public virtual Object {
 public:
  using Entry = MapEntry<K, V>;
  virtual int32_t size() = 0;
  bool isEmpty() { return size() == 0; }
  virtual Entry* getEntry(const K& k) = 0;
  // Java get(): returns V{} (nullptr for pointer values) when absent. Use getOpt() when the
  // Java value type is a boxed primitive and the code distinguishes null.
  V get(const K& k) {
    Entry* e = getEntry(k);
    return e ? e->value : V{};
  }
  std::optional<V> getOpt(const K& k) {
    Entry* e = getEntry(k);
    if (!e) return std::nullopt;
    return e->value;
  }
  V getOrDefault(const K& k, V def) {
    Entry* e = getEntry(k);
    return e ? e->value : def;
  }
  bool containsKey(const K& k) { return getEntry(k) != nullptr; }
  bool containsValue(const V& v) {
    for (Entry* e : entryVector())
      if (equalsOf<V>(e->value, v)) return true;
    return false;
  }
  // Returns the previous value (V{} if none).
  virtual V put(K k, V v) = 0;
  virtual std::optional<V> removeOpt(const K& k) = 0;
  V remove(const K& k) {
    auto r = removeOpt(k);
    return r ? *r : V{};
  }
  virtual void clear() = 0;
  // Entries in Java iteration order. Entries are live (setValue writes through).
  virtual std::vector<Entry*> entryVector() = 0;

  V putIfAbsent(K k, V v) {
    Entry* e = getEntry(k);
    if (e) return e->value;
    put(std::move(k), std::move(v));
    return V{};
  }
  template <class F>
  V computeIfAbsent(const K& k, F f) {
    Entry* e = getEntry(k);
    if (e) return e->value;
    V v = f(k);
    put(k, v);
    return v;
  }
  template <class F>
  V merge(const K& k, V v, F f) {
    Entry* e = getEntry(k);
    if (!e) {
      put(k, v);
      return v;
    }
    e->value = f(e->value, v);
    return e->value;
  }
  void putAll(Map<K, V>* m) {
    for (Entry* e : m->entryVector()) put(e->key, e->value);
  }
  // Snapshots (Java returns live views; mutate the map itself instead of these).
  ArrayList<Entry*>* entrySet() { return new ArrayList<Entry*>(entryVector()); }
  LinkedHashSet<K>* keySet();
  ArrayList<V>* values() {
    auto* r = new ArrayList<V>();
    for (Entry* e : entryVector()) r->add(e->value);
    return r;
  }
  template <class F>
  void forEach(F f) {
    for (Entry* e : entryVector()) f(e->key, e->value);
  }
  bool equals(Object* o) override {
    auto* m = dynamic_cast<Map<K, V>*>(o);
    if (!m) return false;
    if (m == this) return true;
    if (m->size() != size()) return false;
    for (Entry* e : entryVector()) {
      Entry* x = m->getEntry(e->key);
      if (!x || !equalsOf<V>(x->value, e->value)) return false;
    }
    return true;
  }
  int32_t hashCode() override {
    int32_t h = 0;
    for (Entry* e : entryVector()) h += hashOf<K>(e->key) ^ hashOf<V>(e->value);
    return h;
  }
};

// ---------------------------------------------------------------------------------------------
// HashMap / LinkedHashMap / IdentityHashMap: replicate java.util.HashMap's table layout.
// ---------------------------------------------------------------------------------------------
enum class HashMode { Hash, Linked, IdentityLinked };

template <class K, class V>
class HashMap : public virtual Map<K, V> {
 public:
  using Entry = MapEntry<K, V>;
  struct Node : public Entry {
    Node(int32_t h, K k, V v) : Entry(std::move(k), std::move(v)), hash(h) {}
    int32_t hash;
    Node* next = nullptr;
    Node* before = nullptr;  // linked modes only
    Node* after = nullptr;
  };

  HashMap() = default;
  explicit HashMap(int32_t initial_capacity, float load_factor = 0.75f) : load_factor_(load_factor) {
    if (initial_capacity < 0) throw IllegalArgumentException("Illegal initial capacity");
    threshold_ = tableSizeFor(initial_capacity);
  }
  explicit HashMap(Map<K, V>* m) { putMapEntries(m); }

  int32_t size() override { return size_; }
  Entry* getEntry(const K& k) override { return findNode(k); }
  V put(K k, V v) override {
    int32_t h = spread(k);
    if (table_.empty()) resize();
    int32_t n = static_cast<int32_t>(table_.size());
    int32_t i = (n - 1) & h;
    Node* p = table_[i];
    if (p == nullptr) {
      table_[i] = newNode(h, std::move(k), std::move(v));
    } else {
      Node* last = nullptr;
      for (Node* e = p; e != nullptr; e = e->next) {
        if (e->hash == h && keyEquals(e->key, k)) {
          V old = e->value;
          e->value = std::move(v);
          return old;
        }
        last = e;
      }
      last->next = newNode(h, std::move(k), std::move(v));
    }
    if (++size_ > threshold_) resize();
    return V{};
  }
  std::optional<V> removeOpt(const K& k) override {
    if (table_.empty()) return std::nullopt;
    int32_t h = spread(k);
    int32_t i = (static_cast<int32_t>(table_.size()) - 1) & h;
    Node* prev = nullptr;
    for (Node* e = table_[i]; e != nullptr; prev = e, e = e->next) {
      if (e->hash == h && keyEquals(e->key, k)) {
        if (prev) prev->next = e->next;
        else table_[i] = e->next;
        unlinkOrder(e);
        --size_;
        return e->value;
      }
    }
    return std::nullopt;
  }
  void clear() override {
    std::fill(table_.begin(), table_.end(), nullptr);
    size_ = 0;
    head_ = tail_ = nullptr;
  }
  std::vector<Entry*> entryVector() override {
    std::vector<Entry*> out;
    out.reserve(size_);
    forEachNode([&](Node* n) { out.push_back(n); });
    return out;
  }
  // Fast iteration in Java order without snapshot allocation. Do not modify the map in f.
  template <class F>
  void forEachNode(F f) {
    if (mode_ != HashMode::Hash) {
      for (Node* n = head_; n != nullptr; n = n->after) f(n);
    } else {
      for (Node* b : table_)
        for (Node* n = b; n != nullptr; n = n->next) f(n);
    }
  }
  // Range-for over live entries (Entry*). Do not modify the map inside such a loop.
  class NodeIter {
   public:
    NodeIter(HashMap* m, bool end) : m_(m) {
      if (end) return;
      if (m->mode_ != HashMode::Hash) {
        cur_ = m->head_;
      } else {
        advanceBucket(0);
      }
    }
    Entry* operator*() const { return cur_; }
    NodeIter& operator++() {
      if (m_->mode_ != HashMode::Hash) {
        cur_ = cur_->after;
      } else if (cur_->next) {
        cur_ = cur_->next;
      } else {
        advanceBucket(bucket_ + 1);
      }
      return *this;
    }
    bool operator!=(const NodeIter& o) const { return cur_ != o.cur_; }

   private:
    void advanceBucket(std::size_t from) {
      cur_ = nullptr;
      for (bucket_ = from; bucket_ < m_->table_.size(); ++bucket_) {
        if (m_->table_[bucket_]) {
          cur_ = m_->table_[bucket_];
          return;
        }
      }
    }
    HashMap* m_;
    Node* cur_ = nullptr;
    std::size_t bucket_ = 0;
  };
  NodeIter begin() { return NodeIter(this, false); }
  NodeIter end() { return NodeIter(this, true); }

 protected:
  explicit HashMap(HashMode mode) : mode_(mode) {}
  HashMap(HashMode mode, int32_t initial_capacity) : mode_(mode) { threshold_ = tableSizeFor(initial_capacity); }

  static int32_t tableSizeFor(int32_t cap) {
    int32_t n = cap <= 1 ? 0 : static_cast<int32_t>(0xffffffffu >> __builtin_clz(static_cast<uint32_t>(cap - 1)));
    return (n < 0) ? 1 : (n >= (1 << 30)) ? (1 << 30) : n + 1;
  }
  void putMapEntries(Map<K, V>* m) {
    int32_t s = m->size();
    if (s > 0) {
      if (table_.empty()) {
        double dt = std::ceil(s / static_cast<double>(load_factor_));
        int32_t t = dt < static_cast<double>(1 << 30) ? static_cast<int32_t>(dt) : (1 << 30);
        if (t > threshold_) threshold_ = tableSizeFor(t);
      } else {
        while (s > threshold_ && static_cast<int32_t>(table_.size()) < (1 << 30)) resize();
      }
      for (Entry* e : m->entryVector()) put(e->key, e->value);
    }
  }

 private:
  bool keyEquals(const K& a, const K& b) {
    if (mode_ == HashMode::IdentityLinked) {
      if constexpr (std::is_pointer_v<K>) return a == b;
    }
    return equalsOf<K>(a, b);
  }
  int32_t spread(const K& k) {
    int32_t h;
    if (mode_ == HashMode::IdentityLinked) {
      if constexpr (std::is_pointer_v<K>) {
        using P = std::remove_cv_t<std::remove_pointer_t<K>>;
        if constexpr (std::is_base_of_v<Object, P>) h = k ? static_cast<Object*>(const_cast<P*>(k))->identityHashCode() : 0;
        else h = hashOf<K>(k);
      } else {
        h = hashOf<K>(k);
      }
    } else {
      h = hashOf<K>(k);
    }
    return h ^ static_cast<int32_t>(static_cast<uint32_t>(h) >> 16);
  }
  Node* findNode(const K& k) {
    if (table_.empty()) return nullptr;
    int32_t h = spread(k);
    for (Node* e = table_[(static_cast<int32_t>(table_.size()) - 1) & h]; e != nullptr; e = e->next)
      if (e->hash == h && keyEquals(e->key, k)) return e;
    return nullptr;
  }
  Node* newNode(int32_t h, K k, V v) {
    Node* n = new (Like{this}) Node(h, std::move(k), std::move(v));  // map's domain
    if (mode_ != HashMode::Hash) {
      n->before = tail_;
      if (tail_) tail_->after = n;
      else head_ = n;
      tail_ = n;
    }
    return n;
  }
  void unlinkOrder(Node* n) {
    if (mode_ == HashMode::Hash) return;
    if (n->before) n->before->after = n->after;
    else head_ = n->after;
    if (n->after) n->after->before = n->before;
    else tail_ = n->before;
    n->before = n->after = nullptr;
  }
  void resize() {
    int32_t old_cap = static_cast<int32_t>(table_.size());
    int32_t old_thr = threshold_;
    int32_t new_cap, new_thr = 0;
    if (old_cap > 0) {
      if (old_cap >= (1 << 30)) {
        threshold_ = Integer::MAX_VALUE;
        return;
      }
      new_cap = old_cap << 1;
      if (new_cap < (1 << 30) && old_cap >= 16) new_thr = old_thr << 1;
    } else if (old_thr > 0) {
      new_cap = old_thr;
    } else {
      new_cap = 16;
      new_thr = 12;
    }
    if (new_thr == 0) {
      float ft = static_cast<float>(new_cap) * load_factor_;
      new_thr = (new_cap < (1 << 30) && ft < static_cast<float>(1 << 30)) ? static_cast<int32_t>(ft) : Integer::MAX_VALUE;
    }
    threshold_ = new_thr;
    decltype(table_) nt(new_cap, nullptr, table_.get_allocator());
    for (int32_t j = 0; j < old_cap; ++j) {
      Node* e = table_[j];
      if (!e) continue;
      Node *lo_head = nullptr, *lo_tail = nullptr, *hi_head = nullptr, *hi_tail = nullptr;
      for (Node* nx; e != nullptr; e = nx) {
        nx = e->next;
        e->next = nullptr;
        if ((e->hash & old_cap) == 0) {
          if (lo_tail) lo_tail->next = e;
          else lo_head = e;
          lo_tail = e;
        } else {
          if (hi_tail) hi_tail->next = e;
          else hi_head = e;
          hi_tail = e;
        }
      }
      nt[j] = lo_head;
      nt[j + old_cap] = hi_head;
    }
    table_.swap(nt);
  }

  std::vector<Node*, DomainAlloc<Node*>> table_{DomainAlloc<Node*>(domain_arena_of(this))};
  int32_t size_ = 0;
  int32_t threshold_ = 0;
  float load_factor_ = 0.75f;
  HashMode mode_ = HashMode::Hash;
  Node* head_ = nullptr;
  Node* tail_ = nullptr;
};

template <class K, class V>
class LinkedHashMap : public HashMap<K, V> {
 public:
  LinkedHashMap() : HashMap<K, V>(HashMode::Linked) {}
  explicit LinkedHashMap(int32_t cap) : HashMap<K, V>(HashMode::Linked, cap) {}
  explicit LinkedHashMap(Map<K, V>* m) : HashMap<K, V>(HashMode::Linked) { this->putMapEntries(m); }
};

// java.util.IdentityHashMap: identity keys. Java's iteration order depends on random identity
// hashes; here it is insertion order (deterministic).
template <class K, class V>
class IdentityHashMap : public HashMap<K, V> {
 public:
  IdentityHashMap() : HashMap<K, V>(HashMode::IdentityLinked) {}
  explicit IdentityHashMap(int32_t cap) : HashMap<K, V>(HashMode::IdentityLinked, cap) {}
};

template <class K, class V>
using ConcurrentHashMap = HashMap<K, V>;
template <class K, class V>
using WeakHashMap = HashMap<K, V>;

// ---------------------------------------------------------------------------------------------
// HashSet / LinkedHashSet (backed by HashMap like in Java)
// ---------------------------------------------------------------------------------------------
struct Present {
  bool operator==(const Present&) const { return true; }
  int32_t hashCode() { return 0; }
};

template <class T>
class HashSet : public virtual Set<T> {
 public:
  using Collection<T>::addAll;
  HashSet() : m_(new (Like{this}) HashMap<T, Present>()) {}
  explicit HashSet(int32_t cap) : m_(new (Like{this}) HashMap<T, Present>(cap)) {}
  explicit HashSet(Collection<T>* c)
      : m_(new (Like{this}) HashMap<T, Present>(Math::max(static_cast<int32_t>(c->size() / .75f) + 1, 16))) {
    this->addAll(c);
  }
  HashSet(std::initializer_list<T> init) : HashSet() {
    for (auto& x : init) add(x);
  }
  int32_t size() override { return m_->size(); }
  bool add(T x) override {
    if (m_->containsKey(x)) return false;
    m_->put(std::move(x), Present{});
    return true;
  }
  bool contains(const T& x) override { return m_->containsKey(x); }
  bool remove(const T& x) override { return m_->removeOpt(x).has_value(); }
  void clear() override { m_->clear(); }

  class It : public Iterator<T> {
   public:
    explicit It(HashSet* s) : s_(s), entries_(s->m_->entryVector()) {}
    bool hasNext() override { return i_ < entries_.size(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      last_ = entries_[i_++];
      return last_->key;
    }
    void remove() override {
      if (!last_) throw IllegalStateException();
      s_->m_->removeOpt(last_->key);
      last_ = nullptr;
    }

   private:
    HashSet* s_;
    std::vector<MapEntry<T, Present>*> entries_;
    std::size_t i_ = 0;
    MapEntry<T, Present>* last_ = nullptr;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }

  class KeyIter {
   public:
    explicit KeyIter(typename HashMap<T, Present>::NodeIter it) : it_(it) {}
    T operator*() const { return (*it_)->key; }
    KeyIter& operator++() {
      ++it_;
      return *this;
    }
    bool operator!=(const KeyIter& o) const { return it_ != o.it_; }

   private:
    typename HashMap<T, Present>::NodeIter it_;
  };
  // Fast iteration; do not modify the set inside the loop.
  KeyIter begin() { return KeyIter(m_->begin()); }
  KeyIter end() { return KeyIter(m_->end()); }

 protected:
  explicit HashSet(HashMap<T, Present>* m) : m_(m) {}
  HashMap<T, Present>* m_;
};

template <class T>
class LinkedHashSet : public HashSet<T> {
 public:
  LinkedHashSet() : HashSet<T>(new LinkedHashMap<T, Present>()) {}
  explicit LinkedHashSet(int32_t cap) : HashSet<T>(new LinkedHashMap<T, Present>(cap)) {}
  explicit LinkedHashSet(Collection<T>* c)
      : HashSet<T>(new LinkedHashMap<T, Present>(Math::max(2 * c->size(), 11))) {
    this->addAll(c);
  }
  LinkedHashSet(std::initializer_list<T> init) : LinkedHashSet() {
    for (auto& x : init) this->add(x);
  }
};

template <class K, class V>
LinkedHashSet<K>* Map<K, V>::keySet() {
  auto* s = new LinkedHashSet<K>();
  for (Entry* e : entryVector()) s->add(e->key);
  return s;
}

// ---------------------------------------------------------------------------------------------
// TreeSet / TreeMap (SortedSet, NavigableSet, SortedMap, NavigableMap)
// ---------------------------------------------------------------------------------------------
template <class T>
struct CmpLess {
  Comparator<T>* c;
  bool natural = false;  // c is naturalOrder<T>(): call compareOf directly (same result)
  bool operator()(const T& a, const T& b) const {
    return (natural ? compareOf<T>(a, b) : (*c)(a, b)) < 0;
  }
};

template <class T>
class TreeSet : public virtual Set<T> {
 public:
  using Collection<T>::addAll;
  TreeSet()
      : cmp_(new (Like{this}) Comparator<T>(naturalOrder<T>())),
        s_(CmpLess<T>{cmp_, true}, DomainAlloc<T>(domain_arena_of(this))),
        natural_(true) {}
  explicit TreeSet(Comparator<T> c)
      : cmp_(new (Like{this}) Comparator<T>(c ? c : naturalOrder<T>())),
        s_(CmpLess<T>{cmp_, !c}, DomainAlloc<T>(domain_arena_of(this))),
        natural_(!c) {}
  using SetT = std::set<T, CmpLess<T>, DomainAlloc<T>>;  // storage in the collection's domain
  explicit TreeSet(Collection<T>* c) : TreeSet() { this->addAll(c); }
  TreeSet(Comparator<T> c, Collection<T>* src) : TreeSet(c) { this->addAll(src); }

  int32_t size() override { return static_cast<int32_t>(s_.size()); }
  bool add(T x) override { return s_.insert(std::move(x)).second; }
  bool contains(const T& x) override { return s_.find(x) != s_.end(); }
  bool remove(const T& x) override { return s_.erase(x) > 0; }
  void clear() override { s_.clear(); }
  Comparator<T> comparator() { return *cmp_; }
  // Java `set.comparator() == null`: true when the set uses natural ordering.
  bool hasNaturalOrdering() { return natural_; }

  T first() {
    if (s_.empty()) throw NoSuchElementException();
    return *s_.begin();
  }
  T last() {
    if (s_.empty()) throw NoSuchElementException();
    return *s_.rbegin();
  }
  // poll*/higher/lower/ceiling/floor return T{} (nullptr) when absent, like Java's null.
  T pollFirst() {
    if (s_.empty()) return T{};
    T x = *s_.begin();
    s_.erase(s_.begin());
    return x;
  }
  T pollLast() {
    if (s_.empty()) return T{};
    auto it = std::prev(s_.end());
    T x = *it;
    s_.erase(it);
    return x;
  }
  T ceiling(const T& x) {
    auto it = s_.lower_bound(x);
    return it == s_.end() ? T{} : *it;
  }
  T higher(const T& x) {
    auto it = s_.upper_bound(x);
    return it == s_.end() ? T{} : *it;
  }
  T floor(const T& x) {
    auto it = s_.upper_bound(x);
    if (it == s_.begin()) return T{};
    return *std::prev(it);
  }
  T lower(const T& x) {
    auto it = s_.lower_bound(x);
    if (it == s_.begin()) return T{};
    return *std::prev(it);
  }
  // Snapshots (Java returns live views).
  TreeSet* headSet(const T& to, bool inclusive = false) {
    auto* r = new TreeSet(*cmp_);
    auto e = inclusive ? s_.upper_bound(to) : s_.lower_bound(to);
    for (auto it = s_.begin(); it != e; ++it) r->s_.insert(*it);
    return r;
  }
  TreeSet* tailSet(const T& from, bool inclusive = true) {
    auto* r = new TreeSet(*cmp_);
    for (auto it = inclusive ? s_.lower_bound(from) : s_.upper_bound(from); it != s_.end(); ++it) r->s_.insert(*it);
    return r;
  }
  TreeSet* subSet(const T& from, const T& to) {
    auto* r = new TreeSet(*cmp_);
    for (auto it = s_.lower_bound(from); it != s_.end() && (*cmp_)(*it, to) < 0; ++it) r->s_.insert(*it);
    return r;
  }
  ArrayList<T>* descendingList() { return new ArrayList<T>(std::vector<T>(s_.rbegin(), s_.rend())); }

  class It : public Iterator<T> {
   public:
    explicit It(TreeSet* s) : s_(s), cur_(s->s_.begin()), last_(s->s_.end()) {}
    bool hasNext() override { return cur_ != s_->s_.end(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      last_ = cur_++;
      return *last_;
    }
    void remove() override {
      if (last_ == s_->s_.end()) throw IllegalStateException();
      s_->s_.erase(last_);
      last_ = s_->s_.end();
    }

   private:
    TreeSet* s_;
    typename SetT::iterator cur_, last_;
  };
  class DescIt : public Iterator<T> {
   public:
    explicit DescIt(TreeSet* s) : s_(s), cur_(s->s_.end()) {}
    bool hasNext() override { return cur_ != s_->s_.begin(); }
    T next() override {
      if (!hasNext()) throw NoSuchElementException();
      --cur_;
      valid_ = true;
      return *cur_;
    }
    void remove() override {
      if (!valid_) throw IllegalStateException();
      cur_ = s_->s_.erase(cur_);
      valid_ = false;
    }

   private:
    TreeSet* s_;
    typename SetT::iterator cur_;
    bool valid_ = false;
  };
  Iterator<T>* iterator() override { return new It(this); }
  Iterator<T>* iteratorIn(IterBuf* buf) override { return make_iter_in<It, T>(buf, this); }
  Iterator<T>* descendingIterator() { return new DescIt(this); }
  auto begin() { return s_.begin(); }
  auto end() { return s_.end(); }

 private:
  Comparator<T>* cmp_;
  SetT s_;
  bool natural_ = true;
};

template <class T>
using SortedSet = TreeSet<T>;
template <class T>
using NavigableSet = TreeSet<T>;

template <class K, class V>
class TreeMap : public virtual Map<K, V> {
 public:
  using Entry = MapEntry<K, V>;
  using MapT = std::map<K, Entry*, CmpLess<K>, DomainAlloc<std::pair<const K, Entry*>>>;  // domain storage
  TreeMap()
      : cmp_(new (Like{this}) Comparator<K>(naturalOrder<K>())),
        m_(CmpLess<K>{cmp_, true}, DomainAlloc<std::pair<const K, Entry*>>(domain_arena_of(this))) {}
  explicit TreeMap(Comparator<K> c)
      : cmp_(new (Like{this}) Comparator<K>(c ? c : naturalOrder<K>())),
        m_(CmpLess<K>{cmp_, !c}, DomainAlloc<std::pair<const K, Entry*>>(domain_arena_of(this))) {}
  explicit TreeMap(Map<K, V>* src) : TreeMap() { this->putAll(src); }

  int32_t size() override { return static_cast<int32_t>(m_.size()); }
  Entry* getEntry(const K& k) override {
    auto it = m_.find(k);
    return it == m_.end() ? nullptr : it->second;
  }
  V put(K k, V v) override {
    auto it = m_.find(k);
    if (it != m_.end()) {
      V old = it->second->value;
      it->second->value = std::move(v);
      return old;
    }
    Entry* e = new (Like{this}) Entry(k, std::move(v));
    m_.emplace(std::move(k), e);
    return V{};
  }
  std::optional<V> removeOpt(const K& k) override {
    auto it = m_.find(k);
    if (it == m_.end()) return std::nullopt;
    V v = it->second->value;
    m_.erase(it);
    ++erases_;
    return v;
  }
  void clear() override {
    m_.clear();
    ++erases_;
  }
  std::vector<Entry*> entryVector() override {
    std::vector<Entry*> out;
    out.reserve(m_.size());
    for (auto& kv : m_) out.push_back(kv.second);
    return out;
  }
  K firstKey() {
    if (m_.empty()) throw NoSuchElementException();
    return m_.begin()->first;
  }
  K lastKey() {
    if (m_.empty()) throw NoSuchElementException();
    return m_.rbegin()->first;
  }
  // *Entry navigation returns nullptr when absent.
  Entry* firstEntry() { return m_.empty() ? nullptr : m_.begin()->second; }
  Entry* lastEntry() { return m_.empty() ? nullptr : m_.rbegin()->second; }
  Entry* pollFirstEntry() {
    if (m_.empty()) return nullptr;
    Entry* e = m_.begin()->second;
    m_.erase(m_.begin());
    ++erases_;
    return e;
  }
  Entry* pollLastEntry() {
    if (m_.empty()) return nullptr;
    auto it = std::prev(m_.end());
    Entry* e = it->second;
    m_.erase(it);
    ++erases_;
    return e;
  }
  Entry* ceilingEntry(const K& k) {
    auto it = m_.lower_bound(k);
    return it == m_.end() ? nullptr : it->second;
  }
  Entry* higherEntry(const K& k) {
    auto it = m_.upper_bound(k);
    return it == m_.end() ? nullptr : it->second;
  }
  Entry* floorEntry(const K& k) {
    auto it = m_.upper_bound(k);
    return it == m_.begin() ? nullptr : std::prev(it)->second;
  }
  Entry* lowerEntry(const K& k) {
    auto it = m_.lower_bound(k);
    return it == m_.begin() ? nullptr : std::prev(it)->second;
  }
  TreeMap* headMap(const K& to, bool inclusive = false) {
    auto* r = new TreeMap(*cmp_);
    auto e = inclusive ? m_.upper_bound(to) : m_.lower_bound(to);
    for (auto it = m_.begin(); it != e; ++it) r->put(it->first, it->second->value);
    return r;
  }
  TreeMap* tailMap(const K& from, bool inclusive = true) {
    auto* r = new TreeMap(*cmp_);
    for (auto it = inclusive ? m_.lower_bound(from) : m_.upper_bound(from); it != m_.end(); ++it)
      r->put(it->first, it->second->value);
    return r;
  }
  // Range-for over live entries (Entry*) in key order.
  class EIter {
   public:
    explicit EIter(typename MapT::iterator it) : it_(it) {}
    Entry* operator*() const { return it_->second; }
    EIter& operator++() {
      ++it_;
      return *this;
    }
    bool operator!=(const EIter& o) const { return it_ != o.it_; }

   private:
    typename MapT::iterator it_;
  };
  EIter begin() { return EIter(m_.begin()); }
  EIter end() { return EIter(m_.end()); }

  // Raw navigation for iterators that want O(1) steps. A MapIt stays valid as long as
  // eraseCount() is unchanged (insertions never invalidate std::map iterators).
  using MapIt = typename MapT::iterator;
  MapIt mapBegin() { return m_.begin(); }
  MapIt mapEnd() { return m_.end(); }
  MapIt mapUpperBound(const K& k) { return m_.upper_bound(k); }
  uint64_t eraseCount() const { return erases_; }

 private:
  Comparator<K>* cmp_;
  MapT m_;
  uint64_t erases_ = 0;
};

template <class K, class V>
using SortedMap = TreeMap<K, V>;
template <class K, class V>
using NavigableMap = TreeMap<K, V>;
template <class K, class V>
using ConcurrentSkipListMap = TreeMap<K, V>;

// ---------------------------------------------------------------------------------------------
// java.util.Collections / Arrays helpers
// ---------------------------------------------------------------------------------------------
struct Collections {
  template <class T>
  static void sort(List<T>* l, Comparator<T> c = nullptr) {
    l->sort(c);
  }
  template <class T>
  static void reverse(List<T>* l) {
    auto v = l->toVector();
    std::reverse(v.begin(), v.end());
    for (int32_t i = 0; i < static_cast<int32_t>(v.size()); ++i) l->set(i, v[i]);
  }
  template <class T>
  static void swap(List<T>* l, int32_t i, int32_t j) {
    T a = l->get(i);
    l->set(i, l->set(j, a));
  }
  // Same algorithm as java.util.Collections.shuffle(List, Random).
  template <class T>
  static void shuffle(List<T>* l, Random* rnd) {
    for (int32_t i = l->size(); i > 1; --i) swap(l, i - 1, rnd->nextInt(i));
  }
  template <class T>
  static T max(Collection<T>* c, Comparator<T> cmp = nullptr) {
    if (!cmp) cmp = naturalOrder<T>();
    ScopedIter<T> it(c);
    T best = it->next();
    while (it->hasNext()) {
      T x = it->next();
      if (cmp(x, best) > 0) best = x;
    }
    return best;
  }
  template <class T>
  static T min(Collection<T>* c, Comparator<T> cmp = nullptr) {
    if (!cmp) cmp = naturalOrder<T>();
    ScopedIter<T> it(c);
    T best = it->next();
    while (it->hasNext()) {
      T x = it->next();
      if (cmp(x, best) < 0) best = x;
    }
    return best;
  }
  template <class T>
  static ArrayList<T>* emptyList() {
    return new ArrayList<T>();
  }
  template <class T>
  static HashSet<T>* emptySet() {
    return new HashSet<T>();
  }
  template <class K, class V>
  static HashMap<K, V>* emptyMap() {
    return new HashMap<K, V>();
  }
  template <class T>
  static ArrayList<T>* singletonList(T x) {
    return new ArrayList<T>({x});
  }
  template <class T>
  static ArrayList<T>* nCopies(int32_t n, T x) {
    return new ArrayList<T>(std::vector<T>(n, x));
  }
  template <class C>
  static C* unmodifiable(C* c) {
    return c;
  }
  template <class T>
  static int32_t frequency(Collection<T>* c, const T& x) {
    int32_t n = 0;
    for (ScopedIter<T> it(c); it->hasNext();)
      if (equalsOf<T>(it->next(), x)) ++n;
    return n;
  }
};

struct Arrays {
  template <class T>
  static void fill(Array<T>* a, T v) {
    std::fill(a->begin(), a->end(), v);
  }
  template <class T>
  static void fill(Array<T>* a, int32_t from, int32_t to, T v) {
    std::fill(a->begin() + from, a->begin() + to, v);
  }
  template <class T>
  static Array<T>* copyOf(Array<T>* a, int32_t n) {
    auto* r = new Array<T>(n);
    std::copy(a->begin(), a->begin() + std::min(n, a->length), r->begin());
    return r;
  }
  template <class T>
  static Array<T>* copyOfRange(Array<T>* a, int32_t from, int32_t to) {
    auto* r = new Array<T>(to - from);
    std::copy(a->begin() + from, a->begin() + std::min(to, a->length), r->begin());
    return r;
  }
  template <class T>
  static ArrayList<T>* asList(std::initializer_list<T> init) {
    return new ArrayList<T>(init);
  }
  template <class T>
  static ArrayList<T>* asList(Array<T>* a) {
    return new ArrayList<T>(a->toVector());
  }
  template <class T>
  static void sort(Array<T>* a, Comparator<T> c = nullptr) {
    if (!c) c = naturalOrder<T>();
    std::stable_sort(a->begin(), a->end(), [&](const T& x, const T& y) { return c(x, y) < 0; });
  }
  template <class T>
  static void sort(Array<T>* a, int32_t from, int32_t to, Comparator<T> c = nullptr) {
    if (!c) c = naturalOrder<T>();
    std::stable_sort(a->begin() + from, a->begin() + to, [&](const T& x, const T& y) { return c(x, y) < 0; });
  }
  template <class T>
  static bool equals(Array<T>* a, Array<T>* b) {
    if (a == b) return true;
    if (!a || !b || a->length != b->length) return false;
    for (int32_t i = 0; i < a->length; ++i)
      if (!equalsOf<T>((*a)[i], (*b)[i])) return false;
    return true;
  }
  template <class T>
  static int32_t hashCode(Array<T>* a) {
    if (!a) return 0;
    int32_t h = 1;
    for (auto& x : *a) h = hashCombine(h, hashOf<T>(x));
    return h;
  }
  template <class T>
  static std::string toString(Array<T>* a) {
    if (!a) return "null";
    std::string s = "[";
    for (int32_t i = 0; i < a->length; ++i) {
      if (i) s += ", ";
      s += str((*a)[i]);
    }
    return s + "]";
  }
};

// System.arraycopy
template <class T>
void arraycopy(Array<T>* src, int32_t src_pos, Array<T>* dst, int32_t dst_pos, int32_t n) {
  if (n <= 0) return;
  if (src_pos < 0 || dst_pos < 0 || src_pos + n > src->length || dst_pos + n > dst->length)
    throw ArrayIndexOutOfBoundsException("arraycopy: last source index out of bounds");
  if (src == dst && src_pos < dst_pos) {
    std::copy_backward(src->data() + src_pos, src->data() + src_pos + n, dst->data() + dst_pos + n);
  } else {
    std::copy(src->data() + src_pos, src->data() + src_pos + n, dst->data() + dst_pos);
  }
}

// Objects.hash(a, b, ...)
template <class... Ts>
int32_t hashAll(const Ts&... xs) {
  int32_t h = 1;
  ((h = hashCombine(h, hashOf<Ts>(xs))), ...);
  return h;
}

}  // namespace jrt
