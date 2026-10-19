"""gui_revamp_livecheck.py - the 2.16 GUI revamp live check, steps 2 (chrome) and 4 (navigation)
(research_20261017/GUI_REVAMP2_LIVECHECK.md): reads turbo_gui.log after one LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per
point and one RESULT line. Read-only: it never touches the game, the install or turbo_output (it only reads the log file it is given).

  py -3 -I tools/gui_revamp_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-17 21:30:00"
  py -3 -I tools/gui_revamp_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

What it reads (the lines come from core/frame_cost.h, written by win/overlay_dx12.cpp: one line per page left after 60 frames, named by the
page label; App::notify, which logs every toast):
  session   the last "Turbo GUI loading in process" session at or after --since
  reload    "Lists reloaded from the game" (step 2: the button is now Settings > Reload from game; the old top-bar Refresh said
            "Lists refreshed from the game"): PASS; only the old text = FAIL (the old build is installed); nothing = FAIL (not pressed)
  changes   a frame-cost line for the page "Changes & undo" (step 2: the Changes button in the top bar opened it and it stayed 1 s)
  settings  a frame-cost line for "Settings" (step 2: the page that now holds Refresh, the show/hide key, the version and the Advanced switch)
  looks     a frame-cost line for "Appearance Editor" (2.20: the area that was Faces & looks; before 2.20 the page was "Faces & hair")
  players   a frame-cost line for "Edit a player" (step 4: the renamed first page of Players; the old build says "Player edit")
  errors    no "frame error:" line in the session (a draw exception in any new page)
The operator also takes ONE screenshot of the top bar (the sheet lists what it must show): the slim status line and the Changes button
cannot be read from the log.
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_COST = re.compile(r"Turbo frame cost on (?P<page>.+?) \((?P<why>left|so far)\): (?P<ms>[\d.]+) ms per frame")


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


def pages_seen(lines):
    return {m.group("page") for m in (RE_COST.search(l) for l in lines) if m}


def check(text, since=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    body = "\n".join(lines)
    seen = pages_seen(lines)
    out.append(("session", "PASS", "%d log lines; pages with a frame-cost line: %s" % (len(lines), ", ".join(sorted(seen)) or "none")))
    # reload
    if "Lists reloaded from the game" in body:
        out.append(("reload", "PASS", "Settings > Reload from game was pressed and the lists were read again"))
    elif "Lists refreshed from the game" in body:
        out.append(("reload", "FAIL", "the old text 'Lists refreshed from the game': the old top-bar Refresh ran, so this is not the revamp build"))
    else:
        out.append(("reload", "FAIL", "no 'Lists reloaded from the game' line: press Reload from game on the Settings page"))
    for point, page, how in (
            ("changes", "Changes & undo", "click Changes in the top bar and stay 2 s on the page"),
            ("settings", "Settings", "open the Settings tab and stay 2 s"),
            ("looks", "Appearance Editor", "open the Appearance Editor tab and stay 2 s (2.20; the page exists only in the revamp build)"),
            ("players", "Edit a player", "open Players > Edit a player and stay 2 s (the old build calls it Player edit)")):
        if page in seen:
            out.append((point, "PASS", "page '%s' was shown for at least 60 frames" % page))
        elif point in ("looks", "players") and ("Player edit" in seen or "Squad" in seen):
            out.append((point, "FAIL", "the old page names are in the log: this is not the step 4 build"))
        else:
            out.append((point, "FAIL", "no frame-cost line for '%s': %s, then switch to another page" % (page, how)))
    errors = [l for l in lines if "frame error:" in l]
    if errors:
        out.append(("errors", "FAIL", "%d 'frame error:' line(s), first: %s" % (len(errors), errors[0][24:160])))
    else:
        out.append(("errors", "PASS", "no draw exception in the session"))
    verdicts = [v for _, v, _ in out]
    result = "FAIL" if "FAIL" in verdicts else "INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS"
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-10s %-12s %s" % (point, verdict, detail))
    print("RESULT: %s" % result)


# ---------------------------------------------------------------- self-test (synthetic logs, never the game)
def _cost(t, page, why="left"):
    return ("2026-10-17 %s  Turbo frame cost on %s (%s): 0.9 ms per frame (App::tick 0.1, input + draw + render 0.8), max 4.0 ms, "
            "0 frames over 50 ms, 240 frames\n" % (t, page, why))


GOOD = ("2026-10-17 21:30:01.000  Turbo GUI loading in process 1234\n" + _cost("21:31:00.000", "Changes & undo") + _cost("21:31:05.000", "Settings") +
        "2026-10-17 21:31:06.000  Lists reloaded from the game\n" + _cost("21:31:09.000", "Appearance Editor") + _cost("21:31:12.000", "Edit a player"))
OLD_BUILD = ("2026-10-17 21:30:01.000  Turbo GUI loading in process 1234\n" + _cost("21:31:00.000", "Player edit") + _cost("21:31:05.000", "Settings") +
             "2026-10-17 21:31:06.000  Lists refreshed from the game\n" + _cost("21:31:09.000", "Squad"))
NOT_PRESSED = GOOD.replace("Lists reloaded from the game", "something else")
BROKEN = GOOD + "2026-10-17 21:31:20.000  frame error: ImGui assertion failed: x (imgui.cpp:1)\n"
TWO_SESSIONS = OLD_BUILD.replace("21:3", "20:3") + GOOD


def self_test():
    cases = [
        ("a session that used every new place passes", GOOD, "", "PASS", {"reload": "PASS", "changes": "PASS", "looks": "PASS"}),
        ("the old build fails (old Refresh text, old page names)", OLD_BUILD, "", "FAIL", {"reload": "FAIL", "looks": "FAIL", "players": "FAIL"}),
        ("Reload from game never pressed fails", NOT_PRESSED, "", "FAIL", {"reload": "FAIL"}),
        ("a draw exception fails", BROKEN, "", "FAIL", {"errors": "FAIL"}),
        ("--since picks the session that started after it", TWO_SESSIONS, "2026-10-17 21:00:00", "PASS", {}),
        ("--since after every session: no session, FAIL", GOOD, "2026-10-17 23:00:00", "FAIL", {"session": "FAIL"}),
    ]
    failed = 0
    for name, text, since, want, points in cases:
        result, out = check(text, since)
        got = {p: v for p, v, _ in out}
        ok = result == want and all(got.get(p) == v for p, v in points.items())
        failed += 0 if ok else 1
        print(("  ok  " if ok else "  FAIL  ") + "gui_revamp_livecheck: %s (%s)" % (name, result))
        if not ok:
            report(result, out)
    print("SELF-TEST %s" % ("PASSED" if failed == 0 else "FAILED"))
    return 0 if failed == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="", help="local time 'YYYY-MM-DD HH:MM:SS' of the launch (the log's own clock)")
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
