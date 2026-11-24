"""What the plugin takes from a board (pcbnew, no GUI), then a routing run with it.

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tests/test_board_rules.py <board.kicad_pcb> <work dir> [<freerouting-cpp.exe>]

1. copies the board into the work folder with an empty .kicad_pro next to it, and checks
   plugin.prepare(): design rules -> --router.* keys, the log lines, project files, LUT
   net classes and the narrow-track warning;
2. exports the DSN and routes it with the board rules and the LUT profile (the exe when given,
   otherwise as the settings say), and checks that the router accepted the keys.
Project files are not passed to the router in step 2 (it does not read them yet).
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import pcbnew  # noqa: E402
from freerouting_cpp import plugin, runner  # noqa: E402


def main(pcb, work, exe=None):
    work = os.path.abspath(work)
    os.makedirs(work, exist_ok=True)
    name = os.path.splitext(os.path.basename(pcb))[0]
    copy = os.path.join(work, name + ".kicad_pcb")
    shutil.copy(pcb, copy)
    pro = os.path.join(work, name + ".kicad_pro")
    if os.path.exists(pro):
        os.remove(pro)
    board = pcbnew.LoadBoard(copy)
    # after loading: KiCad crashes on an empty .kicad_pro; the plugin only looks for the file
    open(pro, "w").close()
    ds = board.GetDesignSettings()

    s = dict(runner.DEFAULT_SETTINGS, project_files=True, lut=True)
    rules, files, notes, warning = plugin.prepare(board, s)
    print("\n".join(notes))
    assert rules["neck_width_um"] == max(ds.m_TrackMinWidth, ds.m_MinConn) / 1000.0, rules
    assert rules["copper_to_edge_um"] == ds.m_CopperEdgeClearance / 1000.0, rules
    assert rules["hole_clearance_um"] == ds.m_HoleClearance / 1000.0, rules
    assert rules["hole_to_hole_um"] == ds.m_HoleToHoleMin / 1000.0, rules
    assert any("min clearance" in n and "not passed" in n for n in notes)
    assert files == [os.path.join(work, name + ".kicad_pro"), copy], files
    classes = plugin.net_classes(board)
    assert classes and classes[0][0] == "Default", classes
    narrow = plugin.narrow_classes(classes)
    assert bool(warning) == bool(narrow), (warning, narrow)
    print("prepare ok; LUT warning:", warning.splitlines()[0] if warning else "none")

    _, _, notes_off, _ = plugin.prepare(board, dict(runner.DEFAULT_SETTINGS, board_rules=False))
    assert notes_off == ["Board design rules: not passed (switched off in the settings)."], notes_off

    dsn = os.path.join(work, name + ".dsn")
    ses = os.path.join(work, name + ".ses")
    for f in (dsn, ses):
        if os.path.exists(f):
            os.remove(f)
    ok, err = plugin.export_dsn(board, dsn)
    assert ok, err
    run_s = dict(s, project_files=False)
    if exe:
        run_s.update(mode="native", native_path=os.path.abspath(exe))
    run = runner.RouterRun(run_s, dsn, ses, work, rules=rules, extra_files=files, notes=notes)
    print("command:", " ".join(run.command))
    for arg in runner.board_rule_args(rules) + list(runner.LUT_ARGS):
        assert arg in run.command, arg
    run.start()
    run.wait()
    log = open(run.log_path, encoding="utf-8").read()
    assert notes[0] in log, "notes missing in router.log"
    assert "unknown" not in log.lower(), log[-2000:]
    assert run.succeeded(), "router failed, see %s" % run.log_path
    print("routed:", run.progress.status)


if __name__ == "__main__":
    main(*sys.argv[1:4])
