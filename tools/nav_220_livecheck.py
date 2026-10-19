"""nav_220_livecheck.py - the 2.20 IA wiring chunk 1 live check (brief 2.20-IA-wiring): the empty "Faces & looks" placeholder pages are gone and the
Appearance Editor opens. About 3 minutes, one LEICESTER launch by the Tester's runner; this script only READS turbo_gui.log (never the game,
the install or turbo_output beyond that one file) and prints PASS / FAIL per point and one RESULT line.

The operator's steps (Turbo window open in the career hub):
  1. Open Players (it opens on Edit a player). Stay 2 s.
  2. Click the Appearance Editor tab (it is its own tab, no second row of page buttons). Stay 3 s. A screenshot of the tab bar.
  3. Click the Players tab again (this makes the Appearance Editor page "left", which is what writes its line in the log).
  4. Click the other tabs once each if there is time (Kits & club, Tactics, Career): nothing else is required.

What it reads: the frame-cost lines of core/frame_cost.h ("Turbo frame cost on <page label> (left): ..."), one for every page left after 60 frames,
named by the label the page strip shows. So the log is the list of pages the GUI really drew.
  edit       a line for "Edit a player"
  appearance a line for "Appearance Editor"
  gone       NO line for "Faces & hair", "Body, tattoos & gear" or "Looks studio" (the 2.19 names)
  errors     no "frame error:" line in the session

  py -3 -I tools/nav_220_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "YYYY-MM-DD HH:MM:SS"
  py -3 -I tools/nav_220_livecheck.py --self-test
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui_revamp_livecheck import _cost, pages_seen, report, session_lines  # noqa: E402

OLD_NAMES = ("Faces & hair", "Body, tattoos & gear", "Looks studio")


def check(text, since=""):
    lines = session_lines(text, since)
    if not lines:
        return "FAIL", [("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start"))]
    seen = pages_seen(lines)
    out = [("session", "PASS", "%d log lines; pages drawn: %s" % (len(lines), ", ".join(sorted(seen)) or "none"))]
    for point, page, how in (("edit", "Edit a player", "open Players and stay 2 s, then switch to another tab"),
                             ("appearance", "Appearance Editor", "click the Appearance Editor tab, stay 3 s, then click the Players tab")):
        out.append((point, "PASS" if page in seen else "FAIL",
                    "page '%s' was drawn for at least 60 frames" % page if page in seen else "no frame-cost line for '%s': %s" % (page, how)))
    old = [n for n in OLD_NAMES if n in seen]
    out.append(("gone", "FAIL" if old else "PASS", ("the old page(s) still drawn: " + ", ".join(old)) if old else "none of the 2.19 placeholder names was drawn"))
    err = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if err else "PASS", ("%d 'frame error:' line(s), first: %s" % (len(err), err[0][24:160])) if err else "no draw exception in the session"))
    v = [x for _, x, _ in out]
    return ("FAIL" if "FAIL" in v else "PASS"), out


def self_test():
    head = "2026-10-19 15:10:01.000  Turbo GUI loading in process 77\n"
    cases = [("both pages drawn, no placeholder", head + _cost("15:11:00.000", "Edit a player") + _cost("15:11:05.000", "Appearance Editor"), "PASS"),
             ("no Appearance Editor line (never left)", head + _cost("15:11:00.000", "Edit a player"), "FAIL"),
             ("an old 2.19 build (Faces & hair drawn)", head + _cost("15:11:00.000", "Edit a player") + _cost("15:11:05.000", "Faces & hair") + _cost("15:11:09.000", "Appearance Editor"), "FAIL"),
             ("a draw exception", head + _cost("15:11:00.000", "Edit a player") + _cost("15:11:05.000", "Appearance Editor") + "2026-10-19 15:11:06.000  frame error: assert\n", "FAIL"),
             ("no session", "nothing\n", "FAIL")]
    bad = 0
    for name, text, want in cases:
        got, out = check(text)
        ok = got == want
        bad += 0 if ok else 1
        print(("  ok  " if ok else "  FAIL  ") + "nav_220_livecheck: %s (%s)" % (name, got))
        if not ok:
            report(got, out)
    print("SELF-TEST %s" % ("PASSED" if not bad else "FAILED"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    with open(a.log, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    result, out = check(text, a.since)
    report(result, out)
    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
