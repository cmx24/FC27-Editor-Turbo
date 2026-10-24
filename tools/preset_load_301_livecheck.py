"""preset_load_301_livecheck.py - the 3.0.1 tactic preset load live check (brief 3.0.1-tactics, defect 1: GUI30H_B wrote nothing, Line
Height stayed 25). Reads turbo_gui.log (and, optionally, turbo_output\\tactic_preset_last_load.json and two copies of settings_pending.json)
after one LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the
install or turbo_output (it only reads the files it is given).

  py -3 -I tools/preset_load_301_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-24 18:00:00" \\
      [--preset GUI30H_B --want-line 60] [--last-load last_load_after_load.json] [--pending-before p0.json --pending-after p1.json]
  py -3 -I tools/preset_load_301_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  0. Before launch: copy turbo_dev\\evidence\\2026-10-24\\GUI30H\\GUI30H_B.json into turbo_output\\tactic_presets\\ (line height 60, approach
     Deep 0, build-up 0); copy turbo_output\\settings_pending.json to p0.json (or note that it is absent: pass --pending-before absent).
  1. Career hub (Team Management CLOSED), Turbo: Tactics > Tactic presets. The "Live now" line shows Line Height 25 (shot S1).
  2. Select GUI30H_B, press "Apply preset". The dialog must list "Line Height 25 -> 60" and "Defensive Approach Deep (0) -> Balanced (1)"
     with the note that the preset's approach follows the line's band (shot S2); it must NOT say "already holds". Press Yes (Load).
  3. The status line says Loaded "GUI30H_B": written into your live tactic and read back (shot S3). Press "Refresh" (the check needs the
     read after the load). Copy turbo_output\\tactic_preset_last_load.json to last_load_after_load.json.
  4. Press "Undo last load", Yes. The status says "Put back the values from before the load ..."; press "Refresh" (shot S4). Copy
     turbo_output\\settings_pending.json to p1.json (or note absent). Kill the game (the runner). Never save. Never open Edit Tactic and answer
     "Yes" (that commits the screen and autosaves the career).

What it reads (ui/ui_tactic_presets.cpp say() / read_all() lines, 3.0.1):
  session   the last "Turbo GUI loading in process" session at or after --since
  before    the latest "tactic presets: live read (open|refresh): tactic ok: ... line height L0 ..." before the load line (L0 = the live value)
  loaded    "tactic presets: Loaded \\"<preset>\\": written into your live tactic and read back." FAIL on "\\"<preset>\\": the tactic is NOT
            changed (...)" or a "Not loaded: ..." line after the pick (the reason is printed). b153e48 logged neither: it said "already holds"
  read      a "live read (refresh)" after the load line: line height == --want-line and approach == the band of that line (1-30 Deep 0,
            31-60 Balanced 1, 61-90 High 2); INCONCLUSIVE when no Refresh followed the load
  undo      "tactic presets: Put back the values from before the load of \\"<preset>\\" (read back)." and a later live read with line height
            == L0; INCONCLUSIVE when no Undo was pressed
  lastload  (with --last-load) the file parses, preset == --preset, after.line_height == --want-line, after.approach == the band, and no
            -1 in after (b153e48 copied -1 for an unwritten field and the record then failed to parse)
  pending   (with --pending-before/--pending-after) settings_pending.json after the Undo equals the one before the load ("absent" = no file):
            b153e48's Undo deleted an earlier staging along with the load's own
  sliders   DATA, never counted: the "custom sliders ok: ..." part of the last live read (the Settings FILE's values, not the running game)
  errors    no "frame error:" line in the session
"""
import argparse
import json
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_READ = re.compile(r"tactic presets: live read \((?P<why>open|refresh)\): tactic ok: \"[^\"]*\" team (?P<team>-?\d+), approach (?P<ap>-?\d+), "
                     r"line height (?P<lh>-?\d+), build-up (?P<bu>-?\d+).*?; custom sliders (?P<sliders>.*)$")


def band(line):
    return 0 if 1 <= line <= 30 else 1 if line <= 60 else 2 if line <= 90 else 3


def session_lines(text, since):
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if "Turbo GUI loading in process" in line:
            m = RE_TIME.match(line)
            if not since or (m and m.group(1) >= since):
                start = i
    return [] if start is None else lines[start:]


def reads(lines):
    """[(index, why, approach, line, build_up, sliders)] of every good live read"""
    out = []
    for i, l in enumerate(lines):
        m = RE_READ.search(l)
        if m:
            out.append((i, m.group("why"), int(m.group("ap")), int(m.group("lh")), int(m.group("bu")), m.group("sliders")))
    return out


def check(log_text, since, preset, want_line, last_load_text=None, pending_before=None, pending_after=None):
    """[(point, verdict, detail)]; verdict PASS | FAIL | INCONCLUSIVE | DATA"""
    res = []
    lines = session_lines(log_text, since)
    if not lines:
        return [("session", "FAIL", "no 'Turbo GUI loading in process' session at or after " + (since or "the start of the log"))]
    res.append(("session", "PASS", "%d lines" % len(lines)))
    want_ap = band(want_line)
    q = '"' + preset + '"'
    i_load = next((i for i, l in enumerate(lines) if ("tactic presets: Loaded " + q + ": written into your live tactic and read back.") in l), None)
    i_not = next((i for i, l in enumerate(lines) if ("tactic presets: " + q + ": the tactic is NOT changed") in l or
                  ("tactic presets: Not loaded: " in l)), None)
    rr = reads(lines)
    if i_load is None:
        res.append(("loaded", "FAIL", lines[i_not].split("tactic presets: ", 1)[1][:300] if i_not is not None else
                    "no 'Loaded " + q + ": written into your live tactic and read back.' line (the load was not made, or nothing was written)"))
    else:
        res.append(("loaded", "PASS", lines[i_load].split("tactic presets: ", 1)[1][:300]))
    before = [r for r in rr if i_load is not None and r[0] < i_load]
    l0 = before[-1][3] if before else None
    res.append(("before", "DATA", "live line height before the load: %s" % (l0 if l0 is not None else "not read")))
    i_undo = None
    if i_load is not None:
        i_undo = next((i for i, l in enumerate(lines) if i > i_load and
                       ("tactic presets: Put back the values from before the load of " + q + " (read back).") in l), None)
        after = [r for r in rr if r[0] > i_load and r[1] == "refresh" and (i_undo is None or r[0] < i_undo)]
        if not after:
            res.append(("read", "INCONCLUSIVE", "no Refresh after the load (press Refresh before Undo)"))
        else:
            r = after[-1]
            ok = r[3] == want_line and r[2] == want_ap
            res.append(("read", "PASS" if ok else "FAIL", "after the load: line height %d (want %d), approach %d (want %d), build-up %d" %
                        (r[3], want_line, r[2], want_ap, r[4])))
    else:
        res.append(("read", "INCONCLUSIVE", "no load"))
    if i_undo is None:
        res.append(("undo", "INCONCLUSIVE", "no 'Put back the values from before the load of " + q + "' line (Undo not pressed)"))
    else:
        later = [r for r in rr if r[0] > i_undo]
        if not later or l0 is None:
            res.append(("undo", "INCONCLUSIVE", "Undo logged, but no live read after it (press Refresh) or none before the load"))
        else:
            ok = later[-1][3] == l0
            res.append(("undo", "PASS" if ok else "FAIL", "after Undo: line height %d (before the load %d)" % (later[-1][3], l0)))
    if last_load_text is not None:
        try:
            j = json.loads(last_load_text)
            a = j.get("after") or {}
            bad = [k for k in ("line_height", "build_up", "approach") if not isinstance(a.get(k), int) or a.get(k) < 0]
            ok = j.get("preset") == preset and a.get("line_height") == want_line and a.get("approach") == want_ap and not bad
            res.append(("lastload", "PASS" if ok else "FAIL", "preset %r state %r after line %r approach %r build-up %r%s" %
                        (j.get("preset"), j.get("state"), a.get("line_height"), a.get("approach"), a.get("build_up"),
                         (", not set: " + ",".join(bad)) if bad else "")))
        except (ValueError, AttributeError) as e:
            res.append(("lastload", "FAIL", "not readable JSON: %s" % e))
    if pending_before is not None and pending_after is not None:
        same = pending_before == pending_after
        res.append(("pending", "PASS" if same else "FAIL", "settings_pending.json after Undo %s the one before the load (%s / %s)" %
                    ("equals" if same else "DIFFERS FROM", "absent" if pending_before == "absent" else "%d bytes" % len(pending_before),
                     "absent" if pending_after == "absent" else "%d bytes" % len(pending_after))))
    res.append(("sliders", "DATA", (rr[-1][5][:200] + " (from the Settings file, not the running game)") if rr else "no live read"))
    fe = [l for l in lines if "frame error:" in l]
    res.append(("errors", "FAIL" if fe else "PASS", fe[0][:200] if fe else "no frame error"))
    return res


def verdict(res):
    counted = [v for _, v, _ in res if v in ("PASS", "FAIL", "INCONCLUSIVE")]
    return "FAIL" if "FAIL" in counted else "INCONCLUSIVE" if "INCONCLUSIVE" in counted else "PASS"


def show(res):
    for p, v, d in res:
        print("%-9s %-12s %s" % (p, v, d))
    print("RESULT %s" % verdict(res))


def read_opt(path):
    if path is None:
        return None
    if path == "absent":
        return "absent"
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def self_test():
    T = "2026-10-24 18:0%d:00 "
    read = lambda why, lh, ap: 'tactic presets: live read (%s): tactic ok: "433" team 95, approach %d, line height %d, build-up 0; custom sliders ok: 125 values, user.sprint_speed 50' % (why, ap, lh)
    good = "\n".join([
        T % 0 + "Turbo GUI loading in process 1234",
        T % 1 + read("open", 25, 0),
        T % 2 + 'tactic presets: Loaded "GUI30H_B": written into your live tactic and read back. Sliders: staged 3 ... Takes effect at the next match',
        T % 3 + read("refresh", 60, 1),
        T % 4 + 'tactic presets: Put back the values from before the load of "GUI30H_B" (read back). Sliders: ...',
        T % 5 + read("refresh", 25, 0)])
    ll = json.dumps({"preset": "GUI30H_B", "state": "loaded", "after": {"line_height": 60, "approach": 1, "build_up": 0}})
    fails = 0

    def expect(name, res, want):
        nonlocal fails
        got = verdict(res)
        ok = got == want
        fails += 0 if ok else 1
        print("  %s %s: %s (want %s)" % ("ok  " if ok else "FAIL", name, got, want))
        if not ok:
            show(res)

    expect("good log + last load + pending kept", check(good, "", "GUI30H_B", 60, ll, "{\"a\":1}", "{\"a\":1}"), "PASS")
    # b153e48: the dialog said "already holds" and the finish line says nothing was written; the read stays 25
    old = "\n".join([T % 0 + "Turbo GUI loading in process 1", T % 1 + read("open", 25, 0),
                     T % 2 + 'tactic presets: "GUI30H_B": the tactic is NOT changed (the preset\'s values are values Turbo does not write).',
                     T % 3 + read("refresh", 25, 0)])
    expect("nothing written", check(old, "", "GUI30H_B", 60), "FAIL")
    expect("still 25 after a Loaded line", check(good.replace(read("refresh", 60, 1), read("refresh", 25, 0)), "", "GUI30H_B", 60), "FAIL")
    expect("approach not following the band", check(good.replace(read("refresh", 60, 1), read("refresh", 60, 0)), "", "GUI30H_B", 60), "FAIL")
    expect("no refresh after the load", check(good.replace(T % 3 + read("refresh", 60, 1), T % 3 + "x"), "", "GUI30H_B", 60), "INCONCLUSIVE")
    expect("last load with -1 (b153e48 overlay)", check(good, "", "GUI30H_B", 60, ll.replace('"build_up": 0', '"build_up": -1')), "FAIL")
    expect("Undo dropped the earlier staging (b153e48)", check(good, "", "GUI30H_B", 60, ll, "{\"a\":1}", "absent"), "FAIL")
    expect("session before --since", check(good, "2026-10-25 00:00:00", "GUI30H_B", 60), "FAIL")
    expect("frame error", check(good + "\n" + T % 6 + "frame error: boom", "", "GUI30H_B", 60), "FAIL")
    expect("undo not back to 25", check(good.replace(T % 5 + read("refresh", 25, 0), T % 5 + read("refresh", 60, 1)), "", "GUI30H_B", 60), "FAIL")
    print("self-test: %s (%d failed)" % ("PASS" if fails == 0 else "FAIL", fails))
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log")
    ap.add_argument("--since", default="")
    ap.add_argument("--preset", default="GUI30H_B")
    ap.add_argument("--want-line", type=int, default=60)
    ap.add_argument("--last-load")
    ap.add_argument("--pending-before")
    ap.add_argument("--pending-after")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.log:
        ap.error("--log is required (or --self-test)")
    with open(a.log, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    res = check(text, a.since, a.preset, a.want_line, read_opt(a.last_load), read_opt(a.pending_before), read_opt(a.pending_after))
    show(res)
    return 0 if verdict(res) == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
