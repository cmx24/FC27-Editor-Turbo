#!/usr/bin/env python3
"""s_b1_check.py - the S-B1 pass point of the 2.4 tactics overhaul (WP-B1, docs/TACTICS_OVERHAUL_DECISION.md section 6), read-only.

What it proves, from Turbo's own log (turbo_output\\turbo_gui.log), for the last game launch:
  1. a channel E Apply in the hub was written and read back:
       "channel E: team 95 record 0 approach 0->2 line height 25->70 build-up 0->1; cm_mentalities ... (read back ok) ... [record now LH 70 BU 1 DA 2]"
  2. after kick-off the match's own copy of the tactic holds exactly those values:
       "channel E kick-off: match copy team 95 LH 70 BU 1 DA 2 roles 11/11 = Turbo (ok) | ..."
PASS only when the kick-off line comes after the Apply line, names the same club, says "= Turbo (ok)" with roles 11/11, and its LH / BU / DA
equal the Apply line's "record now" values (and no undo was logged in between).

It never touches the game, the dev service or any file but the log it reads. It does not launch or kill FC 27: the coordinator kills the game
after it (Part C). Time-boxed: --wait polls the log for the kick-off line (default 100 s, the check stays under 2 minutes).

Usage:
  python -I tools/s_b1_check.py                    # the last launch in C:\\FC 27 Live Editor\\turbo_output\\turbo_gui.log, wait up to 100 s
  python -I tools/s_b1_check.py --log PATH --wait 0
  python -I tools/s_b1_check.py --since "2026-10-14 09:30:00"
  python -I tools/s_b1_check.py --self-test        # offline, synthetic logs
Prints PASS / FAIL / INCONCLUSIVE on its last line; exit 0 / 1 / 3.
"""
import argparse
import os
import re
import sys
import tempfile
import time

LE = r"C:\FC 27 Live Editor"
DEFAULT_LOG = os.path.join(LE, "turbo_output", "turbo_gui.log")
OUT_DIR = os.path.join(LE, "turbo_output")

TS = r"(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+"
RE_LAUNCH = re.compile(TS + r"channel E: anchors ok on build (?P<build>\S+)")
RE_APPLY = re.compile(TS + r"channel E: team (?P<team>\d+) record (?P<rec>\d+)(?P<what>.*?) \(read back ok\)(?P<tail>.*)$")
RE_NOW = re.compile(r"\[record now LH (?P<lh>\d+) BU (?P<bu>\d+) DA (?P<da>\d+)\]")
RE_UNDO = re.compile(TS + r"channel E: undo team (?P<team>\d+) record \d+.*\(read back ok\)")
RE_KICK = re.compile(TS + r"channel E kick-off: match copy team (?P<team>-?\d+) LH (?P<lh>\S+) BU (?P<bu>\S+) DA (?P<da>\S+) roles (?P<roles>\d+)/11 "
                     r"(?P<verdict>= [^|]*?)(?: \| (?P<details>.*))?$")
RE_KICK_OTHER = re.compile(TS + r"channel E kick-off: (?P<what>(?:not checked|no match copy|check failed).*)$")
RE_PHASE = re.compile(TS + r"channel E: Lua phase \S+ -> match")

# switch files in turbo_output: (name, must exist)
SWITCHES = [("call_team_sheet_on.txt", True), ("call_team_sheet_off.txt", False), ("tactics_off.txt", False),
            ("channel_e_kickoff_off.txt", False), ("channel_e_db_off.txt", False), ("channel_e_db_mirror_off.txt", False),
            ("tactics_db_off.txt", False)]


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def launch_start(lines, since):
    """Index of the first line of the run to check: after `since` (a timestamp prefix), else the last 'anchors ok' line (each launch)."""
    if since:
        for i, l in enumerate(lines):
            if l[:23] >= since:
                return i
        return len(lines)
    start = 0
    for i, l in enumerate(lines):
        if RE_LAUNCH.match(l):
            start = i
    return start


def evaluate(lines, start):
    """(verdict, message lines): verdict 'PASS', 'FAIL' or 'WAIT' (no kick-off line yet)."""
    out = []
    apply_i, apply_m, now = -1, None, None
    kick = None
    for i in range(start, len(lines)):
        l = lines[i]
        m = RE_APPLY.match(l)
        if m and " refused (" not in l:
            n = RE_NOW.search(m.group("tail"))
            if n:
                apply_i, apply_m, now = i, m, n
                kick = None  # a newer Apply: the kick-off must come after it
            continue
        if apply_m is None:
            continue
        if RE_UNDO.match(l) and kick is None:
            apply_i, apply_m, now = -1, None, None  # Turbo's values were put back before any kick-off
            out.append("note: an undo after the Apply at " + l[:23] + " (Apply again before kick-off)")
            continue
        k = RE_KICK.match(l)
        if k and kick is None:
            kick = (i, k)
            continue
        o = RE_KICK_OTHER.match(l)
        if o and kick is None:
            out.append("kick-off line without a match copy: " + l)
            if "looking again" not in l:
                return "FAIL", out + ["FAIL kick-off check did not read the match copy: " + o.group("what")]
    if apply_m is None:
        return "FAIL", out + ["FAIL no channel E Apply line '(read back ok) ... [record now LH .. BU .. DA ..]' in this launch (Apply in the hub first)"]
    out.append("apply  : " + lines[apply_i])
    if kick is None:
        return "WAIT", out + ["no kick-off line yet (kick off in Tactical View; the line comes about 20 s of Turbo's clock after Lua's match phase)"]
    i, k = kick
    out.append("kickoff: " + lines[i][:400] + (" ..." if len(lines[i]) > 400 else ""))
    want = (apply_m.group("team"), now.group("lh"), now.group("bu"), now.group("da"))
    got = (k.group("team"), k.group("lh"), k.group("bu"), k.group("da"))
    bad = []
    if got[0] != want[0]:
        bad.append("team %s, the Apply's %s" % (got[0], want[0]))
    for name, g, w in zip(("LH", "BU", "DA"), got[1:], want[1:]):
        if g != w:
            bad.append("%s %s, Turbo wrote %s" % (name, g, w))
    if k.group("roles") != "11":
        bad.append("roles %s/11" % k.group("roles"))
    verdict = k.group("verdict").strip()
    if verdict != "= Turbo (ok)":
        bad.append("verdict '%s'" % verdict)
    if bad:
        return "FAIL", out + ["FAIL the match copy does not hold Turbo's values: " + "; ".join(bad)]
    row = "cm_mentalities row written in the same job" if "; cm_mentalities artificialkey" in apply_m.group("what") else \
        "cm_mentalities row NOT written (" + (re.search(r"\[cm_mentalities not written: ([^\]]*)\]", apply_m.group("tail")) or [None, "nothing to change"])[1] + ")"
    return "PASS", out + ["PASS team %s LH %s BU %s DA %s roles 11/11 in the match copy = Turbo's Apply (%s; Apply %s, kick-off %s)" %
                          (got + (row, lines[apply_i][:23], lines[i][:23]))]


def preflight(out_dir):
    msgs = []
    for name, must in SWITCHES:
        have = os.path.exists(os.path.join(out_dir, name))
        if have != must:
            msgs.append("WARN turbo_output\\%s %s (S-B1 needs it %s)" % (name, "present" if have else "missing", "present" if must else "absent"))
    return msgs


def run(log, since, wait, out_dir, quiet_preflight=False):
    if not quiet_preflight:
        for m in preflight(out_dir):
            print(m)
    deadline = time.time() + max(0.0, wait)
    while True:
        if not os.path.exists(log):
            print("FAIL no log at " + log)
            return 1
        lines = read_lines(log)
        v, msgs = evaluate(lines, launch_start(lines, since))
        if v != "WAIT" or time.time() >= deadline:
            for m in msgs:
                print(m)
            if v == "WAIT":
                print("INCONCLUSIVE no kick-off line within %.0f s" % wait)
                return 3
            return 0 if v == "PASS" else 1
        time.sleep(2.0)


# ----------------------------------------------------------------------------------------------------------------- self-test
APPLY = ("2026-10-14 09:31:02.100  channel E: team 95 record 0 approach 0->2 line height 25->70 build-up 0->1; cm_mentalities artificialkey 0 "
         "defensivedepth 25->70 buildupplay 0->1 (read back ok) [record now LH 70 BU 1 DA 2] [job 412 us, 31 reads]")
APPLY_NOROW = ("2026-10-14 09:31:02.100  channel E: team 95 record 0 approach 0->2 line height 25->70 build-up 0->1 (read back ok) "
               "[cm_mentalities not written: kill switch turbo_output\\channel_e_db_off.txt is present] [record now LH 70 BU 1 DA 2] [job 380 us, 28 reads]")
LAUNCH = "2026-10-14 09:28:00.001  channel E: anchors ok on build 6AC07E31-2145C000 (VT1 0x14AD7F6C8, SetSheet 0x145E43A60, UpdateSheet 0x145E43BA4, registry 0x14BE2A1F8)"
PHASE = "2026-10-14 09:32:10.000  channel E: Lua phase hub -> match: kick-off check in 20 s (the tactic after Turbo's write: Line Height 70, ...)"
KICK_OK = ("2026-10-14 09:32:16.700  channel E kick-off: match copy team 95 LH 70 BU 1 DA 2 roles 11/11 = Turbo (ok) | 2 copies (0xFA6EF878, "
           "0xFA76FA80; 3 pid / 3 role-word hit(s)); decode LH +0x158 BU +0x15B DA +0x15C u8 (T2 ...); window +0x150: 00 00 00 00 00 00 00 00 46 02 02 01 02")
KICK_DIFF = ("2026-10-14 09:32:16.700  channel E kick-off: match copy team 95 LH 25 BU 0 DA 0 roles 11/11 = game (differs: copy 1: LH 25 not 70, "
             "BU 0 not 1, DA 0 not 2; copy 2: LH 25 not 70, BU 0 not 1, DA 0 not 2) | 2 copies (...)")
KICK_NOT = "2026-10-14 09:32:16.700  channel E kick-off: not checked (channel E is not switched on (turbo_output\\call_team_sheet_on.txt))"
KICK_NONE1 = "2026-10-14 09:32:16.700  channel E kick-off: no match copy found (0 pid / 0 role-word hit(s), 0 in the match-setup layout); looked for ...; looking again in 30 s"
KICK_NONE2 = "2026-10-14 09:32:26.700  channel E kick-off: no match copy found (0 pid / 0 role-word hit(s), 0 in the match-setup layout); looked for ... (second look)"
UNDO = "2026-10-14 09:31:40.000  channel E: undo team 95 record 0 approach 2->0 line height 70->25 build-up 1->0 (read back ok) [record now LH 25 BU 0 DA 0]"
OLD_LAUNCH_APPLY = APPLY.replace("2026-10-14 09:31", "2026-10-13 19:22")


def self_test():
    cases = [
        ("pass", [OLD_LAUNCH_APPLY, LAUNCH, APPLY, PHASE, KICK_OK], 0, "PASS team 95 LH 70 BU 1 DA 2", "cm_mentalities row written in the same job"),
        ("pass, row left out", [LAUNCH, APPLY_NOROW, PHASE, KICK_OK], 0, "PASS", "cm_mentalities row NOT written (kill switch"),
        ("the match copy differs", [LAUNCH, APPLY, PHASE, KICK_DIFF], 1, "FAIL the match copy does not hold Turbo's values", "LH 25, Turbo wrote 70"),
        ("not checked", [LAUNCH, APPLY, PHASE, KICK_NOT], 1, "FAIL kick-off check did not read the match copy", "not switched on"),
        ("no copy twice", [LAUNCH, APPLY, PHASE, KICK_NONE1, KICK_NONE2], 1, "FAIL kick-off check did not read the match copy", "second look"),
        ("no copy yet, retry pending", [LAUNCH, APPLY, PHASE, KICK_NONE1], 3, "INCONCLUSIVE", ""),
        ("no apply in this launch (only the last launch's)", [OLD_LAUNCH_APPLY, LAUNCH, PHASE, KICK_OK], 1, "FAIL no channel E Apply line", ""),
        ("no kick-off yet", [LAUNCH, APPLY, PHASE], 3, "INCONCLUSIVE no kick-off line", ""),
        ("undo before kick-off", [LAUNCH, APPLY, UNDO, PHASE, KICK_OK], 1, "FAIL no channel E Apply line", "an undo after the Apply"),
        ("a refused apply does not count", [LAUNCH, "2026-10-14 09:31:00.000  channel E: refused (G1): turbo_output\\no call_team_sheet_on.txt: nothing written",
                                             PHASE, KICK_OK], 1, "FAIL no channel E Apply line", ""),
        ("a kick-off of another club", [LAUNCH, APPLY, PHASE, KICK_OK.replace("match copy team 95", "match copy team 96")], 1, "team 96, the Apply's 95", ""),
        ("roles 10/11", [LAUNCH, APPLY, PHASE, KICK_OK.replace("roles 11/11 = Turbo (ok)", "roles 10/11 = game (differs: copy 2: roles 10/11)")], 1,
         "roles 10/11", ""),
        ("a newer apply after the kick-off: waits for its own kick-off", [LAUNCH, APPLY, PHASE, KICK_OK, APPLY.replace("09:31:02", "09:40:00")], 3, "INCONCLUSIVE", ""),
    ]
    fails = 0
    tmp = tempfile.mkdtemp(prefix="s_b1_")
    log = os.path.join(tmp, "turbo_gui.log")
    import io
    import contextlib
    for name, lines, code, want, want2 in cases:
        with open(log, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            got = run(log, None, 0, tmp, quiet_preflight=True)
        text = buf.getvalue()
        ok = got == code and want in text and (not want2 or want2 in text)
        print("  %s %s (exit %d)" % ("ok  " if ok else "FAIL", name, got))
        if not ok:
            fails += 1
            print("      " + text.replace("\n", "\n      "))
    # --since picks the run by time; the preflight names the switch files
    with open(log, "w", encoding="utf-8") as f:
        f.write("\n".join([LAUNCH, APPLY, PHASE, KICK_OK]) + "\n")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        got = run(log, "2026-10-14 09:32:00", 0, tmp, quiet_preflight=True)
    ok = got == 1 and "FAIL no channel E Apply line" in buf.getvalue()
    print("  %s --since after the Apply finds none (exit %d)" % ("ok  " if ok else "FAIL", got))
    fails += 0 if ok else 1
    pf = preflight(tmp)
    ok = any("call_team_sheet_on.txt missing" in m for m in pf) and len(pf) == 1
    print("  %s preflight names the missing opt-in only" % ("ok  " if ok else "FAIL"))
    fails += 0 if ok else 1
    open(os.path.join(tmp, "channel_e_kickoff_off.txt"), "w").close()
    pf = preflight(tmp)
    ok = any("channel_e_kickoff_off.txt present" in m for m in pf)
    print("  %s preflight names a kill switch that is present" % ("ok  " if ok else "FAIL"))
    fails += 0 if ok else 1
    print("SELF-TEST " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--log", default=DEFAULT_LOG)
    ap.add_argument("--since", default=None, help="'YYYY-MM-DD HH:MM:SS': check the lines from then on (default: the last launch)")
    ap.add_argument("--wait", type=float, default=100.0, help="seconds to wait for the kick-off line (default 100)")
    ap.add_argument("--out-dir", default=OUT_DIR, help="turbo_output (the switch files' pre-flight)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    return run(a.log, a.since, min(a.wait, 110.0), a.out_dir)


if __name__ == "__main__":
    sys.exit(main())
