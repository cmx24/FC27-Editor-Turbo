"""test_battery_run.py - offline end-to-end proof of battery_run.sh (the 2.5 knob battery) over the mock world. Never touches the game:
  - every run is --mock (battery_mock.py world, kill only RECORDED in <dir>/mock/killed.txt),
  - PATH starts with a shim folder whose taskkill / tasklist / powershell only log their arguments: even a bug that reached the live kill
    path could not kill anything, and the test FAILS if the shim was called at all,
  - the non-mock cases run with a GAME_STATE file that says CLOSED and must refuse before any process is looked at.
python test_battery_run.py --self-test [--bash PATH]   (bash: TOOLS_BASH, else Git for Windows / MSYS2; never the WSL launcher)
"""
import os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SHIMS = ("taskkill", "tasklist", "powershell")


def find_bash(explicit=None):
    cands = [explicit, os.environ.get("TOOLS_BASH"), r"C:\Program Files\Git\bin\bash.exe", r"C:\msys64\usr\bin\bash.exe",
             shutil.which("bash")]
    for c in cands:
        if c and os.path.exists(c) and "system32" not in c.lower() and "windowsapps" not in c.lower():
            return c
    return None


def self_test(bash=None):
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    bash = find_bash(bash)
    if not bash:
        print("  FAIL  no bash found (set TOOLS_BASH)")
        print("SELF-TEST FAILED")
        return 1
    with tempfile.TemporaryDirectory() as d:
        shim = os.path.join(d, "shim")
        os.makedirs(shim)
        calls = os.path.join(d, "shim_calls.log")
        for c in SHIMS:
            with open(os.path.join(shim, c), "w", newline="\n") as f:
                f.write('#!/bin/sh\necho "%s $*" >> "%s"\nexit 0\n' % (c, calls.replace("\\", "/")))
        knobs = os.path.join(d, "knobs.txt")
        with open(knobs, "w") as f:
            f.write("shape.width_in\nshape.width_out\nshape.width_in_cb\nlineband.deep_max\n")
        base_env = dict(os.environ, PATH=shim + os.pathsep + os.environ.get("PATH", ""), PYTHON=sys.executable,
                        TURBO_OUTPUT_DIR=os.path.join(d, "to"), TURBO_GAME_STATE=os.path.join(d, "GAME_STATE.txt"),
                        TURBO_EVIDENCE_DIR=os.path.join(d, "ev"), BATTERY_TIME_SCALE="0.02")
        r = subprocess.run([bash, "-c", "taskkill /IM shim-probe.exe"], env=base_env, capture_output=True, text=True, timeout=30)
        shim_works = os.path.exists(calls) and "shim-probe" in open(calls).read()
        check(shim_works, "the PATH shim intercepts taskkill (probe with a dummy image name)")
        if not shim_works:
            print("SELF-TEST FAILED")
            return 1
        os.remove(calls)

        def run(name, args, **env):
            bd = os.path.join(d, name)
            e = dict(base_env, BATTERY_DIR=bd, **env)
            t = time.time()
            r = subprocess.run([bash, os.path.join(HERE, "battery_run.sh")] + args, env=e, capture_output=True, text=True, timeout=240)
            killed = os.path.join(bd, "mock", "killed.txt")
            nk = len(open(killed).read().splitlines()) if os.path.exists(killed) else 0
            return r.returncode, r.stdout + r.stderr, nk, time.time() - t, bd

        rc, out, nk, _, _ = run("plan", ["plan", "--knobs", knobs])
        check(rc == 0 and "schedule" in out and "engine-read: band_att" in out and nk == 0, "plan: offline schedule with the engine-read column (exit %d)" % rc)
        rc, out, nk, _, _ = run("hub", ["--mock", "hub", "--knobs", knobs])
        check(rc == 0 and "BATTERY HUB PREFLIGHT: PASS" in out and nk == 0, "hub: 4 knobs located, preflight PASS, game kept (exit %d, kills %d)" % (rc, nk))
        rc, out, nk, sec, bd = run("match", ["--mock", "match", "--knobs", knobs, "--budget", "420"])
        check(rc == 0 and "VERDICT PASS (battery_run match)" in out and nk == 1 and "BATTERY KNOB shape.width_in:" in out,
              "match: the run completes, per-knob lines printed, the game killed once (exit %d, kills %d, %.0f s)" % (rc, nk, sec))
        check("engine=PASS effect=PASS" in out.split("BATTERY KNOB shape.width_in:")[1].split("\n")[0], "match: width_in engine PASS + effect PASS in the printed line")
        check("BATTERY SKIPPED lineband.deep_max" in out, "match: the set-up-only knob is reported as skipped")
        evs = [os.path.join(dp, f) for dp, dn, fn in os.walk(bd) for f in fn]
        names = {os.path.basename(p) for p in evs}
        check({"plan.json", "positions.csv", "engine.csv", "writes.json", "report.txt", "result.json", "log.txt", "battery_run.log"} <= names,
              "match: the evidence files are written (%s)" % ", ".join(sorted(names)))
        # failures: every exit kills
        rc, out, nk, _, _ = run("wfail", ["--mock", "match", "--knobs", knobs], BATTERY_MOCK_WORLD="scale=0.02;delay=2;failwrite=3")
        check(rc == 1 and nk == 1 and "VERDICT FAIL (battery_run match)" in out, "a failing dev write: FAIL, game killed (exit %d, kills %d)" % (rc, nk))
        rc, out, nk, _, _ = run("nomatch", ["--mock", "match", "--knobs", knobs, "--wait-start", "8"], BATTERY_MOCK_WORLD="scale=0.02;delay=100000")
        check(rc == 3 and nk == 1 and "no running match" in out, "no match within the wait: INCONCLUSIVE, game killed (exit %d)" % rc)
        rc, out, nk, sec, bd = run("wd", ["--mock", "match", "--knobs", knobs], BATTERY_WATCHDOG_S="4")
        log = open(sorted(os.path.join(dp, f) for dp, dn, fn in os.walk(bd) for f in fn if f == "battery_run.log")[-1]).read()
        check(rc == 3 and nk == 1 and "watchdog" in log and sec < 25, "the watchdog stops a running battery at once and kills (exit %d, %.0f s)" % (rc, sec))
        # live path with GAME_STATE CLOSED: refused before anything
        with open(base_env["TURBO_GAME_STATE"], "w") as f:
            f.write("state: CLOSED (21:00) - offline test\n")
        rc, out, nk, _, _ = run("closed", ["match", "--knobs", knobs])
        check(rc == 3 and "refused" in out and nk == 0, "no --mock and GAME_STATE CLOSED: match refused, exit 3, no kill")
        rc, out, nk, _, _ = run("closed", ["hub", "--knobs", knobs])
        check(rc == 3 and "refused" in out, "hub: the same refusal")
        check(not os.path.exists(calls), "no taskkill / tasklist / powershell call was made by any run")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        b = sys.argv[sys.argv.index("--bash") + 1] if "--bash" in sys.argv else None
        sys.exit(self_test(b))
    print(__doc__)
    sys.exit(2)
