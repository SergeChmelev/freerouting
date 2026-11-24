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
| Run | Auto / Native (.exe) / WSL, see above | Auto |
| Native executable | path of `freerouting-cpp.exe` | empty = next to the plugin |
| WSL distribution | `wsl -d <distribution>` | `Ubuntu` |
| Router in WSL | Linux path of the router | `/usr/local/bin/freerouting-cpp` |

The values are kept in `freerouting_cpp/settings.json` next to the plugin.
The plugin passes no rules file (`-dr`): the DSN that KiCad writes already carries the board's
rules.

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
`upstream` is the Java checkout next to the repository
(`C:\Users\HP\VibeCodes\freerouting\upstream`); in a worktree give its full path.

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
