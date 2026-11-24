# Phase 2 brief: implement the .cpp files

You are one of ~17 agents porting the Java PCB autorouter freerouting to C++20 in parallel.
Repo: `C:\Users\HP\VibeCodes\freerouting` (Windows checkout; compile in WSL Ubuntu).
Phase 1 is done: **every ported class already has a complete C++ header** (`src/**/*.h`, all
352 compile, standalone and together). Your job is to write the `.cpp` implementations for the
files of ONE bucket, as a faithful mechanical port of the Java method bodies.

## Read first
1. `PORTING.md` — the translation rules. Follow them exactly.
2. `docs/porting/phase1-decisions.md` — cross-cutting decisions made while writing headers (renamed
   fields, static accessors, optional<> uses, two-phase init, board snapshots, deepCopy hooks…).
3. `runtime/jrt/lang.h`, `collections.h`, `bignum.h` — the Java runtime emulation.
4. Your bucket's file list in `docs/porting/phase2-buckets.md` (paths without extension). For each
   entry `src/<path>`: Java source = `upstream/src/main/java/app/freerouting/<path>.java`,
   header = `src/<path>.h`, you write `src/<path>.cpp`.

## Rules
* Port **every** method body that is declared but not defined in the header (and the
  out-of-line parts of hoisted nested types, records' equals/hashCode/toString, enum helpers
  that are only declared, static accessors). Keep Java's statement order, expression order,
  int/long/float/double types, loop structure and iteration order. No simplifications, no
  "improvements", no skipped branches. Odd Java behaviour is ported as is. Deviations only with
  a `// PORT NOTE:` comment.
* When calling into other classes, **read their C++ header** for the exact API (names,
  pointer vs value, optional<>, static accessor `X::NAME()`, `removeAt`, renamed fields with `_`,
  etc.). Never guess from the Java source alone.
* You may edit **your own bucket's headers** (add private helpers, fix a wrong type, add an
  include). Avoid changing public signatures; if you must, check every caller with grep and
  list the change in your report. **Do not edit files of other buckets**, `runtime/`,
  `PORTING.md`, `src/fwd.h`. If another bucket's header is wrong or lacks something, work around
  it locally if trivial, otherwise leave a `// TODO(port): ...` and report it.
* Memory: Boehm GC, raw pointers, `new`, never `delete`. Streams on the stack.
* Numbers: `jrt::d2i/d2l` for float→int casts, `jrt::Math::*`, `jrt::str(x)` for string
  concatenation of numbers, `jrt::format` for `String.format`.
* Single-threaded: drop synchronization; executors run tasks sequentially in submission order.
* Logging: keep FRLogger calls (see `src/logger/FRLogger.h` for the API).

## Verify
Syntax/instantiation check of your files (from Windows):
`wsl.exe -d Ubuntu -e bash -lc 'cd /mnt/c/path/to/freerouting && tools/check_cpp.sh src/<file>.cpp ...'`
(a directory argument checks every .cpp directly in it). Your files must end with 0 errors.
Errors that come only from another bucket's header must be reported, not "fixed" there.
Do not run the full CMake build (other agents are mid-way; it will not link yet).
Do not run git commands.
**Do not spawn sub-agents** (the user is watching plan-usage limits); do all the work yourself.

## Report (under 350 words)
Files written; anything not ported or ported with a deviation (PORT NOTEs that change
behaviour); public signature changes you made in your headers; problems found in other
buckets' headers (file, what is wrong, what you need); check result.
