#!/usr/bin/env python3
"""profile_livecheck.py - the 2.20 Profile and Positions/Attributes/PlayStyles pages live check (brief 2.20-IA-pages, track ia-profile).
After ONE short LEICESTER session it reads turbo_gui.log and prints, per point, PASS / FAIL / INCONCLUSIVE with the values read back, and
one RESULT line. Read-only: it never touches the game, the install or turbo_output (it only reads the log file it is given).

  py -3 -I tools/profile_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-19 15:20:00" [--player "Name"]
  py -3 -I tools/profile_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 3 minutes; the Tester's runner launches and kills the game):
  1. Career hub, Turbo window: Players -> pick ONE player -> Profile. Screenshot S1 (the page: names, commentary name, personal details,
     club and contract read only, no tabs, no ids on the page). In "Personal details" change Weight by typing a new value
     (for example 1 more than shown) and press Enter. Screenshot S2.
  2. Same player -> Positions, attributes & PlayStyles. Screenshot S3 (overall and potential, 7 positions, playing style, roles,
     attributes, PlayStyles). In "PlayStyles and traits" tick or untick ONE box. Screenshot S4.
  3. Press the header "Undo" twice (the PlayStyle first, then the Weight). Screenshot S5: both fields show their first values again.
  4. Kill the game (the runner). Never save.

What it reads (App::edit_field writes one line per field write: words, then "[players.<field> = <value>]"):
  session   the last "Turbo GUI loading in process" session at or after --since
  weight    exactly two writes of players.weight: the edit (value A) and the undo (value B), A != B
  bits      exactly two writes of ONE PlayStyle field (trait1, icontrait1, trait2 or icontrait2): the tick and its undo; the two values
            differ by exactly one bit (the one box)
  order     the undo ran last-in first-out: the PlayStyle undo line comes before the Weight undo line
  keys      no other players field was written in the session (a stray click on another box shows up here)
  errors    no "frame error:" line in the session (a draw exception in either new page)
The operator compares S1..S5 by eye (nothing in the log shows the page layout): MANUAL lines at the end.
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_WRITE = re.compile(r"\[players\.(?P<field>\w+) = (?P<value>-?\d+)\]")
BIT_FIELDS = ("trait1", "icontrait1", "trait2", "icontrait2")


def session_lines(text, since):
    """The lines of the last Turbo session that started at or after `since` ("" = the last session in the log)."""
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if "Turbo GUI loading in process" in line:
            m = RE_TIME.match(line)
            if not since or (m and m.group(1) >= since):
                start = i
    return [] if start is None else lines[start:]


def writes_of(lines, player):
    out = []
    for i, line in enumerate(lines):
        m = RE_WRITE.search(line)
        if m and (not player or player.lower() in line.lower()):
            out.append((i, m.group("field"), int(m.group("value"))))
    return out


def check(text, since="", player=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))
    ws = writes_of(lines, player)

    weight = [w for w in ws if w[1] == "weight"]
    if not weight:
        out.append(("weight", "INCONCLUSIVE", "no players.weight write: the Weight field was not changed (step 1)"))
    elif len(weight) != 2:
        out.append(("weight", "FAIL", "%d weight write(s) %s (want the edit and its undo)" % (len(weight), [w[2] for w in weight])))
    elif weight[0][2] == weight[1][2]:
        out.append(("weight", "FAIL", "the undo wrote the same value %d as the edit" % weight[0][2]))
    else:
        out.append(("weight", "PASS", "edit wrote %d, undo wrote %d (read back from the live row)" % (weight[0][2], weight[1][2])))

    bits = [w for w in ws if w[1] in BIT_FIELDS]
    if not bits:
        out.append(("bits", "INCONCLUSIVE", "no PlayStyle field write: no box was ticked (step 2)"))
    elif len(bits) != 2 or bits[0][1] != bits[1][1]:
        out.append(("bits", "FAIL", "%d PlayStyle write(s) %s (want one tick and its undo on one field)" % (len(bits), [(b[1], b[2]) for b in bits])))
    else:
        x = (bits[0][2] ^ bits[1][2]) & 0xFFFFFFFF
        if x and x & (x - 1) == 0:
            out.append(("bits", "PASS", "%s: tick wrote %d, undo wrote %d (one box = bit %d)" % (bits[0][1], bits[0][2], bits[1][2], x.bit_length() - 1)))
        else:
            out.append(("bits", "FAIL", "%s: %d and %d differ by %#x (want exactly one bit)" % (bits[0][1], bits[0][2], bits[1][2], x)))

    if len(weight) == 2 and len(bits) == 2:
        ok = bits[1][0] < weight[1][0]
        out.append(("order", "PASS" if ok else "FAIL", "PlayStyle undo %s the Weight undo" % ("before" if ok else "AFTER (want before)")))
    else:
        out.append(("order", "INCONCLUSIVE", "needs both pairs"))

    stray = sorted({w[1] for w in ws if w[1] != "weight" and w[1] not in BIT_FIELDS})
    out.append(("keys", "FAIL" if stray else "PASS", ("other players fields written: " + ", ".join(stray)) if stray else "only weight and one PlayStyle field were written"))

    errs = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0][:200] if errs else "no frame error"))

    verdicts = {v for _, v, _ in out}
    return ("FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")), out


def report(text, since="", player=""):
    result, points = check(text, since, player)
    for name, verdict, detail in points:
        print("%-8s %-12s %s" % (name, verdict, detail))
    print("RESULT %s" % result)
    print("MANUAL: S1 Profile page has the sections and no tabs; S3 Positions page has overall, positions, style, roles, attributes, PlayStyles; "
          "S5 after the two Undo presses both fields show their first values.")
    return {"PASS": 0, "FAIL": 1, "INCONCLUSIVE": 2}[result]


def _log(*rows):
    head = "2026-10-19 15:20:00.000  Turbo GUI loading in process 111\n"
    return head + "".join("2026-10-19 15:2%d:00.000  %s\n" % (i + 1, r) for i, r in enumerate(rows))


def self_test():
    good = _log("Weight changed 74 -> 75 (Ali Test)  [players.weight = 75]",
                "PlayStyle changed (Ali Test)  [players.trait1 = 5]",
                "undo: PlayStyle changed back (Ali Test)  [players.trait1 = 1]",
                "undo: Weight changed back (Ali Test)  [players.weight = 74]")
    cases = [
        ("good", good, "", "PASS"),
        ("nothing written", _log("hello"), "", "INCONCLUSIVE"),
        ("no session", "nothing\n", "", "FAIL"),
        ("two bits", good.replace("trait1 = 1", "trait1 = 0"), "", "FAIL"),
        ("undo of weight not written", good.replace("weight = 74", "weight = 75"), "", "FAIL"),
        ("wrong order", _log("Weight  [players.weight = 75]", "PlayStyle  [players.trait1 = 5]", "undo  [players.weight = 74]", "undo  [players.trait1 = 1]"), "", "FAIL"),
        ("stray field", good + "2026-10-19 15:29:00.000  Potential  [players.potential = 80]\n", "", "FAIL"),
        ("frame error", good + "2026-10-19 15:29:00.000  frame error: boom\n", "", "FAIL"),
        ("other player filtered", good, "Someone Else", "INCONCLUSIVE"),
    ]
    bad = 0
    for name, text, player, want in cases:
        got, _ = check(text, "", player)
        print("%-28s want %-12s got %-12s %s" % (name, want, got, "ok" if got == want else "WRONG"))
        bad += got != want
    print("SELF-TEST %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=r"C:\FC 27 Live Editor\turbo_output\turbo_gui.log")
    ap.add_argument("--since", default="")
    ap.add_argument("--player", default="")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("RESULT FAIL (cannot read %s: %s)" % (a.log, e))
        return 1
    return report(text, a.since, a.player)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
