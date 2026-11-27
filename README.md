# freerouting-cpp

A C++20 port of [freerouting](https://github.com/freerouting/freerouting) — the open-source
printed circuit board autorouter — with a headless command line, a KiCad plugin and an
experimental Lua/Nuklear user interface.

The port exists to make the autorouter easier to integrate natively into EDA systems as a
plugin: a single native executable (or library) with no Java runtime to install, start or ship.

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
| MCP server | `freerouting-cpp --mcp` (stdio): route a design from Claude Desktop, Cursor and other MCP clients |
| REST API | `freerouting-cpp --api`: Java's local API v1 (sessions, jobs, `/v1/autoroute`, `/v1/mcp`) on 127.0.0.1:37864 |
| Not ported | Java GUI (Swing), KiCad JSON board input/output, KiCad DRC report (the DRC summary is ported), SES input, result JSON; the API's cloud parts (API keys, analytics, rate limits, Swagger) |

Speed: about **20 % faster than the Java version** on average (same machine, 1, 4 and 10 threads,
six large test boards; from 5–10 % on the slowest boards to about a third on others, several
times on small ones), with less CPU time and usually less memory. These numbers are for the
profile-guided build (`tools/pgo_build.sh`, below); see the table in
[MEMORY_MODEL.md](MEMORY_MODEL.md) (E20).

## Command line

```
freerouting-cpp -de board.dsn -do board.ses [-dr board.rules] [-mt 4] [-mp 50]
                [--timeout PT10M] [--no-optimizer] [--no-drc]
                [--router.<setting>=<value> ...] [-q|-v|-vv]
freerouting-cpp --mcp                                 MCP server on stdin / stdout
freerouting-cpp --api [--port 37864] [--host 127.0.0.1]   local REST API v1
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

The fastest build is profile-guided (GCC): `tools/pgo_build.sh` builds an instrumented binary,
routes a few test boards with it and rebuilds with the profile (about 40 minutes; the result is
`~/build/fr-pgo/freerouting-cpp`, `FR_BUILD_DIR` changes it). It is 8–11 % faster than a plain
release build.

### macOS

Needs Xcode 16.3+ (or its Command Line Tools: Apple clang 17) and Homebrew:

```bash
brew install cmake ninja gmp geos
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DJRT_CHECKED=0
cmake --build build --target freerouting-cpp
```

CMake finds GMP and GEOS under Homebrew's prefix. The Lua module of the UI (`ui/`) is built on
Linux only. The macOS build has not been tested yet.

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

The MCP and REST servers use two header-only libraries, nlohmann/json 3.11.3 and cpp-httplib
0.18.7 (MIT). Nothing of them is in the repository: CMake uses installed ones if it finds them
(`apt install nlohmann-json3-dev libcpp-httplib-dev`), otherwise it downloads the pinned headers
into the build directory and checks their SHA-256 ([cmake/ServerDeps.cmake](cmake/ServerDeps.cmake));
Windows builds always use the pinned ones (`-DFR_FETCH_SERVER_DEPS=ON` does that on Linux too).

## KiCad plugin

[integrations/kicad](integrations/kicad) — an Action Plugin for the PCB editor (KiCad 6–10):
exports the board to Specctra DSN, runs `freerouting-cpp` (the native `.exe` next to the plugin,
or the Linux binary in WSL), shows progress with a cancel button and imports the session back.
Installation: [README](integrations/kicad/README.md), in Russian:
[INSTALL.ru.md](integrations/kicad/INSTALL.ru.md).

## MCP server and REST API

For EDA tools and AI assistants that drive the router as a service. Both run the routing jobs on
the same engine as the command line, one job at a time (others wait in a queue), and the SES they
return is byte-identical to the CLI's for the same input and settings (`tools/server/parity.sh`).

**MCP** (Model Context Protocol, JSON-RPC 2.0 over stdin/stdout): `freerouting-cpp --mcp`.
Claude Desktop (`claude_desktop_config.json`), Cursor, Cline and other clients:

```json
{
  "mcpServers": {
    "freerouting": { "command": "C:/path/to/freerouting-cpp.exe", "args": ["--mcp"] }
  }
}
```

Tools: `autoroute_board` (a design by `filePath` or `fileContent`, optional rules / KiCad files,
`maxPasses`, `maxThreads`, `optimizer`, `timeoutSeconds`, `routerSettings`; writes the SES to
`outputPath` and returns it and/or a DRC summary; sends progress notifications; `wait: false`
returns a job id at once), `check_board_drc` (DRC of a design without routing),
`get_job_details`, `cancel_job`, `list_jobs`, `get_job_output`,
`download_job_output_to_local_file`, `get_job_drc_summary`, `get_job_logs`, `get_system_status`.
The names follow Java's MCP server where the tool does the same. A session with a minimal client
([tools/server/mcp_client.py](tools/server/mcp_client.py)):

```
$ tools/server/mcp_client.py ./freerouting-cpp autoroute_board \
    '{"filePath": "DAC2020_bm02.dsn", "outputPath": "DAC2020_bm02.ses"}'
>> {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "mcp_client.py", "version": "1.0"}}}
<< {"jsonrpc": "2.0", "id": 1, "result": {"protocolVersion": "2025-06-18", "capabilities": {"tools": {"listChanged": false}}, "serverInfo": {"name": "freerouting-cpp", "title": "Freerouting (C++)", ...}, "instructions": "PCB autorouter (freerouting, C++). ..."}}
>> {"jsonrpc": "2.0", "method": "notifications/initialized"}
>> {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}
<< {"jsonrpc": "2.0", "id": 2, "result": {"tools": [{"name": "autoroute_board", "description": "Autoroutes a PCB design ...", "inputSchema": {...}}, ...]}}
   tools: autoroute_board, check_board_drc, get_job_details, cancel_job, list_jobs, download_job_output_to_local_file, get_job_output, get_job_drc_summary, get_job_logs, get_system_status
>> {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "autoroute_board", "arguments": {"filePath": "DAC2020_bm02.dsn", "outputPath": "DAC2020_bm02.ses"}, "_meta": {"progressToken": "route-1"}}}
<< {"jsonrpc": "2.0", "method": "notifications/progress", "params": {"progressToken": "route-1", "progress": 1, "message": "READY_TO_START IDLE"}}
<< {"jsonrpc": "2.0", "method": "notifications/progress", "params": {"progressToken": "route-1", "progress": 2, "message": "RUNNING OPTIMIZATION, pass 3, 0 unrouted, 0 violations"}}
<< {"jsonrpc": "2.0", "id": 3, "result": {"content": [{"type": "text", "text": "..."}], "isError": false, "structuredContent":
     {"job_id": "7a1e9f08-608a-43fd-8318-aa1668207e25", "session_id": "3b96753d-94f4-49b6-94c5-411ed4fa48dc",
      "status": "COMPLETED", "duration_seconds": 1.09, "unrouted_connections": 0, "clearance_violations": 0, "outputs": {},
      "drc_summary": {"clearance_violations_count": 0, "unconnected_nets_count": 0, "violations": [], "congestion_zones": [],
                      "hints": ["All design rules satisfied. Board is DRC clean."]},
      "output_path": "DAC2020_bm02.ses",
      "message": "Routing completed successfully in 1.09s. The SES was written to DAC2020_bm02.ses (11498 bytes)."}}}
```

`tools/server/parity.sh <freerouting-cpp> <out-dir> <boards...>` routes boards with the CLI, the
MCP server and the REST API (one server process for all boards) and compares the SES byte for
byte; on bm02, Issue029 (with rules), Issue034, Issue054, Issue102, Issue107 (with rules) and
Issue313 the three are identical, on Linux and (bm02) with the Windows `.exe`.

`ctest -R server_test` ([tests/server_test.cpp](tests/server_test.cpp), about 20 s) checks the
servers in one process: the MCP protocol (initialize, tools/list, errors), every tool, cancel and
time limit, the REST endpoints and their errors, MCP over HTTP, and the SES of MCP and REST against
the CLI's on bm02, Issue107 (with rules) and Issue313. It needs the Java project's fixtures in
`upstream/fixtures` (`-DFR_FIXTURES_DIR=<dir>` points elsewhere).

**REST API**: `freerouting-cpp --api [--port 37864] [--host 127.0.0.1]` serves Java's API v1 with
the same paths and JSON field names ([upstream docs](https://github.com/freerouting/freerouting/blob/master/docs/API/API_v1.md)):
`GET /v1/system/status|environment`, `POST /v1/sessions/create`, `GET /v1/sessions/list|{id}|{id}/logs`,
`POST /v1/jobs/enqueue`, `GET /v1/jobs/list/{sessionId}`, `GET /v1/jobs/{id}[?compact=true]`,
`PUT /v1/jobs/{id}/start|cancel`, `GET|POST /v1/jobs/{id}/settings`, `POST /v1/jobs/{id}/input|rules`
(Base64 DSN / rules), `GET /v1/jobs/{id}/output` (Base64 SES), `GET /v1/jobs/{id}/output/stream`
and `/logs/stream` (server-sent events), `GET /v1/jobs/{id}/logs`, `GET /v1/jobs/{id}/drc/summary`,
the one-call `POST /v1/autoroute` and the MCP server over HTTP at `POST /v1/mcp` (so upstream's
`@freerouting/freerouting-mcp-server` bridge works with `FREEROUTING_API_URL=http://127.0.0.1:37864/v1/mcp`).
It listens on 127.0.0.1 only and has no authentication (a local plugin service); the
`Freerouting-Profile-ID` / `Freerouting-Environment-Host` headers are accepted but not required.
Not served: KiCad JSON (`/input/json`, `/output/json`: the upstream KiCad plugin's experimental
API mode needs them; the plugin's default DSN mode and our plugin don't), the KiCad DRC report
(`/drc`), partial output while a job runs (the SES comes when the job ends).

## Repository layout

```
runtime/jrt/        Java runtime emulation: Object, collections in Java order, BigInteger (GMP),
                    the allocator (heap, job heap, arenas, regions)
runtime/platform/   operating-system services: linux.{h,cpp}, windows.{h,cpp}
src/                the ported freerouting core (mirrors the Java packages) and src/main.cpp (CLI)
server/             MCP server, REST API v1, routing job manager
bindings/lua/, ui/  Lua module and the Nuklear user interface
integrations/kicad/ KiCad plugin
tests/              runtime unit tests, BigInteger fuzzing against GMP, KiCad inputs, MCP / REST servers
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
