"""looks_studio_livecheck.py - the 2.17 Looks studio live check (brief 2.17-looks-studio, track U; route FULLBODY_FAST_ROUTE.md point 6):
reads turbo_gui.log after one LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never
touches the game, the install or turbo_output (it only reads the log file it is given).

  py -3 -I tools/looks_studio_livecheck.py --log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" --since "2026-10-17 22:40:00" [--player ID]
  py -3 -I tools/looks_studio_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  1. Career hub, Turbo window (F8): Faces & looks > Looks studio. Players. Search a real-face starter of the squad, click him.
  2. Click "Draw him" under the large pane, wait 2 s: it shows the game's picture ("Drawn by the game at ..."; head and shoulders,
     Tester 2026-10-17 22:09). Screenshot S1.
  3. Body type card: click "Tall, stocky" (or any other generic body). Wait 2 s: the pane draws him again. Screenshot S2.
  4. Click "Undo: Body type" at the top of the cards. Wait 2 s. Screenshot S3. Kill the game (the runner). Never save.

What it reads (ui/ui_looks_studio.cpp App::log lines; App::edit_field / App::undo lines; the capture service's own lines):
  session   the last "Turbo GUI loading in process" session at or after --since
  asked     "looks studio: picture asked for <name> (id N, camera 1)": the page asked the game for his full-body picture
  request   the capture service's line for that id with "mode 1" and "default descriptor" (the Architect's pre-registered request)
  picture   "looks studio: picture of id N (<format>)" after the ask: FAIL on "looks studio: no picture for id N"
  body      an edit "[players.bodytypecode = B]" after the first picture, then a new ask AND a new picture for the same id
  undo      "undo players.bodytypecode = A" after the body edit (the page's Undo through App's per-player Undo)
  errors    no "frame error:" line in the session (a draw exception in the page)
The picture is head and shoulders (mode 1 in game): S1-S3 show the page's honest line under it; that the body type reaches the model is
NOT proven by this check (the game shows the body in Customise > Edit Players > Edit Real Players).
"""
import argparse
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_ASK = re.compile(r"looks studio: picture asked for .* \(id (?P<id>\d+), camera (?P<cam>\d+)\)")
RE_PIC = re.compile(r"looks studio: picture of id (?P<id>\d+) \((?P<fmt>[^)]*)\)")
RE_NOPIC = re.compile(r"looks studio: no picture for id (?P<id>\d+)")
RE_BODY = re.compile(r"\[players\.bodytypecode = (?P<v>\d+)\]")
RE_UNDO = re.compile(r"undo players\.bodytypecode = (?P<v>\d+)")


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


def first(lines, rx, start=0, pid=None):
    """(index, match) of the first line at or after `start` that matches rx (and the id, when given); (-1, None) when none"""
    for i in range(start, len(lines)):
        m = rx.search(lines[i])
        if m and (pid is None or m.groupdict().get("id") == pid):
            return i, m
    return -1, None


def check(text, since="", player=""):
    """Returns (result, [(point, verdict, detail)])"""
    lines = session_lines(text, since)
    out = []
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))
    i_ask, m_ask = first(lines, RE_ASK, 0, player or None)
    if i_ask < 0:
        out.append(("asked", "FAIL", "no 'looks studio: picture asked for' line%s: the page was not opened on a player, or the game "
                                     "could not draw (a match, another picture)" % (" for id " + player if player else "")))
        return "FAIL", out
    pid = m_ask.group("id")
    out.append(("asked", "PASS" if m_ask.group("cam") == "1" else "FAIL", "id %s, camera %s (want 1)" % (pid, m_ask.group("cam"))))
    req = [l for l in lines[i_ask:i_ask + 40] if ("request" in l and pid in l and "mode " in l)]
    if not req:
        out.append(("request", "INCONCLUSIVE", "no capture request line for id %s within 40 lines of the ask" % pid))
    elif any("mode 1" in l and "default descriptor" in l for l in req):
        out.append(("request", "PASS", req[0].strip()[:160]))
    else:
        out.append(("request", "FAIL", "the request was not mode 1 with the default descriptor: " + req[0].strip()[:160]))
    i_pic, m_pic = first(lines, RE_PIC, i_ask, pid)
    i_no, m_no = first(lines, RE_NOPIC, i_ask, pid)
    if i_pic >= 0 and (i_no < 0 or i_pic < i_no):
        out.append(("picture", "PASS", "picture of id %s (%s)" % (pid, m_pic.group("fmt"))))
    else:
        out.append(("picture", "FAIL", lines[i_no].strip()[:200] if i_no >= 0 else "no picture line after the ask"))
        i_pic = -1
    if i_pic >= 0:
        i_body, m_body = first(lines, RE_BODY, i_pic)
        if i_body < 0:
            out.append(("body", "INCONCLUSIVE", "no body type change after the first picture (step 3 not done)"))
        else:
            i_ask2, _ = first(lines, RE_ASK, i_body, pid)
            i_pic2, _ = first(lines, RE_PIC, i_ask2, pid) if i_ask2 >= 0 else (-1, None)
            if i_ask2 >= 0 and i_pic2 >= 0:
                out.append(("body", "PASS", "body type %s written, then a new picture of id %s" % (m_body.group("v"), pid)))
            else:
                out.append(("body", "FAIL", "body type %s written but %s" % (m_body.group("v"), "no new ask" if i_ask2 < 0 else "no new picture")))
            i_undo, m_undo = first(lines, RE_UNDO, i_body)
            if i_undo >= 0:
                out.append(("undo", "PASS", "body type back to %s" % m_undo.group("v")))
            else:
                out.append(("undo", "INCONCLUSIVE", "no undo of the body type after the change (step 4 not done)"))
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
    head = ["2026-10-17 22:40:01 Turbo GUI loading in process 1234"]
    return "\n".join(head + ["2026-10-17 22:40:%02d %s" % (10 + i, b) for i, b in enumerate(body)]) + "\n"


ASK = "looks studio: picture asked for Test Player (id 4242, camera 1)"
REQ = "player capture: request Looks studio Test Player: id 4242, mode 1, extra 0 (default descriptor); desc id 4242"
PIC = "looks studio: picture of id 4242 (PNG 1024x1024)"
BODY = "Body type changed Average height, normal build -> Tall, stocky (Test Player)  [players.bodytypecode = 6]"
UNDO = "undo players.bodytypecode = 2"


def self_test():
    cases = [
        ("pass: every step", _log(ASK, REQ, PIC, BODY, ASK, REQ, PIC, UNDO), "PASS"),
        ("fail: no picture", _log(ASK, REQ, "looks studio: no picture for id 4242: the game did not finish"), "FAIL"),
        ("fail: camera 2", _log(ASK.replace("camera 1", "camera 2"), REQ, PIC), "FAIL"),
        ("fail: learned descriptor", _log(ASK, REQ.replace("default descriptor", "learned descriptor"), PIC, BODY, ASK, PIC, UNDO), "FAIL"),
        ("fail: no ask", _log("something else"), "FAIL"),
        ("fail: body but no new picture", _log(ASK, REQ, PIC, BODY, UNDO), "FAIL"),
        ("fail: frame error", _log(ASK, REQ, PIC, BODY, ASK, PIC, UNDO, "frame error: boom"), "FAIL"),
        ("inconclusive: steps 3-4 not done", _log(ASK, REQ, PIC), "INCONCLUSIVE"),
        ("inconclusive: no undo", _log(ASK, REQ, PIC, BODY, ASK, PIC), "INCONCLUSIVE"),
        ("fail: no session", "2026-10-17 22:40:01 nothing\n", "FAIL"),
    ]
    bad = 0
    for name, text, want in cases:
        got, _ = check(text)
        ok = got == want
        bad += 0 if ok else 1
        print("  %s %s (got %s, want %s)" % ("ok" if ok else "FAIL", name, got, want))
    got, _ = check(_log(ASK, REQ, PIC), player="1111")
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --player filters the id (got %s)" % ("ok" if ok else "FAIL", got))
    got, _ = check(_log(ASK, REQ, PIC), since="2026-10-18 00:00:00")
    ok = got == "FAIL"
    bad += 0 if ok else 1
    print("  %s --since skips older sessions (got %s)" % ("ok" if ok else "FAIL", got))
    print("SELF-TEST %s" % ("PASSED" if bad == 0 else "FAILED"))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default="C:/FC 27 Live Editor/turbo_output/turbo_gui.log")
    ap.add_argument("--since", default="", help="local time 'YYYY-MM-DD HH:MM:SS' of the launch (the log's own clock)")
    ap.add_argument("--player", default="", help="the player id picked in step 1 (default: the first one the page asked for)")
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
