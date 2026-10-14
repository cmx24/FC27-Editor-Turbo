"""test_gate_runs.py - offline end-to-end proof of x2_run.sh and x3_run.sh over synthetic worlds. Never touches the game: every run is
--mock (kill only recorded), PATH starts with a shim whose taskkill / tasklist / powershell only log (the test FAILS if they were
called), and the non-mock cases run with GAME_STATE = CLOSED and must refuse before anything.
python test_gate_runs.py --self-test [--bash PATH]
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sheet_copies as sc  # noqa: E402
import x2_readout  # noqa: E402
from test_x1_run import find_bash, SHIMS  # noqa: E402


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
        shim, calls = os.path.join(d, "shim"), os.path.join(d, "shim_calls.log")
        os.makedirs(shim)
        for c in SHIMS:
            with open(os.path.join(shim, c), "w", newline="\n") as f:
                f.write('#!/bin/sh\necho "%s $*" >> "%s"\nexit 0\n' % (c, calls.replace("\\", "/")))
        env0 = dict(os.environ, PATH=shim + os.pathsep + os.environ.get("PATH", ""), PYTHON=sys.executable,
                    TURBO_OUTPUT_DIR=os.path.join(d, "to"), TURBO_GAME_STATE=os.path.join(d, "GAME_STATE.txt"))

        def run(script, args, **env):
            r = subprocess.run([bash, os.path.join(HERE, script)] + args, env=dict(env0, **env), capture_output=True, text=True,
                               timeout=180)
            return r.returncode, r.stdout + r.stderr

        def kills(folder):
            p = os.path.join(folder, "killed.txt")
            return len(open(p).read().splitlines()) if os.path.exists(p) else 0

        # ---- X2
        x2d, log = os.path.join(d, "x2"), os.path.join(d, "turbo_gui.log")
        sheets, readout = os.path.join(d, "sheets.json"), os.path.join(d, "readout.json")
        sc.mock_world(sheets, log)
        x2_readout.mock_world(readout)  # the match copy carries the map sentinel 95 / 1 / 0
        e2 = {"X2_DIR": x2d, "X2_MOCK_SHEETS": sheets, "X2_MOCK_READOUT": readout, "X2_LOG": log}
        rc, out = run("x2_run.sh", ["--mock", "hub"], **e2)
        check(rc == 0 and "x2_sentinels: map" in out and "(read back ok)" in out and kills(x2d) == 0,
              "x2 hub: map record 95/1/0 written and read back, game kept (exit %d)" % rc)
        rc, out = run("x2_run.sh", ["--mock", "match"], **e2)
        check(rc == 0 and "X2 BRANCH: map record is the channel" in out and "DA STORED" in out and kills(x2d) == 1,
              "x2 match: readout -> X2 BRANCH map record, DA stored; killed (exit %d)" % rc)
        x2b = os.path.join(d, "x2b")
        os.makedirs(x2b)
        open(os.path.join(x2b, "sentinels_off.txt"), "w").close()
        sheets2 = os.path.join(d, "sheets2.json")
        sc.mock_world(sheets2, None)
        rc, out = run("x2_run.sh", ["--mock", "hub"], **dict(e2, X2_DIR=x2b, X2_MOCK_SHEETS=sheets2))
        check(rc == 3 and "kill switch" in out and kills(x2b) == 1, "x2 hub with the sentinel kill switch: refused, game killed (exit %d)" % rc)
        # ---- X3
        x3d = os.path.join(d, "x3")
        sheets3 = os.path.join(d, "sheets3.json")
        mem, svc = sc.mock_world(sheets3, None)
        e3 = {"X3_DIR": x3d, "X3_MOCK_SHEETS": sheets3, "X3_LOG": log}
        rc, out = run("x3_run.sh", ["--mock", "hub"], **e3)
        check(rc == 0 and out.count("(read back ok)") == 4 and "all 4 checked sentinels read back" in out and kills(x3d) == 0,
              "x3 hub: 4 copies written (map/B0/A0/C0) and confirmed, game kept (exit %d)" % rc)
        # Edit Tactic opens: X0 gets the screen's copy (B0's sentinel 85 / 1 / 1)
        rec = sc.record(sc.MOCK_CODE, 85, 1, 1)
        mem.write(svc + sc.RECS["X0"], rec)
        rc, out = run("x3_run.sh", ["--mock", "open"], **e3)
        check(rc == 0 and "SCREEN SOURCE = B0" in out and "merge 2.4/channel-e3" in out and kills(x3d) == 0,
              "x3 open: SCREEN SOURCE = B0 -> WP-B2 (a); the game stays up for Q5 (exit %d)" % rc)
        mem.write(svc + sc.RECS["X0"] + sc.F_BU, struct.pack("<i", 2))  # Short Passing saved
        rc, out = run("x3_run.sh", ["--mock", "after"], **e3)
        check(rc == 0 and "stored as Build-Up 2" in out and kills(x3d) == 1, "x3 after: Q5 value printed, killed (exit %d)" % rc)
        mem.write(svc + sc.RECS["X0"], sc.record(sc.MOCK_CODE, 25, 0, 0))
        rc, out = run("x3_run.sh", ["--mock", "open"], **e3)
        check(rc == 1 and "SCREEN SOURCE = none" in out and kills(x3d) == 1, "x3 open with the career-load value: a valid FAIL outcome, not killed")
        # ---- refusals (no --mock, GAME_STATE CLOSED)
        with open(env0["TURBO_GAME_STATE"], "w") as f:
            f.write("state: CLOSED (21:00) - offline test\n")
        for script, phase in (("x2_run.sh", "match"), ("x3_run.sh", "after")):
            rc, out = run(script, [phase])
            check(rc == 3 and "refused" in out, "%s %s without --mock while GAME_STATE is CLOSED: refused, exit 3" % (script, phase))
        for token in ("USER", "HUB"):  # USER = Cassio's own game; HUB = a read-only window (wrappers may write and kill)
            with open(env0["TURBO_GAME_STATE"], "w") as f:
                f.write("state: %s (21:00) - offline test\n" % token)
            for script in ("x1_run.sh", "x2_run.sh", "x3_run.sh"):
                rc, out = run(script, ["hub"])
                check(rc == 3 and "refused" in out, "%s hub while GAME_STATE is %s: refused before any trap, exit 3" % (script, token))
        check(not os.path.exists(calls), "no taskkill / tasklist / powershell call was made by any run")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        b = sys.argv[sys.argv.index("--bash") + 1] if "--bash" in sys.argv else None
        sys.exit(self_test(b))
    print(__doc__)
    sys.exit(2)
