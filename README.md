# freerouting-cpp

A C++20 port of [freerouting](https://github.com/freerouting/freerouting) — the open-source
printed circuit board autorouter — with a headless command line, a KiCad plugin and an
experimental Lua/Nuklear user interface.

The port follows the Java sources (freerouting v2.x, upstream commit `f3fbba4`) class by class,
so its routing results are **byte-for-byte identical to Java's**: the Specctra session (`.ses`) it
writes for a design is the same file Java writes. Differences are only in the runtime:

- no garbage collector: explicit owners, operation regions and arenas
  ([MEMORY_MODEL.md](MEMORY_MODEL.md));
- the optimizer runs its candidates on a thread pool like Java (`-mt N`), results do not depend on
  the thread count;
- native Linux and Windows builds (a single static `freerouting-cpp.exe`).

## Status

| | |
|---|---|
| Autorouter, fanout, optimizer, DRC, Specctra DSN / `.rules` input, SES output | ported, SES identical to Java on the test corpus |
| KiCad project inputs | `.kicad_dru` custom clearance rules, `.kicad_pro` minimum track width, `.kicad_pcb` zone fill settings (`-de board.dsn+board.kicad_dru+...`) |
| CLI | `-de/-do/-dr/-mt`, `--router.<setting>=<value>` (Java's JSON setting names) |
| KiCad | Action Plugin: route the open board with one button ([integrations/kicad](integrations/kicad)) |
| UI | Lua + Nuklear + MoonGL: view, autoroute, interactive routing and dragging ([ui/](ui)) |
| Not ported | Java GUI (Swing), API server, MCP, KiCad JSON board input, KiCad DRC report, SES input, result JSON |

Speed (one thread, same machine): from about Java's speed on large boards with long post-processing
to several times faster on boards dominated by the optimizer; see the speed comparison in
[MEMORY_MODEL.md](MEMORY_MODEL.md).

## Command line

```
freerouting-cpp -de board.dsn -do board.ses [-dr board.rules] [-mt 4] [-mp 50]
                [--timeout PT10M] [--no-optimizer] [--no-drc]
                [--router.<setting>=<value> ...] [-q|-v|-vv]
```

With a KiCad design, its custom rules, project and board file can be given with the DSN, in any
order (the extension decides), as in Java: `-de board.dsn+board.kicad_dru+board.kicad_pro+board.kicad_pcb`.

Examples of `--router.*` settings (the same paths as Java's CLI and `freerouting.json`):

```
--router.scoring.via_costs=200            more expensive vias: fewer vias
--router.automatic_neckdown=false         no narrower traces at small pins
--router.fanout.enabled=false             skip the SMD fanout stage
--router.autorouter.fallback_strategies=none
--router.optimizer.max_passes=10
```

`-mt N` sets the optimizer threads (default: available processors − 1, like Java; 4–8 is
usually fastest).

## Building

### Linux (and WSL)

Needs a C++20 compiler (GCC 13+), CMake 3.25+, Ninja, GMP and GEOS:

```bash
sudo apt install g++ cmake ninja-build libgmp-dev libgeos-dev
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DJRT_CHECKED=0
cmake --build build --target freerouting-cpp
```

`-DJRT_CHECKED=0` turns off the runtime's internal consistency checks (bounds and lifetime checks
of the Java runtime emulation) for release builds.

### Windows (cross-compiled in WSL / Linux)

```bash
sudo apt install g++-mingw-w64-x86-64-posix cmake ninja-build make m4 curl
cmake -G Ninja -B build-win -DCMAKE_TOOLCHAIN_FILE=tools/windows/mingw-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DJRT_CHECKED=0
cmake --build build-win --target freerouting-cpp
```

CMake downloads GMP and GEOS (checked by SHA-256), builds them as static libraries
([cmake/BundledDeps.cmake](cmake/BundledDeps.cmake)) and produces one self-contained
`freerouting-cpp.exe`. `-DFR_BUNDLED_DEPS=ON` does the same for a Linux build.

## KiCad plugin

[integrations/kicad](integrations/kicad) — an Action Plugin for the PCB editor (KiCad 6–10):
exports the board to Specctra DSN, runs `freerouting-cpp` (the native `.exe` next to the plugin,
or the Linux binary in WSL), shows progress with a cancel button and imports the session back.
Installation: [README](integrations/kicad/README.md), in Russian:
[INSTALL.ru.md](integrations/kicad/INSTALL.ru.md).

## Repository layout

```
runtime/jrt/        Java runtime emulation: Object, collections in Java order, BigInteger (GMP),
                    the allocator (heap, job heap, arenas, regions)
runtime/platform/   operating-system services: linux.{h,cpp}, windows.{h,cpp}
src/                the ported freerouting core (mirrors the Java packages) and src/main.cpp (CLI)
bindings/lua/, ui/  Lua module and the Nuklear user interface
integrations/kicad/ KiCad plugin
tests/              runtime unit tests, BigInteger fuzzing against GMP
tools/              parity checks against Java (parity/, parity_corpus.sh, smoke.sh),
                    benchmarks, memory tools (mem/), Windows toolchain, porting generators
docs/porting/       notes of the porting phases
PORTING.md          Java -> C++ translation rules
MEMORY_MODEL.md     the memory model without a garbage collector
```

## Verifying against Java

The parity tools route the upstream test fixtures with both implementations and compare the SES
files byte for byte: `tools/smoke.sh` (about 40 boards, a few minutes), `tools/parity_sample.sh`,
`tools/parity_corpus.sh` (all fixtures; needs the Java build of freerouting). The fixtures and the
Java sources are expected in `upstream/` (a checkout of freerouting, not part of this repository).

## License

GPL-3.0, like freerouting: this is a derivative work of freerouting
(Copyright © the freerouting contributors). See [LICENSE](LICENSE).
Bundled into Windows builds: GMP (LGPL-3.0) and GEOS (LGPL-2.1). The UI fonts (Roboto, Droid Sans)
are under the Apache License 2.0.
