"""Checks of freerouting_cpp.runner without KiCad.

    python tests/test_runner.py <fixtures dir> <work dir> [<freerouting-cpp.exe>]

Without the exe the router runs as the settings say (auto: the exe next to the plugin, else WSL);
with it, natively from that path.

1. routes a small DSN and checks the SES and the parsed progress;
2. starts a large DSN, cancels it after a few seconds and checks that the Linux process is gone.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from freerouting_cpp import runner  # noqa: E402


def main(fixtures, work, exe=None):
    os.makedirs(work, exist_ok=True)
    s = runner.load_settings()
    if exe:
        s.update(mode="native", native_path=os.path.abspath(exe))
    print("mode:", runner.resolve(s))

    # 1. a full run
    dsn = os.path.join(fixtures, "Issue107-freq_teiler_200kHz_kicad.dsn")
    ses = os.path.join(work, "small.ses")
    if os.path.exists(ses):
        os.remove(ses)
    statuses = []
    run = runner.RouterRun(s, dsn, ses, work, on_line=lambda l: statuses.append(run.progress.status))
    run.start()
    run.wait(600)
    assert run.succeeded(), "router failed, see %s" % run.log_path
    assert run.progress.summary is not None and run.progress.summary[0] == 0, run.progress.summary
    assert run.progress.initial_unrouted == 163, run.progress.initial_unrouted
    assert open(ses).read().lstrip().startswith("(session"), "not an SES file"
    print("full run ok:", run.progress.status)

    # 2. cancel
    big = os.path.join(fixtures, "Issue508-DAC2020", "DAC2020_bm01", "DAC2020_bm01.unrouted.dsn")
    ses2 = os.path.join(work, "big.ses")
    run2 = runner.RouterRun(s, big, ses2, work)
    run2.start()
    time.sleep(6)
    assert run2.running(), "big board finished too fast for the cancel check"
    pid = None
    if run2.pidfile:
        with open(run2.pidfile) as f:
            pid = f.read().strip()
    t = time.time()
    run2.cancel()
    run2.wait(30)
    assert not run2.running() and not run2.succeeded()
    alive = pid is not None and subprocess.call(["wsl", "-d", s["wsl_distro"], "-e", "kill", "-0", pid],
                            stderr=subprocess.DEVNULL) == 0
    assert not alive, "router process %s still alive in WSL" % pid
    print("cancel ok in %.1f s, exit code %s, status: %s" % (time.time() - t, run2.returncode, run2.progress.status))


if __name__ == "__main__":
    main(*sys.argv[1:4])
