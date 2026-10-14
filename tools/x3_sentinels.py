"""x3_sentinels.py - coordinator-only guarded WRITE of the gate-X3 sentinels (decision 4.3: one distinct valid Line Height per candidate
team-sheet copy in the hub) through the dev service; also the engine of x2_sentinels.py (gate X2, map record only).

  python x3_sentinels.py --allow-write [--copies map,B0,A0,C0,X0,Y0] [--plan x3_plan.json] [--svc 0x..] [--team N]
  python x3_sentinels.py --dry-run          read-only: resolve every copy, print what would be written (and what is refused)
  python x3_sentinels.py --undo --allow-write   restore this tool's last writes (career-load values) - only where they still hold
  --mock MEMFILE / TURBO_MOCK_MEM: offline over sheet_copies.mock_world; --self-test.
Per target record (B.4 for a dev-service write):
  1. GAME_STATE live, --allow-write, kill switch TO\\sentinels_off.txt absent (re-read before every record).
  2. resolve the service (Turbo's log line, header checks: VT pair in the image, loaded flag, bucket / node counts) and every copy.
  3. identity: the record's code at +0x68 == the code of the club's active MAP record (the tactic); a copy without it is skipped.
  4. owner rule: LH / BU / DA == the plan's career-load values or this tool's last write to that address; else refused.
  5. re-read right before the write (code and values unchanged), write +0xA4 (LH) and +0xB0 (BU, DA), read back the record;
     any mismatch -> every record written in this run is rolled back (and read back), FAIL.
  6. one log line per record in TO\\sentinels.log and on stdout: "x3_sentinels: B0 0x... LH 25->85 BU 0->1 DA 0->1 (read back ok)";
     the snapshot goes to the state file (the undo source; it dies with the game process anyway: LTEST is never saved).
Exit 0 PASS (every planned copy that exists was written and read back), 1 FAIL (read-back / rollback), 3 INCONCLUSIVE (refused,
nothing written for a refused copy, or no copy to write).
"""
import argparse, json, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import sheet_copies as sc  # noqa: E402


class Refused(Exception):
    pass


class Failed(Exception):
    pass


class Dev:
    def __init__(self, call, budget):
        self.call, self.budget = call, budget

    def read(self, pairs):
        if not pairs:
            return []
        r = self.call({"op": "multi", "ops": [{"op": "bytes", "addr": sc.H(a), "len": n} for a, n in pairs]}, timeout=min(20, self.budget))
        if r.get("error"):
            raise Refused("dev service: %s" % r["error"])
        return [bytes.fromhex(x["hex"]) if x.get("ok") and "hex" in x else None for x in r.get("results", [])]

    def write(self, addr, data):
        r = self.call({"op": "write", "addr": sc.H(addr), "hex": data.hex(" ")}, timeout=20, allow_write=True)
        if r.get("error") or not r.get("ok"):
            raise Failed("write at %s failed: %s" % (sc.H(addr), r.get("error") or r))


def vals(f):
    return [f["line"], f["buildup"], f["approach"]]


def put(dev, addr, lh, bu, da):
    dev.write(addr + sc.F_LH, struct.pack("<i", lh))
    dev.write(addr + sc.F_BU, struct.pack("<ii", bu, da))


def main(argv=None, plan_obj=None, tool="x3_sentinels", state_name="x3_sentinels_state.json"):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", default=os.path.join(HERE, "x3_plan.json"))
    ap.add_argument("--copies", default=None, help="comma list of copies to write (default: every copy of the plan)")
    ap.add_argument("--svc", default=None)
    ap.add_argument("--team", type=int, default=None)
    ap.add_argument("--log", default=sc.LOG)
    ap.add_argument("--state", default=os.path.join(harness.TO, state_name))
    ap.add_argument("--wlog", default=os.path.join(harness.TO, "sentinels.log"))
    ap.add_argument("--off", default=os.path.join(harness.TO, "sentinels_off.txt"), help="kill switch file")
    ap.add_argument("--allow-write", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--undo", action="store_true")
    ap.add_argument("--budget", type=float, default=60.0)
    ap.add_argument("--mock", nargs="?", const=os.environ.get("TURBO_MOCK_MEM"), default=None, metavar="MEMFILE")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    harness.require_live(a.mock is not None, tool=tool)
    box = harness.timebox(a.budget + 15, tool)
    try:
        if a.mock is not None and not a.mock:
            raise Refused("--mock needs a MEMFILE (or TURBO_MOCK_MEM)")
        if a.mock:
            dev = Dev(harness.mock_call(a.mock), a.budget)
        else:
            import dev2
            dev = Dev(dev2.call, a.budget)
        if not a.dry_run and not a.allow_write:
            raise Refused("a write needs --allow-write (coordinator only); --dry-run is read-only")
        if a.undo:
            return undo(a, dev, tool)
        plan = plan_obj or json.load(open(a.plan, encoding="utf-8"))
        return write(a, dev, plan, tool)
    except Refused as e:
        return harness.finish("INCONCLUSIVE", ["refused: %s" % e], tool=tool)
    except Failed as e:
        return harness.finish("FAIL", [str(e)], tool=tool)
    except sc.Unusable as e:
        return harness.finish("INCONCLUSIVE", [str(e)], tool=tool)
    finally:
        box.cancel()


def locate(a, dev):
    lsvc, lteam = sc.from_log(a.log)
    svc = int(a.svc, 16) if a.svc else lsvc
    team = a.team if a.team is not None else lteam
    if not svc or team is None:
        raise Refused("no service address / club: open Turbo's Tactics card once (log 'channel E: service located ... at 0x...' and "
                      "'channel E: team N record'), or pass --svc / --team")
    R = sc.resolve(dev.read, svc, team)
    if not R["map_code"]:
        raise Refused("the club's active map record holds no tactic code: nothing to anchor the identity check on")
    return svc, team, R


def write(a, dev, plan, tool):
    svc, team, R = locate(a, dev)
    code = R["map_code"]
    load = plan["career_load"]
    load_v = [load["line"], load["buildup"], load["approach"]]
    try:
        prev = json.load(open(a.state, encoding="utf-8"))
    except (OSError, ValueError):
        prev = {}
    mine = {e["addr"]: e["new"] for e in prev.get("entries", []) if prev.get("svc") == sc.H(svc)}
    first = {e["addr"]: e["old"] for e in prev.get("entries", []) if prev.get("svc") == sc.H(svc)}  # the values before this tool
    want = set(a.copies.split(",")) if a.copies else None
    todo, notes = [], []
    for s in plan["sentinels"]:
        cp = s["copy"]
        if cp.startswith("db.") or (want and cp not in want):
            continue
        new = [s["line"], s["buildup"], s["approach"]]
        if not (1 <= new[0] <= 100 and 0 <= new[1] <= 1 and 0 <= new[2] <= 2):
            raise Refused("%s: %s is outside the values Turbo may write (LH 1..100, BU 0..1, DA 0..2)" % (cp, new))
        hit = sc.target(R["copies"], cp)
        if hit is None:
            notes.append("%s: absent / holds no tactic in the hub -> not written" % cp)
            continue
        lab, (addr, f) = hit
        if f["code"] != code:
            notes.append("%s (%s): its code is not the tactic's -> not written" % (cp, lab))
            continue
        cur = vals(f)
        if cur != load_v and cur != mine.get(sc.H(addr)):
            raise Refused("%s (%s at %s) holds %s: neither the career-load %s nor this tool's last write -> someone else owns it" % (
                cp, lab, sc.H(addr), cur, load_v))
        # undo source: the values before this tool ever wrote there (a re-run over its own write keeps the first snapshot)
        orig = first.get(sc.H(addr), cur) if cur == mine.get(sc.H(addr)) else cur
        todo.append({"copy": cp, "label": lab, "addr": addr, "old": cur, "new": new, "orig": orig})
    for n in notes:
        print("  " + n)
    for t in todo:
        print("  plan: %-4s %-12s %s  LH %d->%d BU %d->%d DA %d->%d" % (t["copy"], t["label"], sc.H(t["addr"]), t["old"][0], t["new"][0],
                                                                    t["old"][1], t["new"][1], t["old"][2], t["new"][2]))
    if not todo:
        return harness.finish("INCONCLUSIVE", ["no copy to write"], tool=tool)
    if a.dry_run:
        return harness.finish("PASS", ["dry run: nothing written"], tool=tool)
    done = []
    try:
        for t in todo:
            if os.path.exists(a.off):
                raise Refused("kill switch %s exists" % a.off)
            b = dev.read([(t["addr"], sc.REC)])[0]
            f = sc.rec_fields(b) if b else None
            if not f or f["code"] != code or vals(f) != t["old"]:
                raise Failed("%s changed between the resolve and the write (identity / values): stopped" % t["copy"])
            put(dev, t["addr"], *t["new"])
            done.append(t)
            b = dev.read([(t["addr"], sc.REC)])[0]
            f = sc.rec_fields(b) if b else None
            if not f or f["code"] != code or vals(f) != t["new"]:
                raise Failed("%s read back %s instead of %s" % (t["copy"], vals(f) if f else "nothing", t["new"]))
            line = "%s: %s %s LH %d->%d BU %d->%d DA %d->%d (read back ok)" % (tool, t["copy"], sc.H(t["addr"]), t["old"][0], t["new"][0],
                                                                             t["old"][1], t["new"][1], t["old"][2], t["new"][2])
            print(line)
            harness.append_log(a.wlog, line)
    except (Failed, Refused) as e:
        for t in reversed(done):
            put(dev, t["addr"], *t["old"])
            b = dev.read([(t["addr"], sc.REC)])[0]
            ok = b is not None and vals(sc.rec_fields(b)) == t["old"]
            line = "%s: %s %s ROLLED BACK to LH %d BU %d DA %d (%s)" % (tool, t["copy"], sc.H(t["addr"]), t["old"][0], t["old"][1], t["old"][2],
                                                                    "read back ok" if ok else "READ-BACK MISMATCH")
            print(line)
            harness.append_log(a.wlog, line)
        raise
    # the undo source (only after a complete run; a rolled-back run leaves the previous state valid)
    json.dump({"tool": tool, "t": time.strftime("%H:%M:%S"), "svc": sc.H(svc), "team": team, "code": code,
               "entries": [{"copy": t["copy"], "addr": sc.H(t["addr"]), "old": t["orig"], "new": t["new"]} for t in done]},
              open(a.state, "w", encoding="utf-8"), indent=1)
    return harness.finish("PASS", ["%d copies written and read back; undo: %s --undo --allow-write" % (len(done), tool)], tool=tool)


def undo(a, dev, tool):
    try:
        st = json.load(open(a.state, encoding="utf-8"))
    except (OSError, ValueError):
        raise Refused("no state file %s: nothing to undo" % a.state)
    restored, skipped = 0, []
    for e in st["entries"]:
        addr = int(e["addr"], 16)
        b = dev.read([(addr, sc.REC)])[0]
        f = sc.rec_fields(b) if b else None
        if not f or f["code"] != st["code"] or vals(f) != e["new"]:
            skipped.append("%s %s: no longer holds this tool's write (%s): left alone" % (e["copy"], e["addr"], vals(f) if f else "unreadable"))
            continue
        put(dev, addr, *e["old"])
        b = dev.read([(addr, sc.REC)])[0]
        if b is None or vals(sc.rec_fields(b)) != e["old"]:
            raise Failed("undo of %s did not read back" % e["copy"])
        line = "%s: %s %s undo -> LH %d BU %d DA %d (read back ok)" % (tool, e["copy"], e["addr"], *e["old"])
        print(line)
        harness.append_log(a.wlog, line)
        restored += 1
    os.replace(a.state, a.state + ".undone")
    return harness.finish("PASS" if not skipped else "INCONCLUSIVE", ["%d restored" % restored] + skipped, tool=tool)


def self_test():
    import contextlib, io, subprocess, tempfile
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

        rc, out = go()
        check(rc == 3 and "--allow-write" in out, "no --allow-write: refused")
        rc, out = go("--dry-run")
        check(rc == 0 and out.count("plan:") == 4 and "X0: absent" in out, "dry run: 4 copies planned (map, B0, A0, C0), X0/Y0 absent, nothing written")
        check(harness.MockMem(mem_p)._read_json()["writes"] == 0, "dry run wrote nothing")
        rc, out = go("--allow-write")
        c = copies()
        got = {cp: vals(sc.target(c, cp)[1][1]) for cp in ("map", "B0", "A0", "C0")}
        check(rc == 0 and got == {"map": [95, 1, 0], "B0": [85, 1, 1], "A0": [75, 1, 0], "C0": [65, 1, 1]} and out.count("(read back ok)") == 4,
              "write: map 95/1/0, B0 85/1/1, A0 75/1/0, C0 65/1/1, each read back (exit %d)" % rc)
        rc2, out2 = subprocess.run([sys.executable, os.path.join(HERE, "x3_readback.py"), "--phase", "hub", "--mock", mem_p, "--log", log],
                                   capture_output=True, text=True, timeout=60).returncode, ""
        check(rc2 == 0, "x3_readback --phase hub confirms every sentinel (exit %d)" % rc2)
        rc, out = go("--allow-write")
        check(rc == 0, "a second run over its own values is allowed (owner rule: this tool's last write)")
        rc, out = go("--undo", "--allow-write")
        c = copies()
        check(rc == 0 and all(vals(sc.target(c, cp)[1][1]) == [25, 0, 0] for cp in ("map", "B0", "A0", "C0")), "undo restores 25/0/0 everywhere")
        c0 = sc.target(copies(), "C0")[1][0]
        os.environ["MOCK_WRITE_FAIL"] = sc.H(c0 + sc.F_LH)  # the 4th record's write fails
        try:
            rc, out = go("--allow-write")
        finally:
            del os.environ["MOCK_WRITE_FAIL"]
        c = copies()
        check(rc == 1 and out.count("ROLLED BACK") == 3 and out.count("(read back ok)") == 6
              and all(vals(sc.target(c, cp)[1][1]) == [25, 0, 0] for cp in ("map", "B0", "A0", "C0")),
              "a failing write on the 4th copy: the 3 written copies are rolled back and read back, FAIL (exit %d)" % rc)
        a0 = sc.target(copies(), "A0")[1][0]
        mem.write(a0 + sc.F_LH, struct.pack("<i", 50))  # someone else's value in A0
        rc, out = go("--allow-write")
        check(rc == 3 and "someone else owns it" in out and harness.MockMem(mem_p).read(a0 + sc.F_LH, 4) == struct.pack("<i", 50),
              "owner rule: a foreign value in A0 refuses the whole run before any write")
        mem.write(a0 + sc.F_LH, struct.pack("<i", 25))
        open(os.path.join(d, "off.txt"), "w").close()
        rc, out = go("--allow-write")
        check(rc == 3 and "kill switch" in out and vals(sc.target(copies(), "map")[1][1]) == [25, 0, 0], "kill switch: refused, nothing written")
        os.remove(os.path.join(d, "off.txt"))
        b0 = sc.target(copies(), "B0")[1][0]
        mem.write(b0 + sc.F_CODE, b"OTHERCODE001")
        rc, out = go("--allow-write", "--copies", "B0")
        check(rc == 3 and "not the tactic's" in out, "a copy whose code is not the tactic's is never written")
        bad = {"career_load": {"line": 25, "buildup": 0, "approach": 0}, "sentinels": [{"copy": "map", "line": 95, "buildup": 3, "approach": 0}]}
        json.dump(bad, open(os.path.join(d, "bad.json"), "w"))
        rc, out = go("--allow-write", "--plan", os.path.join(d, "bad.json"))
        check(rc == 3 and "outside the values" in out, "a plan value Turbo may not write (BU 3) is refused")
        st = os.path.join(d, "GAME_STATE.txt")
        open(st, "w").write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--allow-write"], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live while GAME_STATE is CLOSED: refused, exit 3")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
