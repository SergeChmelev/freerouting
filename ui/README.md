# Freerouting C++ — Nuklear front end

A small PCB routing front end for the C++ port, written in Lua 5.3 with
[MoonNuklear](https://github.com/stetre/moonnuklear) + [MoonGL](https://github.com/stetre/moongl) /
[MoonGLFW](https://github.com/stetre/moonglfw), on top of the `frcore` Lua module
(`bindings/lua/`).

```bash
tools/build.sh                        # once; FR_BUILD_DIR=~/build/fr-release for a release build
cmake --build ~/build/fr-release --target frcore_lua   # writes ui/frcore.so
lua5.3 ui/main.lua upstream/fixtures/Issue508-DAC2020_bm02.dsn
```

`FRUI_SCALE=1.5` scales the UI; `FRUI_SHOT=/tmp/x lua5.3 ui/main.lua board.dsn` runs a
non-interactive autopilot (load, route, screenshots `/tmp/x_*.png`, quit).

## Layout (modelled on the Java freerouting GUI)

* menu **File / Route / View / Help**, toolbar **Open · Autoroute · Stop · Save SES · Fit · Unit**
* board canvas (Java GUI default colors): wheel = zoom at cursor, drag = pan
* **Layers** panel: active layer (drawn on top), per-layer visibility, what to show, counters
* **Router** panel: max passes, vias, optimizer, final DRC, timeout; Start/Stop, live stage,
  pass, unrouted, violations, progress
* **Log** panel: the engine's log; status bar: violations, unrouted, message, cursor X/Y
* docking panels, theme and file dialog come from the Demopaja timeline port (`ui/lib/`)

## Threading

All routing work happens on a C++ engine thread created by `frcore` (see
`bindings/lua/engine.h`). Lua only queues commands (`load`, `route`, `cancel`, `save`) and polls
plain data (`status`, `logs`, `snapshot`). Snapshots are immutable float32 vertex buffers that are
uploaded to the GPU only when their version changes. No Lua lock is held while routing runs.

## Lua API (`require('frcore')`)

| call | |
|---|---|
| `fr.engine()` | start the engine thread (one per process) |
| `eng:load(path)` / `eng:save(path)` | queue DSN load / SES save |
| `eng:route{max_passes=, optimize=, vias=, drc=, timeout='PT10M'}` | queue routing |
| `eng:cancel()` | stop the running job, drop queued commands |
| `eng:status()` | `{state, stage, pass, unrouted, violations, message, error, elapsed, version, busy, board_loaded, routed, design}` |
| `eng:snapshot(have_version)` | `nil` or `{version, bbox, unit, resolution, layers={{name, signal, traces, pads, areas, keepouts}}, vias, outline, airlines, items, trace_count, via_count, pin_count, incompletes}` — geometry as float32 xy strings (triangles / line pairs) |
| `eng:logs(from)` | `lines, next_index` |
| `eng:close()` | stop and join the engine thread |
