"""x3_readback.py - READ-ONLY verification for gate X3 (where does Edit Tactic load from? decision 4.3). From the sweep's
T3/x3_readback.py (2026-10-13); the copy resolution lives in sheet_copies.py. dev ops: bytes / multi only; <= 60 s.

  python x3_readback.py --phase hub   [--svc 0x..] [--team N] [--plan x3_plan.json]
        in the hub after x3_sentinels.py (or BEFORE it, to print every copy and its address). PASS = every sentinel of the plan whose
        copy exists in the hub reads back; INCONCLUSIVE = none could be checked.
  python x3_readback.py --phase open  Edit Tactic open: which sentinel X[0] (svc+0xF80, the record the screen displays) holds ->
        "SCREEN SOURCE = <copy>" + the branch of table 4.3; PASS = one sentinel matches; FAIL = none (e.g. 25 = career-load value).
  python x3_readback.py --phase after after "Short Passing" + Save Changes: prints X[0]+0xB0 (the stored Build-Up enum, Q5).
  --mock MEMFILE (or TURBO_MOCK_MEM): offline over a harness.MockMem file (sheet_copies.mock_world); --self-test.
svc / team default to Turbo's log lines (sheet_copies.from_log). Exit 0 PASS / 1 FAIL / 3 INCONCLUSIVE (refused, no answer, budget).
"""
import argparse, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import sheet_copies as sc  # noqa: E402

TOOL = "x3_readback"
PLAN = os.path.join(HERE, "x3_plan.json")
BRANCH = {"map": "the map record IS the source: WP-B1 alone fixes the display (verify once more in the final smoke)",
          "A0": "another service copy (A[0] svc+0x550, the TM working sheet): WP-B2 (b), extend the guarded write to it",
          "B0": "svc+0xA60 inline sheet B[0]: WP-B2 (a), merge 2.4/channel-e3",
          "C0": "another service copy (C[0] svc+0x40): WP-B2 (b)",
          "X0": "X[0] was already filled in the hub: WP-B2 (b) on X[0]",
          "Y0": "Y[0] svc+0x1170: WP-B2 (b)"}


def reader(mock_mem, budget):
    if mock_mem:
        return sc.mock_reader(mock_mem)
    import dev2

    def read(pairs):
        if not pairs:
            return []
        r = dev2.call({"op": "multi", "ops": [{"op": "bytes", "addr": sc.H(a), "len": n} for a, n in pairs]}, timeout=min(20, budget))
        if r.get("error"):
            raise RuntimeError("dev service: %s" % r["error"])
        return [bytes.fromhex(x["hex"]) if x.get("ok") and "hex" in x else None for x in r.get("results", [])]
    return read


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--phase", choices=["hub", "open", "after"], default="hub")
    ap.add_argument("--svc", default=None)
    ap.add_argument("--team", type=int, default=None)
    ap.add_argument("--plan", default=PLAN)
    ap.add_argument("--log", default=sc.LOG)
    ap.add_argument("--budget", type=float, default=60.0)
    ap.add_argument("--mock", nargs="?", const=os.environ.get("TURBO_MOCK_MEM"), default=None, metavar="MEMFILE")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    harness.require_live(a.mock is not None, tool=TOOL)
    box = harness.timebox(a.budget, TOOL)
    try:
        return run(a)
    except (sc.Unusable, RuntimeError) as e:
        return harness.finish("INCONCLUSIVE", [str(e)], tool=TOOL)
    finally:
        box.cancel()


def run(a):
    plan = json.load(open(a.plan, encoding="utf-8"))
    lsvc = lteam = None
    if not (a.svc and a.team is not None):
        try:
            lsvc, lteam, _pid = sc.from_log(a.log, a.mock or None)
        except harness.StaleLog as e:
            return harness.finish("INCONCLUSIVE", ["refused: %s" % e], tool=TOOL)
    svc = int(a.svc, 16) if a.svc else lsvc
    team = a.team if a.team is not None else lteam
    if not svc or team is None:
        return harness.finish("INCONCLUSIVE", ["no service address / team: open Turbo's Tactics card once (log 'channel E: service "
                                               "located ... at 0x...' and 'channel E: team N record'), or pass --svc / --team"], tool=TOOL)
    R = sc.resolve(reader(a.mock, a.budget), svc, team)
    copies = R["copies"]
    print("\nCopies (phase %s):" % a.phase)
    for lab in sorted(copies):
        ad, f = copies[lab]
        print("  - %-12s at %s: %s" % (lab, sc.H(ad), sc.show(f)))
    sent = plan["sentinels"]
    if a.phase == "hub":
        ok, checked = True, 0
        for s in sent:
            hit = sc.target(copies, s["copy"])
            if hit is None:
                print("  sentinel %d on %s: copy absent / holds no tactic in the hub -> skipped" % (s["line"], s["copy"]))
                continue
            lab, (ad, f) = hit
            good = f["line"] == s["line"] and f["buildup"] == s["buildup"] and f["approach"] == s["approach"]
            checked += 1
            ok = ok and good
            print("  sentinel %d/%d/%d on %s (%s at %s): %s" % (s["line"], s["buildup"], s["approach"], s["copy"], lab, sc.H(ad),
                                                               "ok" if good else "MISMATCH " + sc.show(f)))
        if checked == 0:
            return harness.finish("INCONCLUSIVE", ["no sentinel could be checked"], tool=TOOL)
        return harness.finish("PASS" if ok else "FAIL", ["all %d checked sentinels read back" % checked if ok else
                                                         "a sentinel did not read back"], tool=TOOL)
    x0 = copies.get("X0")
    if not x0 or not x0[1]["code"]:
        return harness.finish("FAIL", ["X0 (svc+0xF80, the record Edit Tactic displays) holds no tactic: Edit Tactic is not open or the "
                                       "layout moved"], tool=TOOL)
    f = x0[1]
    if a.phase == "open":
        by_line = {s["line"]: s["copy"] for s in sent}
        src = by_line.get(f["line"])
        q4 = ("DERIVED (DA = band of LH)" if f["approach"] == sc.band(f["line"])
              else "STORED (DA %d != band %d of LH %d)" % (f["approach"], sc.band(f["line"]), f["line"]))
        if src:
            print("SCREEN SOURCE = %s (X0 holds LH %d, the sentinel of %s); Q4: approach %s" % (src, f["line"], src, q4))
            print("X3 BRANCH: %s" % BRANCH.get(src, "?"))
            return harness.finish("PASS", tool=TOOL)
        print("SCREEN SOURCE = none of the sentinelled copies (X0 holds LH %d%s); Q4: %s" % (
            f["line"], ", the career-load value" if f["line"] == plan.get("career_load", {}).get("line") else "", q4))
        print("X3 BRANCH: WP-B2 (c) - reload-based display, honest badge (X4 only with Cassio's yes)")
        return harness.finish("FAIL", tool=TOOL)
    print("after Save Changes: X0 +0xB0 (Build-Up enum) = %d, +0xA4 LH %d, +0xB4 DA %d" % (f["buildup"], f["line"], f["approach"]))
    print("Q5: Short Passing is stored as Build-Up %d" % f["buildup"])
    return harness.finish("PASS", tool=TOOL)


def self_test():
    import contextlib, io, subprocess, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    def go(*args):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(list(args))
        return rc, buf.getvalue()

    plan = json.load(open(PLAN, encoding="utf-8"))
    with tempfile.TemporaryDirectory() as d:
        log = os.path.join(d, "turbo_gui.log")
        mem_p = os.path.join(d, "mem.json")
        mem, svc = sc.mock_world(mem_p, log)
        rc, out = go("--phase", "hub", "--mock", mem_p, "--log", log)
        check(rc == 1 and "MISMATCH" in out and "map.s0.rec0" in out, "hub before the writes: every copy listed, sentinels MISMATCH (exit %d)" % rc)
        R = sc.resolve(sc.mock_reader(mem_p), svc, sc.MOCK_TEAM, say=lambda *x: None)
        for s in plan["sentinels"]:  # simulate the sentinel writes
            hit = sc.target(R["copies"], s["copy"])
            if hit:
                import struct
                mem.write(hit[1][0] + sc.F_LH, struct.pack("<i", s["line"]))
                mem.write(hit[1][0] + sc.F_BU, struct.pack("<ii", s["buildup"], s["approach"]))
        rc, out = go("--phase", "hub", "--mock", mem_p, "--log", log)
        check(rc == 0 and "all 4 checked sentinels read back" in out, "hub after the writes: the 4 existing copies read back, X0/Y0 skipped")
        for lh, want_rc, want in ((85, 0, "SCREEN SOURCE = B0"), (75, 0, "SCREEN SOURCE = A0"), (95, 0, "SCREEN SOURCE = map"),
                                  (25, 1, "SCREEN SOURCE = none")):
            m2 = os.path.join(d, "open%d.json" % lh)
            sc.mock_world(m2, None, x0=(lh, 1, 0))
            rc, out = go("--phase", "open", "--mock", m2, "--log", log)
            check(rc == want_rc and want in out and "X3 BRANCH" in out, "open with X0 = %d: %s (exit %d)" % (lh, want, rc))
        m3 = os.path.join(d, "after.json")
        sc.mock_world(m3, None, x0=(85, 2, 0))
        rc, out = go("--phase", "after", "--mock", m3, "--log", log)
        check(rc == 0 and "stored as Build-Up 2" in out, "after Short Passing: Q5 value printed")
        rc, out = go("--phase", "open", "--mock", mem_p, "--log", log)
        check(rc == 1 and "holds no tactic" in out, "open while X0 is empty: FAIL")
        m4 = os.path.join(d, "freed.json")
        sc.mock_world(m4, None, loaded=0)
        rc, out = go("--phase", "hub", "--mock", m4, "--log", log)
        check(rc == 3 and "header checks" in out, "a service failing its header checks: INCONCLUSIVE, nothing trusted")
        rc, out = go("--phase", "hub", "--mock", mem_p, "--log", os.path.join(d, "none.log"))
        check(rc == 3 and "unreadable" in out, "no log and no --svc: refused, INCONCLUSIVE")
        # review B1: a two-process log - the service line belongs to process 1111, the latest start is the running 4242
        two = os.path.join(d, "two.log")
        with open(two, "w") as f:
            f.write("t  Turbo GUI loading in process 1111\n")
            f.write("t  channel E: service located by the registry Find at 0x%X (Find returned 0x%X)\n" % (svc, svc))
            f.write("t  channel E: role allow-list: 1 triples from 2 teams (your club %d left out, 0 club(s) skipped)\n" % sc.MOCK_TEAM)
            f.write("t  Turbo GUI loading in process %d\n" % harness.MOCK_PID)
        rc, out = go("--phase", "hub", "--mock", mem_p, "--log", two)
        check(rc == 3 and "no service address" in out, "two-process log: the older process's service address is never used")
        mem.set_kv("fc27_pid", 1111)
        rc, out = go("--phase", "hub", "--mock", mem_p, "--log", two)
        check(rc == 3 and "addresses are stale" in out, "two-process log, running pid != the latest start: refused as stale")
        mem.set_kv("fc27_pid", harness.MOCK_PID)
        st = os.path.join(d, "GAME_STATE.txt")
        open(st, "w").write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--phase", "hub"], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live while GAME_STATE is CLOSED: refused, exit 3")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
