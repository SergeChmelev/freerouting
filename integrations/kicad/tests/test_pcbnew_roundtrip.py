"""Headless check of the plugin's board path with KiCad's own Python (no GUI):
load a .kicad_pcb, export DSN, route it with freerouting-cpp, import the SES, save a copy.

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tests/test_pcbnew_roundtrip.py <board.kicad_pcb> <work dir>

The routed board is written to <work dir>/<name>.routed.kicad_pcb (the input is not changed).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import pcbnew  # noqa: E402
from freerouting_cpp import plugin, runner  # noqa: E402


def count(board):
    tracks = vias = 0
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            vias += 1
        else:
            tracks += 1
    return tracks, vias


def main(pcb, work):
    os.makedirs(work, exist_ok=True)
    name = os.path.splitext(os.path.basename(pcb))[0]
    board = pcbnew.LoadBoard(pcb)
    before = count(board)
    print("KiCad %s, %s: %d tracks, %d vias before" % ((pcbnew.Version(), name) + before))

    dsn = os.path.join(work, name + ".dsn")
    ses = os.path.join(work, name + ".ses")
    for f in (dsn, ses):
        if os.path.exists(f):
            os.remove(f)
    ok, err = plugin.export_dsn(board, dsn)
    assert ok and os.path.getsize(dsn) > 0, "DSN export failed: %s" % err
    print("DSN exported: %d bytes" % os.path.getsize(dsn))

    s = runner.load_settings()
    run = runner.RouterRun(s, dsn, ses, work)
    run.start()
    run.wait()
    print(run.progress.status)
    assert run.succeeded(), "router failed, see %s" % run.log_path

    ok, err = plugin.import_ses(board, ses)
    assert ok, "SES import failed: %s" % err
    plugin.refill_zones(board)
    after = count(board)
    print("after import: %d tracks, %d vias" % after)
    assert after[0] > before[0], "no tracks imported"

    out = os.path.join(work, name + ".routed.kicad_pcb")
    pcbnew.SaveBoard(out, board)
    print("saved", out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
