"""test_x1_run.py - offline end-to-end proof of x1_run.sh (gate X1) over the mock engine. Never touches the game:
  - every run is --mock (synthetic engine, harness.MockMem, kill only RECORDED in <dir>/mock/killed.txt),
  - PATH starts with a shim folder whose taskkill / tasklist / powershell only log their arguments: even a bug that reached the live
    kill path could not kill anything, and the test FAILS if the shim was called at all,
  - the one non-mock case runs with a GAME_STATE file that says CLOSED and must refuse before any process is looked at.
python test_x1_run.py --self-test [--bash PATH]   (bash: TOOLS_BASH, else Git for Windows / MSYS2; never the WSL launcher)
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
        base_env = dict(os.environ, PATH=shim + os.pathsep + os.environ.get("PATH", ""), PYTHON=sys.executable,
                        TURBO_OUTPUT_DIR=os.path.join(d, "to"), TURBO_GAME_STATE=os.path.join(d, "GAME_STATE.txt"),
                        X1_TIME_SCALE="0.05", X1_KO_END="100000", X1_MOCK_LATENCY="0.9")
        r = subprocess.run([bash, "-c", "taskkill /IM shim-probe.exe"], env=base_env, capture_output=True, text=True, timeout=30)
        shim_works = os.path.exists(calls) and "shim-probe" in open(calls).read()
        check(shim_works, "the PATH shim intercepts taskkill (probe with a dummy image name)")
        if not shim_works:
            print("SELF-TEST FAILED")
            return 1
        os.remove(calls)

        def run(name, args, **env):
            x1 = os.path.join(d, name)
            e = dict(base_env, X1_DIR=x1, **env)
            t = time.time()
            r = subprocess.run([bash, os.path.join(HERE, "x1_run.sh")] + args, env=e, capture_output=True, text=True, timeout=240)
            killed = os.path.join(x1, "mock", "killed.txt")
            nk = len(open(killed).read().splitlines()) if os.path.exists(killed) else 0
            return r.returncode, r.stdout + r.stderr, nk, time.time() - t, x1

        # 1. engine reads live -> outcome 1
        rc, out, nk, _, _ = run("live", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        check(rc == 0 and nk == 0 and "VERDICT PASS (x1_run hub)" in out, "hub: identity + 1.0, 0.6 written, marker B1, game kept (exit %d)" % rc)
        rc, out, nk, sec, x1 = run("live", ["--mock", "match"], X1_MOCK_ENGINE="live")
        check(rc == 0 and "OUTCOME 1" in out and nk == 1 and "in-process schedule" in out,
              "match (default inproc mode, the live-proven pattern), engine reads live: OUTCOME 1, PASS, killed once (exit %d, %.0f s)" % (rc, sec))
        check(out.count("(read back ok)") >= 3, "inproc: the three mid-match writes ran between samples of the same process")
        wl = open(os.path.join(x1, "mock", "x1_write.log")).read()
        check(wl.count("(read back ok)") == 4 and wl.strip().splitlines()[-1].endswith("0.600000 -> 1.000000 (read back ok)"),
              "write log: B1 set, revert, set, revert - last line reverts to 1.0")
        rc, out, nk, _, _ = run("pause", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        rc, out, nk, sec, _ = run("pause", ["--mock", "match", "--mode", "pause"], X1_MOCK_ENGINE="live")
        check(rc == 0 and "OUTCOME 1" in out and nk == 1 and out.count("(read back ok)") >= 3,
              "--mode pause (offline-proven only): the separate writer gets the emulated dev lock, OUTCOME 1 (exit %d, %.0f s)" % (rc, sec))
        # 2. engine reads once at setup -> outcome 3 (the ABAB cannot see it)
        rc, out, nk, _, _ = run("once", ["--mock", "hub"], X1_MOCK_ENGINE="once")
        rc, out, nk, sec, _ = run("once", ["--mock", "match"], X1_MOCK_ENGINE="once")
        check(rc in (1, 3) and "OUTCOME 3" in out and nk == 1, "match, engine latches at setup: OUTCOME 3, not PASS, killed (exit %d)" % rc)
        # 3. failure paths: every exit kills (mock record), a refusal does not
        os.makedirs(os.path.join(d, "hubfail", "mock"))
        open(os.path.join(d, "hubfail", "mock", "x1_write_off.txt"), "w").close()  # the kill switch of the write
        rc, out, nk, _, _ = run("hubfail", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        check(rc == 1 and "kill switch" in out and nk == 1, "hub with the write kill switch: FAIL, game killed (exit %d, kills %d)" % (rc, nk))
        rc, out, nk, _, _ = run("nohub", ["--mock", "match"], X1_MOCK_ENGINE="live")
        check(rc == 3 and "not in place after kick-off" in out and nk == 1, "match without the hub value: INCONCLUSIVE, killed (exit %d)" % rc)
        rc, out, nk, _, _ = run("late", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        rc, out, nk, _, x1 = run("late", ["--mock", "match", "--since-ko", "300"], X1_MOCK_ENGINE="live", X1_KO_END="445")
        wl = open(os.path.join(x1, "mock", "x1_write.log")).read()
        check(rc == 3 and "windows would be" in out and nk == 1 and wl.count("(read back ok)") == 1,
              "started too late (300 s after kick-off): INCONCLUSIVE, killed at once (no dev call before the kill) (exit %d)" % rc)
        rc, out, nk, _, _ = run("wd", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        rc, out, nk, sec, x1 = run("wd", ["--mock", "match"], X1_MOCK_ENGINE="live", X1_WATCHDOG_S="140")
        log = open(sorted(os.path.join(x1, f) for f in os.listdir(x1) if f.endswith("_match.log"))[-1]).read()
        check(rc == 3 and nk == 1 and "watchdog" in log and sec < 18,
              "the watchdog stops a running schedule at once and kills (exit %d, %.0f s; a full run takes ~21 s)" % (rc, sec))
        # 4. live path with GAME_STATE CLOSED: refused before anything (no process lookup, no kill)
        with open(base_env["TURBO_GAME_STATE"], "w") as f:
            f.write("state: CLOSED (21:00) - offline test\n")
        rc, out, nk, _, _ = run("closed", ["match"])
        check(rc == 3 and "refused" in out, "no --mock and GAME_STATE CLOSED: refused, exit 3")
        rc, out, nk, _, _ = run("closed", ["hub"])
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
