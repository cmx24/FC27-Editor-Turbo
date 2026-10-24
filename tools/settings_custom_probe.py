"""settings_custom_probe.py - the 3.0.1 defect 2 probe helper (brief 3.0.1-tactics: slider values written by turbo_settings_apply.exe --apply
read 50 in game). Reads FC 27 Settings files and prints, per file: name, size, md5, the 5 bytes before the Custom block (and whether --apply
accepts them), and the three screen-proven Custom sliders. Read-only: it opens the files it is given for reading and writes nothing.

  py -3 -I tools/settings_custom_probe.py --dir "C:/Users/cmode/AppData/Local/EA SPORTS FC 27/settings" [--expect user.sprint_speed=33,...]
  py -3 -I tools/settings_custom_probe.py --file <Settings file> [--file ...] [--expect ...]
  py -3 -I tools/settings_custom_probe.py --self-test     (offline, synthetic files)

--dir reads every Settings2* file in the folder (the newest last). With --expect the LAST file printed must hold those values: one RESULT
line, PASS or FAIL. Without --expect the RESULT line is DATA.

Layout (core/game_settings.h, slider map 6AC07E31 "fbchunks-454-bc650"): file size 772790; the Custom block (GameplayTypeCustomData, 125
u8) at file 0xB8DDC (payload 0xB8976 + 0x466); --apply refuses a file whose 5 bytes before it are not 01 00 00 00 01 (8F10EEB9 holds
01 00 00 00 00: GUI30J settings_flag_table.md). Screen-proven sliders (TEST_31 G-S1): user.sprint_speed 0x50, user.acceleration 0x51,
cpu.sprint_speed 0x24. This tool reads the FILE; what the running game shows is the probe's observation (the EA Custom screen).
"""
import argparse
import hashlib
import os
import sys

SIZE = 772790
BLOCK = 0x466 + 0xB8976
PREFIX = bytes([1, 0, 0, 0, 1])
PROVEN = [("user.sprint_speed", 0x50), ("user.acceleration", 0x51), ("cpu.sprint_speed", 0x24)]


def describe(name, b):
    """(line, values dict or None)"""
    md5 = hashlib.md5(b).hexdigest()
    if len(b) != SIZE:
        return "%s: %d bytes md5 %s: not the known layout (want %d bytes); nothing read" % (name, len(b), md5, SIZE), None
    pre = b[BLOCK - 5:BLOCK]
    vals = {k: b[BLOCK + i] for k, i in PROVEN}
    acc = "--apply accepts" if pre == PREFIX else "--apply REFUSES (needs 01 00 00 00 01)"
    return ("%s: md5 %s; bytes before the Custom block %s (%s); %s" %
            (name, md5, " ".join("%02X" % x for x in pre), acc, ", ".join("%s %d" % (k, v) for k, v in vals.items()))), vals


def parse_expect(s):
    out = {}
    for part in (s or "").split(","):
        if part.strip():
            k, v = part.split("=")
            out[k.strip()] = int(v)
    return out


def run(files, expect):
    """(lines, result)"""
    lines, last = [], None
    for name, b in files:
        line, vals = describe(name, b)
        lines.append(line)
        last = vals
    if not files:
        return ["no Settings file"], "FAIL" if expect else "DATA"
    if not expect:
        return lines, "DATA"
    if last is None:
        return lines + ["the last file is not the known layout"], "FAIL"
    bad = ["%s %d (want %d)" % (k, last.get(k, -1), v) for k, v in expect.items() if last.get(k) != v]
    lines.append(("the last file holds " + ", ".join("%s %d" % (k, v) for k, v in expect.items())) if not bad else ("the last file differs: " + "; ".join(bad)))
    return lines, "FAIL" if bad else "PASS"


def self_test():
    fails = 0

    def mk(flag=1, vals=None):
        b = bytearray(SIZE)
        b[BLOCK - 5:BLOCK] = bytes([1, 0, 0, 0, flag])
        for i in range(125):
            b[BLOCK + i] = 50
        for k, i in PROVEN:
            if vals and k in vals:
                b[BLOCK + i] = vals[k]
        return bytes(b)

    def expect(name, got, want, lines):
        nonlocal fails
        ok = got == want
        fails += 0 if ok else 1
        print("  %s %s: %s (want %s)" % ("ok  " if ok else "FAIL", name, got, want))
        if not ok:
            print("\n".join(lines))

    applied = {"user.sprint_speed": 33, "user.acceleration": 46, "cpu.sprint_speed": 34}
    l, r = run([("a", mk(1, applied))], applied)
    expect("applied values present", r, "PASS", l)
    l, r = run([("a", mk(1))], applied)
    expect("all 50 (the GUI30H after-boot file 6a805dc2)", r, "FAIL", l)
    l, r = run([("a", mk(1, applied)), ("b", mk(1))], applied)
    expect("the newest file (last) decides", r, "FAIL", l)
    l, r = run([("a", mk(0))], {})
    expect("flag 00 is named", r == "DATA" and "REFUSES" in l[0] and "01 00 00 00 00" in l[0], True, l)
    l, r = run([("a", b"\0" * 10)], applied)
    expect("unknown size", r, "FAIL", l)
    print("self-test: %s (%d failed)" % ("PASS" if fails == 0 else "FAIL", fails))
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir")
    ap.add_argument("--file", action="append", default=[])
    ap.add_argument("--expect", default="")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    paths = list(a.file)
    if a.dir:
        paths += sorted(os.path.join(a.dir, n) for n in os.listdir(a.dir) if n.startswith("Settings2"))
    files = []
    for p in paths:
        with open(p, "rb") as f:
            files.append((os.path.basename(p), f.read()))
    lines, result = run(files, parse_expect(a.expect))
    print("\n".join(lines))
    print("RESULT %s" % result)
    return 0 if result in ("PASS", "DATA") else 1


if __name__ == "__main__":
    sys.exit(main())
