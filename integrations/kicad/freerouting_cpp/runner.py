# runner.py -- starts freerouting-cpp, reads its log, reports progress, cancels it.
#
# Pure Python (no pcbnew, no wx), so it can be tested outside KiCad:
#   python -m freerouting_cpp.runner in.dsn out.ses
#
# On Windows the router is a Linux binary inside WSL; it is started as
#   wsl -d <distro> -e sh -c 'echo $$ > <pidfile>; exec "$@"' sh-pidfile <binary> <args...>
# so that Cancel can kill the Linux process by its pid (closing wsl.exe alone does not
# always stop the process inside WSL).

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
    "extra_args": "",  # more arguments, e.g. --router.automatic_neckdown=false
    "use_wsl": IS_WINDOWS,
    "wsl_distro": "Ubuntu",
    "router_path": "/home/halt/opt/freerouting-cpp/freerouting-cpp",
}

SETTINGS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings.json")


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


def router_args(settings, dsn, ses, path_conv=lambda p: p):
    """Arguments after the binary name."""
    args = ["-de", path_conv(dsn), "-do", path_conv(ses)]
    if settings.get("threads", 0) > 0:
        args += ["-mt", str(settings["threads"])]
    if settings.get("max_passes", 0) > 0:
        args += ["-mp", str(settings["max_passes"])]
    if settings.get("timeout_minutes", 0) > 0:
        args += ["--timeout", "PT%dM" % settings["timeout_minutes"]]
    if settings.get("no_optimizer"):
        args.append("--no-optimizer")
    if settings.get("via_costs", 0) > 0:
        args.append("--router.scoring.via_costs=%d" % settings["via_costs"])
    extra = settings.get("extra_args", "").strip()
    if extra:
        args += extra.split()
    return args


def build_command(settings, dsn, ses, pidfile=None):
    """Full command line (list) for subprocess."""
    if settings.get("use_wsl"):
        args = router_args(settings, dsn, ses, to_wsl_path)
        cmd = ["wsl"]
        if settings.get("wsl_distro"):
            cmd += ["-d", settings["wsl_distro"]]
        if pidfile:
            cmd += ["-e", "sh", "-c", 'echo $$ > "$0"; exec "$@"', to_wsl_path(pidfile)]
        else:
            cmd += ["-e"]
        return cmd + [settings["router_path"]] + args
    return [settings["router_path"]] + router_args(settings, dsn, ses)


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

    def __init__(self, settings, dsn, ses, work_dir, on_line=None, on_exit=None):
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
        self.pidfile = os.path.join(work_dir, "router.pid") if settings.get("use_wsl") else None
        self.log_path = os.path.join(work_dir, "router.log")
        self.command = build_command(settings, dsn, ses, self.pidfile)
        self._thread = None

    def start(self):
        if self.pidfile and os.path.exists(self.pidfile):
            os.remove(self.pidfile)
        kwargs = {}
        if IS_WINDOWS:
            kwargs["creationflags"] = 0x08000000  # CREATE_NO_WINDOW: no console window pops up
        self.proc = subprocess.Popen(
            self.command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            cwd=self.work_dir, **kwargs)
        self._thread = threading.Thread(target=self._read, name="freerouting-cpp-reader")
        self._thread.daemon = True
        self._thread.start()

    def _read(self):
        with open(self.log_path, "w", encoding="utf-8") as log:
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
            self.proc.terminate()
        except OSError:
            pass

    def succeeded(self):
        return (not self.cancelled and self.returncode == 0 and os.path.exists(self.ses)
                and os.path.getsize(self.ses) > 0)


def _main(argv):
    """Command-line check: python -m freerouting_cpp.runner in.dsn out.ses [threads]"""
    if len(argv) < 3:
        print("usage: python -m freerouting_cpp.runner in.dsn out.ses [threads]")
        return 2
    s = load_settings()
    if len(argv) > 3:
        s["threads"] = int(argv[3])
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
