"""nav_220_livecheck.py - the 2.20 IA wiring live check (brief 2.20-IA-wiring, chunks 1 and 2): the empty "Faces & looks" placeholder pages are gone, the
Appearance Editor opens, Edit a player has its four tabs, and Player creation, Bulk edit, Team > Players and Team > Transfers open. About 3 minutes, one
LEICESTER launch by the Tester's runner; this script only READS turbo_gui.log (never the game, the install or turbo_output beyond that one file) and prints
PASS / FAIL per point and one RESULT line.

The operator's steps, 11 clicks (the Turbo window open in the career hub; stay about 2 s on each, the page must be shown for 60 frames):
  1. Players tab (it opens on Edit a player, tab Profile).
  2. Tab "Positions, attributes & PlayStyles", then tab "Appearance", then tab "Classic editor" (was All fields; the tabs inside Edit a player).
  3. Players page strip: "Player creation", then "Bulk edit".
  4. The tab "Appearance Editor" (its own tab, no second row of page buttons).
  5. The tab "Team", then in its strip "Players", then "Transfers".
  6. Any other tab (Career): this makes Team > Transfers "left", which is what writes its line in the log.
A screenshot of the Team strip (Kits, Club name, Players, Transfers, Stadium) if there is time.

What it reads: the frame-cost lines of core/frame_cost.h ("Turbo frame cost on <page label> (left): ..."), one for every page left after 60 frames,
named by the label the page strip shows, and the line "Edit a player tab: <name>" that the page writes when a tab is shown. So the log is the list
of pages and tabs the GUI really drew.
  tabs       a line for each of: Profile, Positions, attributes & PlayStyles, Appearance, Classic editor
  pages      a frame-cost line for Edit a player, Player creation, Bulk edit, Appearance Editor, Players (Team), Transfers (Team)
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
TABS = ("Profile", "Positions, attributes & PlayStyles", "Appearance", "Classic editor")
PAGES = (("edit", "Edit a player", "click the Players tab and stay 2 s, then click the next tab"),
         ("creation", "Player creation", "Players strip: click Player creation, stay 2 s, then click Bulk edit"),
         ("bulk", "Bulk edit", "Players strip: click Bulk edit, stay 2 s, then click the Appearance Editor tab"),
         ("appearance", "Appearance Editor", "click the Appearance Editor tab, stay 2 s, then click the Team tab"),
         ("team_players", "Players", "Team tab, strip: click Players, stay 2 s, then click Transfers"),
         ("team_transfers", "Transfers", "Team tab, strip: click Transfers, stay 2 s, then click the Career tab"))


def check(text, since=""):
    lines = session_lines(text, since)
    if not lines:
        return "FAIL", [("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start"))]
    seen = pages_seen(lines)
    out = [("session", "PASS", "%d log lines; pages drawn: %s" % (len(lines), ", ".join(sorted(seen)) or "none"))]
    body = "\n".join(lines)
    for tab in TABS:
        ok = ("Edit a player tab: " + tab) in body
        out.append(("tab " + tab, "PASS" if ok else "FAIL",
                    "tab '%s' of Edit a player was shown" % tab if ok else "no 'Edit a player tab: %s' line: click that tab inside Edit a player" % tab))
    for point, page, how in PAGES:
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
    tabs = "".join("2026-10-19 15:10:%02d.000  Edit a player tab: %s\n" % (2 + i, t) for i, t in enumerate(TABS))
    pages = "".join(_cost("15:11:%02d.000" % (i + 1), p) for i, (_, p, _) in enumerate(PAGES))
    cases = [("every tab and page", head + tabs + pages, "PASS"),
             ("a tab never shown", head + tabs.replace("Appearance", "Nothing") + pages, "FAIL"),
             ("Bulk edit never drawn", head + tabs + pages.replace("Bulk edit", "Elsewhere"), "FAIL"),
             ("Team > Transfers never left", head + tabs + pages.replace("Transfers", "Elsewhere"), "FAIL"),
             ("the chunk 1 build (no tab lines)", head + pages, "FAIL"),
             ("an old 2.19 build (Faces & hair drawn)", head + tabs + pages + _cost("15:12:00.000", "Faces & hair"), "FAIL"),
             ("a draw exception", head + tabs + pages + "2026-10-19 15:11:06.000  frame error: assert\n", "FAIL"),
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
