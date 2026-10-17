"""gui_perf_livecheck.py - the 2.16 gui/perf live check (research_20261017/GUI_PERF_LIVECHECK.md): reads turbo_gui.log after one
LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the
install or turbo_output (it only reads the log file it is given).

  py -3 -I tools/gui_perf_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-17 21:30:00"
  py -3 -I tools/gui_perf_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

What it checks (the lines come from win/overlay_dx12.cpp and core/frame_cost.h of the gui/perf build):
  build     "mouse wheel hook ready (..." : the build under test has the 2.16 hook (else the old DLL is installed)
  hook      "mouse wheel hook installed (Turbo shown)" then "mouse wheel hook removed (Turbo hidden ...": the low-level mouse
            hook is in only while Turbo is shown (hidden: Windows routes no mouse event through Turbo)
  reinstall no "went quiet while the mouse moved: installed again" (Windows dropped a late hook: pointer lag); 1 = WARN
  lateness  the "mouse wheel (Turbo hidden): ... hook events N, average lateness ..., A at 50 ms or more, B at 200 ms or
            more, max M ms" line (written when Turbo is hidden): PASS when B == 0 and M < 200; INCONCLUSIVE when N == 0 (the
            mouse did not move while Turbo was shown)
  settings  "Turbo frame cost on Settings (...): X ms per frame ... N frames over 50 ms": every such line X <= 3.0 ms and
            N <= 1 (the page's first draw), and at most one "slow Turbo frame on Settings" line
  wheel     notches from the low-level hook > 0 (the operator scrolled while shown): WARN when 0
"""
import argparse
import re
import sys

SETTINGS_MAX_MS = 3.0
LATE_MAX_MS = 200

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_WHEEL = re.compile(r"mouse wheel(?P<when>[^:]*): (?P<ll>\d+) notches from the low-level hook, .*hook events (?P<n>\d+), "
                      r"average lateness (?P<avg>[\d.]+) ms, (?P<a>\d+) at \d+ ms or more, (?P<b>\d+) at \d+ ms or more, max (?P<max>\d+) ms")
RE_COST = re.compile(r"Turbo frame cost on (?P<page>.+?) \((?P<why>left|so far)\): (?P<ms>[\d.]+) ms per frame .* max (?P<max>[\d.]+) ms, "
                     r"(?P<slow>\d+) frames over")


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


def check(text, since=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    body = "\n".join(lines)
    # build
    if "mouse wheel hook ready (" in body:
        out.append(("build", "PASS", "gui/perf hook code is running (time-critical thread, hook only while shown)"))
    else:
        out.append(("build", "FAIL", "no 'mouse wheel hook ready' line: this is not the gui/perf build"))
    # hook only while shown
    first_in = body.find("mouse wheel hook installed (Turbo shown)")
    removed = body.find("mouse wheel hook removed (Turbo hidden", first_in if first_in >= 0 else 0)
    if first_in < 0:
        out.append(("hook", "FAIL", "the hook was never installed: Turbo not shown, or the old build"))
    elif removed < 0:
        out.append(("hook", "FAIL", "installed on show but never removed on hide (hide Turbo before running the check)"))
    else:
        out.append(("hook", "PASS", "installed when Turbo was shown, removed when it was hidden"))
    # reinstalls (Windows dropped a late hook)
    again = body.count("went quiet while the mouse moved: installed again")
    if again == 0:
        out.append(("reinstall", "PASS", "Windows never dropped the hook"))
    elif again == 1:
        out.append(("reinstall", "WARN", "1 reinstall (a SetCursorPos move without a real mouse event also triggers it)"))
    else:
        out.append(("reinstall", "FAIL", "%d reinstalls: the hook's thread was late past LowLevelHooksTimeout" % again))
    # lateness (last wheel stats line of the session)
    wheel = [m for m in (RE_WHEEL.search(l) for l in lines) if m]
    if not wheel:
        out.append(("lateness", "FAIL", "no 'mouse wheel ... hook events' stats line (hide Turbo once: the line is written on hide)"))
        notches = 0
    else:
        w = wheel[-1]
        n, b, mx, notches = int(w.group("n")), int(w.group("b")), int(w.group("max")), int(w.group("ll"))
        detail = "%d events, average %s ms, %s at 50+ ms, %d at 200+ ms, max %d ms" % (n, w.group("avg"), w.group("a"), b, mx)
        if n == 0:
            out.append(("lateness", "INCONCLUSIVE", "no mouse event while Turbo was shown: move the mouse over Turbo, then hide it"))
        elif b == 0 and mx < LATE_MAX_MS:
            out.append(("lateness", "PASS", detail))
        else:
            out.append(("lateness", "FAIL", detail))
    # Settings page frame cost
    costs = [m for m in (RE_COST.search(l) for l in lines) if m and m.group("page") == "Settings"]
    slow_settings = sum(1 for l in lines if "slow Turbo frame on Settings:" in l)
    if not costs:
        out.append(("settings", "FAIL", "no 'Turbo frame cost on Settings' line (stay 35 s on Settings, then switch page or hide)"))
    else:
        worst = max(float(m.group("ms")) for m in costs)
        slow = max(int(m.group("slow")) for m in costs)
        detail = "%d line(s), worst %.3f ms per frame, %d frame(s) over 50 ms in one stretch, %d slow-frame line(s)" % (
            len(costs), worst, slow, slow_settings)
        ok = worst <= SETTINGS_MAX_MS and slow <= 1 and slow_settings <= 1
        out.append(("settings", "PASS" if ok else "FAIL", detail))
    slow_other = [l for l in lines if "slow Turbo frame on" in l and "slow Turbo frame on Settings:" not in l]
    if slow_other:
        out.append(("other pages", "WARN", "%d slow-frame line(s) elsewhere, first: %s" % (len(slow_other), slow_other[0][24:160])))
    # wheel exercised
    if wheel:
        out.append(("wheel", "PASS" if notches > 0 else "WARN",
                    "%d notches from the hook while shown" % notches if notches > 0 else "no wheel notch while shown (scroll a list)"))
    verdicts = [v for _, v, _ in out]
    result = "FAIL" if "FAIL" in verdicts else "INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS"
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-12s %-12s %s" % (point, verdict, detail))
    print("RESULT: %s" % result)


# ---------------------------------------------------------------- self-test (synthetic logs, never the game)
GOOD = """2026-10-17 21:30:01.000  Turbo GUI loading in process 1234
2026-10-17 21:31:10.000  mouse wheel hook ready (low-level mouse hook on its own time-critical thread, installed only while Turbo is shown)
2026-10-17 21:32:00.000  mouse wheel hook installed (Turbo shown)
2026-10-17 21:32:31.000  Turbo frame cost on Player edit (left): 0.812 ms per frame (App::tick 0.101, input + draw + render 0.711), max 9.0 ms, 0 frames over 50 ms, 1800 frames
2026-10-17 21:33:01.000  Turbo frame cost on Settings (so far): 0.954 ms per frame (App::tick 0.090, input + draw + render 0.864), max 12.0 ms, 0 frames over 50 ms, 1800 frames
2026-10-17 21:33:05.000  Turbo frame cost on Settings (left): 0.901 ms per frame (App::tick 0.090, input + draw + render 0.811), max 4.0 ms, 0 frames over 50 ms, 240 frames
2026-10-17 21:33:05.000  mouse wheel (Turbo hidden): 7 notches from the low-level hook, 0 from raw input, 0 from window messages; hook alive, installed 1 times (Turbo shown), removed 0 times (hidden), installed again 0 times (went quiet); hook events 5120, average lateness 0.4 ms, 0 at 50 ms or more, 0 at 200 ms or more, max 16 ms
2026-10-17 21:33:05.001  mouse wheel hook removed (Turbo hidden: no hook while Turbo is not shown)
"""
OLD_BUILD = """2026-10-17 21:30:01.000  Turbo GUI loading in process 1234
2026-10-17 21:31:10.000  mouse wheel hook installed (low-level mouse hook on its own thread)
2026-10-17 21:32:13.941  mouse wheel hook went quiet while the mouse moved: installed again (ok)
2026-10-17 21:32:22.688  mouse wheel hook went quiet while the mouse moved: installed again (ok)
2026-10-17 21:33:39.087  mouse wheel: 0 notches from the low-level hook, 0 from raw input, 0 from window messages; hook quiet, installed again 2 times
"""
LAGGY = GOOD.replace("0 at 200 ms or more, max 16 ms", "3 at 200 ms or more, max 310 ms").replace(
    "Turbo frame cost on Settings (so far): 0.954 ms", "Turbo frame cost on Settings (so far): 41.500 ms").replace(
    "2026-10-17 21:33:05.000  Turbo frame cost on Settings (left)",
    "2026-10-17 21:32:40.000  slow Turbo frame on Settings: 95.0 ms (App::tick 0.1 ms, input + draw + render 94.9 ms)\n"
    "2026-10-17 21:32:50.000  slow Turbo frame on Settings: 91.0 ms (App::tick 0.1 ms, input + draw + render 90.9 ms)\n"
    "2026-10-17 21:33:05.000  Turbo frame cost on Settings (left)")
INJECTED = GOOD.replace("hook events 5120, average lateness 0.4 ms", "hook events 0, average lateness 0.0 ms").replace(
    "max 16 ms", "max 0 ms")
TWO_SESSIONS = OLD_BUILD.replace("21:3", "20:3") + GOOD


def self_test():
    cases = [
        ("a gui/perf session with a smooth Settings page passes", GOOD, "", "PASS", {}),
        ("the old build fails (no hook-ready line, reinstalls, no Settings cost)", OLD_BUILD, "", "FAIL",
         {"build": "FAIL", "reinstall": "FAIL", "settings": "FAIL"}),
        ("late hook calls and a slow Settings page fail", LAGGY, "", "FAIL", {"lateness": "FAIL", "settings": "FAIL"}),
        ("no mouse move while shown: lateness inconclusive", INJECTED, "", "INCONCLUSIVE", {"lateness": "INCONCLUSIVE"}),
        ("--since picks the session that started after it", TWO_SESSIONS, "2026-10-17 21:00:00", "PASS", {"build": "PASS"}),
        ("without --since the last session is read", TWO_SESSIONS, "", "PASS", {}),
        ("--since after every session: no session, FAIL", GOOD, "2026-10-17 23:00:00", "FAIL", {"session": "FAIL"}),
    ]
    failed = 0
    for name, text, since, want, points in cases:
        result, out = check(text, since)
        got = {p: v for p, v, _ in out}
        ok = result == want and all(got.get(p) == v for p, v in points.items())
        failed += 0 if ok else 1
        print(("  ok  " if ok else "  FAIL  ") + "gui_perf_livecheck: %s (%s)" % (name, result))
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
