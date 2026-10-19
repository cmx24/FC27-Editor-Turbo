"""create_prefill_livecheck.py - the 2.19 create-prefill live check (brief 2.19-create-prefill): reads turbo_gui.log after one LEICESTER
launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the install or
turbo_output (it only reads the log file it is given).

  py -3 -I tools/create_prefill_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-18 23:30:00" [--player ID]
  py -3 -I tools/create_prefill_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  1. Career hub, Turbo window: Players. Click a real player of another club (not a Turbo-made one). Click "Create player...".
     Screenshot S1: every box shows his data (names, birth date, overall, potential, positions, foot, height, weight; open
     "His attributes (edit any)": his 34 attributes).
  2. Club: Free Agents (111592). Change nothing else. Click "Create player". Wait for the toast. Screenshot S2.
  3. Same player again: "Create player...", type a new Potential (any other value), Club Free Agents, "Create player". Screenshot S3.
  4. Optional: Players, search the two new players (the highest ids), open Edit a player for one of them next to the original and
     compare. Kill the game (the runner). Never save.

What it reads (ui/ui_presets.cpp App::log lines, features/create_player.lua's summary in the result toast):
  session   the last "Turbo GUI loading in process" session at or after --since
  noedit    "Create player from <id>: 0 changed field(s), names kept" (the dialog sent nothing but the template and the club)
  copy      the result after it: "Create player: new player N ...; copy check: X of X columns equal player <id>, 0 of 0 set field(s) as
            written, names copied" with no "DIFFER" (every column of the new row, read back in game, equals the template's)
  oneedit   "Create player from <id>: 1 changed field(s)" and its result "1 of 1 set field(s) as written" with no "DIFFER"
            (INCONCLUSIVE when step 3 was not done)
  errors    no "frame error:" line in the session
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_SENT = re.compile(r"Create player from (?P<id>\d+): (?P<n>\d+) changed field\(s\), names (?P<names>kept|changed)")
RE_RESULT = re.compile(r"Create player: (?P<text>.*)$")
RE_COPY = re.compile(r"copy check: (?P<same>\d+) of (?P<n>\d+) columns equal player (?P<id>\d+), (?P<sok>\d+) of (?P<sn>\d+) set field\(s\) as written")


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


def result_after(lines, i):
    """(index, text) of the first create_player result line after line i (the Lua summary or its refusal); (-1, '') when none"""
    for k in range(i + 1, len(lines)):
        m = RE_RESULT.search(lines[k])
        if m and ("new player" in m.group("text") or "ERROR" in lines[k]):
            return k, m.group("text")
    return -1, ""


def judge_copy(text, tpl, want_set):
    m = RE_COPY.search(text)
    if "new player" not in text:
        return "FAIL", "no new player: " + text[:200]
    if not m:
        return "FAIL", "no copy check in the result (an old Lua side, or not a copy of a database player): " + text[:200]
    if m.group("id") != tpl:
        return "FAIL", "copy check of player %s, the dialog's template was %s" % (m.group("id"), tpl)
    if "DIFFER" in text:
        return "FAIL", text[text.find("DIFFER"):][:200]
    if m.group("same") != m.group("n") or int(m.group("n")) < 1:
        return "FAIL", "%s of %s columns equal" % (m.group("same"), m.group("n"))
    if int(m.group("sn")) != want_set or m.group("sok") != m.group("sn"):
        return "FAIL", "%s of %s set field(s) as written, want %d" % (m.group("sok"), m.group("sn"), want_set)
    if want_set == 0 and "names copied" not in text:
        return "FAIL", "the names were not copied: " + text[:200]
    return "PASS", "%s of %s columns equal player %s, %s set field(s) as written" % (m.group("same"), m.group("n"), tpl, m.group("sn"))


def check(text, since="", player=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))
    sent = [(i, RE_SENT.search(l)) for i, l in enumerate(lines)]
    sent = [(i, m) for i, m in sent if m and (not player or m.group("id") == player)]
    noedit = [(i, m) for i, m in sent if m.group("n") == "0" and m.group("names") == "kept"]
    if not noedit:
        out.append(("noedit", "FAIL", "no 'Create player from <id>: 0 changed field(s), names kept' line%s: step 2 not done, or the dialog "
                                      "sent fields nobody changed" % (" for id " + player if player else "")))
        if sent:
            out.append(("noedit", "FAIL", "sent instead: " + lines[sent[0][0]].strip()[:200]))
        return "FAIL", out
    i0, m0 = noedit[0]
    tpl = m0.group("id")
    out.append(("noedit", "PASS", "template %s, nothing but the template and the club sent" % tpl))
    k, res = result_after(lines, i0)
    if k < 0:
        out.append(("copy", "FAIL", "no create_player result after the click"))
    else:
        out.append(("copy",) + judge_copy(res, tpl, 0))
    one = [(i, m) for i, m in sent if m.group("n") == "1" and m.group("names") == "kept" and m.group("id") == tpl]
    if not one:
        out.append(("oneedit", "INCONCLUSIVE", "no create with one changed field of player %s (step 3 not done)" % tpl))
    else:
        k1, res1 = result_after(lines, one[0][0])
        if k1 < 0:
            out.append(("oneedit", "FAIL", "no create_player result after the one-field click"))
        else:
            out.append(("oneedit",) + judge_copy(res1, tpl, 1))
    errs = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0].strip()[:200] if errs else "no frame error in the session"))
    verdicts = [v for _, v, _ in out]
    result = "FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-8s %-12s %s" % (point, verdict, detail))
    print("RESULT %s" % result)
    return 0 if result == "PASS" else 1


def _log(*body):
    head = ["2026-10-18 23:30:01 Turbo GUI loading in process 1234"]
    return "\n".join(head + ["2026-10-18 23:30:%02d %s" % (10 + i, b) for i, b in enumerate(body)]) + "\n"


ID0 = "Create player: automatic player id"
SENT0 = "Create player from 4242: 0 changed field(s), names kept"
RES0 = ("Create player: new player 300001 Test Player at Free Agents (111592): copy of player 4242, shirt 1, position reserve, joined "
        "today; copy check: 149 of 149 columns equal player 4242, 0 of 0 set field(s) as written, names copied")
SENT1 = "Create player from 4242: 1 changed field(s), names kept"
RES1 = ("Create player: new player 300002 Test Player at Free Agents (111592): copy of player 4242, shirt 2, position reserve, joined "
        "today; copy check: 149 of 149 columns equal player 4242, 1 of 1 set field(s) as written, names copied")


def self_test():
    cases = [
        ("pass: no edit, then one edit", _log(ID0, SENT0, RES0, ID0, SENT1, RES1), "PASS"),
        ("inconclusive: step 3 not done", _log(ID0, SENT0, RES0), "INCONCLUSIVE"),
        ("fail: a column differs", _log(ID0, SENT0, RES0 + "; DIFFER: haircolorcode", ID0, SENT1, RES1), "FAIL"),
        ("fail: fewer columns equal", _log(ID0, SENT0, RES0.replace("149 of 149", "148 of 149"), ID0, SENT1, RES1), "FAIL"),
        ("fail: fields sent without an edit", _log(ID0, SENT0.replace(": 0 changed", ": 9 changed"), RES0), "FAIL"),
        ("fail: no copy check (old Lua side)", _log(ID0, SENT0, RES0.split("; copy check")[0]), "FAIL"),
        ("fail: names not copied", _log(ID0, SENT0, RES0.replace("names copied", "names NOT copied"), ID0, SENT1, RES1), "FAIL"),
        ("fail: refused", _log(ID0, SENT0, "ERROR: Create player: the club has no free shirt number"), "FAIL"),
        ("fail: one edit wrote more", _log(ID0, SENT0, RES0, ID0, SENT1, RES1.replace("1 of 1 set", "2 of 2 set")), "FAIL"),
        ("fail: frame error", _log(ID0, SENT0, RES0, ID0, SENT1, RES1, "frame error: boom"), "FAIL"),
        ("fail: no session", "2026-10-18 23:30:01 nothing\n", "FAIL"),
    ]
    bad = 0
    for name, text, want in cases:
        got, _ = check(text)
        ok = got == want
        bad += 0 if ok else 1
        print("  %s %s (got %s, want %s)" % ("ok" if ok else "FAIL", name, got, want))
    got, _ = check(_log(ID0, SENT0, RES0, ID0, SENT1, RES1), player="1111")
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --player filters the id (got %s)" % ("ok" if ok else "FAIL", got))
    got, _ = check(_log(ID0, SENT0, RES0, ID0, SENT1, RES1), since="2026-10-19 00:00:00")
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --since skips older sessions (got %s)" % ("ok" if ok else "FAIL", got))
    print("SELF-TEST %s" % ("PASSED" if bad == 0 else "FAILED"))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="", help="local time 'YYYY-MM-DD HH:MM:SS' of the launch (the log's own clock)")
    ap.add_argument("--player", default="", help="the template player's id (default: the first one created from)")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (a.log, e))
        print("RESULT FAIL")
        return 1
    return report(*check(text, a.since, a.player))


if __name__ == "__main__":
    sys.exit(main())
