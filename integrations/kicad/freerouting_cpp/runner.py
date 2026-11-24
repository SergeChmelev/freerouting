# runner.py -- starts freerouting-cpp, reads its log, reports progress, cancels it.
#
# Pure Python (no pcbnew, no wx), so it can be tested outside KiCad:
#   python -m freerouting_cpp.runner in.dsn out.ses
#
# Two ways to run the router (setting "mode"):
#   native -- freerouting-cpp.exe (or a Linux binary on Linux) started directly; by default
#             the one next to the plugin: freerouting_cpp/bin/freerouting-cpp.exe or
#             freerouting_cpp/freerouting-cpp.exe;
#   wsl    -- the Linux binary inside WSL, started as
#             wsl -d <distro> -e sh -c 'echo $$ > <pidfile>; exec "$@"' <pidfile> <binary> <args...>
#             so that Cancel can kill the Linux process by its pid (closing wsl.exe alone
#             does not always stop the process inside WSL);
#   auto   -- native when the binary is found, otherwise wsl (the default).

import json
import os
import re
import subprocess
import sys
import threading
import time

IS_WINDOWS = sys.platform.startswith("win")

DEFAULT_SETTINGS = {
    "threads": 4,  # -mt
    "via_costs": 0,  # --router.scoring.via_costs; 0 = router/board default
    "max_passes": 0,  # -mp; 0 = router default
    "timeout_minutes": 0,  # --timeout PT<n>M; 0 = no limit
    "no_optimizer": False,  # --no-optimizer
    "no_fanout": False,  # --router.fanout.enabled=false
    "extra_args": "",  # more arguments, e.g. --router.automatic_neckdown=false
    "board_rules": True,  # pass the board's design rules as --router.* keys (see board_rule_args)
    "project_files": False,  # -de board.dsn+board.kicad_pro+board.kicad_dru+board.kicad_pcb
    "lut": False,  # "LUT / single-layer" profile (see LUT_ARGS)
    "lut_via_costs": 1000,  # --router.scoring.via_costs of the LUT profile
    "mode": "auto",  # auto, native, wsl (see the top of this file)
    "native_path": "",  # router executable for native mode; empty = the one next to the plugin
    "wsl_distro": "Ubuntu",
    "router_path": "/usr/local/bin/freerouting-cpp",  # inside WSL
}
MODES = ("auto", "native", "wsl")

PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
SETTINGS_FILE = os.path.join(PLUGIN_DIR, "settings.json")
EXE_NAME = "freerouting-cpp.exe" if IS_WINDOWS else "freerouting-cpp"


def bundled_router(plugin_dir=PLUGIN_DIR):
    """The router shipped with the plugin, or None."""
    for p in (os.path.join(plugin_dir, "bin", EXE_NAME), os.path.join(plugin_dir, EXE_NAME)):
        if os.path.isfile(p):
            return p
    return None


def resolve(settings, plugin_dir=PLUGIN_DIR):
    """(mode, native executable) to use: mode is "native" or "wsl"; the executable is None
    in wsl mode, and also in native mode when nothing was found (start() then fails with a
    clear message)."""
    mode = settings.get("mode", "auto")
    exe = settings.get("native_path", "").strip() or bundled_router(plugin_dir)
    if mode == "wsl" or (mode == "auto" and not exe and IS_WINDOWS):
        return "wsl", None
    if not exe and not IS_WINDOWS:
        exe = settings.get("router_path") or None  # on Linux the "WSL" path is a native one
    return "native", exe


def load_settings(path=SETTINGS_FILE):
    s = dict(DEFAULT_SETTINGS)
    try:
        with open(path, "r", encoding="utf-8") as f:
            saved = json.load(f)
        for k in s:
            if k in saved and isinstance(saved[k], type(s[k])):
                s[k] = saved[k]
    except (OSError, ValueError):
        pass
    if s["mode"] not in MODES:
        s["mode"] = "auto"
    return s


def save_settings(s, path=SETTINGS_FILE):
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(s, f, indent=2)
    except OSError:
        pass


def to_wsl_path(path):
    """C:\\Users\\x\\a.dsn -> /mnt/c/Users/x/a.dsn (paths already in POSIX form are kept)."""
    p = os.path.abspath(path) if IS_WINDOWS else path
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", p)
    if not m:
        return p.replace("\\", "/")
    return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2).replace("\\", "/"))


# Board design rules (KiCad: Board Setup -> Design Rules -> Constraints) -> router keys, in
# micrometres. Only rules with a matching router setting are passed: the router has no key for
# KiCad's minimum clearance (net-class clearances reach it through the DSN), and
# clearance_tolerance_um is a rounding tolerance, not a clearance.
BOARD_RULE_KEYS = (
    ("neck_width_um", "neck_width_um"),  # max(min track width, min connection width)
    ("copper_to_edge_um", "copper_to_edge_clearance_um"),
    ("hole_clearance_um", "hole_clearance_um"),
    ("hole_to_hole_um", "hole_to_hole_um"),
)

# The "LUT / single-layer" profile (toner transfer, home-made boards): no SMD fanout, no
# automatic neckdown, no fallback strategies, and vias made expensive (setting lut_via_costs).
# Trace width and clearance come from KiCad's net classes through the DSN.
LUT_ARGS = (
    "--router.fanout.enabled=false",
    "--router.automatic_neckdown=false",
    "--router.autorouter.fallback_strategies=none",
)
LUT_MIN_WIDTH_MM = 0.5  # narrower net-class tracks get a warning in LUT mode


def _um(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def board_rule_args(rules):
    """--router.* keys of the board rules {name: micrometres} (see BOARD_RULE_KEYS);
    rules that are 0 or missing are left to the router."""
    args = []
    for name, key in BOARD_RULE_KEYS:
        v = rules.get(name) if rules else None
        if v and v > 0:
            args.append("--router.%s=%s" % (key, _um(v)))
    return args


def project_files(board_file):
    """board.kicad_pro, board.kicad_dru, board.kicad_pcb next to the board file, those that exist."""
    if not board_file:
        return []
    base = os.path.splitext(board_file)[0]
    return [base + ext for ext in (".kicad_pro", ".kicad_dru", ".kicad_pcb") if os.path.isfile(base + ext)]


def de_value(dsn, extra_files, path_conv=lambda p: p):
    """Value of -de: the DSN alone, or dsn+pro+dru+pcb. A '+' inside a path would split it,
    so then the project files are left out."""
    paths = [path_conv(dsn)] + [path_conv(p) for p in extra_files or []]
    if len(paths) > 1 and any("+" in p for p in paths):
        return paths[0]
    return "+".join(paths)


def router_args(settings, dsn, ses, path_conv=lambda p: p, rules=None, extra_files=None):
    """Arguments after the binary name. rules: board design rules (see board_rule_args), used
    when settings["board_rules"]; extra_files: project files for -de, used when
    settings["project_files"]. Later arguments win, so the order is: board rules, LUT
    profile, dialog fields, extra arguments."""
    files = extra_files if settings.get("project_files") else None
    args = ["-de", de_value(dsn, files, path_conv), "-do", path_conv(ses)]
    if settings.get("board_rules"):
        args += board_rule_args(rules)
    if settings.get("lut"):
        args += list(LUT_ARGS)
    if settings.get("threads", 0) > 0:
        args += ["-mt", str(settings["threads"])]
    if settings.get("max_passes", 0) > 0:
        args += ["-mp", str(settings["max_passes"])]
    if settings.get("timeout_minutes", 0) > 0:
        args += ["--timeout", "PT%dM" % settings["timeout_minutes"]]
    if settings.get("no_optimizer"):
        args.append("--no-optimizer")
    if settings.get("no_fanout") and not settings.get("lut"):
        args.append("--router.fanout.enabled=false")
    via_costs = settings.get("lut_via_costs", 0) if settings.get("lut") else settings.get("via_costs", 0)
    if via_costs > 0:
        args.append("--router.scoring.via_costs=%d" % via_costs)
    extra = settings.get("extra_args", "").strip()
    if extra:
        args += extra.split()
    return args


def build_command(settings, dsn, ses, pidfile=None, mode=None, exe=None, rules=None, extra_files=None):
    """Full command line (list) for subprocess; mode and exe as returned by resolve()."""
    if mode is None:
        mode, exe = resolve(settings)
    if mode == "wsl":
        args = router_args(settings, dsn, ses, to_wsl_path, rules, extra_files)
        cmd = ["wsl"]
        if settings.get("wsl_distro"):
            cmd += ["-d", settings["wsl_distro"]]
        if pidfile:
            cmd += ["-e", "sh", "-c", 'echo $$ > "$0"; exec "$@"', to_wsl_path(pidfile)]
        else:
            cmd += ["-e"]
        return cmd + [settings["router_path"]] + args
    return [exe or EXE_NAME] + router_args(settings, dsn, ses, rules=rules, extra_files=extra_files)


# --- log parsing -------------------------------------------------------------------

_RE_START = re.compile(r"Auto-routing stage started .* for (\d+) unrouted items")
_RE_ROUTE_PASS = re.compile(r"Auto-routing pass #(\d+) .* in ([\d.]+) seconds .*\((\d+) unrouted and (\d+) violations\)")
_RE_OPT_START = re.compile(r"Optimization stage started")
_RE_OPT_PASS = re.compile(r"Optimizer pass #(\d+): optimizer score ([\d.]+) -> ([\d.]+)")
_RE_OPT_DONE = re.compile(r"Optimization stage completed")
_RE_SUMMARY = re.compile(r": (\d+) unrouted, (\d+) clearance violations, ([\d.]+) s")
_RE_LOG_PREFIX = re.compile(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+ (\w+)\s+(?:\[\w+\] )?")


class Progress(object):
    """State parsed from the router log."""

    def __init__(self):
        self.stage = "starting"  # starting, routing, optimizing, done
        self.initial_unrouted = None
        self.unrouted = None
        self.violations = None
        self.route_pass = 0
        self.opt_pass = 0
        self.summary = None  # (unrouted, violations, seconds) from the last stdout line
        self.status = "Starting the router..."

    def fraction(self):
        """0..1 for a progress bar, or None when unknown (then the bar pulses)."""
        if self.stage == "routing" and self.initial_unrouted and self.unrouted is not None:
            done = self.initial_unrouted - self.unrouted
            return max(0.0, min(1.0, float(done) / self.initial_unrouted))
        if self.stage in ("optimizing", "done"):
            return 1.0
        return None

    def feed(self, line):
        """Updates the state from one log line; returns True when the status changed."""
        final = self.summary
        changed = self._feed(line)
        if final is not None:
            # The summary goes to stdout and the log to stderr; a native router's stdout is
            # buffered, so the summary can arrive before the last log lines. It stays final.
            self.stage = "done"
            self.summary = final
            self.unrouted, self.violations = final[0], final[1]
            self.status = "Done: %d unrouted, %d clearance violations, %.1f s" % final
            return False
        return changed

    def _feed(self, line):
        m = _RE_START.search(line)
        if m:
            self.stage = "routing"
            self.initial_unrouted = self.unrouted = int(m.group(1))
            self.status = "Routing: %d connections to route" % self.initial_unrouted
            return True
        m = _RE_ROUTE_PASS.search(line)
        if m:
            self.stage = "routing"
            self.route_pass = int(m.group(1))
            self.unrouted = int(m.group(3))
            self.violations = int(m.group(4))
            self.status = "Routing pass %d: %d unrouted, %d violations" % (
                self.route_pass, self.unrouted, self.violations)
            return True
        if _RE_OPT_START.search(line):
            self.stage = "optimizing"
            self.status = "Optimizing (unrouted: %s)" % (self.unrouted if self.unrouted is not None else "?")
            return True
        m = _RE_OPT_PASS.search(line)
        if m:
            self.stage = "optimizing"
            self.opt_pass = int(m.group(1))
            self.status = "Optimizer pass %d: score %s -> %s" % (self.opt_pass, m.group(2), m.group(3))
            return True
        if _RE_OPT_DONE.search(line):
            self.status = "Optimization done, writing the session file..."
            return True
        m = _RE_SUMMARY.search(line)
        if m:
            self.stage = "done"
            self.summary = (int(m.group(1)), int(m.group(2)), float(m.group(3)))
            self.unrouted, self.violations = self.summary[0], self.summary[1]
            self.status = "Done: %d unrouted, %d clearance violations, %.1f s" % self.summary
            return True
        return False


def short_line(line):
    """Drops the timestamp and job id of a log line."""
    m = _RE_LOG_PREFIX.match(line)
    if not m:
        return line
    level = m.group(1)
    rest = line[m.end():]
    return rest if level == "INFO" else "%s %s" % (level, rest)


# --- process --------------------------------------------------------------------

class RouterRun(object):
    """One router run. start() returns at once; on_line(line) and on_exit(code) are
    called from a reader thread (the GUI must hand them over to its own thread)."""

    def __init__(self, settings, dsn, ses, work_dir, on_line=None, on_exit=None, rules=None,
                 extra_files=None, notes=None):
        self.settings = settings
        self.dsn = dsn
        self.ses = ses
        self.work_dir = work_dir
        self.on_line = on_line or (lambda line: None)
        self.on_exit = on_exit or (lambda code: None)
        self.progress = Progress()
        self.proc = None
        self.cancelled = False
        self.returncode = None
        self.mode, self.exe = resolve(settings)
        self.pidfile = os.path.join(work_dir, "router.pid") if self.mode == "wsl" else None
        self.log_path = os.path.join(work_dir, "router.log")
        self.command = build_command(settings, dsn, ses, self.pidfile, self.mode, self.exe, rules, extra_files)
        self.notes = list(notes or [])  # lines for the log before the router output (what came from the board)
        self._thread = None

    def start(self):
        """Starts the router; raises OSError when it cannot be started."""
        if self.mode == "native" and not (self.exe and os.path.isfile(self.exe)):
            raise OSError("router executable not found: %s (expected %s, or set the path in the settings)"
                          % (self.exe or EXE_NAME, os.path.join(PLUGIN_DIR, "bin", EXE_NAME)))
        if self.pidfile and os.path.exists(self.pidfile):
            os.remove(self.pidfile)
        kwargs = {}
        if IS_WINDOWS:
            # CREATE_NO_WINDOW: no console window pops up; CREATE_NEW_PROCESS_GROUP: the router
            # gets no Ctrl+C/Ctrl+Break meant for KiCad.
            kwargs["creationflags"] = 0x08000000 | 0x00000200
        self.proc = subprocess.Popen(
            self.command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            cwd=self.work_dir, **kwargs)
        self._thread = threading.Thread(target=self._read, name="freerouting-cpp-reader")
        self._thread.daemon = True
        self._thread.start()

    def _read(self):
        with open(self.log_path, "w", encoding="utf-8") as log:
            for note in self.notes:
                log.write(note + "\n")
            log.write("command: %s\n" % subprocess.list2cmdline(self.command))
            for raw in iter(self.proc.stdout.readline, b""):
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                log.write(line + "\n")
                log.flush()
                self.progress.feed(line)
                self.on_line(line)
            self.proc.stdout.close()
            self.returncode = self.proc.wait()
            log.write("exit code: %s%s\n" % (self.returncode, " (cancelled)" if self.cancelled else ""))
        self.on_exit(self.returncode)

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def wait(self, timeout=None):
        if self._thread:
            self._thread.join(timeout)
        return self.returncode

    def cancel(self):
        """Stops the router. Its SES is then not imported."""
        if not self.running():
            return
        self.cancelled = True
        if self.pidfile:
            pid = None
            for _ in range(20):  # the pid file is written right after wsl starts
                try:
                    with open(self.pidfile, "r") as f:
                        pid = int(f.read().strip())
                    break
                except (OSError, ValueError):
                    time.sleep(0.05)
            if pid:
                kill = ["wsl"]
                if self.settings.get("wsl_distro"):
                    kill += ["-d", self.settings["wsl_distro"]]
                kill += ["-e", "kill", "-TERM", str(pid)]
                try:
                    subprocess.call(kill, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL,
                                    creationflags=0x08000000 if IS_WINDOWS else 0, timeout=15)
                except (OSError, subprocess.SubprocessError):
                    pass
                for _ in range(40):
                    if not self.running():
                        return
                    time.sleep(0.05)
        try:
            self.proc.kill()  # TerminateProcess on Windows, SIGKILL elsewhere
        except OSError:
            pass

    def succeeded(self):
        return (not self.cancelled and self.returncode == 0 and os.path.exists(self.ses)
                and os.path.getsize(self.ses) > 0)


def _main(argv):
    """Command-line check: python -m freerouting_cpp.runner in.dsn out.ses [threads [auto|native|wsl]]"""
    if len(argv) < 3:
        print("usage: python -m freerouting_cpp.runner in.dsn out.ses [threads [auto|native|wsl]]")
        return 2
    s = load_settings()
    if len(argv) > 3:
        s["threads"] = int(argv[3])
    if len(argv) > 4:
        s["mode"] = argv[4]
    work = os.path.dirname(os.path.abspath(argv[2]))
    run = RouterRun(s, argv[1], argv[2], work, on_line=lambda l: None)
    last = [None]

    def on_line(line):
        if run.progress.status != last[0]:
            last[0] = run.progress.status
            print("[%s] %s" % (run.progress.stage, last[0]))

    run.on_line = on_line
    print(subprocess.list2cmdline(run.command))
    run.start()
    code = run.wait()
    print("exit code", code, "succeeded", run.succeeded(), "log", run.log_path)
    return 0 if run.succeeded() else 1


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
