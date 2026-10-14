"""x2_sentinels.py - coordinator-only guarded WRITE of the gate-X2 sentinel (decision 4.2): the club's active MAP record gets the
inconsistent pair LH 95 + DA 0 (Deep) + BU 1 (Counter); no other copy is touched. Same engine and safety as x3_sentinels.py (identity =
the tactic code, owner rule = career-load values 25/0/0 or this tool's last write, re-read before the write, read-back, rollback, undo,
kill switch TO\\sentinels_off.txt, one log line per write in TO\\sentinels.log).

  python x2_sentinels.py --allow-write        (in the hub, Tactics card opened once so Turbo logged the service)
  python x2_sentinels.py --dry-run | --undo --allow-write | --self-test
The DB half of X2 is NOT written here: teams.defensivedepth 77 and teams.buildupplay 0 go through Turbo's Tactics tab (UI), and the
kick-off readout needs TO\\call_team_sheet_on.txt. Then kick off and run x2_readout.py.
Exit 0 PASS / 1 FAIL / 3 INCONCLUSIVE (refused).
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import x3_sentinels  # noqa: E402

PLAN = {"career_load": {"line": 25, "buildup": 0, "approach": 0},
        "sentinels": [{"copy": "map", "line": 95, "buildup": 1, "approach": 0}]}
DB_SENTINELS = {"teams.defensivedepth": 77, "teams.buildupplay": 0}  # written by the coordinator through Turbo's Tactics tab


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--self-test" in argv:
        return self_test()
    if "--plan" in argv or "--copies" in argv:
        print("x2_sentinels: the X2 plan is fixed (map record LH 95 / BU 1 / DA 0); --plan / --copies are not accepted")
        return 3
    rc = x3_sentinels.main(argv, plan_obj=PLAN, tool="x2_sentinels", state_name="x2_sentinels_state.json")
    if rc == 0 and "--undo" not in argv and "--dry-run" not in argv:
        print("next (coordinator): Turbo Tactics tab -> teams.defensivedepth %d, teams.buildupplay %d (read back), "
              "touch TO\\call_team_sheet_on.txt, kick off, then x2_readout.py" % (DB_SENTINELS["teams.defensivedepth"],
                                                                                 DB_SENTINELS["teams.buildupplay"]))
    return rc


def self_test():
    import contextlib, io, subprocess, tempfile
    import harness
    import sheet_copies as sc
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        log, mem_p = os.path.join(d, "turbo_gui.log"), os.path.join(d, "mem.json")
        mem, svc = sc.mock_world(mem_p, log)
        common = ["--mock", mem_p, "--log", log, "--state", os.path.join(d, "st.json"), "--wlog", os.path.join(d, "w.log"),
                  "--off", os.path.join(d, "off.txt")]

        def go(*args):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = main(list(args) + common)
            return rc, buf.getvalue()

        def copies():
            return sc.resolve(sc.mock_reader(mem_p), svc, sc.MOCK_TEAM, say=lambda *x: None)["copies"]

        rc, out = go("--allow-write")
        c = copies()
        check(rc == 0 and x3_sentinels.vals(sc.target(c, "map")[1][1]) == [95, 1, 0] and "x2_sentinels: map" in out,
              "map record: LH 95 / BU 1 / DA 0 written and read back (exit %d)" % rc)
        check(all(x3_sentinels.vals(sc.target(c, cp)[1][1]) == [25, 0, 0] for cp in ("A0", "B0", "C0")), "no other copy touched")
        check("teams.defensivedepth 77" in out, "the DB half (Turbo UI) and the kick-off opt-in are named")
        rc, out = go("--copies", "A0", "--allow-write")
        check(rc == 3, "--copies is refused (the X2 plan is fixed)")
        rc, out = go("--undo", "--allow-write")
        check(rc == 0 and x3_sentinels.vals(sc.target(copies(), "map")[1][1]) == [25, 0, 0], "undo: the map record is back at 25/0/0")
        st = os.path.join(d, "GAME_STATE.txt")
        open(st, "w").write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--allow-write"], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live while GAME_STATE is CLOSED: refused, exit 3")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
