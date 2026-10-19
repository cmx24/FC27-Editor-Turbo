#!/usr/bin/env python
"""2.20 K4 live check (W2): the minikit(s) a kit import makes, in the MK1 form. Read-only; 2 minutes; prints PASS/FAIL per slot.

  python -I minikit_livecheck.py before <teamid> [--slots 0,1,2,3] [--kits DIR] [--state FILE]
      before the import: records size + md5 of j<slot>_<teamid>_0.dds (or ABSENT) for each slot in FILE
  python -I minikit_livecheck.py after  <teamid> [--slots ...] [--kits DIR] [--state FILE] [--log LE_LOG] [--expect-drawn]
      after the import: every slot FILE named in --slots must (1) exist, (2) be 348,288 bytes, (3) carry the MK1 header (128 bytes),
      (4) hold a shirt: some clear pixels and some opaque ones, more than 3 colours, not all white, (5) differ from the "before" md5.
      --log LE_LOG: informational only - lists the 'kits/j<t>_<id>_0.dds' lines of the Live Editor log (the MK1 minikits showed in game
      with NO such line, so a missing line is never a FAIL).
  python -I minikit_livecheck.py undo   <teamid> [--slots ...] [--kits DIR] [--state FILE]
      after the Undo (Restore): every slot is byte-identical to its "before" md5 (ABSENT before = the file is gone)
  python -I minikit_livecheck.py --self-test      (synthetic files in a temp folder; touches nothing else)
The in-game observation (screenshot) is the Tester's: Select Kits shows the new small shirt for the slot. FAIL there = a white / blank
small shirt or the old one. Exit 0 = PASS, 1 = FAIL.
"""
import hashlib
import json
import os
import sys
import tempfile

KITS_DEFAULT = r"C:\FC 27 Live Editor\mods\legacy\data\ui\imgAssets\kits"
SIZE = 348288
# the first 128 bytes of the 66 live minikits (identical in all of them): the game's own legacy header
HEADER_HEX = ("444453207c0000000710080000010000000100000000040000000000040000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000004100000000000000200000000000ff0000ff0000ff000000000000ff0810400000000000000000000000000000000000")
HEADER = bytes.fromhex(HEADER_HEX)


def md5(b):
    return hashlib.md5(b).hexdigest().upper()


def fname(team, slot):
    return "j%d_%d_0.dds" % (slot, team)


def snapshot(kits, team, slots):
    out = {}
    for s in slots:
        p = os.path.join(kits, fname(team, s))
        if os.path.isfile(p):
            b = open(p, "rb").read()
            out[str(s)] = {"size": len(b), "md5": md5(b)}
        else:
            out[str(s)] = {"size": 0, "md5": "ABSENT"}
    return out


def inspect(b):
    """(ok, why) for the bytes of one minikit file."""
    if len(b) != SIZE:
        return False, "size %d, not %d" % (len(b), SIZE)
    if b[:128] != HEADER:
        return False, "header differs from the MK1 header"
    px = b[128:128 + 256 * 256 * 4]
    opaque = clear = 0
    colours = set()
    white = 0
    for i in range(0, len(px), 4 * 7):  # every 7th texel is plenty
        bl, g, r, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        if a == 0:
            clear += 1
        else:
            opaque += 1
            colours.add((r >> 3, g >> 3, bl >> 3))
            if r > 245 and g > 245 and bl > 245:
                white += 1
    if opaque == 0:
        return False, "no opaque pixel (blank)"
    if clear == 0:
        return False, "no clear pixel (a shirt has a silhouette)"
    if len(colours) <= 3:
        return False, "%d colours (a flat patch, not a shirt)" % len(colours)
    if white == opaque:
        return False, "all white"
    return True, "%d opaque / %d clear samples, %d colours" % (opaque, clear, len(colours))


def log_lines(path, team, slots):
    out = []
    try:
        for line in open(path, "r", encoding="utf-8", errors="replace"):
            for s in slots:
                if "kits/" + fname(team, s) in line.replace("\\", "/"):
                    out.append(line.strip())
    except OSError as e:
        out.append("(cannot read %s: %s)" % (path, e))
    return out


def run(mode, team, slots, kits, state, log=None):
    lines, ok = [], True
    if mode == "before":
        snap = snapshot(kits, team, slots)
        json.dump({"team": team, "slots": snap}, open(state, "w"), indent=2)
        for s in slots:
            lines.append("slot %d: %s (%d bytes) recorded" % (s, snap[str(s)]["md5"], snap[str(s)]["size"]))
        return True, lines
    try:
        before = json.load(open(state))["slots"]
    except Exception as e:  # noqa: BLE001
        return False, ["FAIL: no 'before' record %s (%s): run 'before' first" % (state, e)]
    for s in slots:
        p = os.path.join(kits, fname(team, s))
        b0 = before.get(str(s), {"md5": "ABSENT"})
        if mode == "after":
            if not os.path.isfile(p):
                ok = False
                lines.append("slot %d: FAIL %s is not there" % (s, fname(team, s)))
                continue
            b = open(p, "rb").read()
            good, why = inspect(b)
            changed = md5(b) != b0["md5"]
            if not good:
                ok = False
            if not changed:
                ok = False
                why += "; md5 unchanged from before (the import wrote nothing)"
            lines.append("slot %d: %s %s md5 %s (before %s)" % (s, "PASS" if good and changed else "FAIL", why, md5(b), b0["md5"]))
        else:  # undo
            now = md5(open(p, "rb").read()) if os.path.isfile(p) else "ABSENT"
            same = now == b0["md5"]
            ok = ok and same
            lines.append("slot %d: %s now %s, before %s" % (s, "PASS" if same else "FAIL", now, b0["md5"]))
    if log and mode == "after":
        found = log_lines(log, team, slots)
        lines.append("log (informational, never a FAIL): %d served line(s)" % len(found))
        lines.extend("   " + x for x in found[:8])
    return ok, lines


def shirt_file(colour_a=(35, 79, 142), flat=False, size=SIZE):
    """A synthetic minikit: a shirt-ish blob on a clear background (or a flat patch / all-white for the negative cases)."""
    px = bytearray()
    for y in range(256):
        for x in range(256):
            if flat:
                px += bytes((255, 255, 255, 255))
            elif 60 < x < 200 and 40 < y < 220:
                shade = 200 + (x * 3 + y * 5) % 50
                px += bytes((min(255, colour_a[2] * shade // 255), min(255, colour_a[1] * shade // 255), min(255, colour_a[0] * shade // 255), 255))
            else:
                px += bytes((0, 0, 0, 0))
    body = bytes(px)
    out = HEADER + body
    # the three smaller levels: any bytes of the right length
    out += bytes(len(px) // 4) + bytes(len(px) // 16) + bytes(len(px) // 64)
    return out[:size] if size < len(out) else out


def self_test():
    d = tempfile.mkdtemp(prefix="minikit_check_")
    team = 4242
    fails = []

    def expect(name, got, want):
        if got != want:
            fails.append("%s: got %r want %r" % (name, got, want))
        print("  %s %s" % ("ok  " if got == want else "FAIL", name))

    good = shirt_file()
    expect("synthetic file has the right size", len(good), SIZE)
    expect("a shirt passes inspect", inspect(good)[0], True)
    expect("a flat white patch fails", inspect(shirt_file(flat=True))[0], False)
    expect("a short file fails", inspect(good[:-1])[0], False)
    bad_hdr = bytearray(good)
    bad_hdr[8] ^= 1
    expect("another header fails", inspect(bytes(bad_hdr))[0], False)
    kits = os.path.join(d, "kits")
    os.makedirs(kits)
    st = os.path.join(d, "state.json")
    # before: slot 0 has an MK1 file, slot 1 has none
    open(os.path.join(kits, fname(team, 0)), "wb").write(shirt_file((200, 20, 20)))
    expect("before records", run("before", team, [0, 1], kits, st)[0], True)
    # after, nothing changed: FAIL
    expect("after with no import fails", run("after", team, [0, 1], kits, st)[0], False)
    # the import: slot 0 overwritten, slot 1 created
    open(os.path.join(kits, fname(team, 0)), "wb").write(good)
    open(os.path.join(kits, fname(team, 1)), "wb").write(shirt_file((20, 200, 20)))
    ok, lines = run("after", team, [0, 1], kits, st)
    expect("after the import passes", ok, True)
    # a blank slot fails
    open(os.path.join(kits, fname(team, 1)), "wb").write(shirt_file(flat=True))
    expect("a white small shirt fails", run("after", team, [0, 1], kits, st)[0], False)
    # undo: slot 0 back, slot 1 gone
    open(os.path.join(kits, fname(team, 0)), "wb").write(shirt_file((200, 20, 20)))
    os.remove(os.path.join(kits, fname(team, 1)))
    expect("undo restores", run("undo", team, [0, 1], kits, st)[0], True)
    open(os.path.join(kits, fname(team, 0)), "wb").write(good)
    expect("undo with the new file still there fails", run("undo", team, [0, 1], kits, st)[0], False)
    # the log lines are informational
    lg = os.path.join(d, "le.log")
    open(lg, "w").write("x\nserved data/ui/imgAssets/kits/j0_%d_0.dds\ny\n" % team)
    expect("log lines found", len(log_lines(lg, team, [0, 1])), 1)
    print("SELF-TEST %s (%d failed)" % ("PASS" if not fails else "FAIL", len(fails)))
    for f in fails:
        print("  FAIL " + f)
    return not fails


def main(argv):
    if "--self-test" in argv:
        return 0 if self_test() else 1
    if len(argv) < 3 or argv[1] not in ("before", "after", "undo"):
        print(__doc__)
        return 2
    mode, team = argv[1], int(argv[2])
    opt = {"--slots": "0,1,2,3", "--kits": KITS_DEFAULT, "--state": "minikit_livecheck_state.json", "--log": None}
    i = 3
    while i < len(argv):
        if argv[i] in opt and i + 1 < len(argv):
            opt[argv[i]] = argv[i + 1]
            i += 2
        else:
            i += 1
    slots = [int(x) for x in opt["--slots"].split(",") if x != ""]
    ok, lines = run(mode, team, slots, opt["--kits"], opt["--state"], opt["--log"])
    for ln in lines:
        print(ln)
    print("%s minikit %s team %d" % ("PASS" if ok else "FAIL", mode, team))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
