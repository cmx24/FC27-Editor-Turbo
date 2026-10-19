"""club_fields_livecheck.py - the 2.20 K3 live check (brief 2.20-K3-club-fields, W2): reads turbo_gui.log after one LEICESTER launch and
prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the install or turbo_output
(it only reads the log file and, when asked, hashes the kept-edits file it is given).

  py -3 -I tools/club_fields_livecheck.py --md5 "C:/FC 27 Live Editor/turbo_output/reapply_edits.json"       BEFORE the launch: prints the md5
  py -3 -I tools/club_fields_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-19 15:00:00" \\
        --reapply "C:/FC 27 Live Editor/turbo_output/reapply_edits.json" --reapply-before <md5 printed above> [--team ID]
  py -3 -I tools/club_fields_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  1. Career hub, Turbo window: Club & Kits > Stadium & extras. Open "Crowd & presentation".
  2. "Tifo display": pick Style 1 (a stock Leicester has Style 2). Screenshot S1.
  3. "Goal flame jets": pick Style 2 (a stock Leicester has Off). Screenshot S2.
  4. Optional: "Undo last change" once, then pick Style 2 again. Optional visual: play to kick-off and look for the tifo.
  5. Kill the game (the runner). Never save.

What it reads (ui/ui_club_fields.cpp write_club_field, one line per write, in turbo_gui.log and the in-app Log):
  Club field: teams.hastifo team <id> = 1 (was 0), read back 1
  session   the last "Turbo GUI loading in process" session at or after --since
  hastifo   a line with value 1, a different value before it, read back 1 (the live database row equals what was written; the stock
            file has Leicester (team 95) at tifo 2, the brief's "0" was a guess: the check only needs a change to 1)
  flame     a line with teams.stanchionflamethrower value 2, changed from another value, read back 2
  sameteam  both lines name the same club (and it is --team when given)
  keys      no other "Club field:" write than those two (an Undo and a rewrite of the same value are allowed, they end on the same value)
  reapply   reapply_edits.json is byte-identical to --reapply-before (the club columns are kept by the career save itself, not by a
            reapply row: the brief's "reapply row holds exactly those keys" is blocker 1 of the board row; pass --reapply-keys
            hastifo,stanchionflamethrower once a reapply row type exists). INCONCLUSIVE when --reapply-before is not given.
  errors    no "frame error:" and no "ERROR: ... Club field" line in the session
"""
import argparse
import hashlib
import json
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_FIELD = re.compile(r"Club field(?P<undone> undone)?: (?P<table>\w+)\.(?P<field>\w+) team (?P<team>\d+) = (?P<value>-?\d+)(?: \(was (?P<was>-?\d+)\))?, read back (?P<back>-?\d+|nothing)")


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


def writes(lines):
    """[(index, undone, table, field, team, value, was, back)] in log order"""
    out = []
    for i, l in enumerate(lines):
        m = RE_FIELD.search(l)
        if m:
            out.append((i, bool(m.group("undone")), m.group("table"), m.group("field"), m.group("team"), int(m.group("value")),
                        None if m.group("was") is None else int(m.group("was")), m.group("back")))
    return out


def md5_file(path):
    h = hashlib.md5()
    try:
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
    except FileNotFoundError:
        return "absent"
    return h.hexdigest()


def reapply_keys(path):
    """The set of field names the kept-edits file holds for teams and stadiums rows, for --reapply-keys"""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    found = set()

    def walk(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k in ("hastifo", "stanchionflamethrower"):
                    found.add(k)
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(data)
    return found


def check(text, since="", team="", reapply_path="", reapply_before="", want_keys=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))
    ws = writes(lines)
    live = [w for w in ws if not w[1]]

    def last_write(field, value):
        hit = [w for w in live if w[3] == field and w[5] == value]
        return hit[-1] if hit else None

    def judge(point, field, value, label):
        w = last_write(field, value)
        if not w:
            seen = [x for x in live if x[3] == field]
            out.append((point, "FAIL", "no 'Club field: teams.%s ... = %d' line%s: the %s was not changed" %
                        (field, value, (" (saw value %d)" % seen[-1][5]) if seen else "", label)))
            return None
        if w[7] != str(value):
            out.append((point, "FAIL", "written %d but the live row read back %s: %s" % (value, w[7], lines[w[0]].strip()[:160])))
            return None
        if w[6] is not None and w[6] == value:
            out.append((point, "FAIL", "the row already held %d before the write: not a change (was %d)" % (value, w[6])))
            return None
        out.append((point, "PASS", "teams.%s team %s: %s -> %d, live row reads %d" % (field, w[4], "?" if w[6] is None else w[6], value, value)))
        return w

    a = judge("hastifo", "hastifo", 1, "tifo display")
    b = judge("flame", "stanchionflamethrower", 2, "goal flame jets")
    if a and b:
        ok = a[4] == b[4] and (not team or a[4] == team)
        out.append(("sameteam", "PASS" if ok else "FAIL",
                    "both writes are for team %s" % a[4] if ok else "tifo for team %s, flames for team %s, wanted %s" % (a[4], b[4], team or "the same club")))
    extra = sorted({(w[2], w[3]) for w in ws} - {("teams", "hastifo"), ("teams", "stanchionflamethrower")})
    out.append(("keys", "FAIL" if extra else "PASS",
                ("other fields were written: %s" % ", ".join("%s.%s" % e for e in extra)) if extra else "only hastifo and stanchionflamethrower were written"))
    if want_keys:
        found = reapply_keys(reapply_path)
        want = set(k for k in want_keys.split(",") if k)
        out.append(("reapply", "PASS" if found == want else "FAIL", "the kept-edits file holds %s, wanted %s" % (sorted(found) if found is not None else "nothing readable", sorted(want))))
    elif reapply_before:
        now = md5_file(reapply_path)
        out.append(("reapply", "PASS" if now == reapply_before else "FAIL",
                    "reapply_edits.json md5 %s (%s)" % (now, "unchanged: club columns are kept by the career save, no row" if now == reapply_before else "CHANGED from " + reapply_before)))
    else:
        out.append(("reapply", "INCONCLUSIVE", "no --reapply-before md5 given (run --md5 on the file before the launch)"))
    errs = [l for l in lines if "frame error:" in l or ("ERROR:" in l and "Club field" in l)]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0].strip()[:200] if errs else "no frame error and no refused club field write"))
    verdicts = [v for _, v, _ in out]
    result = "FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-9s %-12s %s" % (point, verdict, detail))
    print("RESULT %s" % result)
    return 0 if result == "PASS" else 1


def _log(*body):
    head = ["2026-10-19 15:00:01 Turbo GUI loading in process 1234"]
    return "\n".join(head + ["2026-10-19 15:00:%02d %s" % (10 + i, b) for i, b in enumerate(body)]) + "\n"


TIFO = "Club field: teams.hastifo team 241 = 1 (was 0), read back 1"
FLAME = "Club field: teams.stanchionflamethrower team 241 = 2 (was 0), read back 2"


def self_test():
    import os
    import tempfile
    tmp = tempfile.mkdtemp()
    p = os.path.join(tmp, "reapply_edits.json")
    with open(p, "w") as f:
        f.write('{"version": 1}')
    m0 = md5_file(p)
    cases = [
        ("pass: both writes read back, store unchanged", _log(TIFO, FLAME), dict(reapply_path=p, reapply_before=m0), "PASS"),
        ("pass: undo and rewrite end on the value", _log(TIFO, FLAME, "Club field undone: teams.stanchionflamethrower team 241 = 0, read back 0",
                                                      "Club field: teams.stanchionflamethrower team 241 = 2 (was 0), read back 2"), dict(reapply_path=p, reapply_before=m0), "PASS"),
        ("inconclusive: no store md5", _log(TIFO, FLAME), dict(), "INCONCLUSIVE"),
        ("fail: flame jets not changed", _log(TIFO), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: read back differs", _log(TIFO, FLAME.replace("read back 2", "read back 0")), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: another field written", _log(TIFO, FLAME, "Club field: teams.ballid team 241 = 55 (was 0), read back 55"), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: two clubs", _log(TIFO, FLAME.replace("team 241", "team 7")), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: wrong club", _log(TIFO, FLAME), dict(team="7", reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: value was already set", _log(TIFO.replace("was 0", "was 1"), FLAME), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("pass: the tifo was Style 2 before (Leicester in the stock file)", _log(TIFO.replace("was 0", "was 2"), FLAME), dict(reapply_path=p, reapply_before=m0), "PASS"),
        ("fail: a refused write", _log(TIFO, FLAME, "ERROR: Crowd skin tones: the game's data never uses 50. Choose one of Style 20 ... Club field"), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: frame error", _log(TIFO, FLAME, "frame error: boom"), dict(reapply_path=p, reapply_before=m0), "FAIL"),
        ("fail: no session", "2026-10-19 15:00:01 nothing\n", dict(), "FAIL"),
    ]
    bad = 0
    for name, text, kw, want in cases:
        got, _ = check(text, **kw)
        ok = got == want
        bad += 0 if ok else 1
        print("  %s %s (got %s, want %s)" % ("ok" if ok else "FAIL", name, got, want))
    with open(p, "w") as f:
        f.write('{"version": 1, "kits": {}}')
    got, _ = check(_log(TIFO, FLAME), reapply_path=p, reapply_before=m0)
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s a changed store fails the byte-identical point (got %s)" % ("ok" if ok else "FAIL", got))
    with open(p, "w") as f:
        f.write('{"teams": {"241": {"hastifo": 1, "stanchionflamethrower": 2}}}')
    got, _ = check(_log(TIFO, FLAME), reapply_path=p, want_keys="hastifo,stanchionflamethrower")
    ok = got == "PASS"
    bad += 0 if ok else 1
    print("  %s --reapply-keys: a row with exactly those keys passes (got %s)" % ("ok" if ok else "FAIL", got))
    got, _ = check(_log(TIFO, FLAME), since="2026-10-20 00:00:00", reapply_path=p, reapply_before=m0)
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --since skips older sessions (got %s)" % ("ok" if ok else "FAIL", got))
    print("SELF-TEST %s" % ("PASSED" if bad == 0 else "FAILED"))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="", help="local time 'YYYY-MM-DD HH:MM:SS' of the launch (the log's own clock)")
    ap.add_argument("--team", default="", help="the club's teamid the two writes must name (default: any, but the same for both)")
    ap.add_argument("--reapply", default="C:/FC 27 Live Editor/turbo_output/reapply_edits.json")
    ap.add_argument("--reapply-before", default="", help="md5 of the kept-edits file before the launch (--md5 prints it; 'absent' when the file did not exist)")
    ap.add_argument("--reapply-keys", default="", help="instead of byte-identical: the field names the kept-edits file must hold (only if a reapply row type exists)")
    ap.add_argument("--md5", metavar="FILE", help="print the md5 of FILE (or 'absent') and exit: run it before the launch")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.md5:
        print(md5_file(a.md5))
        return 0
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (a.log, e))
        print("RESULT FAIL")
        return 1
    return report(*check(text, a.since, a.team, a.reapply, a.reapply_before, a.reapply_keys))


if __name__ == "__main__":
    sys.exit(main())
