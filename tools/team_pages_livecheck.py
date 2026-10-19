"""team_pages_livecheck.py - the 2.20 ia-team live check (brief 2.20-IA-pages, row "team"): Team > Players and Team > Transfers show the
squad of the user's club. Reads turbo_gui.log after one LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT
line. Read-only: it never touches the game, the install or turbo_output (it only reads the log file it is given). About 3 minutes.

  py -3 -I tools/team_pages_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "YYYY-MM-DD HH:MM:SS" --expect N
  py -3 -I tools/team_pages_livecheck.py --self-test

The operator's steps (LEICESTER career hub, Turbo window open, never saved; the Tester's runner launches and kills the game):
  1. Team > Players. Stay 2 s. The Player tab lists the club's players; screenshot S1 (the list, the Contract card of the first player).
  2. Team > Transfers. Stay 2 s. Screenshot S2 (the Moves, Loans, Move a player, Contract extensions and Squad roles cards).
  3. Kill the game (the runner). Nothing is sent to the game, nothing is saved.

--expect N is the number of players LEICESTER has in the career: the Tester reads it before the launch (the old Players list filtered
by the club, or the club's squad in the career hub) and passes it here; the check prints what the pages logged and compares.
Without --expect the counts are printed and the point is INCONCLUSIVE (the two pages must still agree with each other).

What it reads (ui/ui_team_pages.h pages, App::log lines written once per change):
  session    the last "Turbo GUI loading in process" session at or after --since
  players    "Team page Players: club <id> (<name>), <N> players"; the club name contains --club (default Leicester)
  transfers  "Team page Transfers: club <id> (<name>), <N> players"; same club
  agree      both pages list the same club and the same number of players
  expected   that number equals --expect
  errors     no "frame error:" line in the session
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_PAGE = re.compile(r"Team page (?P<page>Players|Transfers): club (?P<id>\d+) \((?P<name>.*)\), (?P<n>\d+) players")


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


def last_page_line(lines, page, club):
    """(club id, name, count) of the last line of `page` whose club name contains `club`; None when there is none"""
    found = None
    for line in lines:
        m = RE_PAGE.search(line)
        if m and m.group("page") == page and club.lower() in m.group("name").lower():
            found = (int(m.group("id")), m.group("name"), int(m.group("n")))
    return found


def check(text, since="", club="Leicester", expect=None):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    if not lines:
        return "FAIL", [("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start"))]
    out = [("session", "PASS", "%d log lines" % len(lines))]
    got = {}
    for page, how in (("Players", "open Team > Players and stay 2 s"), ("Transfers", "open Team > Transfers and stay 2 s")):
        g = last_page_line(lines, page, club)
        got[page] = g
        key = page.lower()
        if g:
            out.append((key, "PASS", "club %d (%s): %d players" % g))
        else:
            out.append((key, "FAIL", "no 'Team page %s' line for a club named like '%s': %s" % (page, club, how)))
    if got["Players"] and got["Transfers"]:
        same = got["Players"][0] == got["Transfers"][0] and got["Players"][2] == got["Transfers"][2]
        out.append(("agree", "PASS" if same else "FAIL",
                    "both pages: club %d, %d players" % (got["Players"][0], got["Players"][2]) if same
                    else "Players %s vs Transfers %s" % (got["Players"], got["Transfers"])))
        n = got["Players"][2]
        if expect is None:
            out.append(("expected", "INCONCLUSIVE", "logged %d players; pass --expect N (the club's player count read before the launch)" % n))
        else:
            out.append(("expected", "PASS" if n == expect and got["Transfers"][2] == expect else "FAIL",
                        "expected %d, Players page %d, Transfers page %d" % (expect, n, got["Transfers"][2])))
    else:
        out.append(("agree", "FAIL", "a page was not drawn"))
        out.append(("expected", "FAIL", "a page was not drawn"))
    err = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if err else "PASS", ("%d 'frame error:' line(s), first: %s" % (len(err), err[0][24:160])) if err else "no draw exception in the session"))
    v = [x for _, x, _ in out]
    return ("FAIL" if "FAIL" in v else "INCONCLUSIVE" if "INCONCLUSIVE" in v else "PASS"), out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-12s %-12s %s" % (point, verdict, detail))
    print("RESULT %s" % result)


def self_test():
    head = "2026-10-19 15:10:01.000  Turbo GUI loading in process 77\n"
    pl = "2026-10-19 15:11:00.000  Team page Players: club 14 (Leicester City), 26 players\n"
    tr = "2026-10-19 15:11:30.000  Team page Transfers: club 14 (Leicester City), 26 players\n"
    cases = [("both pages, count as expected", head + pl + tr, 26, "PASS"),
             ("both pages, no expected count", head + pl + tr, None, "INCONCLUSIVE"),
             ("wrong count", head + pl + tr, 25, "FAIL"),
             ("Transfers never opened", head + pl, 26, "FAIL"),
             ("pages disagree", head + pl + tr.replace("26 players", "24 players"), 26, "FAIL"),
             ("another club only", head + pl.replace("Leicester City", "Everton") + tr.replace("Leicester City", "Everton"), 26, "FAIL"),
             ("a draw exception", head + pl + tr + "2026-10-19 15:11:40.000  frame error: assert\n", 26, "FAIL"),
             ("no session", "nothing\n", 26, "FAIL")]
    bad = 0
    for name, text, expect, want in cases:
        got, out = check(text, expect=expect)
        ok = got == want
        bad += 0 if ok else 1
        print(("  ok  " if ok else "  FAIL  ") + "team_pages_livecheck: %s (%s)" % (name, got))
        if not ok:
            report(got, out)
    print("SELF-TEST %s" % ("PASSED" if not bad else "FAILED"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="")
    ap.add_argument("--club", default="Leicester")
    ap.add_argument("--expect", type=int, default=None)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("RESULT FAIL (cannot read %s: %s)" % (a.log, e))
        return 1
    result, out = check(text, a.since, a.club, a.expect)
    report(result, out)
    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
