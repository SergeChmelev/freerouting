"""Command-line building of freerouting_cpp.runner, for each mode and option; runs nothing.

    python tests/test_command.py
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from freerouting_cpp import runner  # noqa: E402

RULES = {"neck_width_um": 200.0, "copper_to_edge_um": 500.0, "hole_clearance_um": 250.0,
         "hole_to_hole_um": 0.0}
RULE_ARGS = ["--router.neck_width_um=200", "--router.copper_to_edge_clearance_um=500",
             "--router.hole_clearance_um=250"]  # hole_to_hole 0: left to the router


def base(**kw):
    s = dict(runner.DEFAULT_SETTINGS)
    s.update(kw)
    return s


def after_paths(cmd):
    """The arguments after -do <ses>."""
    return cmd[cmd.index("-do") + 2:]


def check(name, got, want):
    assert got == want, "%s:\n  got  %r\n  want %r" % (name, got, want)
    print("ok", name)


def main():
    tmp = tempfile.mkdtemp(prefix="fr cmd ")
    try:
        dsn = os.path.join(tmp, "b.dsn")
        ses = os.path.join(tmp, "b.ses")
        exe = os.path.join(tmp, "freerouting-cpp.exe")

        # defaults: board rules on, project files off, LUT off
        cmd = runner.build_command(base(), dsn, ses, mode="native", exe=exe, rules=RULES)
        check("native defaults", cmd, [exe, "-de", dsn, "-do", ses] + RULE_ARGS + ["-mt", "4"])

        cmd = runner.build_command(base(board_rules=False), dsn, ses, mode="native", exe=exe, rules=RULES)
        check("board rules off", after_paths(cmd), ["-mt", "4"])

        check("rule rounding", runner.board_rule_args({"neck_width_um": 152.4, "hole_to_hole_um": 254.0001}),
              ["--router.neck_width_um=152.4", "--router.hole_to_hole_um=254"])
        check("no rules", runner.board_rule_args(None), [])

        # all dialog fields; extra arguments come last so they win
        s = base(threads=8, max_passes=30, timeout_minutes=5, no_optimizer=True, no_fanout=True,
                 via_costs=200, extra_args="--router.automatic_neckdown=false  --router.optimizer.max_passes=3")
        cmd = runner.build_command(s, dsn, ses, mode="native", exe=exe, rules=RULES)
        check("all fields", after_paths(cmd), RULE_ARGS + [
            "-mt", "8", "-mp", "30", "--timeout", "PT5M", "--no-optimizer", "--router.fanout.enabled=false",
            "--router.scoring.via_costs=200", "--router.automatic_neckdown=false", "--router.optimizer.max_passes=3"])

        # LUT: its keys after the board rules, its own via cost, fanout not repeated
        s = base(lut=True, lut_via_costs=1500, via_costs=200, no_fanout=True)
        cmd = runner.build_command(s, dsn, ses, mode="native", exe=exe, rules=RULES)
        check("LUT", after_paths(cmd), RULE_ARGS + list(runner.LUT_ARGS) + ["-mt", "4", "--router.scoring.via_costs=1500"])
        assert cmd.count("--router.fanout.enabled=false") == 1

        # project files: only those that exist, in the order dsn+pro+dru+pcb
        board = os.path.join(tmp, "proj", "my board.kicad_pcb")
        os.makedirs(os.path.dirname(board))
        for ext in (".kicad_pcb", ".kicad_pro"):
            open(os.path.splitext(board)[0] + ext, "w").close()
        files = runner.project_files(board)
        base_name = os.path.splitext(board)[0]
        check("project files found", files, [base_name + ".kicad_pro", base_name + ".kicad_pcb"])
        open(base_name + ".kicad_dru", "w").close()
        files = runner.project_files(board)
        check("project files with dru", files, [base_name + ext for ext in (".kicad_pro", ".kicad_dru", ".kicad_pcb")])
        check("no board file", runner.project_files(""), [])

        cmd = runner.build_command(base(project_files=True), dsn, ses, mode="native", exe=exe, extra_files=files)
        check("native -de with project files", cmd[1:3], ["-de", "+".join([dsn] + files)])
        cmd = runner.build_command(base(), dsn, ses, mode="native", exe=exe, extra_files=files)
        check("project files flag off", cmd[1:3], ["-de", dsn])

        check("'+' in a path", runner.de_value(dsn, [os.path.join(tmp, "a+b.kicad_pro")]), dsn)

        # WSL: every path converted, the router path from the settings
        pid = os.path.join(tmp, "router.pid")
        s = base(project_files=True, lut=True, wsl_distro="Ubuntu", router_path="/opt/fr/freerouting-cpp")
        cmd = runner.build_command(s, dsn, ses, pidfile=pid, mode="wsl", rules=RULES, extra_files=files)
        head = ["wsl", "-d", "Ubuntu", "-e", "sh", "-c", 'echo $$ > "$0"; exec "$@"', runner.to_wsl_path(pid),
                "/opt/fr/freerouting-cpp"]
        check("wsl head", cmd[:len(head)], head)
        de = cmd[cmd.index("-de") + 1].split("+")
        check("wsl -de", de, [runner.to_wsl_path(p) for p in [dsn] + files])
        if runner.IS_WINDOWS:
            assert all(p.startswith("/mnt/") for p in de), de
        check("wsl -do", cmd[cmd.index("-do") + 1], runner.to_wsl_path(ses))
        check("wsl rest", after_paths(cmd), RULE_ARGS + list(runner.LUT_ARGS) + ["-mt", "4", "--router.scoring.via_costs=1000"])

        if runner.IS_WINDOWS:
            check("wsl path", runner.to_wsl_path(r"C:\Users\x y\a.dsn"), "/mnt/c/Users/x y/a.dsn")

        # settings file: new keys load, wrong types fall back to the defaults
        sf = os.path.join(tmp, "settings.json")
        with open(sf, "w") as f:
            f.write('{"lut": true, "lut_via_costs": "x", "project_files": true, "mode": "bad"}')
        s = runner.load_settings(sf)
        check("settings file", (s["lut"], s["lut_via_costs"], s["project_files"], s["board_rules"], s["mode"]),
              (True, 1000, True, True, "auto"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("all command checks passed")


if __name__ == "__main__":
    main()
