# KiCad plugin: route with freerouting-cpp

A KiCad Action Plugin for the PCB editor. A toolbar button (or **Tools → External Plugins →
Route with freerouting-cpp**) routes the open board with the C++ port:

1. the board is exported as Specctra DSN (`pcbnew.ExportSpecctraDSN`) into
   `%TEMP%\kicad-freerouting-cpp\<board>\`;
2. `freerouting-cpp` routes it; on Windows it runs in WSL:
   `wsl -d Ubuntu -e /home/halt/opt/freerouting-cpp/freerouting-cpp -de /mnt/c/... -do /mnt/c/...`;
3. a dialog shows the router log, the stage (routing pass, unrouted count, optimizer pass),
   a progress bar (share of routed connections while routing) and the elapsed time;
   **Cancel** kills the router inside WSL and leaves the board unchanged;
4. on success the SES is imported (`pcbnew.ImportSpecctraSES`), copper zones are refilled and
   the view is refreshed. If connections stay unrouted or clearance violations remain, a
   message says so; run the DRC to see them.

It uses the same path as the upstream Java plugin's default DSN mode
(`upstream/integrations/KiCad/kicad-freerouting`): the SWIG `pcbnew` API of KiCad 6–10.
Checked with KiCad 10.0.8 (the version installed here); KiCad's IPC API is not used.
KiCad plans to drop SWIG plugins after 10; then this plugin needs an IPC rewrite.

## Settings (dialog before each run)

| Field | Router argument | Default |
|---|---|---|
| Threads | `-mt N` (optimizer threads) | 4 |
| Via cost | `--router.scoring.via_costs=N`, more = fewer vias | 0 = router/board default |
| Max passes | `-mp N` (autorouter passes) | 0 = router default |
| Timeout, min | `--timeout PT<N>M` | 0 = no limit |
| Skip the optimizer | `--no-optimizer` | off |
| Extra arguments | appended as they are, e.g. `--router.automatic_neckdown=false` | empty |
| Run in WSL / distribution | `wsl -d <distribution> -e ...` | on (Windows), `Ubuntu` |
| Router binary | path of `freerouting-cpp` (inside WSL when WSL is on) | `/home/halt/opt/freerouting-cpp/freerouting-cpp` |

The values are kept in `freerouting_cpp/settings.json` next to the plugin.
Without WSL (Linux, or a native build) clear "Run in WSL" and give the binary's path.

## Installation (KiCad 10 on Windows)

Copy the folder `freerouting_cpp` (the whole folder, not its files) into KiCad's user plugin
folder:

```powershell
Copy-Item -Recurse -Force integrations\kicad\freerouting_cpp "$env:USERPROFILE\Documents\KiCad\10.0\scripting\plugins\"
```

so that `Documents\KiCad\10.0\scripting\plugins\freerouting_cpp\__init__.py` exists. For other
KiCad versions use `Documents\KiCad\<version>\scripting\plugins`; KiCad shows the folder under
**Tools → External Plugins → Open Plugin Directory** in the PCB editor. Then in the PCB editor:
**Tools → External Plugins → Refresh Plugins** (or restart KiCad). The button appears in the
top toolbar; if not, enable it in **Preferences → PCB Editor → Action Plugins**.

Requirements: WSL with the `Ubuntu` distribution, the router at
`/home/halt/opt/freerouting-cpp/freerouting-cpp` with its libraries (`libgeos_c`), see the
router's README.txt. Check it from PowerShell:

```powershell
wsl -d Ubuntu -e /home/halt/opt/freerouting-cpp/freerouting-cpp --help
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

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_runner.py ..\..\upstream\fixtures $env:TEMP\fr-test
```

routes a fixture DSN and cancels a large one (checks that the Linux process is gone);

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_pcbnew_roundtrip.py ..\..\upstream\fixtures\Issue508-DAC2020\DAC2020_bm08\bm8.unrouted.kicad_pcb $env:TEMP\fr-test
```

loads a board with `pcbnew`, exports DSN, routes, imports the SES and saves a routed copy;

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" tests\test_dialogs.py ..\..\upstream\fixtures $env:TEMP\fr-test
```

opens the settings and progress dialogs in a bare wx app and drives them by timers (a run to
the end and a cancelled run). `upstream` is the Java checkout next to the repository
(`C:\Users\HP\VibeCodes\freerouting\upstream`); in a worktree give its full path.
