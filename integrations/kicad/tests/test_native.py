"""Native-mode checks of freerouting_cpp.runner with the stub router (tests/stub).

    python tests/test_native.py <stub freerouting-cpp.exe> <work dir>

Build the stub first (in WSL, or with any mingw):
    x86_64-w64-mingw32-gcc -O2 -static -o freerouting-cpp.exe tests/stub/stub_router.c

Checks: mode choice (auto/native/wsl, the executable next to the plugin, an explicit path),
Windows paths on the command line, a successful run, a failed run, a missing executable,
and Cancel killing the process.
"""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from freerouting_cpp import runner  # noqa: E402


def make_run(s, work, name):
    dsn = os.path.join(work, name + ".dsn")
    with open(dsn, "w") as f:
        f.write("(pcb stub)\n")
    ses = os.path.join(work, name + ".ses")
    if os.path.exists(ses):
        os.remove(ses)
    return runner.RouterRun(s, dsn, ses, work)


def main(stub, work):
    stub = os.path.abspath(stub)
    work = os.path.abspath(os.path.join(work, "native dir"))  # a space in the path on purpose
    os.makedirs(work, exist_ok=True)
    base = dict(runner.DEFAULT_SETTINGS)

    # mode choice, with a fake plugin directory
    plug = os.path.join(work, "plugin")
    shutil.rmtree(plug, ignore_errors=True)
    os.makedirs(os.path.join(plug, "bin"))
    if runner.IS_WINDOWS:
        assert runner.resolve(base, plug) == ("wsl", None), "auto without an exe must fall back to WSL"
    assert runner.resolve(dict(base, mode="native"), plug)[0] == "native"
    for where in (os.path.join(plug, "bin", runner.EXE_NAME), os.path.join(plug, runner.EXE_NAME)):
        shutil.copy(stub, where)
        assert runner.resolve(base, plug) == ("native", where), runner.resolve(base, plug)
        assert runner.resolve(dict(base, mode="wsl"), plug) == ("wsl", None)
        os.remove(where)
    assert runner.resolve(dict(base, native_path=stub), plug) == ("native", stub)
    print("mode choice ok")

    s = dict(base, native_path=stub, mode="native", via_costs=200, no_fanout=True)

    # successful run
    run = make_run(s, work, "board")
    assert run.command[0] == stub and "wsl" not in run.command, run.command
    run.start()
    run.wait(60)
    assert run.succeeded(), (run.returncode, run.log_path)
    log = open(run.log_path, encoding="utf-8").read()
    assert "arg: " + run.dsn in log and "arg: " + run.ses in log, "Windows paths expected:\n" + log
    assert "/mnt/" not in log
    assert "arg: --router.fanout.enabled=false" in log and "arg: --router.scoring.via_costs=200" in log
    assert run.progress.summary == (0, 0, 0.5), run.progress.summary
    print("native run ok:", run.progress.status)

    # failed run: empty SES, exit 1 -> not succeeded
    run = make_run(s, work, "fail")
    run.start()
    run.wait(60)
    assert run.returncode == 1 and not run.succeeded()
    print("failed run ok: exit", run.returncode)

    # missing executable
    run = make_run(dict(s, native_path=os.path.join(work, "nope.exe")), work, "board")
    try:
        run.start()
        raise AssertionError("start() must fail without the executable")
    except OSError as e:
        print("missing exe ok:", e)

    # cancel
    run = make_run(s, work, "slow")
    run.start()
    time.sleep(1.5)
    assert run.running() and run.progress.route_pass > 0, run.progress.status
    t = time.time()
    run.cancel()
    run.wait(10)
    assert not run.running() and run.cancelled and not run.succeeded()
    print("cancel ok in %.2f s, exit code %s" % (time.time() - t, run.returncode))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
