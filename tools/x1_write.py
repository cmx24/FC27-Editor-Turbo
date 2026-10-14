"""x1_write.py - coordinator-only guarded write of the X1 knob (gate X1, docs/TACTICS_OVERHAUL_DECISION.md 4.1) through the dev service.

usage
  python x1_write.py check [--expect V]                      read-only: identity + current value (FAIL if --expect differs)
  python x1_write.py set V --allow-write [--marker F --label B1 [--transition x]]
  python x1_write.py revert --allow-write [--marker F --label A1 [--transition x]]   (= set FC 27's vanilla value)
options: --knob width_in (default; the only X1 knob) --inst HEX (else the last tuning-tab locator line of TO\\turbo_gui.log)
         --log PATH --state PATH --budget S (time box, default 30) --mock --mock-mem PATH (offline: harness.MockMem)

Knob width_in = gp_positioningformation_attackinglinelengthwidth.lineWidthModifier (f32 at instance +0x28, one shared instance for all
gameplay DBs; FC 27 vanilla 1.0; generic range 0.5..1.7). One "job" per write, in this order (brief B.4 for a dev-service write):
  1. GAME_STATE live (else refused), kill switch TO\\x1_write_off.txt absent (re-read right before the write), --allow-write given.
  2. build check: FC27.exe's PE TimeDateStamp names a build this knob has a TypeInfo constant for (else refused: unknowns are refused).
  3. identity: +0 vtable in the FC27.exe image, +8 TypeInfo == the build constant, +0x18 Name == the asset path.
  4. owner rule: the current value is FC 27's vanilla or this tool's last write to this instance (TO\\x1_write_state.json); else refused.
  5. snapshot (old value) -> optional marker = --transition (samples taken during the change are labelled and dropped) -> write ->
     ONE multi read-back of identity + value; mismatch -> roll back the old value, read back, FAIL.
  6. log line appended to TO\\x1_write.log and printed: "x1_write: width_in 0x... 1.000000 -> 0.600000 (read back ok)".
  7. marker = --label (the window the following samples belong to).
Exit: 0 PASS, 1 FAIL (identity / read-back / expect mismatch), 3 INCONCLUSIVE (refused, timeout, no dev answer).
"""
import argparse, json, os, re, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

TOOL = "x1_write"


class Refused(Exception):
    pass


class Failed(Exception):
    pass


class Dev:
    def __init__(self, call):
        self.call = call

    def multi(self, reads):
        r = self.call({"op": "multi", "ops": [{"op": "bytes", "addr": "0x%X" % a, "len": n} for a, n in reads]}, timeout=20)
        if r.get("error"):
            raise Refused("dev service: %s" % r["error"])
        out = []
        for x in r.get("results", []):
            out.append(bytes.fromhex(x["hex"]) if x.get("ok") and x.get("hex") is not None else None)
        return out

    def write(self, addr, data):
        r = self.call({"op": "write", "addr": "0x%X" % addr, "hex": data.hex(" ")}, timeout=20, allow_write=True)
        if r.get("error") or not r.get("ok"):
            raise Failed("write at 0x%X failed: %s" % (addr, r.get("error") or r))
        return r

    def build(self):
        r = self.call({"op": "modules"}, timeout=30)
        if r.get("error"):
            raise Refused("dev service: %s" % r["error"])
        for m in r.get("modules", []):
            if m["name"].lower() == "fc27.exe":
                base, size = int(str(m["base"]), 16), int(m["size"])
                hdr = self.multi([(base, 0x400)])[0]
                if hdr is None:
                    raise Refused("FC27.exe header unreadable")
                e = struct.unpack_from("<I", hdr, 0x3C)[0]
                return "%08X" % struct.unpack_from("<I", hdr, e + 8)[0], base, size
        raise Refused("FC27.exe not in the module list")


def instance_from_log(path, regex):
    hit = None
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.search(regex, line)
                if m:
                    hit = (line[:23], int(m.group(1), 16))
    except OSError as e:
        raise Refused("log %s unreadable: %s" % (path, e))
    if not hit:
        raise Refused("no tuning-tab locator line in %s: open Competitions > Gameplay tuning first (about 25 s locate)" % path)
    print("instance from the tuning tab's locator line of %s: 0x%X" % hit)
    return hit[1]


def identity(dev, knob, build, inst, base, size):
    """-> (value, reasons[]) ; reads +0..+0x58 and the Name string in two dev calls"""
    k = harness.KNOBS[knob]
    blk = dev.multi([(inst, 0x58)])[0]
    if blk is None:
        return None, ["instance 0x%X unreadable" % inst]
    vt, ti, namep = struct.unpack_from("<Q", blk, 0)[0], struct.unpack_from("<Q", blk, 8)[0], struct.unpack_from("<Q", blk, 0x18)[0]
    want = k["name"].encode() + b"\0"
    name = dev.multi([(namep, len(want))])[0] if 0x10000 <= namep < 0x7FFFFFFFFFFF else None
    bad = []
    if not (base <= vt < base + size):
        bad.append("+0 0x%X is not an FC27.exe image pointer" % vt)
    if ti != k["typeinfo"][build]:
        bad.append("+8 TypeInfo 0x%X != 0x%X (build %s)" % (ti, k["typeinfo"][build], build))
    if name != want:
        bad.append("+0x18 Name %r != %r" % ((name or b"")[:60], k["name"][:60]))
    val = struct.unpack_from(k["fmt"], blk, k["off"])[0]
    return val, bad


def load_state(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["check", "set", "revert"])
    ap.add_argument("value", nargs="?", type=float)
    ap.add_argument("--knob", default="width_in", choices=sorted(harness.KNOBS))
    ap.add_argument("--inst", default=None, help="instance address (hex); default: the tuning tab's last locator line")
    ap.add_argument("--log", default=os.path.join(harness.TO, "turbo_gui.log"))
    ap.add_argument("--state", default=os.path.join(harness.TO, "x1_write_state.json"))
    ap.add_argument("--wlog", default=os.path.join(harness.TO, "x1_write.log"), help="append-only write log")
    ap.add_argument("--off", default=os.path.join(harness.TO, "x1_write_off.txt"), help="kill switch file")
    ap.add_argument("--expect", type=float, default=None)
    ap.add_argument("--allow-write", action="store_true")
    ap.add_argument("--marker", default=None)
    ap.add_argument("--label", default=None)
    ap.add_argument("--transition", default=None, help="marker label while the value changes (e.g. x)")
    ap.add_argument("--budget", type=float, default=30.0)
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--mock-mem", default=os.environ.get("TURBO_MOCK_MEM"))
    a = ap.parse_args(argv)
    harness.require_live(a.mock, tool=TOOL)
    box = harness.timebox(a.budget, TOOL)
    k = harness.KNOBS[a.knob]
    try:
        if a.mock:
            if not a.mock_mem:
                raise Refused("--mock needs --mock-mem (or TURBO_MOCK_MEM)")
            dev = Dev(harness.mock_call(a.mock_mem))
        else:
            import dev2
            dev = Dev(dev2.call)
        if a.cmd != "check":
            if not a.allow_write:
                raise Refused("a write needs --allow-write (coordinator only)")
            target = k["vanilla"] if a.cmd == "revert" else a.value
            if target is None:
                raise Refused("set needs a value")
            lo, hi = k["range"]
            if not (lo <= target <= hi):
                raise Refused("%.3f is outside the generic range %.1f..%.1f" % (target, lo, hi))
            if os.path.exists(a.off):
                raise Refused("kill switch %s exists" % a.off)
        build, base, size = dev.build()
        if build not in k["typeinfo"]:
            raise Refused("build %s: no TypeInfo constant for %s (unknown build is refused)" % (build, a.knob))
        inst = int(a.inst, 16) if a.inst else instance_from_log(a.log, k["log_re"])
        val, bad = identity(dev, a.knob, build, inst, base, size)
        if bad:
            raise Failed("identity check failed at 0x%X: %s" % (inst, "; ".join(bad)))
        print("identity OK (build %s, 0x%X): %s = %.6f" % (build, inst, k["label"], val))
        if a.cmd == "check":
            if a.expect is not None and abs(val - a.expect) > 1e-5:
                raise Failed("value %.6f != expected %.6f" % (val, a.expect))
            return harness.finish("PASS", tool=TOOL)
        st = load_state(a.state)
        mine = st.get("value") if st.get("inst") == "0x%X" % inst and st.get("build") == build else None
        if abs(val - k["vanilla"]) > 1e-6 and (mine is None or abs(val - mine) > 1e-6):
            raise Refused("value %.6f is neither FC 27's %.1f nor this tool's last write (%s): someone else owns it" % (
                val, k["vanilla"], mine))
        if os.path.exists(a.off):  # re-read right before the write
            raise Refused("kill switch %s exists" % a.off)
        if a.marker and a.transition:
            harness.write_marker(a.marker, a.transition)
        data = struct.pack(k["fmt"], target)
        dev.write(inst + k["off"], data)
        back, bad = identity(dev, a.knob, build, inst, base, size)
        if bad or back is None or abs(back - target) > 1e-6:
            dev.write(inst + k["off"], struct.pack(k["fmt"], val))
            rb, _ = identity(dev, a.knob, build, inst, base, size)
            line = "x1_write: %s 0x%X %.6f -> %.6f READ-BACK MISMATCH (%s); rolled back to %.6f (read back %s)" % (
                a.knob, inst, val, target, "; ".join(bad) or "read %s" % back, val, rb)
            harness.append_log(a.wlog, line)
            raise Failed(line)
        line = "x1_write: %s 0x%X %.6f -> %.6f (read back ok)" % (a.knob, inst, val, target)
        print(line)
        harness.append_log(a.wlog, line)
        with open(a.state, "w", encoding="utf-8") as f:
            json.dump({"build": build, "inst": "0x%X" % inst, "value": target, "t": time.strftime("%H:%M:%S")}, f)
        if a.marker and a.label:
            harness.write_marker(a.marker, a.label)
            print("marker %s = %s" % (a.marker, a.label))
        return harness.finish("PASS", tool=TOOL)
    except Refused as e:
        return harness.finish("INCONCLUSIVE", ["refused: %s" % e], tool=TOOL)
    except Failed as e:
        return harness.finish("FAIL", [str(e)], tool=TOOL)
    finally:
        box.cancel()


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

    with tempfile.TemporaryDirectory() as d:
        mem_p, log_p = os.path.join(d, "mem.json"), os.path.join(d, "turbo_gui.log")
        common = ["--mock", "--mock-mem", mem_p, "--log", log_p, "--state", os.path.join(d, "st.json"),
                  "--wlog", os.path.join(d, "w.log"), "--off", os.path.join(d, "off.txt")]
        mem, inst = harness.build_x1_world(mem_p, log_p)
        marker = os.path.join(d, "win.txt")
        rc, out = run("check", "--expect", "1.0", *common)
        check(rc == 0 and "identity OK" in out, "check: identity OK, 1.0 (exit %d)" % rc)
        rc, out = run("set", "0.6", *common)
        check(rc == 3 and "--allow-write" in out, "set without --allow-write: refused, exit 3")
        rc, out = run("set", "0.3", "--allow-write", *common)
        check(rc == 3 and "generic range" in out, "set 0.3: outside the generic range, refused")
        rc, out = run("set", "0.6", "--allow-write", "--marker", marker, "--label", "B1", "--transition", "x", *common)
        v = struct.unpack("<f", mem.read(inst + 0x28, 4))[0]
        check(rc == 0 and abs(v - 0.6) < 1e-6 and "1.000000 -> 0.600000 (read back ok)" in out,
              "set 0.6: written, read back, log line (exit %d)" % rc)
        check(open(marker).read().split() == ["B1"], "marker = B1 after the write")
        check("(read back ok)" in open(os.path.join(d, "w.log")).read(), "write log has the read-back line")
        rc, out = run("check", "--expect", "1.0", *common)
        check(rc == 1, "check --expect 1.0 while 0.6: FAIL")
        rc, out = run("revert", "--allow-write", *common)
        v = struct.unpack("<f", mem.read(inst + 0x28, 4))[0]
        check(rc == 0 and abs(v - 1.0) < 1e-6, "revert: back to 1.0")
        mem.write(inst + 0x28, struct.pack("<f", 1.25))  # someone else's value
        rc, out = run("set", "0.6", "--allow-write", *common)
        v = struct.unpack("<f", mem.read(inst + 0x28, 4))[0]
        check(rc == 3 and "someone else owns it" in out and abs(v - 1.25) < 1e-6, "owner rule: foreign 1.25 kept, refused")
        mem.write(inst + 0x28, struct.pack("<f", 1.0))
        open(os.path.join(d, "off.txt"), "w").close()
        rc, out = run("set", "0.6", "--allow-write", *common)
        check(rc == 3 and "kill switch" in out, "kill switch file: refused")
        os.remove(os.path.join(d, "off.txt"))
        mem2_p = os.path.join(d, "mem2.json")
        harness.build_x1_world(mem2_p, None, wrong_typeinfo=True)
        rc, out = run("set", "0.6", "--allow-write", "--inst", "0x%X" % inst, *[c if c != mem_p else mem2_p for c in common])
        check(rc == 1 and "TypeInfo" in out, "wrong TypeInfo: identity FAIL, no write")
        mem3_p = os.path.join(d, "mem3.json")
        harness.build_x1_world(mem3_p, None, build="6AC07E31")
        m3 = harness.MockMem(mem3_p)
        hdr = bytearray(m3.read(harness.IMAGE_BASE, 0x400))
        struct.pack_into("<I", hdr, 0x88, 0x12300000)
        m3.write(harness.IMAGE_BASE, bytes(hdr))
        rc, out = run("check", "--inst", "0x%X" % inst, *[c if c != mem_p else mem3_p for c in common])
        check(rc == 3 and "unknown build" in out, "unknown build: refused")
        rc, out = run("check", *[c if c != log_p else os.path.join(d, "nolog.txt") for c in common])
        check(rc == 3 and "unreadable" in out, "no log, no --inst: refused")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:2] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(main())
