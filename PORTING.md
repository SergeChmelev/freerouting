# freerouting C++ port — porting rules

This repository is a faithful C++20 port of the routing core of
[freerouting](https://github.com/freerouting/freerouting) at commit
`f3fbba4a16c57c6e2f3c430d32dce95847d761dc` (v2.5.1-SNAPSHOT). The Java reference sources are in
`upstream/src/main/java/app/freerouting/` (read-only, not part of the build).

**Goal: behavioural parity.** Same input + same settings must give the same routing result as
Java. Therefore the port is *mechanical*: same classes, same method names, same algorithms,
same iteration orders. Do not "improve", simplify or reorder algorithms. Do not drop code paths
because they look unimportant. When Java does something odd, port the oddity. If you must
deviate, leave a `// PORT NOTE: ...` comment explaining why.

Scope: the classes listed in `tools/classes.txt` (transitive closure of
`core.facade.FreeroutingHeadlessEngine`, without GUI / KiCad / REST / analytics). Every type in
those files (including nested types) is listed in `tools/classmap.tsv` with its C++ name and
header. References to classes **outside** that list (gui, api, management, analytics, kicad,
util.gson, debug) must be removed / stubbed with a `// PORT NOTE` (e.g. GUI callbacks become
no-ops, KiCad-specific inputs are treated as absent).

## Build

Linux / WSL, g++ 15, CMake + Ninja, Boehm GC (`libgc`), GMP. Build with `tools/build.sh` inside
WSL (`wsl.exe -d Ubuntu -e bash -lc 'cd /mnt/c/Users/HP/VibeCodes/freerouting && tools/build.sh'`).
Compiler flags include `-fwrapv` (Java int overflow semantics).

## Files, names, namespaces

* One Java top-level type `app.freerouting.<pkg>.<Name>` → `src/<pkg as path>/<Name>.h` and
  `src/<pkg as path>/<Name>.cpp`. Example: `board.model.items.Trace` →
  `src/board/model/items/Trace.h/.cpp`. Include paths are relative to `src/`:
  `#include "board/model/items/Trace.h"`.
* Namespaces: `io.specctra` and `io.specctra.parser` → `namespace fr::dsn`; `io` → `namespace
  fr::io`; everything else → `namespace fr`. Inside `fr::dsn`, refer to the core types that
  collide in name with parser types (`Net`, `NetClass`, `Shape`, `Polygon`, `Circle`, `Layer`,
  `LayerStructure`, `Component`, `Package`, `Unit`) as `fr::Net` etc.
* Class names are the Java simple names. **Nested / inner Java types are hoisted to namespace
  scope as `Outer_Inner`** (deeper: `Outer_Mid_Inner`) and are defined in the outer type's
  header (before the outer class when the outer class needs them complete). `tools/classmap.tsv`
  is authoritative for every C++ name.
* Method and field names: **exactly the Java names** (Java snake_case stays snake_case,
  camelCase stays camelCase), so call sites translate mechanically. Exceptions:
  * Identifier is a C++ keyword or common macro (`delete`, `union`, `register`, `template`,
    `typename`, `operator`, `friend`, `namespace`, `signed`, `unsigned`, `auto`, `and`, `or`,
    `not`, `xor`, `export`, `extern`, `inline`, `mutable`, `explicit`, `virtual`, `struct`,
    `typedef`, `sizeof`, `NULL`, `EOF`, `errno`, `stdin`, `stdout`, `stderr`, `assert`,
    `unix`, `linux`) → append `_` (`delete_`).
  * A field and a method with the same name in one class (legal in Java, not in C++) → the
    **field** gets a trailing `_`. Java records: component field `x_`, accessor `x()`.
* `src/fwd.h` (generated) forward-declares every type. Headers include `fwd.h`, `jrt/jrt.h`,
  and only the headers they really need (base classes, enums whose enumerators are used inline,
  templates). Everything else is used through pointers and forward declarations. `.cpp` files
  include whatever they use.
* Enums: `enum class Name : int32_t { ... }` — the `: int32_t` is mandatory (matches fwd.h).

## Memory model (important)

The process uses the **Boehm garbage collector for all `new` allocations** (see
`runtime/jrt/gc.cpp`). So Java reference semantics map 1:1:

* Java object reference `Foo` → raw pointer `Foo*`. `new Foo(...)` → `new Foo(...)`.
  `null` → `nullptr`. **Never `delete`**, never use `std::shared_ptr` / `unique_ptr` for ported
  objects, no destructors with side effects (they never run for collected objects).
* Java arrays → `jrt::Array<T>*` (`new jrt::Array<int32_t>(n)` is zero-initialised like Java;
  `arr->length`, `(*arr)[i]`, `jrt::Array<T>::of({a, b})`, `jrt::newArray2<T>(n, m)`,
  `System.arraycopy` → `jrt::arraycopy`).
* Java collections → pointers to `jrt` collections with **the same declared type as in Java**
  (keep interface vs concrete exactly as Java declares it, so all agents agree on signatures):

  | Java | C++ |
  |---|---|
  | `Collection<T>` | `jrt::Collection<T>*` |
  | `List<T>` | `jrt::List<T>*` |
  | `ArrayList<T>`, `Vector<T>` | `jrt::ArrayList<T>*` |
  | `LinkedList<T>` | `jrt::LinkedList<T>*` |
  | `Set<T>` | `jrt::Set<T>*` |
  | `HashSet<T>` / `LinkedHashSet<T>` | `jrt::HashSet<T>*` / `jrt::LinkedHashSet<T>*` |
  | `SortedSet<T>`, `NavigableSet<T>`, `TreeSet<T>` | `jrt::TreeSet<T>*` |
  | `Map<K,V>` | `jrt::Map<K,V>*` |
  | `HashMap` / `LinkedHashMap` / `IdentityHashMap` / `ConcurrentHashMap` | same name in `jrt` |
  | `SortedMap`, `NavigableMap`, `TreeMap` | `jrt::TreeMap<K,V>*` |
  | `Queue<T>` / `Deque<T>` / `ArrayDeque<T>` / `PriorityQueue<T>` | same name in `jrt` |
  | `Iterator<T>` | `jrt::Iterator<T>*` |
  | `Map.Entry<K,V>` | `jrt::MapEntry<K,V>*` |
  | `Comparator<T>` | `jrt::Comparator<T>` (a `std::function<int32_t(T,T)>`, by value) |

  Element types follow the same rules (`List<Item>` → `jrt::List<Item*>*`,
  `Map<String,Integer>` → `jrt::Map<std::string,int32_t>*`).
* `jrt` collection semantics to remember:
  * `List.remove(int index)` → `removeAt(i)`; `remove(Object)` → `remove(x)`.
  * `map.get(k)` returns `V{}` (nullptr for pointers) when absent. If V is a boxed primitive and
    Java tests for null, use `getOpt(k)` (returns `std::optional<V>`). `put` returns previous
    value or `V{}`; `remove(k)` likewise, `removeOpt(k)` gives an optional.
  * `keySet()` (a `LinkedHashSet` in map order), `values()`, `entrySet()` and TreeSet/TreeMap
    `headSet/tailSet/subSet/headMap/tailMap` return **snapshots**, not live views. If Java
    mutates through a view (`map.keySet().removeIf(...)`, `map.values().remove(x)`,
    `list.subList(a,b).clear()`), rewrite it as explicit operations on the backing collection
    (`ArrayList::removeRange`, iterate entries and `remove`).
  * Queue/Deque/TreeSet "null on empty" methods (`poll`, `peek`, `pollFirst`, `higher`,
    `ceiling`, ...) return `T{}`; for non-pointer element types use the `*Opt` variants.
  * Range-for works on all collections: `for (Item* it : *list)`; maps:
    `for (auto* e : *map->entrySet())` → `e->getKey()`, `e->getValue()`. Concrete types iterate
    fast; interface-typed (`jrt::Collection<T>*`) loops go through a virtual iterator.
  * Explicit iterator with removal: `for (auto it = c->iterator(); it->hasNext();) { auto x =
    it->next(); ... it->remove(); }`.
  * `Collections.sort(l, c)`, `l.sort(c)`, `Arrays.sort` are **stable** like Java. Comparators
    are lambdas returning `int32_t`. `Comparator.comparingInt(f)` → `jrt::comparingInt<T>(f)`,
    `thenComparing` → `jrt::thenComparing`, `reversed()` → `jrt::reverseOrder<T>(c)`.
  * `HashMap/HashSet` reproduce Java's iteration order given Java-compatible `hashCode()`s.
    Integer/Long/Double/Boolean/String/enum keys hash exactly like Java (enums hash by ordinal —
    Java's would be random). Ported classes that override `hashCode()/equals()` in Java **must**
    override them identically in C++ (see Object below).
* Declaring a field `jrt::ArrayList<X*>*` with `X` only forward-declared is fine, but
  **instantiating** it (`new jrt::ArrayList<X*>()`, also via a default member initializer or an
  inline constructor) needs `X` complete (hashing/equality check `X`'s base classes). So create
  collections of forward-declared element types in the `.cpp` constructor, not in the header.
* `java.util.function.*` (`Function`, `Predicate`, `Consumer`, `Supplier`, `BiFunction`,
  `IntUnaryOperator`, ...) → `std::function<...>` by value. The project's own interfaces stay
  abstract classes even if Java passes lambdas to them; at lambda call sites write a small
  adapter class (or a `static X* of(std::function<...>)` helper in the interface's header).
* Streams (`stream().filter().map().collect()`) → explicit loops, preserving order.

## Classes, interfaces, inheritance

* Every root class and every interface derives **`public virtual jrt::Object`**. Interfaces are
  abstract classes; Java interface inheritance and `implements` are **always `public virtual`**.
  The superclass (`extends`) is inherited normally (`public Base`).
  ```cpp
  class Trace : public Item, public virtual Connectable { ... };      // Item -> virtual Object
  class Shape : public virtual Area { ... };                           // interface
  ```
* All methods that can be overridden in Java are `virtual` (Java methods are virtual unless
  `private`, `static` or `final`; `final` methods may be non-virtual). Use `override`.
  Abstract methods → `= 0`. Interface `default` methods → virtual with body.
* **No `const` on methods** and no `const` pointers to ported types (Java has no const). Pass
  `std::string` as `const std::string&`.
* `jrt::Object` provides `virtual bool equals(jrt::Object*)`, `virtual int32_t hashCode()`,
  `virtual std::string toString()`. Port Java overrides with these exact signatures
  (`bool equals(jrt::Object* o) override` → `auto* other = dynamic_cast<Foo*>(o)`). Default
  `hashCode()` is a deterministic identity hash.
* `instanceof` → `dynamic_cast<X*>(p) != nullptr` (or `jrt::instanceof<X>(p)`); pattern
  `if (o instanceof Foo f)` → `if (auto* f = dynamic_cast<Foo*>(o))`. Casts: from an interface
  type or `Object*` you **must** use `dynamic_cast` (virtual base); class→subclass may use
  `static_cast`. `getClass() == o.getClass()` → `typeid(*this) == typeid(*o)`;
  `getClass().getSimpleName()` → `jrt::className(obj)`.
* Constructors: Java `this(...)` → delegating constructor; `super(...)` → base initializer.
  **Virtual calls inside constructors** behave differently in C++ (they don't dispatch to the
  subclass). If a Java constructor relies on virtual dispatch, restructure with a
  `// PORT NOTE` (e.g. two-phase init).
* Inner (non-static) classes get an explicit `Outer* outer` pointer. Anonymous classes →
  local classes or lambdas.
* Generic classes → templates (header-only). Generic methods → templates, or plain pointer
  types if all instantiations are the same. Bounded wildcards (`? extends Item`) → the bound.
* Records → classes with private fields `name_`, accessors `name()`, a constructor with all
  components, and `equals`/`hashCode`/`toString` like Java records (hash = `31*h + hash(c)`
  over components in order).
* Java enums → `enum class E : int32_t { A, B };` plus free functions in the same header for
  everything else: `E_name(E)`, `E_toString(E)` (Java `toString()`, usually the name),
  `E_values()` → `jrt::Array<E>*`, `E_valueOf(const std::string&)`, and each enum method `foo`
  as `E_foo(E self, ...)`. Enum fields become `switch`/lookup tables inside those functions.
  `e.ordinal()` → `static_cast<int32_t>(e)`. `EnumMap/EnumSet` → `jrt::TreeMap/TreeSet`.

## Statics

* `static final` primitive constants → `static constexpr`. String constants →
  `static inline const std::string NAME = "...";`.
* Reference-typed static fields (objects, arrays, collections) → **static accessor functions**
  with a function-local static, to avoid C++ static-initialisation-order bugs:
  ```cpp
  static IntPoint* ZERO();                       // header
  IntPoint* IntPoint::ZERO() { static IntPoint* v = new IntPoint(0, 0); return v; }  // .cpp
  ```
  Callers write `IntPoint::ZERO()`. Mutable reference statics return a reference:
  `static Foo*& current();`. Mutable primitive statics → `static inline int32_t x = 0;`.
  Java `static { ... }` blocks go into the accessor that needs them.

## Numbers and Java semantics (critical for parity)

* `int` → `int32_t`, `long` → `int64_t`, `short` → `int16_t`, `byte` → `int8_t`,
  `char` → `char` (ASCII text) or `char16_t` if arithmetic on UTF-16 matters, `boolean` → `bool`,
  `float`, `double` as is. Boxed nullable `Integer`/`Double`/`Boolean` → `std::optional<...>`;
  non-nullable boxed → the primitive.
* **`(int) d` / `(long) d` for floating `d` → `jrt::d2i(d)` / `jrt::d2l(d)`** (Java saturates;
  a plain C++ cast is undefined behaviour). `(int) someLong` → `jrt::l2i(v)` or
  `static_cast<int32_t>` (wraps).
* `Math.*` → `jrt::Math::*` (`Math.round(double)` returns `int64_t`, `Math.round(float)`
  returns `int32_t`, `Math.abs(Integer.MIN_VALUE)` etc. behave like Java). Beware overload
  resolution: `Math.max(int, long)` in Java widens to long — cast explicitly in C++.
* `>>>` → `jrt::ushr(v, n)`. Shift counts above 31/63 → `jrt::shl/shr`.
  Integer `/` and `%` are fine (truncating); if the divisor can be -1 with MIN_VALUE or 0,
  use `jrt::idiv` / `jrt::ldiv`.
* `Integer.MAX_VALUE` → `jrt::Integer::MAX_VALUE`, likewise `Long`, `Double`
  (`Double::MAX_VALUE`, `Double::POSITIVE_INFINITY`, `Double::compare`, `Double::isNaN`...).
* `java.math.BigInteger` → `jrt::BigInteger` (**value type**, same method names;
  `BigInteger.valueOf(x)` → `jrt::BigInteger(x)`/`BigInteger::valueOf(x)`, `BigInteger.ONE` →
  `jrt::BigInteger::ONE()`); `BigDecimal` → `jrt::BigDecimal`; commons `BigFraction` →
  `jrt::BigFraction`. A nullable BigInteger field → `std::optional<jrt::BigInteger>`.
* Floating point expressions: keep the exact Java evaluation order and types (`float` vs
  `double`, int→double conversions), otherwise results drift.

## Strings, formatting, I/O

* `String` → `std::string` (UTF-8). If `null` is meaningful, `std::optional<std::string>`.
* **Concatenation of non-strings must use `jrt::str(x)`** (`"w=" + jrt::str(width)`), which
  prints numbers exactly like Java (`Double.toString` format!). Enums: `E_toString(e)`.
* `String.format(...)`/`formatted` → `jrt::format("%d ...", ...)` (printf; pass `.c_str()` for
  `%s`, use `%lld` with `(long long)` for longs, Java `%n` → `\n`). Locale is always ROOT/US.
* `StringBuilder` → `std::string` (`+=`). `equals` → `==`, `equalsIgnoreCase`,
  `startsWith`, `compareTo`, `trim`, `split`, `toLowerCase`, ... → helpers in `jrt/lang.h`.
  `s.hashCode()` → `jrt::stringHashCode(s)`. `Integer.parseInt` → `jrt::Integer::parseInt`, etc.
* Streams: `InputStream`/`Reader` → `std::istream&` (or `std::istream*`), `OutputStream`/`Writer`
  → `std::ostream&`. Files → `std::ifstream`/`std::ofstream` **on the stack** (destructors of
  GC'd objects never run, so a heap stream would never be flushed/closed).
* Exceptions: `jrt::Exception`, `RuntimeException`, `IllegalArgumentException`,
  `IllegalStateException`, `IOException`, ... (see `jrt/lang.h`). `catch (Exception e)` →
  `catch (const jrt::Exception& e)`; `e.getMessage()` works. Java `Error`s rarely need porting.

## Logging

`FRLogger` is ported in `src/logger/`. Keep the calls (they document the algorithm). `trace(...)`
is a no-op unless enabled; do not build expensive strings when tracing is disabled (guard with
the same checks Java uses).

## Threads

The port is **single-threaded**. Remove `synchronized`, locks, `volatile`, atomics (use plain
fields), `ConcurrentHashMap` → `jrt::ConcurrentHashMap` (= HashMap). Thread pools / executors /
futures: run the tasks **sequentially, in submission order, on the calling thread**, keeping the
exact same result-merging logic (the Java code is designed to be deterministic regardless of
scheduling). `Thread.currentThread().isInterrupted()` → `false`. Keep `StoppableThread`-style
stop/timeout checks (they only stop work).

## Serialization

`Serializable`, `transient`, `serialVersionUID`, `readObject/writeObject` are dropped. The places
that deep-copy a board through Java serialization (`RoutingBoard.deepCopy()`,
`BoardSnapshotManager`) are re-implemented explicitly (see notes in those files) — the copy must
be fully independent in all mutable state and keep item ids and item iteration order.

## Style

Google-ish C++: 2-space indent, `#pragma once`, keep Java's comments that explain algorithms
(shortened Javadoc is fine), no `using namespace` in headers. Prefer readability of the
mechanical mapping over cleverness. Keep the Java method order inside files so the two can be
diffed side by side.
