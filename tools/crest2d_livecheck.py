#!/usr/bin/env python3
"""crest2d_livecheck.py - the 2.20 K2 shirt-crest live check (brief 2.20-K2-crest-lua). Read-only: it reads Turbo's turbo_gui.log and the
Live Editor log of ONE boot and checks that the Tester's pitch screenshot is filed; it never touches the game, the install, the saves or
turbo_output. One PASS / FAIL / INFO line per point and one RESULT line. ASCII only, stdlib only.

  py -3 -I tools/crest2d_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --lelog "C:/FC 27 Live Editor/logs/live_editor_19-10-2026.log"
        --since "2026-10-19 15:02:00" --clubs 1 --shot "<evidence>/pitch.png" [--expect-k 18] [--look pass|fail]
  py -3 -I tools/crest2d_livecheck.py --self-test      (offline: synthetic passing and failing logs)

The Tester's steps (LEICESTER, never saved, about 2 minutes; the runner launches and kills the game):
  0. Before launch: Leicester 95's crest in mods\\legacy visibly altered (backup first, restore at teardown) and
     turbo_output\\kits\\crest2d.json = {"teams": [95]} (Turbo's Kits page, club Leicester, More, "Use my crest on the shirt").
  1. Open one kit preview of Leicester (Kick Off team/kit select): the mannequin / static-jersey paths commit C guards.
  2. Kick Off, Leicester at home, until the players walk out / the first close-up. Screenshot of a Leicester shirt: --shot.
     Crowd (commit B): a wide shot of the stands with low-res crowd shirts as a second screenshot (a second run with --shot).
  3. Kill the game (runner). Run this script. Judge the picture by eye: --look pass when the shirt crest is the altered Turbo crest,
     --look fail for the stock stitched crest or the plain crest_fallback decal (this script never judges a picture).

What it reads:
  served    turbo_gui.log 'lua_loader: player_kit served: v4 built in memory (stock md5 S, <note>, N B, md5 M)': note must say
            '<clubs> club(s) on the shirt-crest list' (missing = the list was not in the build; 'shirt-crest list refused (...)' =
            the build dropped it, the reason is printed)
  subst     every 'SUBSTITUTED row=player_kit override bytes N md5 M ... rc=0' line has the served md5 (K printed: the hair/beard
            served count, 18/18 in every earlier boot; --expect-k checks it)
  nofall    no 'fallback to' / 'stale override' / 'served captured stock' line for player_kit
  luaerr    (decisive) the Live Editor log of the boot (lines at or after --since converted to the LE log's UTC clock;
            --le-offset-hours overrides the machine's offset) has no Lua error line: 'invalid read, no key
            in the table', 'attempt to index', 'attempt to compare', 'attempt to call', 'bad argument', 'player_kit.lua:<n>:'
  shot      --shot exists, is a PNG or JPEG, and was written at or after --since
Exit code 0 = RESULT PASS, 1 = FAIL, 2 = bad arguments.
"""
import argparse
import datetime
import os
import re
import sys
import tempfile

STAMP = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(\.\d+)?\s+(.*)$")
LE_STAMP = re.compile(r"^(\d{2}:\d{2}:\d{2})(\.\d+)?\s")
START = re.compile(r"lua_loader: on \(2\.16")
SERVED = re.compile(r"lua_loader: player_kit served: v4 built in memory \(stock md5 ([0-9a-f]{32}), (.*?), (\d+) B, md5 ([0-9a-f]{32})\)")
SERVED_STOCK = re.compile(r"lua_loader: player_kit served: stock \((.*)\)\s*$")
SUBST = re.compile(r"lua_loader: #\d+ .*chunk=\"data/fifarna/lua/assets/player_kit\.lua\".* SUBSTITUTED row=player_kit override bytes (\d+) md5 ([0-9a-f]{32}) .*rc=(-?\d+)")
BAD_SUBST = re.compile(r"lua_loader: #\d+ .*player_kit\.lua\".*(fallback to|stale override|served captured stock)")
CLUBS = re.compile(r"(\d+) clubs? on the shirt-crest list")
REFUSED = re.compile(r"shirt-crest list refused \((.*)\)")
LUA_ERR = re.compile(r"invalid read, no key in the table|attempt to index|attempt to compare|attempt to call|attempt to perform|bad argument|"
                     r"player_kit\.lua:\d+:", re.I)
STOCK = "9399e39a1e46a15d151ada423759abe9"

results = []


def say(kind, text):
    results.append(kind)
    print("%s %s" % (kind, text))


def scope(lines, since):
    """Turbo log lines (local clock) of ONE boot: from --since up to the next boot's 'lua_loader: on (2.16' line (else the last boot)"""
    rows = []
    for line in lines:
        m = STAMP.match(line)
        if m:
            rows.append((m.group(1), m.group(3)))
    if since:
        rows = [r for r in rows if r[0] >= since]
        starts = [i for i, r in enumerate(rows) if START.search(r[1])]
        return rows[:starts[1]] if len(starts) > 1 else rows
    last = max([i for i, r in enumerate(rows) if START.search(r[1])] or [0])
    return rows[last:]


def le_tod(since, le_offset_h=None):
    """--since (local time) on the Live Editor log's clock, which is UTC (2026-10-19: file written 13:24:50 EDT, last line 17:24:50).
    le_offset_h = hours to add to local time (EDT: 4); None = this machine's own local-to-UTC offset at that moment."""
    if not since:
        return ""
    dt = datetime.datetime.strptime(since, "%Y-%m-%d %H:%M:%S")
    if le_offset_h is None:
        utc = dt.astimezone(datetime.timezone.utc)
    else:
        utc = dt + datetime.timedelta(hours=le_offset_h)
    return utc.strftime("%H:%M:%S")


def le_scope(lines, since, le_offset_h=None):
    """Live Editor log lines at or after --since converted to the LE clock (UTC). The LE file is one per day and has no date on its
    lines: a boot after 20:00 EDT (00:00 UTC) is not scoped right; the INFO line prints both times"""
    tod = le_tod(since, le_offset_h)
    out = []
    for line in lines:
        m = LE_STAMP.match(line)
        if m and (not tod or m.group(1) >= tod):
            out.append(line)
    return out


def check(turbo_text, le_text, since, clubs, shot, expect_k=None, look=None, team=95, le_offset_h=None):
    del results[:]
    rows = scope(turbo_text.splitlines(), since)
    print("INFO scope: %d Turbo log lines%s" % (len(rows), (" from " + since) if since else " from the last 'lua_loader: on (2.16' line"))
    served = [SERVED.search(t) for _, t in rows]
    served = [m for m in served if m]
    stock_served = [SERVED_STOCK.search(t) for _, t in rows]
    stock_served = [m for m in stock_served if m]
    md5 = None
    if not served:
        say("FAIL", "no 'player_kit served: v4 built in memory' line%s" % ((": served stock (" + stock_served[-1].group(1) + ")") if stock_served else ""))
    else:
        s = served[-1]
        md5 = s.group(4)
        note = s.group(2)
        say("PASS" if s.group(1) == STOCK else "FAIL", "built from stock md5 %s (%s B, md5 %s)" % (s.group(1), s.group(3), md5))
        c = CLUBS.search(note)
        r = REFUSED.search(note)
        if r:
            say("FAIL", "the build dropped the shirt-crest list: %s" % r.group(1))
        elif not c:
            say("FAIL", "the served note '%s' names no shirt-crest list: the list was not in this build (crest2d.json missing, or a Turbo without 2.20 K2)" % note)
        else:
            say("PASS" if int(c.group(1)) == clubs else "FAIL", "the build carries the shirt-crest list: %s club(s) (want %d) - note '%s'" % (c.group(1), clubs, note))
    subs = [SUBST.search(t) for _, t in rows]
    subs = [m for m in subs if m]
    if not subs:
        say("FAIL", "no SUBSTITUTED row=player_kit line: the game did not compile Turbo's player_kit.lua")
    else:
        mds = sorted(set(m.group(2) for m in subs))
        rcs = sorted(set(m.group(3) for m in subs))
        good = mds == [md5] and rcs == ["0"]
        say("PASS" if good else "FAIL", "K = %d SUBSTITUTED lines (hair/beard served count), md5 %s, rc %s (rule: one md5 = the served one, rc 0)" % (len(subs), ",".join(mds), ",".join(rcs)))
        if expect_k is not None:
            say("PASS" if len(subs) == expect_k else "FAIL", "K = %d, expected %d (the faces livecheck count of earlier boots)" % (len(subs), expect_k))
    bad = [t for _, t in rows if BAD_SUBST.search(t)]
    say("PASS" if not bad else "FAIL", "no fallback / stale override / served captured stock on player_kit (%d)%s" % (len(bad), (": " + bad[0][:160]) if bad else ""))
    if le_text is None:
        say("FAIL", "no Live Editor log given (--lelog): the Lua error grep cannot run")
    else:
        le = le_scope(le_text.splitlines(), since, le_offset_h)
        print("INFO Live Editor log scope: lines at or after %s on its UTC clock (--since %s local)" % (le_tod(since, le_offset_h) or "the start", since or "-"))
        errs = [l for l in le if LUA_ERR.search(l)]
        say("PASS" if not errs else "FAIL", "Live Editor log: %d lines in scope, %d Lua error line(s)%s" % (len(le), len(errs), (": " + errs[0][:200]) if errs else ""))
        # informative only: with LE's legacy-file log flags on, the 2D crest the shirt asks for shows as a full-size crest path
        hits = [l for l in le if ("imgAssets/crest/dark/l%d.dds" % team) in l or ("imgAssets/crest/light/l%d.dds" % team) in l]
        print("INFO Live Editor log lines naming data/ui/imgAssets/crest/<dark|light>/l%d.dds: %d (needs log_load_legacy_file / log_try_find_legacy_file; the menus ask for it too)" % (team, len(hits)))
    if not shot:
        say("FAIL", "no pitch screenshot given (--shot)")
    elif not os.path.isfile(shot):
        say("FAIL", "pitch screenshot not found: %s" % shot)
    else:
        with open(shot, "rb") as f:
            head = f.read(8)
        kind = "PNG" if head.startswith(b"\x89PNG") else ("JPEG" if head.startswith(b"\xff\xd8") else None)
        when = datetime.datetime.fromtimestamp(os.path.getmtime(shot)).strftime("%Y-%m-%d %H:%M:%S")
        fresh = not since or when >= since
        say("PASS" if kind and fresh else "FAIL", "pitch screenshot filed: %s (%s, %d B, written %s%s)" % (shot, kind or "not a PNG/JPEG", os.path.getsize(shot), when, "" if fresh else ", BEFORE --since"))
    if look == "fail":
        say("FAIL", "the Tester's look: the shirt shows the stock crest or the crest_fallback decal")
    elif look == "pass":
        say("PASS", "the Tester's look: the shirt crest is the altered Turbo crest")
    else:
        print("INFO the picture is judged by eye (this script never does): the altered Turbo crest = pass; the stitched stock crest or a plain decal = fail")
    res = "PASS" if results and all(k == "PASS" for k in results) else "FAIL"
    print("RESULT %s" % res)
    return res


# ---------------------------------------------------------------- self-test
def _lines(clubs_note, md5="b" * 32, k=18, rc=0, bad=False, stock=STOCK):
    t = ["2026-10-19 15:02:01.000  lua_loader: on (2.16: whenever Turbo is installed): SUBSTITUTE mode, player_kit.lua v4 built in memory",
         "2026-10-19 15:02:04.000  lua_loader: installed, SUBSTITUTE: lua_load hooked at FC27.exe+0x81F470"]
    t.append("2026-10-19 15:02:08.100  lua_loader: player_kit served: v4 built in memory (stock md5 %s, 0 players on the saved list%s, 282773 B, md5 %s)" % (stock, clubs_note, md5))
    for i in range(k):
        t.append("2026-10-19 15:02:08.200  lua_loader: #%d t=15:02:08.651 tid=1 caller=FC27.exe+0x2277C9C reader=FC27.exe+0x46AE738(other) chunk=\"data/fifarna/lua/assets/player_kit.lua\" mode=(null) size=- SUBSTITUTED row=player_kit override bytes 282773 md5 %s (chunkname match) rc=%d" % (i + 1, md5, rc))
    if bad:
        t.append("2026-10-19 15:02:09.000  lua_loader: #19 chunk=\"data/fifarna/lua/assets/player_kit.lua\" fallback to captured stock")
    return "\n".join(t) + "\n"


def self_test():
    fails = 0
    d = tempfile.mkdtemp(prefix="crest2d_lc_")
    shot = os.path.join(d, "pitch.png")
    with open(shot, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"\0" * 32)
    t = datetime.datetime(2026, 10, 19, 15, 5, 0).timestamp()  # the synthetic boot is at 15:02; the shot is filed after it
    os.utime(shot, (t, t))
    notpng = os.path.join(d, "pitch.txt")
    with open(notpng, "wb") as f:
        f.write(b"hello")
    # the LE log runs on UTC: the boot at 15:02 EDT logs at 19:02 (le_offset_h=4 below; the CLI default is the machine's offset)
    le_ok = "19:01:00.000000\tINFO\told line attempt to index a nil value (before the boot)\n19:02:30.000000\tINFO\tsomething fine\n"
    le_bad = le_ok + "19:03:10.000000\tERROR\t[string \"data/fifarna/lua/assets/player_kit.lua\"]:2618: bad argument #2 to 'format' (number expected, got nil)\n"
    # an error of an EARLIER boot (11:03 EDT = 15:03 UTC): after --since 15:02 read as local time, before it on the LE clock
    le_early = "15:03:10.000000\tERROR\t[string \"data/fifarna/lua/assets/player_kit.lua\"]:2624: error: invalid read, no key in the table: KitColorJerseyPri\n" + le_ok
    since = "2026-10-19 15:02:00"
    old = sys.stdout
    cases = [
        ("pass", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=shot, expect_k=18, look="pass"), "PASS"),
        ("pass without a look", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=shot), "PASS"),
        ("look fail", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=shot, look="fail"), "FAIL"),
        ("list not in the build", dict(turbo_text=_lines(""), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("list refused", dict(turbo_text=_lines("; shirt-crest list refused (anchor md5 mismatch at stock line 2610)"), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("wrong club count", dict(turbo_text=_lines("; 2 clubs on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("lua error in the LE log", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_bad, since=since, clubs=1, shot=shot), "FAIL"),
        ("no LE log", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=None, since=since, clubs=1, shot=shot), "FAIL"),
        ("fallback", dict(turbo_text=_lines("; 1 club on the shirt-crest list", bad=True), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("no substitution", dict(turbo_text=_lines("; 1 club on the shirt-crest list", k=0), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("rc 1", dict(turbo_text=_lines("; 1 club on the shirt-crest list", rc=1), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("K 17 of 18", dict(turbo_text=_lines("; 1 club on the shirt-crest list", k=17), le_text=le_ok, since=since, clubs=1, shot=shot, expect_k=18), "FAIL"),
        ("other stock", dict(turbo_text=_lines("; 1 club on the shirt-crest list", stock="c" * 32), le_text=le_ok, since=since, clubs=1, shot=shot), "FAIL"),
        ("no shot", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=os.path.join(d, "none.png")), "FAIL"),
        ("shot not a picture", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since=since, clubs=1, shot=notpng), "FAIL"),
        ("shot older than the boot", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_ok, since="2099-01-01 00:00:00", clubs=1, shot=shot), "FAIL"),
        ("a later boot in the same log is out of scope", dict(turbo_text=_lines("; 1 club on the shirt-crest list") + _lines("", md5="e" * 32).replace("15:02:", "16:10:"), le_text=le_ok, since=since, clubs=1, shot=shot, expect_k=18), "PASS"),
        ("an earlier boot's Lua error is out of scope (UTC clock)", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_early, since=since, clubs=1, shot=shot), "PASS"),
        ("the same error read on the local clock would be in scope", dict(turbo_text=_lines("; 1 club on the shirt-crest list"), le_text=le_early, since=since, clubs=1, shot=shot, le_offset_h=0), "FAIL"),
    ]
    for name, kw, want in cases:
        kw.setdefault("le_offset_h", 4)
        sys.stdout = open(os.devnull, "w")
        try:
            got = check(**kw)
        finally:
            sys.stdout.close()
            sys.stdout = old
        if got == want:
            print("  ok %s -> %s" % (name, got))
        else:
            fails += 1
            print("  FAIL %s: got %s, want %s" % (name, got, want))
    for p in (shot, notpng):
        os.remove(p)
    os.rmdir(d)
    print("SELF-TEST %s (%d cases, %d failed)" % ("PASSED" if not fails else "FAILED", len(cases), fails))
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", help="turbo_gui.log of the boot")
    ap.add_argument("--lelog", help="the Live Editor log of that day (logs\\live_editor_DD-MM-YYYY.log)")
    ap.add_argument("--since", help="'YYYY-MM-DD HH:MM:SS' local time of the launch")
    ap.add_argument("--clubs", type=int, default=1, help="clubs on the shirt-crest list (default 1: Leicester 95)")
    ap.add_argument("--le-offset-hours", dest="le_offset", type=float, help="hours from local time to the LE log clock (default: this machine's local-to-UTC offset; EDT = 4)")
    ap.add_argument("--team", type=int, default=95, help="the listed club whose crest path is counted in the LE log (INFO only)")
    ap.add_argument("--shot", help="the Tester's pitch screenshot")
    ap.add_argument("--expect-k", dest="expect_k", type=int)
    ap.add_argument("--look", choices=["pass", "fail"])
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.log:
        ap.error("--log is required")
    if a.since and not re.match(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$", a.since):
        ap.error("--since must be 'YYYY-MM-DD HH:MM:SS'")
    with open(a.log, encoding="utf-8", errors="replace") as f:
        turbo_text = f.read()
    le_text = None
    if a.lelog:
        with open(a.lelog, encoding="utf-8", errors="replace") as f:
            le_text = f.read()
    print("crest2d_livecheck on %s" % a.log)
    return 0 if check(turbo_text, le_text, a.since or "", a.clubs, a.shot, a.expect_k, a.look, a.team, a.le_offset) == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
