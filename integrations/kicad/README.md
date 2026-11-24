# KiCad plugin: route with freerouting-cpp

A KiCad Action Plugin for the PCB editor. A toolbar button (or **Tools → External Plugins →
Route with freerouting-cpp**) routes the open board with the C++ port:

1. the board is exported as Specctra DSN (`pcbnew.ExportSpecctraDSN`) into
   `%TEMP%\kicad-freerouting-cpp\<board>\`;
2. `freerouting-cpp` routes it: natively, as `freerouting_cpp\bin\freerouting-cpp.exe` next to
   the plugin, or (when there is no exe) in WSL:
   `wsl -d Ubuntu -e /usr/local/bin/freerouting-cpp -de /mnt/c/... -do /mnt/c/...`;
3. a dialog shows the router log, the stage (routing pass, unrouted count, optimizer pass),
   a progress bar (share of routed connections while routing) and the elapsed time;
   **Cancel** kills the router (the exe, or the process inside WSL) and leaves the board unchanged;
4. on success the SES is imported (`pcbnew.ImportSpecctraSES`), copper zones are refilled and
   the view is refreshed. If connections stay unrouted or clearance violations remain, a
   message says so; run the DRC to see them.

It uses the same path as the upstream Java plugin's default DSN mode
(`upstream/integrations/KiCad/kicad-freerouting`): the SWIG `pcbnew` API of KiCad 6–10.
Checked with KiCad 10.0.8 (the version installed here); KiCad's IPC API is not used.
KiCad plans to drop SWIG plugins after 10; then this plugin needs an IPC rewrite.

## Where the router comes from

Setting **Run** (default **Auto**):

| Mode | What runs |
|---|---|
| Auto | the native exe if one is found (see below), otherwise WSL |
| Native (.exe) | the native exe; an error if there is none |
| WSL | `wsl -d <distribution> -e <router in WSL>`, paths converted to `/mnt/c/...` |

The native exe is the "Native executable" setting when it is filled in; otherwise the first of

1. `freerouting_cpp\bin\freerouting-cpp.exe`
2. `freerouting_cpp\freerouting-cpp.exe`

(on Linux the same names without `.exe`, then the "Router in WSL" path). In native mode the
router is started directly with ordinary Windows paths, without a console window
(`CREATE_NO_WINDOW`) and in its own process group (`CREATE_NEW_PROCESS_GROUP`); Cancel
terminates the process. The settings dialog shows which router the chosen mode will run.

## Settings (dialog before each run)

| Field | Router argument | Default |
|---|---|---|
| Threads | `-mt N` (optimizer threads) | 4 |
| Via cost | `--router.scoring.via_costs=N`, more = fewer vias | 0 = router/board default |
| Max passes | `-mp N` (autorouter passes) | 0 = router default |
| Timeout, min | `--timeout PT<N>M` | 0 = no limit |
| Skip the optimizer | `--no-optimizer` | off |
| Skip the SMD fanout stage | `--router.fanout.enabled=false` | off |
| Extra arguments | appended as they are: any `--router.<json path>=<value>`, e.g. `--router.automatic_neckdown=false` | empty |
| Pass the board's design rules | the `--router.*` keys below | on |
| Give the router the project files too | `-de board.dsn+board.kicad_pro+board.kicad_dru+board.kicad_pcb` | off |
| LUT / single-layer profile | see below | off |
| Via cost in this profile | `--router.scoring.via_costs=N` while the LUT profile is on | 1000 |
| Run | Auto / Native (.exe) / WSL, see above | Auto |
| Native executable | path of `freerouting-cpp.exe` | empty = next to the plugin |
| WSL distribution | `wsl -d <distribution>` | `Ubuntu` |
| Router in WSL | Linux path of the router | `/usr/local/bin/freerouting-cpp` |

The values are kept in `freerouting_cpp/settings.json` next to the plugin.
The plugin passes no rules file (`-dr`) and adds nothing to the DSN; everything else goes as
command-line keys. Order on the command line: board rules, LUT profile, dialog fields, extra
arguments; a later key wins, so extra arguments override everything.

### Taken from the board

With "Pass the board's design rules" on, the plugin reads **Board Setup → Design Rules →
Constraints** through `pcbnew` (`board.GetDesignSettings()`) and passes:

| KiCad constraint | Router key |
|---|---|
| max(Minimum track width `m_TrackMinWidth`, Minimum connection width `m_MinConn`) | `--router.neck_width_um` |
| Copper to board edge clearance `m_CopperEdgeClearance` | `--router.copper_to_edge_clearance_um` |
| Copper to hole clearance `m_HoleClearance` | `--router.hole_clearance_um` |
| Hole to hole clearance `m_HoleToHoleMin` | `--router.hole_to_hole_um` |

Values are in micrometres; a constraint that is 0 in KiCad is not passed (the router keeps its
default). **Minimum clearance** (`m_MinClearance`) is not passed: the router has no key for it
(the clearances of the net classes reach it through the DSN), and `clearance_tolerance_um` is
a rounding tolerance, not a clearance. `neck_width_um` switches on the router's opt-in necking:
a connection that fails at its net-class width is retried once at this width.

The progress dialog and `router.log` start with what was taken, e.g.

```
Taken from the board (Board Setup -> Constraints):
  min track width 0.2 mm, min connection width 0 mm -> --router.neck_width_um=200
  copper to board edge 0.5 mm -> --router.copper_to_edge_clearance_um=500
  copper to hole 0.25 mm -> --router.hole_clearance_um=250
  hole to hole 0.25 mm -> --router.hole_to_hole_um=250
  min clearance 0 mm: not passed (the router has no such key; net-class clearances are in the DSN)
```

### Project files

With "Give the router the project files too" on, `-de` becomes
`board.dsn+board.kicad_pro+board.kicad_dru+board.kicad_pcb`, with those of the three project
files that exist next to the open `.kicad_pcb` (in this order). The `.kicad_pcb` is the one on
disk, so save the board first. If a path contains `+`, only the DSN is passed. **Off by
default:** the router does not read these files yet; until it does, switching this on makes
the run fail.

### LUT / single-layer profile

For home-made boards (toner transfer, one copper layer, the top layer at most as jumpers).
One checkbox adds

```
--router.fanout.enabled=false --router.automatic_neckdown=false
--router.autorouter.fallback_strategies=none --router.scoring.via_costs=<Via cost in this profile>
```

and greys out the plain "Via cost" and "Skip the SMD fanout stage" fields (the profile sets
both). Track widths and clearances come from KiCad's net classes through the DSN, as always;
the plugin lists the net classes in the log and asks before routing when a class has tracks
narrower than 0.5 mm. "Top layer = jumpers" is for now only the high via cost: every via is a
jumper wire, so expensive vias keep their number low.

## Installation (KiCad 10 on Windows)

Step-by-step guide in Russian, with troubleshooting: [INSTALL.ru.md](INSTALL.ru.md).

Copy the folder `freerouting_cpp` (the whole folder, not its files) into KiCad's user plugin
folder, then put the router exe into its `bin` subfolder:

```powershell
Copy-Item -Recurse -Force integrations\kicad\freerouting_cpp "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\"
```

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\bin"
```

```powershell
Copy-Item -Force <folder with the exe>\freerouting-cpp.exe "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\bin\"
```

so that `Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\__init__.py` and
`...\freerouting_cpp\bin\freerouting-cpp.exe` exist. For other KiCad versions use
`Documents\KiCad\<version>\scripting\plugins`; KiCad shows the folder under
**Tools → External Plugins → Open Plugin Directory** in the PCB editor. Then in the PCB editor:
**Tools → External Plugins → Refresh Plugins** (or restart KiCad). The button appears in the
top toolbar; if not, enable it in **Preferences → PCB Editor → Action Plugins**.

Without the exe the plugin falls back to WSL. That needs the `Ubuntu` distribution and the
router at `/usr/local/bin/freerouting-cpp` with its libraries (`libgeos_c`,
Ubuntu package `libgeos-c1t64`), see the router's README.txt. Check it from PowerShell:

```powershell
wsl -d Ubuntu -e /usr/local/bin/freerouting-cpp --help
```

## Notes

- **Save the board before routing.** The SES import replaces the board's tracks and vias with
  the routed ones (existing wiring goes through the DSN to the router and comes back), and
  it may not be undoable in one step.
- The temporary files (`<board>.dsn`, `<board>.ses`, `router.log` with the full router output)
  stay in `%TEMP%\kicad-freerouting-cpp\<board>\` until the next run of the same board.
- The router narrows traces at pins (`automatic_neckdown`), which KiCad's DRC can report as
  `track_width`; turn it off with the extra argument `--router.automatic_neckdown=false`.

## Tests (without the KiCad GUI)

Run with KiCad's Python from `integrations\kicad`; the work folder may be any empty folder.
`..\..\upstream` is a checkout of the Java freerouting next to this repository (for its
`fixtures`); give its full path if it is elsewhere.

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_command.py
```

builds the command line for each mode and option (native and WSL paths, board rules, project
files, the LUT profile, argument order, the settings file) without running anything.

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_board_rules.py ..\..\upstream\fixtures\Issue508-DAC2020\DAC2020_bm08\bm8.unrouted.kicad_pcb $env:TEMP\fr-test <path to freerouting-cpp.exe>
```

reads a board's design rules and net classes with `pcbnew` (what the plugin would pass and
log, the narrow-track warning), then routes it with the board rules and the LUT profile and
checks that the router accepts the keys. Without the exe argument the router is chosen as in
the plugin.

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_runner.py ..\..\upstream\fixtures $env:TEMP\fr-test
```

routes a fixture DSN and cancels a large one (checks that the process is gone). Without a
third argument the router is chosen as in the plugin (Auto); with a path to
`freerouting-cpp.exe` as the third argument the same checks run in native mode.

```powershell
wsl -d Ubuntu -e x86_64-w64-mingw32-gcc -O2 -static -o tests/stub/freerouting-cpp.exe tests/stub/stub_router.c
```

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_native.py tests\stub\freerouting-cpp.exe $env:TEMP\fr-test
```

builds a stub router and checks native mode with it: the mode choice (auto/native/wsl, the exe
next to the plugin, an explicit path), Windows paths on the command line, success, failure, a
missing exe and Cancel.

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_pcbnew_roundtrip.py ..\..\upstream\fixtures\Issue508-DAC2020\DAC2020_bm08\bm8.unrouted.kicad_pcb $env:TEMP\fr-test
```

loads a board with `pcbnew`, exports DSN, routes, imports the SES and saves a routed copy.

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_dialogs.py ..\..\upstream\fixtures $env:TEMP\fr-test
```

opens the settings and progress dialogs in a bare wx app and drives them by timers (a run to
the end and a cancelled run).
