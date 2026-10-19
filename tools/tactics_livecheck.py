"""tactics_livecheck.py - the 2.20 Tactics live check (brief 2.20-T-tactics): reads turbo_gui.log after one LEICESTER launch and
prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the install or turbo_output (it
only reads the log file it is given).

  py -3 -I tools/tactics_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-19 15:00:00"
  py -3 -I tools/tactics_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  1. Career hub (Team Management CLOSED), Turbo window: Tactics > Tactic presets. Screenshot S1: the list shows the presets of
     turbo_output\tactic_presets (5 community slider presets on this PC), the "Live now" lines show your tactic and "Custom sliders now".
     Change no switch on the page (the reads this check uses do not need "Let Turbo change my tactic"; one variable per boot).
  2. Click the first preset in the list (only select it: do NOT press Apply). Screenshot S2.
  3. Press "Refresh" next to the list. Screenshot S3 (the status line says "Read again: ...").
  4. Optional (Game sliders page): click a preset, press its Refresh. Kill the game (the runner). Never save.
  Do NOT open Edit Tactic and answer "Yes": that commits the screen and AUTOSAVES the career (forbidden on LEICESTER). A changed value
  between two Refresh clicks is therefore not part of this check (the "changed" point stays INCONCLUSIVE unless a Turbo write changed one).

What it reads (ui/ui_tactic_presets.cpp and ui/ui_tactics_page.cpp App::log lines, 2.20):
  session   the last "Turbo GUI loading in process" session at or after --since
  list      "tactic presets: store scan: N listed, M skipped in <dir>" with N >= 1 (FAIL with the skipped reasons when N = 0)
  pick      "tactic presets: picked \"<name>\": ..." or "game sliders: picked ..." (a click selects a preset)
  sliders   "tactic presets: live read (open|refresh): ...; custom sliders ok: K values ..." (the current Custom slider block is read;
            the latest good read counts: the first look may run before the hub is up)
  tactic    the same lines' "tactic ok: \"<record>\" team T, approach A, line height L, build-up B" (latest good read). Not read with
            "0 active tactic records" = ESCALATED-B (the open cause B, the career's team sheet layout, escalated to the Architect; NOT counted
            in RESULT - turbo_gui.log has no LEICESTER tactic read after 10-18 09:34, so LEICESTER may show it too); any other reason = FAIL
  refresh   a "tactic presets: live read (refresh): ..." line after the open read with the sliders read again (the tactic outcome of
            that read is shown, judged by the "tactic" point only)
  probe     DATA, never counted in RESULT (2.20 cause B probe, read only): the latest record vector of the club's current sheet - from a
            failing read ("probe: ...", also in bench probe / tactic card lines) if any, else from a good read ("records: ..."):
            "N record(s), G with act 1 and src > 0, A with act 1; #i act .. src ../.. fmt .. preset .. lh .. b77 .. name '..' raw7C ..".
            Classified: baseline (G = 1) | B-SRC (G = 0, A >= 1: a record is active but its source formation is not > 0) | B-FLAG
            (A = 0: no record carries act 1 at +0x80). The value to capture for the Architect is that one line.
  changed   INCONCLUSIVE unless two reads in the session show another line height / approach / build-up (a value changed in between)
  errors    no "frame error:" line in the session
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_SCAN = re.compile(r"tactic presets: store scan: (?P<n>\d+) listed, (?P<m>\d+) skipped in (?P<rest>.*)$")
RE_PICK = re.compile(r"(?:tactic presets|game sliders): picked \"(?P<name>[^\"]*)\"")
RE_READ = re.compile(r"tactic presets: live read \((?P<why>open|refresh)\): tactic (?P<tactic>.*?); custom sliders (?P<sliders>.*)$")
RE_TOK = re.compile(r"^ok: \"(?P<rec>[^\"]*)\" team (?P<team>-?\d+), approach (?P<ap>-?\d+), line height (?P<lh>-?\d+), build-up (?P<bu>-?\d+)")
RE_SOK = re.compile(r"^ok: (?P<n>\d+) values")
# 2.20 cause B probe: the record vector of the club's current sheet (team_sheet.cpp records_probe), on a good read ("records: ...") and
# in every "0 active tactic records" reason ("probe: ..."), wherever the reason is logged (this page, the bench probe, the tactic card)
RE_PROBE = re.compile(r"(?:records|probe): (?P<n>\d+) record\(s\), (?P<good>\d+) with act 1 and src > 0, (?P<act1>\d+) with act 1(?P<body>.*)")


def probe_verdict(lines):
    """('DATA', text) from the latest probe in the session, classified; ('NO-DATA', why) when none"""
    hits = [(l, RE_PROBE.search(l)) for l in lines]
    hits = [(l, m) for l, m in hits if m]
    if not hits:
        return "NO-DATA", "no record-vector probe in the session (a build before the 2.20 probe, or no read reached the records)"
    bad = [(l, m) for l, m in hits if int(m.group("good")) != 1]
    l, m = (bad[-1] if bad else hits[-1])
    if int(m.group("good")) == 1:
        kind = "baseline (a good read: exactly 1 record with act 1 and src > 0)"
    elif int(m.group("act1")) >= 1:
        kind = "B-SRC (a record has act 1 but its source formation is not > 0: L4's src > 0 rule is the refusal)"
    else:
        kind = "B-FLAG (no record has act 1: the active flag +0x80 is not what marks this career's tactic)"
    return "DATA", "%s; %s records, %s good, %s act 1%s" % (kind, m.group("n"), m.group("good"), m.group("act1"), m.group("body")[:900])


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


def tactic_verdict(t):
    m = RE_TOK.match(t)
    if m:
        return "PASS", "record \"%s\" team %s, approach %s, line height %s, build-up %s" % (m.group("rec"), m.group("team"), m.group("ap"),
                                                                                       m.group("lh"), m.group("bu"))
    why = t[len("not read: "):] if t.startswith("not read: ") else t
    if "active tactic record" in why:
        return "ESCALATED-B", "not read: %s (cause B, the career's team sheet layout; escalated to the Architect, not counted in RESULT)" % why[:160]
    return "FAIL", "not read: " + why[:200]


def sliders_verdict(s):
    m = RE_SOK.match(s)
    if m and int(m.group("n")) > 0:
        return "PASS", s[:200]
    return "FAIL", s[:200]


def check(text, since=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))
    scans = [RE_SCAN.search(l) for l in lines]
    scans = [m for m in scans if m]
    if not scans:
        out.append(("list", "FAIL", "no 'tactic presets: store scan' line: the Tactic presets page was not opened, or an old build (before 2.20)"))
    else:
        best = max(scans, key=lambda m: int(m.group("n")))
        if int(best.group("n")) >= 1:
            out.append(("list", "PASS", "%s listed, %s skipped (best of %d scan(s))" % (best.group("n"), best.group("m"), len(scans))))
        else:
            out.append(("list", "FAIL", "0 listed, %s skipped: %s" % (best.group("m"), best.group("rest")[:240])))
    picks = [RE_PICK.search(l) for l in lines]
    picks = [m for m in picks if m]
    out.append(("pick", "PASS", "picked \"%s\"" % picks[0].group("name")) if picks else
               ("pick", "FAIL", "no 'picked' line: step 2 not done, or the click did not select a preset"))
    reads = [(i, RE_READ.search(l)) for i, l in enumerate(lines)]
    reads = [(i, m) for i, m in reads if m]
    opens = [(i, m) for i, m in reads if m.group("why") == "open"]
    if not opens:
        out.append(("sliders", "FAIL", "no 'live read (open)' line: the page never did its first read"))
        out.append(("tactic", "FAIL", "no 'live read (open)' line"))
    else:
        # the first look may run before the hub is up: the LATEST good read of either kind counts
        sv = [sliders_verdict(m.group("sliders")) for _, m in reads]
        ok = [x for x in sv if x[0] == "PASS"]
        out.append(("sliders",) + (ok[-1] if ok else sv[-1]))
        tv = [tactic_verdict(m.group("tactic")) for _, m in reads]
        ok = [x for x in tv if x[0] == "PASS"]
        out.append(("tactic",) + (ok[-1] if ok else tv[-1]))
    refreshes = [(i, m) for i, m in reads if m.group("why") == "refresh" and (not opens or i > opens[0][0])]
    if not refreshes:
        out.append(("refresh", "FAIL", "no 'live read (refresh)' line after the first read: step 3 not done, or Refresh did not read again"))
    else:
        i1, m1 = refreshes[-1]
        tv1, td = tactic_verdict(m1.group("tactic"))
        sv1, sd = sliders_verdict(m1.group("sliders"))
        # Refresh re-read: the sliders read again; the tactic outcome is the "tactic" point's (shown here, not judged twice)
        if sv1 == "PASS":
            out.append(("refresh", "PASS", "%d Refresh read(s); last: sliders %s; tactic %s" % (len(refreshes), sd, td)))
        else:
            out.append(("refresh", "FAIL", "Refresh ran (%d) but the sliders were not read: %s" % (len(refreshes), sd)))
    values = []
    for _, m in reads:
        t = RE_TOK.match(m.group("tactic"))
        if t:
            values.append((t.group("ap"), t.group("lh"), t.group("bu")))
    changed = [(a, b) for a, b in zip(values, values[1:]) if a != b]
    if changed:
        a, b = changed[0]
        out.append(("changed", "PASS", "a later read shows the new values: approach/line height/build-up %s -> %s" % ("/".join(a), "/".join(b))))
    else:
        out.append(("changed", "INCONCLUSIVE", "no value changed between reads (%d tactic read(s)); not required by this check" % len(values)))
    out.append(("probe",) + probe_verdict(lines))
    errs = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0].strip()[:200] if errs else "no frame error in the session"))
    verdicts = [v for p, v, _ in out if p not in ("changed", "probe") and v != "ESCALATED-B"]
    result = "FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-8s %-12s %s" % (point, verdict, detail))
    if any(v == "ESCALATED-B" for _, v, _ in out):
        print("NOTE the tactic was not read (cause B, escalated): RESULT covers the list, the pick, the sliders and Refresh only")
    print("RESULT %s" % result)
    return 0 if result == "PASS" else 1


def _log(*body):
    head = ["2026-10-19 15:00:01.000  Turbo GUI loading in process 1234"]
    return "\n".join(head + ["2026-10-19 15:00:%02d.123  %s" % (10 + i, b) for i, b in enumerate(body)]) + "\n"


DIR = "C:\\FC 27 Live Editor\\turbo_output\\tactic_presets"
SCAN5 = "tactic presets: store scan: 5 listed, 0 skipped in " + DIR
SCAN0 = ("tactic presets: store scan: 0 listed, 5 skipped in " + DIR + "; skipped: Community - x.json: note: the note is longer than 300 "
         "characters or holds control characters | ...")
PROBE_OK = "5 record(s), 1 with act 1 and src > 0, 1 with act 1; #0 act 1 src 10/10 fmt 27 preset 1 lh 45 b77 3 name '433' raw7C 000000000100000001000000"
TOK = "ok: \"433\" team 95, approach 1, line height 45, build-up 0, records: " + PROBE_OK
SOK = "ok: 125 values, user.sprint_speed 50, cpu.sprint_speed 50, user.acceleration 50"
OPEN = "tactic presets: live read (open): tactic %s; custom sliders %s" % (TOK, SOK)
PICK = "tactic presets: picked \"Community - dr4zah FC27 community set\": 88 sliders"
REFR = "tactic presets: live read (refresh): tactic %s; custom sliders %s" % (TOK, SOK)
NOTREAD_B = ("not read: layout mismatch (0 active tactic records (exactly 1 needed)) probe: 5 record(s), 0 with act 1 and src > 0, 0 with act 1; "
             "#0 act 0 src 12/12 fmt 31 preset 1 lh 60 b77 3 name '4231' raw7C 000000000000000001000000")


def self_test():
    cases = [
        ("pass: list, pick, open read, refresh", _log(SCAN5, OPEN, PICK, SCAN5, REFR), "PASS"),
        ("pass: game sliders pick counts", _log(SCAN5, OPEN, PICK.replace("tactic presets", "game sliders"), REFR), "PASS"),
        ("fail: nothing listed (the 300-char note refusal)", _log(SCAN0, OPEN, REFR), "FAIL"),
        ("fail: no pick", _log(SCAN5, OPEN, REFR), "FAIL"),
        ("fail: no refresh", _log(SCAN5, OPEN, PICK), "FAIL"),
        ("pass: tactic not read (cause B) is escalated, not counted", _log(SCAN5, OPEN.replace(TOK, NOTREAD_B), PICK, REFR.replace(TOK, NOTREAD_B)), "PASS"),
        ("fail: tactic not read for another reason", _log(SCAN5, OPEN.replace(TOK, "not read: not in a career"), PICK, REFR.replace(TOK, "not read: not in a career")), "FAIL"),
        ("pass: the first look before the hub, Refresh reads", _log(SCAN5, OPEN.replace(TOK, "not read: locating the team sheet service ...").replace(SOK, "not read: no settings file yet"), PICK, REFR), "PASS"),
        ("fail: sliders never read", _log(SCAN5, OPEN.replace(SOK, "not read: no settings file"), PICK, REFR.replace(SOK, "not read: no settings file")), "FAIL"),
        ("fail: refresh did not read", _log(SCAN5, OPEN, PICK, REFR.replace(SOK, "not read: x")), "FAIL"),
        ("fail: old build (no scan line)", _log(OPEN, PICK, REFR), "FAIL"),
        ("fail: refresh before the open read only", _log(SCAN5, REFR, OPEN, PICK), "FAIL"),
        ("fail: frame error", _log(SCAN5, OPEN, PICK, REFR, "frame error: boom"), "FAIL"),
        ("fail: no session", "2026-10-19 15:00:01.000  nothing\n", "FAIL"),
    ]
    bad = 0
    for name, text, want in cases:
        got, _ = check(text)
        ok = got == want
        bad += 0 if ok else 1
        print("  %s %s (got %s, want %s)" % ("ok" if ok else "FAIL", name, got, want))
    _, out = check(_log(SCAN5, OPEN, PICK, REFR.replace("line height 45", "line height 60")))
    ok = [v for p, v, _ in out if p == "changed"] == ["PASS"]
    bad += 0 if ok else 1
    print("  %s a changed line height between reads is seen" % ("ok" if ok else "FAIL"))
    _, out = check(_log(SCAN5, OPEN, PICK, REFR))
    ok = [v for p, v, _ in out if p == "changed"] == ["INCONCLUSIVE"]
    bad += 0 if ok else 1
    print("  %s no change stays INCONCLUSIVE (and does not fail the run)" % ("ok" if ok else "FAIL"))
    _, out = check(_log(SCAN5, OPEN.replace(TOK, NOTREAD_B), PICK, REFR.replace(TOK, NOTREAD_B)))
    ok = any(p == "tactic" and v == "ESCALATED-B" and "cause B" in d for p, v, d in out)
    bad += 0 if ok else 1
    print("  %s a 0-active-record career is named as cause B" % ("ok" if ok else "FAIL"))
    for name, text, want in [
            ("baseline on a good read", _log(SCAN5, OPEN, PICK, REFR), "baseline"),
            ("B-FLAG when no record is active", _log(SCAN5, OPEN.replace(TOK, NOTREAD_B), PICK, REFR.replace(TOK, NOTREAD_B)), "B-FLAG"),
            ("B-SRC when the active record has no source formation",
             _log(SCAN5, OPEN.replace(TOK, NOTREAD_B.replace("0 with act 1;", "1 with act 1;").replace("#0 act 0 src 12/12", "#0 act 1 src 0/12")), PICK, REFR), "B-SRC"),
            ("a failing bench-probe line counts too", _log(SCAN5, OPEN, PICK, REFR, "bench probe: career hub (the sheet as loaded): " + NOTREAD_B), "B-FLAG"),
            ("NO-DATA on an older build", _log(SCAN5, OPEN.split(", records:")[0], PICK, REFR.split(", records:")[0]), "NO-DATA")]:
        _, out = check(text)
        got = [(v, d) for p, v, d in out if p == "probe"]
        ok = bool(got) and (want in got[0][1] if want != "NO-DATA" else got[0][0] == "NO-DATA")
        bad += 0 if ok else 1
        print("  %s probe: %s (%s)" % ("ok" if ok else "FAIL", name, got[0][1][:60] if got else "-"))
    got, _ = check(_log(SCAN5, OPEN, PICK, REFR), since="2026-10-19 16:00:00")
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --since skips older sessions (got %s)" % ("ok" if ok else "FAIL", got))
    print("SELF-TEST %s" % ("PASSED" if bad == 0 else "FAILED"))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="", help="local time 'YYYY-MM-DD HH:MM:SS' of the launch (the log's own clock)")
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
    return report(*check(text, a.since))


if __name__ == "__main__":
    sys.exit(main())
