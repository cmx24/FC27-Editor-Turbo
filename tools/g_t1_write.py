"""g_t1_write.py - coordinator-only guarded writes of the two gate G-T1 knobs (PLAN_tactics_settings.md section 2) through the dev service.
Built on x1_write.py (same identity chain, owner rule, read-back and rollback per write); a SEPARATE state, write log and kill switch.

usage
  python g_t1_write.py check [--expect-vanilla]               read-only: identity + both values (FAIL if --expect-vanilla and not 1.0 / 0.84)
  python g_t1_write.py set --allow-write [--length 0.4] [--width 0.5] [--marker F --label B [--transition x]]
  python g_t1_write.py revert --allow-write [--marker F --label A [--transition x]]      both knobs back to FC 27's vanilla values
  python g_t1_write.py mock-world DIR                          (tests) build the offline mock world
  python g_t1_write.py schedule --allow-write --win 60 --run-dir DIR     B (extremes already written in the hub) W s | restore | A W s
options: --inst HEX (else the last tuning-tab locator line of --log) --log --state-base --wlog --off --budget S --mock --mock-mem PATH

Knobs (both live in the SAME attackinglinelengthwidth instance, f32): length_in = LineLengthModifierForBalanced (+0x30, vanilla 0.84),
width_in = lineWidthModifier (+0x28, vanilla 1.0). Gate values default to 0.4 / 0.5. The accepted ranges are the GATE's own (length
0.3..1.5, width 0.5..1.7: 0.5 is the engine floor), wider than the slider ranges of the inventory (length 0.5..1.5, width 0.6..1.7).
Each knob writes through x1_write.write_job: build check, identity (+0 image pointer, +8 TypeInfo, +0x18 Name), owner rule (current value
= vanilla or this tool's last write), kill switch re-read, read-back, rollback. A failure on the second knob rolls the first one back.
Exit: 0 PASS, 1 FAIL, 3 INCONCLUSIVE (refused / timeout / no dev answer).
"""
import argparse, copy, json, os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402
import x1_write as xw  # noqa: E402

TOOL = "g_t1_write"
GATE_VALUES = {"length": 0.4, "width": 0.5}


def _knobs():
    base = harness.KNOBS["width_in"]
    k_w = dict(base, label="gp_positioningformation_attackinglinelengthwidth.lineWidthModifier", off=0x28, vanilla=1.0, range=(0.5, 1.7))
    k_l = dict(base, label="gp_positioningformation_attackinglinelengthwidth.LineLengthModifierForBalanced", off=0x30, vanilla=0.84,
               range=(0.3, 1.5))
    return {"gt1_length": k_l, "gt1_width": k_w}


KNOBS = _knobs()
harness.KNOBS.update(KNOBS)  # x1_write.identity / write_job look the knob up by name in harness.KNOBS
ORDER = ("gt1_length", "gt1_width")
SHORT = {"gt1_length": "length", "gt1_width": "width"}


def _for_knob(a, knob):
    b = copy.copy(a)
    b.knob = knob
    b.state = "%s.%s.json" % (a.state_base, SHORT[knob])
    return b


def read_both(dev, build, inst, base, size):
    out, bad = {}, []
    for kn in ORDER:
        v, b = xw.identity(dev, kn, build, inst, base, size)
        out[kn] = v
        bad += b
    return out, bad


def set_both(a, dev, build, base, size, inst, targets):
    """write the knobs one by one (length first); a failure rolls back the ones already written. targets: {knob: value}"""
    done = []
    try:
        for kn in ORDER:
            xw.write_job(_for_knob(a, kn), dev, KNOBS[kn], build, base, size, inst, targets[kn])
            done.append(kn)
    except (xw.Failed, xw.Refused):
        for kn in reversed(done):
            try:
                xw.write_job(_for_knob(a, kn), dev, KNOBS[kn], build, base, size, inst, KNOBS[kn]["vanilla"])
            except (xw.Failed, xw.Refused) as e2:
                print("ROLLBACK of %s FAILED: %s" % (kn, e2))
        raise


def revert_both(a, dev, build, base, size, inst):
    first = None
    for kn in ORDER:  # always try both
        try:
            xw.write_job(_for_knob(a, kn), dev, KNOBS[kn], build, base, size, inst, KNOBS[kn]["vanilla"])
        except (xw.Failed, xw.Refused) as e:
            first = first or e
    if first:
        raise first


def mock_world(dirpath):
    """offline world for the wrapper test: the X1 mock world plus the length knob's vanilla value (+0x30)"""
    os.makedirs(dirpath, exist_ok=True)
    mem, inst = harness.build_x1_world(os.path.join(dirpath, "mem.json"), os.path.join(dirpath, "turbo_gui.log"))
    mem.write(inst + 0x30, struct.pack("<f", KNOBS["gt1_length"]["vanilla"]))
    mem.set_kv("x1_inst", "0x%X" % inst)
    print("mock world: %s instance 0x%X" % (os.path.join(dirpath, "mem.json"), inst))
    return 0


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv[:1] == ["mock-world"] and len(argv) == 2:
        return mock_world(argv[1])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["check", "set", "revert", "schedule"])
    ap.add_argument("--length", type=float, default=GATE_VALUES["length"])
    ap.add_argument("--width", type=float, default=GATE_VALUES["width"])
    ap.add_argument("--expect-vanilla", action="store_true")
    ap.add_argument("--inst", default=None)
    ap.add_argument("--log", default=os.path.join(harness.TO, "turbo_gui.log"))
    ap.add_argument("--state-base", default=os.path.join(harness.TO, "g_t1_write_state"))
    ap.add_argument("--wlog", default=os.path.join(harness.TO, "g_t1_write.log"))
    ap.add_argument("--off", default=os.path.join(harness.TO, "g_t1_write_off.txt"))
    ap.add_argument("--allow-write", action="store_true")
    ap.add_argument("--marker", default=None)
    ap.add_argument("--label", default=None)
    ap.add_argument("--transition", default=None)
    ap.add_argument("--budget", type=float, default=None)
    ap.add_argument("--win", type=float, default=60.0)
    ap.add_argument("--guard", type=float, default=5.0)
    ap.add_argument("--run-dir", default=None)
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--mock-mem", default=os.environ.get("TURBO_MOCK_MEM"))
    a = ap.parse_args(argv)
    harness.require_live(a.mock, tool=TOOL, write=a.cmd != "check")
    if a.budget is None:
        a.budget = 2 * a.win + 150 if a.cmd == "schedule" else 30.0
    if a.cmd == "schedule" and not a.run_dir:
        a.run_dir = os.path.join(harness.TO, "g_t1", time.strftime("%Y%m%d-%H%M%S") + "_run")
    box = harness.timebox(a.budget + 30, TOOL)
    try:
        if a.mock:
            if not a.mock_mem:
                raise xw.Refused("--mock needs --mock-mem (or TURBO_MOCK_MEM)")
            dev = xw.Dev(harness.mock_call(a.mock_mem))
        else:
            import dev2
            dev = xw.Dev(dev2.call)
        targets = {"gt1_length": a.length, "gt1_width": a.width}
        if a.cmd in ("set", "schedule"):
            for kn in ORDER:
                lo, hi = KNOBS[kn]["range"]
                if not (lo <= targets[kn] <= hi):
                    raise xw.Refused("%s %.3f is outside the gate range %.1f..%.1f" % (SHORT[kn], targets[kn], lo, hi))
        if a.cmd != "check":
            if not a.allow_write:
                raise xw.Refused("a write needs --allow-write (coordinator only)")
            if os.path.exists(a.off):
                raise xw.Refused("kill switch %s exists" % a.off)
        build, base, size = dev.build()
        if build not in KNOBS["gt1_width"]["typeinfo"]:
            raise xw.Refused("build %s: no TypeInfo constant (unknown build is refused)" % build)
        mm_ = a.mock_mem if a.mock else None
        if a.inst:
            inst = int(a.inst, 16)
            pids = harness.running_fc27_pids(mm_)
            if len(pids) != 1:
                raise xw.Refused("--inst needs exactly one running FC27.exe (found %s)" % (sorted(pids) or "none"))
            a.pid = pids.pop()
        else:
            inst, a.pid = xw.instance_from_log(a.log, harness.KNOBS["width_in"]["log_re"], mm_)
        vals, bad = read_both(dev, build, inst, base, size)
        if bad:
            raise xw.Failed("identity check failed at 0x%X: %s" % (inst, "; ".join(bad)))
        print("identity OK (build %s, 0x%X): length_in %.6f width_in %.6f" % (build, inst, vals["gt1_length"], vals["gt1_width"]))
        if a.cmd == "check":
            if a.expect_vanilla:
                off = [SHORT[k] for k in ORDER if abs(vals[k] - KNOBS[k]["vanilla"]) > 1e-5]
                if off:
                    raise xw.Failed("not at FC 27's vanilla: %s" % ", ".join("%s %.6f" % (n, vals["gt1_" + n]) for n in off))
            return harness.finish("PASS", tool=TOOL)
        if a.cmd == "schedule":
            for kn in ORDER:
                if abs(vals[kn] - targets[kn]) > 1e-5:
                    raise xw.Refused("B precondition: %s is %.6f, not %.6f (the hub write did not reach the match / was not made)" % (
                        SHORT[kn], vals[kn], targets[kn]))
            return schedule(a, dev, build, base, size, inst, targets)
        if a.marker and a.transition:
            harness.write_marker(a.marker, a.transition)
        if a.cmd == "set":
            set_both(a, dev, build, base, size, inst, targets)
        else:
            revert_both(a, dev, build, base, size, inst)
        if a.marker and a.label:
            harness.write_marker(a.marker, a.label)
            print("marker %s = %s" % (a.marker, a.label))
        return harness.finish("PASS", tool=TOOL)
    except xw.Refused as e:
        return harness.finish("INCONCLUSIVE", ["refused: %s" % e], tool=TOOL)
    except xw.Failed as e:
        return harness.finish("FAIL", [str(e)], tool=TOOL)
    finally:
        box.cancel()


def schedule(a, dev, build, base, size, inst, targets):
    """gate G-T1 in ONE process (the live-proven x1_write.schedule pattern): match_measure as a module, its marker reader replaced by a
    time schedule from the first sample - B [0,W) with the extremes | restore | A [W,2W) with the vanilla values; samples within --guard
    s after the restore are 'x'. One mid-match write (both knobs). Ends with FC 27's values."""
    import match_measure as mm
    W, guard = a.win, a.guard
    st = {"t0": None, "idx": 0, "last": -1e9, "writes": 0, "stopped": False}
    stop = os.path.normpath(a.run_dir) + ".stop"
    if os.path.exists(stop):
        os.remove(stop)
    van = {k: KNOBS[k]["vanilla"] for k in ORDER}

    def label(path=None, default=None):
        now = mm._now()
        if st["t0"] is None:
            st["t0"] = now
            print("schedule start %s: windows of %.0f s (B length %.3f width %.3f | A vanilla), guard %.0f s" % (
                time.strftime("%H:%M:%S"), W, targets["gt1_length"], targets["gt1_width"], guard))
            sys.stdout.flush()
        el = now - st["t0"]
        if st["idx"] < 1 and el >= W:
            st["idx"] = 1
            print("%s -> A at %.0f s: restore the vanilla values" % (time.strftime("%H:%M:%S"), el))
            set_both(a, dev, build, base, size, inst, van)
            st["last"], st["writes"] = mm._now(), st["writes"] + 1
        if el >= 2 * W:
            if not st["stopped"]:
                open(stop, "w").close()
                st["stopped"] = True
            return "end"
        if st["idx"] == 0:
            return "B"
        return "x" if mm._now() - st["last"] < guard else "A"

    mm.read_marker = label
    argv = ["--seconds", str(int(2 * W + 60)), "--label", "g_t1", "--marker", "schedule", "--stop", stop,
            "--run-dir", a.run_dir, "--budget", str(a.budget)]
    if a.mock:
        argv += ["--mock", "--mock-mem", a.mock_mem]
    rc = None
    try:
        rc = mm.main(argv)
    finally:
        if os.path.exists(stop):
            os.remove(stop)
        cur, _bad = read_both(dev, build, inst, base, size)
        if any(v is not None and abs(v - van[k]) > 1e-6 for k, v in cur.items()):
            print("final revert to FC 27's values")
            revert_both(a, dev, build, base, size, inst)
    if rc != 0 or st["writes"] != 1:
        raise xw.Failed("schedule incomplete: sampler exit %s, %d of 1 mid-match restore" % (rc, st["writes"]))
    print("schedule complete: 1 mid-match restore, run %s" % a.run_dir)
    return harness.finish("PASS", tool=TOOL)


# ------------------------------------------------------------------------------------------------------------------- self-test
def self_test():
    import contextlib, io, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    def run(*args):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(list(args))
        return rc, buf.getvalue()

    def f32(mem, addr):
        return struct.unpack("<f", mem.read(addr, 4))[0]

    with tempfile.TemporaryDirectory() as d:
        mem_p, log_p = os.path.join(d, "mem.json"), os.path.join(d, "turbo_gui.log")
        common = ["--mock", "--mock-mem", mem_p, "--log", log_p, "--state-base", os.path.join(d, "st"),
                  "--wlog", os.path.join(d, "w.log"), "--off", os.path.join(d, "off.txt")]
        mem, inst = harness.build_x1_world(mem_p, log_p)
        mem.write(inst + 0x30, struct.pack("<f", 0.84))  # the length knob's vanilla value (the shared mock world only seeds width)
        marker = os.path.join(d, "win.txt")
        rc, out = run("check", "--expect-vanilla", *common)
        check(rc == 0 and "length_in 0.840000 width_in 1.000000" in out, "check: identity OK, 0.84 / 1.0 (exit %d)" % rc)
        rc, out = run("set", *common)
        check(rc == 3 and "--allow-write" in out, "set without --allow-write: refused, exit 3")
        rc, out = run("set", "--allow-write", "--length", "0.2", *common)
        check(rc == 3 and "gate range" in out and abs(f32(mem, inst + 0x30) - 0.84) < 1e-6, "set length 0.2: outside the gate range, nothing written")
        rc, out = run("set", "--allow-write", "--width", "0.4", *common)
        check(rc == 3 and "gate range" in out, "set width 0.4 (below the engine floor 0.5): refused")
        rc, out = run("set", "--allow-write", "--marker", marker, "--label", "B", "--transition", "x", *common)
        check(rc == 0 and abs(f32(mem, inst + 0x30) - 0.4) < 1e-6 and abs(f32(mem, inst + 0x28) - 0.5) < 1e-6,
              "set: length 0.4 and width 0.5 written (exit %d)" % rc)
        check(open(marker).read().split() == ["B"], "marker = B after the writes")
        wl = open(os.path.join(d, "w.log")).read()
        check(wl.count("(read back ok)") == 2 and "gt1_length" in wl and "gt1_width" in wl, "write log: two read-back lines, one per knob")
        rc, out = run("check", "--expect-vanilla", *common)
        check(rc == 1 and "not at FC 27's vanilla" in out, "check --expect-vanilla while extremes are in: FAIL")
        rc, out = run("revert", "--allow-write", "--marker", marker, "--label", "A", *common)
        check(rc == 0 and abs(f32(mem, inst + 0x30) - 0.84) < 1e-6 and abs(f32(mem, inst + 0x28) - 1.0) < 1e-6 and open(marker).read().split() == ["A"],
              "revert: both back to vanilla, marker A")
        mem.write(inst + 0x30, struct.pack("<f", 1.1))  # someone else's value on the length knob
        rc, out = run("set", "--allow-write", *common)
        check(rc == 3 and "someone else owns it" in out and abs(f32(mem, inst + 0x28) - 1.0) < 1e-6,
              "owner rule: a foreign length value refuses, the width (not yet written) stays 1.0")
        mem.write(inst + 0x30, struct.pack("<f", 0.84))
        mem.write(inst + 0x28, struct.pack("<f", 1.3))  # a foreign value on the SECOND knob: the first write must roll back
        rc, out = run("set", "--allow-write", *common)
        check(rc == 3 and abs(f32(mem, inst + 0x30) - 0.84) < 1e-6 and abs(f32(mem, inst + 0x28) - 1.3) < 1e-6,
              "second knob refused: the length write is rolled back to 0.84, the foreign width is kept (exit %d)" % rc)
        mem.write(inst + 0x28, struct.pack("<f", 1.0))
        open(os.path.join(d, "off.txt"), "w").close()
        rc, out = run("set", "--allow-write", *common)
        check(rc == 3 and "kill switch" in out, "kill switch file: refused")
        os.remove(os.path.join(d, "off.txt"))
        mem2_p = os.path.join(d, "mem2.json")
        harness.build_x1_world(mem2_p, None, wrong_typeinfo=True)
        rc, out = run("set", "--allow-write", "--inst", "0x%X" % inst, *[c if c != mem_p else mem2_p for c in common])
        check(rc == 1 and "TypeInfo" in out, "wrong TypeInfo: identity FAIL, no write")
        # the separate state: x1's files are never used
        check(not any(f.startswith("x1_") for f in os.listdir(d)) and os.path.exists(os.path.join(d, "st.length.json")),
              "own state files (st.length.json / st.width.json), none of x1's")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:2] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(main())
