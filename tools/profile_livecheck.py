#!/usr/bin/env python3
"""profile_livecheck.py - the 2.20 Profile and Positions/Attributes/PlayStyles pages live check (brief 2.20-IA-pages, track ia-profile).
After ONE short LEICESTER session it reads turbo_gui.log and prints, per point, PASS / FAIL / INCONCLUSIVE with the values the app logged,
and one RESULT line. Read-only: it never touches the game, the install or turbo_output (it only reads the files it is given).

  py -3 -I tools/profile_livecheck.py --md5 "C:/FC 27 Live Editor/turbo_output/reapply_edits.json"          BEFORE the launch: prints the md5
  py -3 -I tools/profile_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --player "Full Name" \
        --since "2026-10-19 15:20:00" [--until "2026-10-19 15:24:00"] [--weight-before 74] \
        [--reapply "C:/FC 27 Live Editor/turbo_output/reapply_edits.json" --reapply-before <md5 printed above>]
  py -3 -I tools/profile_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

--since is the clock time (the log's own clock, YYYY-MM-DD HH:MM:SS) the operator starts step 1; --until, when other tracks share the boot, is
the time step 3 ends. Only log lines inside that window count (the undo line names no player, so the window is what keeps other
tracks' edits out). --player is required: an edit line must name him.

The operator's steps (LEICESTER career, never saved, about 3 minutes; the Tester's runner launches and kills the game):
  0. Write down the starting time, the player's name and his Weight as the page shows it. (Optional: the md5 of reapply_edits.json.)
  1. Career hub, Turbo window: Players -> pick ONE player -> Profile. Screenshot S1 (the page: names, commentary name, personal details,
     club and contract read only, no tabs, no ids on the page). In "Personal details" change Weight by typing a new value
     (for example 1 more than shown) and press Enter. Screenshot S2.
  2. Same player -> Positions, attributes & PlayStyles. Screenshot S3 (overall and potential, 7 positions, playing style, roles,
     attributes, PlayStyles). In "PlayStyles and traits" tick or untick ONE box. Screenshot S4.
  3. Press the header "Undo" twice (the PlayStyle first, then the Weight). Screenshot S5: both fields show their first values again.
  4. Kill the game (the runner). Never save.

What it reads (App::edit_field logs "<words>  [players.<field> = <value>]" for a write; App::undo logs "undo players.<field> = <value>", the
value it put back. The value is the one the app SENT to the database, not a second read of the game's memory):
  window    log lines inside --since / --until
  weight    exactly one edit of players.weight (value A) and one undo (value B), A != B; with --weight-before: B equals it
  bits      exactly one edit and one undo of ONE PlayStyle field (trait1, icontrait1, trait2, icontrait2); the two values differ by exactly
            one bit (the one box)
  order     the undo ran last-in first-out: the PlayStyle undo line comes before the Weight undo line
  keys      no other players field was written in the window (a stray click on another box shows up here)
  errors    no "frame error:" line in the window (a draw exception in either new page)
  reapply   with --reapply-before: reapply_edits.json is byte-identical after the two Undo presses (an undone edit leaves no kept row);
            SKIPPED (not part of the result) when not given
The operator compares S1..S5 by eye (nothing in the log shows the page layout): MANUAL lines at the end.
"""
import argparse
import hashlib
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_EDIT = re.compile(r"\[players\.(?P<field>\w+) = (?P<value>-?\d+)\]")
RE_UNDO = re.compile(r"\bundo players\.(?P<field>\w+) = (?P<value>-?\d+)")
BIT_FIELDS = ("trait1", "icontrait1", "trait2", "icontrait2")
MASK64 = (1 << 64) - 1


def window_lines(text, since, until):
    """The log lines whose time stamp is inside [since, until] (a line with no time stamp belongs to the line before it)."""
    out = []
    inside = False
    for line in text.splitlines():
        m = RE_TIME.match(line)
        if m:
            t = m.group(1)
            inside = t >= since and (not until or t <= until)
        if inside:
            out.append(line)
    return out


def writes_of(lines, player):
    """-> [(index, kind 'edit'|'undo', field, value)]. An edit line must name the player; an undo line names nobody."""
    out = []
    for i, line in enumerate(lines):
        m = RE_EDIT.search(line)
        if m:
            if not player or player.lower() in line.lower():
                out.append((i, "edit", m.group("field"), int(m.group("value"))))
            continue
        m = RE_UNDO.search(line)
        if m:
            out.append((i, "undo", m.group("field"), int(m.group("value"))))
    return out


def md5_of(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def check(text, since, until="", player="", weight_before=None, reapply_now=None, reapply_before=None):
    """Returns (result, [(point, verdict, detail)])"""
    lines = window_lines(text, since, until)
    out = []
    if not lines:
        out.append(("window", "FAIL", "no log line between %s and %s" % (since, until or "the end")))
        return "FAIL", out
    out.append(("window", "PASS", "%d log lines" % len(lines)))
    ws = writes_of(lines, player)

    we = [w for w in ws if w[1] == "edit" and w[2] == "weight"]
    wu = [w for w in ws if w[1] == "undo" and w[2] == "weight"]
    if not we and not wu:
        out.append(("weight", "INCONCLUSIVE", "no players.weight write for this player: the Weight field was not changed (step 1)"))
    elif len(we) != 1 or len(wu) != 1:
        out.append(("weight", "FAIL", "%d edit(s) %s and %d undo(s) %s (want one of each)" % (len(we), [w[3] for w in we], len(wu), [w[3] for w in wu])))
    elif we[0][3] == wu[0][3]:
        out.append(("weight", "FAIL", "the undo put back the same value %d as the edit wrote" % we[0][3]))
    elif weight_before is not None and wu[0][3] != weight_before:
        out.append(("weight", "FAIL", "the undo put back %d, the page showed %d at the start" % (wu[0][3], weight_before)))
    else:
        out.append(("weight", "PASS", "edit wrote %d, undo put back %d%s" % (we[0][3], wu[0][3], " (the starting value)" if weight_before is not None else "")))

    be = [w for w in ws if w[1] == "edit" and w[2] in BIT_FIELDS]
    bu = [w for w in ws if w[1] == "undo" and w[2] in BIT_FIELDS]
    if not be and not bu:
        out.append(("bits", "INCONCLUSIVE", "no PlayStyle field write for this player: no box was ticked (step 2)"))
    elif len(be) != 1 or len(bu) != 1 or be[0][2] != bu[0][2]:
        out.append(("bits", "FAIL", "edits %s, undos %s (want one tick and its undo on one field)" % ([(b[2], b[3]) for b in be], [(b[2], b[3]) for b in bu])))
    else:
        x = (be[0][3] ^ bu[0][3]) & MASK64
        if x and x & (x - 1) == 0:
            out.append(("bits", "PASS", "%s: tick wrote %d, undo put back %d (one box = bit %d)" % (be[0][2], be[0][3], bu[0][3], x.bit_length() - 1)))
        else:
            out.append(("bits", "FAIL", "%s: %d and %d differ by %#x (want exactly one bit)" % (be[0][2], be[0][3], bu[0][3], x)))

    if len(wu) == 1 and len(bu) == 1:
        ok = bu[0][0] < wu[0][0]
        out.append(("order", "PASS" if ok else "FAIL", "PlayStyle undo %s the Weight undo" % ("before" if ok else "AFTER (want before)")))
    else:
        out.append(("order", "INCONCLUSIVE", "needs both undos"))

    stray = sorted({w[2] for w in ws if w[2] != "weight" and w[2] not in BIT_FIELDS})
    out.append(("keys", "FAIL" if stray else "PASS", ("other players fields written: " + ", ".join(stray)) if stray else "only weight and one PlayStyle field were written"))

    errs = [ln for ln in lines if "frame error:" in ln]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0][:200] if errs else "no frame error"))

    if reapply_before is None or reapply_now is None:
        out.append(("reapply", "SKIPPED", "--reapply / --reapply-before not given (not part of the result)"))
    else:
        same = reapply_now == reapply_before
        out.append(("reapply", "PASS" if same else "FAIL",
                    "reapply_edits.json md5 %s %s" % (reapply_now, "= before (nothing left kept)" if same else "differs from before " + reapply_before)))

    verdicts = {v for _, v, _ in out}
    return ("FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")), out


def report(text, since, until="", player="", weight_before=None, reapply_now=None, reapply_before=None):
    result, points = check(text, since, until, player, weight_before, reapply_now, reapply_before)
    for name, verdict, detail in points:
        print("%-8s %-12s %s" % (name, verdict, detail))
    print("RESULT %s" % result)
    print("MANUAL: S1 Profile page has the sections and no tabs; S3 Positions page has overall, positions, style, roles, attributes, PlayStyles; "
          "S5 after the two Undo presses both fields show their first values.")
    return {"PASS": 0, "FAIL": 1, "INCONCLUSIVE": 2}[result]


def _log(*rows):
    return "".join("2026-10-19 15:2%d:00.000  %s\n" % (i, r) for i, r in enumerate(rows))


def self_test():
    # the real line shapes: App::edit_field "<words>  [players.f = v]", App::undo "undo players.f = v" (ui/app.cpp)
    good = _log("Turbo GUI loading in process 111",
                "Weight changed from 74 to 75: Ali Test  [players.weight = 75]",
                "PlayStyle changed: Ali Test  [players.trait1 = 5]",
                "undo players.trait1 = 1",
                "undo players.weight = 74")
    cases = [
        ("good", dict(text=good), "PASS"),
        ("good with starting weight", dict(text=good, weight_before=74), "PASS"),
        ("wrong starting weight", dict(text=good, weight_before=70), "FAIL"),
        ("nothing written", dict(text=_log("hello")), "INCONCLUSIVE"),
        ("no lines in the window", dict(text=good, since="2026-10-19 16:00:00"), "FAIL"),
        ("two bits", dict(text=good.replace("trait1 = 1", "trait1 = 0")), "FAIL"),
        ("high bit", dict(text=good.replace("[players.trait1 = 5]", "[players.trait1 = %d]" % ((1 << 40) + 1)).replace("undo players.trait1 = 1", "undo players.trait1 = 1")), "PASS"),
        ("undo of weight not written", dict(text=good.replace("undo players.weight = 74", "undo players.weight = 75")), "FAIL"),
        ("wrong order", dict(text=_log("Weight  [players.weight = 75] Ali Test", "PlayStyle  [players.trait1 = 5] Ali Test", "undo players.weight = 74", "undo players.trait1 = 1")), "FAIL"),
        ("stray field", dict(text=good + "2026-10-19 15:29:00.000  Potential Ali Test  [players.potential = 80]\n"), "FAIL"),
        ("frame error", dict(text=good + "2026-10-19 15:29:00.000  frame error: boom\n"), "FAIL"),
        ("other player filtered", dict(text=good, player="Someone Else"), "FAIL"),
        ("window keeps other tracks out", dict(text=good + "2026-10-19 15:50:00.000  Wage  [players.wage = 9]\n", until="2026-10-19 15:30:00"), "PASS"),
        ("kept row unchanged", dict(text=good, reapply_now="aa", reapply_before="aa"), "PASS"),
        ("kept row left behind", dict(text=good, reapply_now="bb", reapply_before="aa"), "FAIL"),
    ]
    bad = 0
    for name, kw, want in cases:
        kw = dict(kw)
        text = kw.pop("text")
        kw.setdefault("since", "2026-10-19 15:20:00")
        kw.setdefault("player", "Ali Test")
        got, _ = check(text, **kw)
        print("%-30s want %-12s got %-12s %s" % (name, want, got, "ok" if got == want else "WRONG"))
        bad += got != want
    print("SELF-TEST %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default=r"C:\FC 27 Live Editor\turbo_output\turbo_gui.log")
    ap.add_argument("--player", default="")
    ap.add_argument("--since", default="")
    ap.add_argument("--until", default="")
    ap.add_argument("--weight-before", type=int, default=None)
    ap.add_argument("--reapply", default="")
    ap.add_argument("--reapply-before", default=None)
    ap.add_argument("--md5", default="", help="print the md5 of this file (run before the launch) and stop")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if a.md5:
        print(md5_of(a.md5))
        return 0
    if not a.player or not a.since:
        print("RESULT FAIL (--player and --since are required)")
        return 1
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
        now = md5_of(a.reapply) if a.reapply else None
    except OSError as e:
        print("RESULT FAIL (cannot read a file: %s)" % e)
        return 1
    return report(text, a.since, a.until, a.player, a.weight_before, now, a.reapply_before)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
