"""test_gt1_run.py - offline end-to-end proof of g_t1_run.sh (gate G-T1) over the mock engine. Never touches the game:
  - every run is --mock (synthetic engine, kill only RECORDED in <dir>/mock/killed.txt),
  - PATH starts with a shim folder whose taskkill / tasklist / powershell only log their arguments; the test FAILS if the shim was called,
  - the one non-mock case runs with a GAME_STATE that says CLOSED and must refuse before any process is looked at.
The synthetic engine reacts to the WIDTH knob only, so a full mock run can only end INCONCLUSIVE (length never compresses): this proves
the flow (hub write, in-process schedule, restore, kill, analysis), not the effect; the criterion itself is proven in mm_stats --self-test.
python test_gt1_run.py --self-test [--bash PATH]
"""
import os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SHIMS = ("taskkill", "tasklist", "powershell")


def find_bash(explicit=None):
    for c in (explicit, os.environ.get("TOOLS_BASH"), r"C:\Program Files\Git\bin\bash.exe", r"C:\msys64\usr\bin\bash.exe", shutil.which("bash")):
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
        env0 = dict(os.environ, PATH=shim + os.pathsep + os.environ.get("PATH", ""), PYTHON=sys.executable,
                    TURBO_OUTPUT_DIR=os.path.join(d, "to"), TURBO_GAME_STATE=os.path.join(d, "GAME_STATE.txt"),
                    X1_TIME_SCALE="0.05", X1_KO_END="100000", X1_MOCK_LATENCY="0.9", G_T1_VANILLA="length=40,width=45")
        subprocess.run([bash, "-c", "taskkill /IM shim-probe.exe"], env=env0, capture_output=True, text=True, timeout=30)
        shim_works = os.path.exists(calls) and "shim-probe" in open(calls).read()
        check(shim_works, "the PATH shim intercepts taskkill (probe with a dummy image name)")
        if not shim_works:
            print("SELF-TEST FAILED")
            return 1
        os.remove(calls)

        def run(name, args, **env):
            g = os.path.join(d, name)
            e = dict(env0, G_T1_DIR=g, **env)
            t = time.time()
            r = subprocess.run([bash, os.path.join(HERE, "g_t1_run.sh")] + args, env=e, capture_output=True, text=True, timeout=240)
            killed = os.path.join(g, "mock", "killed.txt")
            nk = len(open(killed).read().splitlines()) if os.path.exists(killed) else 0
            return r.returncode, r.stdout + r.stderr, nk, time.time() - t, g

        rc, out, nk, _, g = run("live", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        check(rc == 0 and nk == 0 and "VERDICT PASS (g_t1_run hub)" in out, "hub: identity + vanilla 0.84/1.0, both extremes written, marker B, game kept (exit %d)" % rc)
        wl = open(os.path.join(g, "mock", "g_t1_write.log")).read()
        check(wl.count("(read back ok)") == 2 and "0.840000 -> 0.400000" in wl and "1.000000 -> 0.500000" in wl,
              "hub write log: length 0.84 -> 0.4 and width 1.0 -> 0.5, read back")
        rc, out, nk, sec, g = run("live", ["--mock", "match"], X1_MOCK_ENGINE="live")
        wl = open(os.path.join(g, "mock", "g_t1_write.log")).read()
        check(nk == 1 and "in-process schedule" in out and "OUTCOME" in out and rc != 0,
              "match (engine reads width live): flow ran, killed once, OUTCOME printed, never PASS (length does not compress in the mock) (exit %d, %.0f s)" % (rc, sec))
        check(wl.count("(read back ok)") == 4 and wl.strip().splitlines()[-1].endswith("0.500000 -> 1.000000 (read back ok)"),
              "write log: 2 hub writes + restore of both knobs; the last line restores width to 1.0")
        csvs = [f for f in os.listdir(g) if f.endswith("_run")]
        wins = set()
        if csvs:
            import csv
            with open(os.path.join(g, csvs[0], "positions.csv"), newline="") as f:
                wins = {r["win"] for r in csv.DictReader(f)}
        check({"B", "A"} <= wins, "positions.csv carries the windows B and A (got %s)" % sorted(wins))
        rc, out, nk, _, _ = run("nohub", ["--mock", "match"], X1_MOCK_ENGINE="live")
        check(rc == 3 and nk == 1 and "refused" in out, "match without the hub values: INCONCLUSIVE (B precondition), killed (exit %d)" % rc)
        os.makedirs(os.path.join(d, "hubfail", "mock"))
        open(os.path.join(d, "hubfail", "mock", "g_t1_write_off.txt"), "w").close()
        rc, out, nk, _, _ = run("hubfail", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        check(rc == 1 and "kill switch" in out and nk == 1, "hub with the kill switch: FAIL, game killed (exit %d, kills %d)" % (rc, nk))
        rc, out, nk, _, _ = run("late", ["--mock", "hub"], X1_MOCK_ENGINE="live")
        rc, out, nk, _, _ = run("late", ["--mock", "match", "--since-ko", "400"], X1_MOCK_ENGINE="live", X1_KO_END="445")
        check(rc == 3 and "windows would be" in out and nk == 1, "started too late: INCONCLUSIVE, killed (exit %d)" % rc)
        with open(env0["TURBO_GAME_STATE"], "w") as f:
            f.write("state: CLOSED (21:00) - offline test\n")
        rc, out, nk, _, _ = run("closed", ["match"])
        check(rc == 3 and "refused" in out, "no --mock and GAME_STATE CLOSED: refused, exit 3")
        check(not os.path.exists(calls), "no taskkill / tasklist / powershell call was made by any run")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        b = sys.argv[sys.argv.index("--bash") + 1] if "--bash" in sys.argv else None
        sys.exit(self_test(b))
    print(__doc__)
    sys.exit(2)
