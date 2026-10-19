"""team_preset_livecheck.py - the 2.20 "My team" preset live check (brief 2.20-S2-team-defaults, W1): reads turbo_gui.log and the Live Editor log
after one LEICESTER launch and prints PASS / FAIL / INCONCLUSIVE per point and one RESULT line. Read-only: it never touches the game, the
install or turbo_output (it only reads the files it is given).

  py -3 -I tools/team_preset_livecheck.py --gui-log "C:/FC 27 Live Editor/turbo_output/turbo_gui.log" \\
      --le-log "C:/FC 27 Live Editor/Logs/live_editor_<date>.log" --since "2026-10-19 12:00:00" \\
      [--reapply-before before.json --reapply-after after.json]
  py -3 -I tools/team_preset_livecheck.py --self-test      (offline: synthetic passing, failing and inconclusive logs)

The operator's steps (LEICESTER career, never saved, about 2 minutes; the Tester's runner launches and kills the game):
  0. Before: copy turbo_output\\reapply_edits.json to before.json (optional, point 6). Note the jersey fit, contract end, morale and squad role of
     two players of the club in Edit Players (screenshot S0), and that "Extra XP development" and "Does not decline" are NOT ticked in
     Players > development and in Tools > Bulk edit (screenshot S1; "Advanced > Career > growth" card too).
  1. Tools > Bulk edit > "Apply the My team preset". Wait for the toast (a few seconds). Screenshot S2: the Teams > Mass actions status line.
  2. Edit Players, the same two players, Squad hub: jersey fit Tight, contract +60 months, morale Very Happy, role Rotation (or Prospect for
     an under-23 below the club median), offers blocked (screenshot S3). Copy reapply_edits.json to after.json. Kill the game (the runner).
     Never save.

Why this check reads logs and not the reapply rows (CHANGE to the brief, board row 11:31): reapply rows keep only the development-plan
fields plus overall and potential (core/reapply kept_player_field), so none of the five preset fields ever reaches reapply_edits.json. Point
6 proves the negative instead: the preset added no kept attribute, potential or overall row.

What it reads:
  session   the last "Turbo GUI loading in process" session in turbo_gui.log at or after --since
  fit       "My team preset: jersey fit tight on N of N players (X changed, 0 not written)" (the window wrote players.jerseyfit = 1, read back)
  sent      "My team preset: sent team_mass for team T actions=long_contract,squad_roles,morale,block_offers role_save=false role_pins=N ..."
            (exactly the four game-side actions, never "all"/form/match_xp/dev_bonus, no saved role rule, one pin per player)
  lua       Live Editor's log: "[Turbo] team_mass done: team T (N players) - long contract: ... | squad roles: ... | morale (very happy): ... |
            block offers: ..." with no "not done" and none of form / match XP / dev bonus, and the same player count N
  errors    no "frame error:" line in the session
  reapply   (with --reapply-before/--reapply-after) no attribute, potential or overall row added or changed
"""
import argparse
import json
import re
import sys

RE_TIME = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
RE_FIT = re.compile(r"My team preset: jersey fit tight on (?P<tight>\d+) of (?P<n>\d+) players \((?P<changed>\d+) changed, (?P<failed>\d+) not written\)")
RE_SENT = re.compile(r"My team preset: sent team_mass for team (?P<team>\d+) actions=(?P<acts>[a-z_,]+) role_save=(?P<save>true|false) role_pins=(?P<pins>\d+)")
RE_DONE = re.compile(r"\[Turbo\] team_mass (?P<verb>done|stopped): team (?P<team>\d+) \((?P<n>\d+) players\) - (?P<parts>.*)")
WANT_ACTIONS = ["long_contract", "squad_roles", "morale", "block_offers"]
WANT_PARTS = ["long contract:", "squad roles:", "morale (very happy):", "block offers:"]
FORBIDDEN_PARTS = ["form:", "match XP:", "dev bonus:"]


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


def last_match(lines, rx):
    found = None
    for l in lines:
        m = rx.search(l)
        if m:
            found = m
    return found


def reapply_keys(text):
    """{(playerid, field): (value, was)} of a reapply_edits.json text; None when it is not readable"""
    try:
        j = json.loads(text)
    except ValueError:
        return None
    out = {}
    for e in j.get("players", []) if isinstance(j, dict) else []:
        if not isinstance(e, dict):
            continue
        for f, v in (e.get("fields") or {}).items():
            if isinstance(v, dict):
                out[(e.get("playerid"), f)] = (v.get("value"), v.get("was"))
    return out


def check(gui_text, le_text, since="", before=None, after=None):
    """Returns (result, [(point, verdict, detail)])"""
    out = []
    lines = session_lines(gui_text, since)
    if not lines:
        out.append(("session", "FAIL", "no 'Turbo GUI loading in process' line at or after %s" % (since or "the start")))
        return "FAIL", out
    out.append(("session", "PASS", "%d log lines" % len(lines)))

    fit = last_match(lines, RE_FIT)
    if not fit:
        out.append(("fit", "FAIL", "no 'My team preset: jersey fit tight on N of N players' line: the button was not pressed, or was refused (look for 'My team preset:' toasts)"))
    elif fit.group("tight") != fit.group("n") or int(fit.group("n")) < 1 or fit.group("failed") != "0":
        out.append(("fit", "FAIL", "tight on %s of %s players, %s not written" % (fit.group("tight"), fit.group("n"), fit.group("failed"))))
    else:
        out.append(("fit", "PASS", "jersey fit Tight on %s of %s players (%s changed)" % (fit.group("tight"), fit.group("n"), fit.group("changed"))))

    sent = last_match(lines, RE_SENT)
    n_pins = None
    if not sent:
        out.append(("sent", "FAIL", "no 'My team preset: sent team_mass' line"))
    else:
        acts = sent.group("acts").split(",")
        n_pins = int(sent.group("pins"))
        if acts != WANT_ACTIONS:
            out.append(("sent", "FAIL", "actions %s, want exactly %s" % (acts, WANT_ACTIONS)))
        elif sent.group("save") != "false":
            out.append(("sent", "FAIL", "role_save=%s: the preset must not save a role rule" % sent.group("save")))
        elif fit and n_pins != int(fit.group("n")):
            out.append(("sent", "FAIL", "%d role pins for %s players" % (n_pins, fit.group("n"))))
        else:
            out.append(("sent", "PASS", "team %s, actions %s, role_save false, %d pins" % (sent.group("team"), ",".join(acts), n_pins)))

    done = last_match(le_text.splitlines(), RE_DONE) if le_text else None
    if not le_text:
        out.append(("lua", "INCONCLUSIVE", "no Live Editor log given (--le-log)"))
    elif not done:
        out.append(("lua", "FAIL", "no '[Turbo] team_mass done:' line in the Live Editor log: the command did not run, or Lua refused it"))
    else:
        parts = done.group("parts")
        problems = []
        if done.group("verb") != "done":
            problems.append("team_mass stopped")
        if "not done" in parts:
            problems.append("an action 'not done': " + parts[parts.find("not done"):][:160])
        # the summary is "<label>: <text> | <label>: <text> ..."; a label starts each part
        heads = [x.strip() for x in parts.split(" | ")]
        for p in WANT_PARTS:
            if not any(h.startswith(p) for h in heads):
                problems.append("missing '%s'" % p)
        for p in FORBIDDEN_PARTS:
            if any(h.startswith(p) for h in heads):
                problems.append("ran '%s' (not part of the preset)" % p)
        if fit and done.group("n") != fit.group("n"):
            problems.append("Lua saw %s players, the window %s" % (done.group("n"), fit.group("n")))
        if sent and done.group("team") != sent.group("team"):
            problems.append("Lua ran team %s, the window sent team %s" % (done.group("team"), sent.group("team")))
        out.append(("lua", "FAIL" if problems else "PASS", "; ".join(problems) if problems else parts[:200]))

    errs = [l for l in lines if "frame error:" in l]
    out.append(("errors", "FAIL" if errs else "PASS", errs[0].strip()[:200] if errs else "no frame error in the session"))

    if before is None or after is None:
        out.append(("reapply", "INCONCLUSIVE", "no --reapply-before/--reapply-after given"))
    else:
        b, a = reapply_keys(before), reapply_keys(after)
        if b is None or a is None:
            out.append(("reapply", "FAIL", "a reapply_edits.json is not readable JSON"))
        else:
            added = sorted(k for k in a if k not in b or a[k] != b[k])
            out.append(("reapply", "FAIL" if added else "PASS",
                        ("%d row(s) added or changed: %s" % (len(added), added[:4])) if added else
                        "the preset added no attribute, potential or overall row (as designed: its five fields are not kept fields)"))

    verdicts = [v for _, v, _ in out]
    result = "FAIL" if "FAIL" in verdicts else ("INCONCLUSIVE" if "INCONCLUSIVE" in verdicts else "PASS")
    return result, out


def report(result, out):
    for point, verdict, detail in out:
        print("  %-8s %-12s %s" % (point, verdict, detail))
    print("RESULT %s" % result)
    return 0 if result == "PASS" else 1


def _gui(*body):
    head = ["2026-10-19 12:00:01 Turbo GUI loading in process 1234"]
    return "\n".join(head + ["2026-10-19 12:00:%02d %s" % (10 + i, b) for i, b in enumerate(body)]) + "\n"


FIT = "My team preset: jersey fit tight on 26 of 26 players (25 changed, 0 not written)"
SENT = "My team preset: sent team_mass for team 1 actions=long_contract,squad_roles,morale,block_offers role_save=false role_pins=26 (prospect 5, rotation 21, left alone 0)"
LE_OK = ("2026-10-19 12:00:20 INFO [Turbo] team_mass done: team 1 (26 players) - long contract: 24 extended to 60 months | "
         "squad roles: 26 set | morale (very happy): 26 set | block offers: 26 players blocked")
LE_BAD = LE_OK.replace("block offers: 26 players blocked", "block offers: not done - hook missing")
LE_EXTRA = LE_OK + " | dev bonus: 26 players +2"
REAPPLY0 = json.dumps({"players": [{"playerid": 5, "fields": {"overallrating": {"value": 80, "was": 79}}}]})
REAPPLY1 = json.dumps({"players": [{"playerid": 5, "fields": {"overallrating": {"value": 80, "was": 79}}},
                                   {"playerid": 6, "fields": {"potential": {"value": 90, "was": 85}}}]})


def self_test():
    cases = [
        ("pass: all points, reapply rows unchanged", _gui(FIT, SENT), LE_OK, REAPPLY0, REAPPLY0, "PASS"),
        ("pass without the reapply files is inconclusive", _gui(FIT, SENT), LE_OK, None, None, "INCONCLUSIVE"),
        ("fail: a player not tight", _gui(FIT.replace("26 of 26", "25 of 26").replace("0 not", "1 not"), SENT), LE_OK, REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: a sixth action (form) sent", _gui(FIT, SENT.replace("block_offers role", "block_offers,form role")), LE_OK, REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: a saved role rule", _gui(FIT, SENT.replace("role_save=false", "role_save=true")), LE_OK, REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: an action not done on the Lua side", _gui(FIT, SENT), LE_BAD, REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: Lua ran dev bonus", _gui(FIT, SENT), LE_EXTRA, REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: the preset added a potential row", _gui(FIT, SENT), LE_OK, REAPPLY0, REAPPLY1, "FAIL"),
        ("fail: the button was never pressed", _gui("something else"), LE_OK, REAPPLY0, REAPPLY0, "FAIL"),
        ("inconclusive: no Live Editor log given", _gui(FIT, SENT), "", REAPPLY0, REAPPLY0, "INCONCLUSIVE"),
        ("fail: Lua never ran the command", _gui(FIT, SENT), "2026-10-19 12:00:20 INFO something else", REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: Lua saw another player count", _gui(FIT, SENT), LE_OK.replace("(26 players)", "(25 players)"), REAPPLY0, REAPPLY0, "FAIL"),
        ("fail: a frame error", _gui(FIT, SENT, "frame error: boom"), LE_OK, REAPPLY0, REAPPLY0, "FAIL"),
    ]
    bad = 0
    for name, gui, le, b, a, want in cases:
        result, out = check(gui, le, "2026-10-19 12:00:00", b, a)
        ok = result == want
        print("  %s %s -> %s" % ("ok  " if ok else "FAIL", name, result))
        bad += not ok
    # a log with no session after --since
    result, _ = check(_gui(FIT, SENT), LE_OK, "2026-10-19 13:00:00", REAPPLY0, REAPPLY0)
    print("  %s no session after --since -> %s" % ("ok  " if result == "FAIL" else "FAIL", result))
    bad += result != "FAIL"
    print("SELF-TEST %s" % ("PASSED" if not bad else "FAILED"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gui-log", help="turbo_output/turbo_gui.log")
    ap.add_argument("--le-log", default="", help="Live Editor's Logs/live_editor_<date>.log")
    ap.add_argument("--since", default="", help="only sessions starting at or after 'YYYY-MM-DD HH:MM:SS'")
    ap.add_argument("--reapply-before", default="")
    ap.add_argument("--reapply-after", default="")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.gui_log:
        ap.error("--gui-log is required (or --self-test)")

    def read(p):
        if not p:
            return None
        with open(p, "r", encoding="utf-8", errors="replace") as f:
            return f.read()

    result, out = check(read(args.gui_log) or "", read(args.le_log) or "", args.since, read(args.reapply_before), read(args.reapply_after))
    return report(result, out)


if __name__ == "__main__":
    sys.exit(main())
