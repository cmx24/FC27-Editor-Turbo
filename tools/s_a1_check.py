"""s_a1_check.py - the in-game pass point S-A1 of the 2.4 tuning track (docs/TACTICS_OVERHAUL_DECISION.md 3.1 / WP-A1), READ-ONLY.

It reads Turbo's log (turbo_output\\turbo_gui.log) of the LAST game session (from its last "hooks installed; press" line) and checks:
  1  signature   game hooks: signature attrib_db_getter: found, match 0x... -> 0x14C26B128
  2  chain       tuning: chain locate ok <n> groups <m> knobs <ms> ms          (n >= --min-groups, default 2) and no
                 "tuning: chain ... REFUSED" / "chain locate found no group" / "chain locate REFUSED" line
  3  boot        tuning: profile applied (boot) <k> knobs (read back ok)         (k >= 1; the main menu, ONLY after Lua reported it: run
                 Live Editor's script turbo_gui_load.lua at the main menu once "tuning: chain locate ok" is in the log; the boot write
                 comes within 60 s of that report. Without it there is no boot write by design: then use --phase hub only)
  4  career_load tuning: profile applied (career_load) <k> knobs (read back ok)  (the hub of the loaded career)
  5  hub         tuning: profile applied (hub) <k> knobs (read back ok)          (right after career_load, and every hub entry)
  6  safety      no "READ-BACK MISMATCH", no "tuning: profile not applied (...)", boot before career_load before hub
It never touches the game, the dev service or game memory. It prints PASS / FAIL / INCONCLUSIVE per check and exits 0 (all PASS), 1 (a
FAIL) or 3 (INCONCLUSIVE: a line not there yet, e.g. the hub not reached).

Usage (python -I, from the repository or anywhere):
  s_a1_check.py --setup [--value shape.width_in=1.0 ...]   BEFORE the launch: writes turbo_output\\tuning_profile.json {"on": true, ...}
                                                           (FC 27's own values by default: shape.width_in 1.0, lineband.deep_max 30 ->
                                                           no gameplay change; an existing profile is kept as tuning_profile.json.bak-<stamp>)
                                                           and the test switch turbo_output\\tuning_test_unproven_on.txt (no knob is
                                                           proven yet: without it the profile writes nothing)
  s_a1_check.py --phase boot --wait 120                    at the main menu: checks 1-3 (polls the log up to 120 s)
  s_a1_check.py --phase hub --wait 60                      in the LTEST hub after the career load: checks 1-6
  s_a1_check.py --phase hub --no-boot --wait 60            the same without the boot check (no main-menu report from Lua was made)
  s_a1_check.py --restore                                  after the session: removes the test switch, puts the kept profile back
                                                           (or switches it off)
  s_a1_check.py --self-test                                offline proof on synthetic logs (PASS, FAIL and INCONCLUSIVE cases)
  options: --log PATH (default <LE>\\turbo_output\\turbo_gui.log), --out DIR (default <LE>\\turbo_output), --min-groups N
"""
import argparse, json, os, re, sys, time

LE = r"C:\FC 27 Live Editor"
SESSION_START = re.compile(r"hooks installed; press ")
LINE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
RX = {
    "signature": re.compile(r"game hooks: signature attrib_db_getter: found, match 0x([0-9A-F]+) -> 0x([0-9A-F]+)"),
    "signature_bad": re.compile(r"game hooks: signature attrib_db_getter: (missing|ambiguous|bad pattern|skipped)", re.I),
    "chain": re.compile(r"tuning: chain locate ok (\d+) groups (\d+) knobs (\d+) ms"),
    "chain_bad": re.compile(r"tuning: chain locate (REFUSED|found no group)|tuning: chain \S+: .*REFUSED"),
    "manager": re.compile(r"tuning: attribute manager: (.*)"),
    "fallback": re.compile(r"the attribulator is located by the name search \(fallback\)"),
    "applied": re.compile(r"tuning: profile applied \((boot|career_load|hub)\) (\d+) knobs \(read back ok\)"),
    "not_applied": re.compile(r"tuning: profile not applied \((boot|career_load|hub)\): (.*)"),
    "nothing": re.compile(r"tuning: profile \((boot|career_load|hub)\): nothing to apply(.*)"),
    "mismatch": re.compile(r"READ-BACK MISMATCH"),
}
TEST_SWITCH = "tuning_test_unproven_on.txt"  # the coordinator's proof-session switch (never shipped, never created by Turbo's view)
DEFAULT_VALUES = {"shape.width_in": 1.0, "lineband.deep_max": 30}  # FC 27's own values (knobs_6AC07E31.json): no gameplay change


def session_lines(text):
    """(timestamp, message) of the last game session in the log"""
    lines = text.splitlines()
    start = 0
    for i, l in enumerate(lines):
        if SESSION_START.search(l):
            start = i
    out = []
    for l in lines[start:]:
        m = LINE.match(l)
        out.append((m.group(1), m.group(2)) if m else ("", l))
    return out


def evaluate(lines, phase, min_groups, boot=True):
    """[(name, verdict, detail)]; verdict PASS / FAIL / INCONCLUSIVE"""
    res = []
    want = ["signature", "chain"] + (["boot"] if boot else []) + (["career_load", "hub", "safety"] if phase in ("hub", "all") else [])

    def first(rx, pred=None):
        for ts, msg in lines:
            m = rx.search(msg)
            if m and (pred is None or pred(m)):
                return ts, msg, m
        return None

    if "signature" in want:
        bad = first(RX["signature_bad"])
        ok = first(RX["signature"])
        if bad:
            res.append(("signature", "FAIL", bad[1]))
        elif ok:
            res.append(("signature", "PASS", "%s  (G = 0x%s)" % (ok[0], ok[2].group(2))))
        else:
            res.append(("signature", "INCONCLUSIVE", "no 'game hooks: signature attrib_db_getter' line in this session yet"))
    if "chain" in want:
        bad = first(RX["chain_bad"])
        ok = first(RX["chain"])
        fb = first(RX["fallback"])
        mgr = first(RX["manager"])
        extra = ("; manager: " + mgr[2].group(1)) if mgr else ""
        if bad:
            res.append(("chain", "FAIL", bad[1] + extra))
        elif ok and int(ok[2].group(1)) >= min_groups:
            res.append(("chain", "PASS", "%s  %s%s" % (ok[0], ok[1], extra)))
        elif ok:
            res.append(("chain", "FAIL", "%s (fewer than %d groups)%s" % (ok[1], min_groups, extra)))
        elif fb:
            res.append(("chain", "FAIL", "located by the name search (fallback): " + fb[1]))
        else:
            res.append(("chain", "INCONCLUSIVE", "no 'tuning: chain locate ok' line yet (the locate runs after the signature scan, ~70-90 s after the launch)"))
    stamps = {}
    for moment in ("boot", "career_load", "hub"):
        if moment not in want:
            continue
        ok = first(RX["applied"], lambda m, mo=moment: m.group(1) == mo and int(m.group(2)) >= 1)
        bad = first(RX["not_applied"], lambda m, mo=moment: m.group(1) == mo)
        none = first(RX["nothing"], lambda m, mo=moment: m.group(1) == mo)
        if ok:
            stamps[moment] = ok[0]
            res.append((moment, "PASS", "%s  %s" % (ok[0], ok[1])))
        elif bad:
            res.append((moment, "FAIL", bad[1]))
        elif none:
            res.append((moment, "FAIL", none[1] + "  (is turbo_output\\tuning_profile.json on, with located knobs? run --setup before the launch)"))
        else:
            hint = {"boot": "wait at the main menu (the profile must be on before the launch)",
                    "career_load": "load the LTEST career and stay in its hub",
                    "hub": "stay in the hub a few seconds"}[moment]
            res.append((moment, "INCONCLUSIVE", "no 'tuning: profile applied (%s)' line yet: %s" % (moment, hint)))
    if "safety" in want:
        mm = first(RX["mismatch"])
        na = first(RX["not_applied"])
        names = (["boot"] if boot else []) + ["career_load", "hub"]
        order = [stamps.get(k) for k in names]
        if mm:
            res.append(("safety", "FAIL", mm[1]))
        elif na:
            res.append(("safety", "FAIL", na[1]))
        elif None in order:
            res.append(("safety", "INCONCLUSIVE", "the moments are not all in the log yet"))
        elif order != sorted(order):
            res.append(("safety", "FAIL", "the moments are out of order: " + ", ".join("%s %s" % (n, o) for n, o in zip(names, order))))
        else:
            res.append(("safety", "PASS", "no read-back mismatch, no refused moment; " + " <= ".join("%s %s" % (n, o) for n, o in zip(names, order))))
    return res


def verdict(res):
    if any(v == "FAIL" for _, v, _ in res):
        return "FAIL", 1
    if any(v == "INCONCLUSIVE" for _, v, _ in res):
        return "INCONCLUSIVE", 3
    return "PASS", 0


def run_check(log, phase, wait, min_groups, boot=True):
    t0 = time.time()
    while True:
        try:
            text = open(log, encoding="utf-8", errors="replace").read()
        except OSError as e:
            text = ""
            err = str(e)
        else:
            err = ""
        res = evaluate(session_lines(text), phase, min_groups, boot) if text else [("log", "INCONCLUSIVE", "cannot read %s: %s" % (log, err))]
        v, code = verdict(res)
        if code != 3 or time.time() - t0 >= wait:
            break
        time.sleep(3)
    for name, vv, detail in res:
        print("%-12s %-12s %s" % (name, vv, detail))
    print("S-A1 %s: %s (%.0f s)" % (phase, v, time.time() - t0))
    return code


def setup(out, values):
    p = os.path.join(out, "tuning_profile.json")
    if os.path.exists(p):
        bak = p + ".bak-" + time.strftime("%Y%m%d-%H%M%S")
        os.replace(p, bak)
        print("kept the existing profile as", bak)
    prof = {"on": True, "preset": "custom", "values": values}
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(prof, indent=1) + "\n")
    os.replace(tmp, p)
    print("wrote", p, json.dumps(prof))
    # 2.4 (review M5): the profile writes only knobs proven in a played match; no knob has a proof yet, so the proof sessions (S-A1, the
    # X1 boot retest) need the coordinator's test switch. --restore removes it again
    sw = os.path.join(out, TEST_SWITCH)
    with open(sw, "w", encoding="utf-8", newline="\n") as f:
        f.write("coordinator proof session (tools/s_a1_check.py --setup %s): the tuning profile also writes knobs whose effect is not proven\n"
                % time.strftime("%Y-%m-%d %H:%M:%S"))
    print("wrote", sw, "(the profile may write unproven knobs; --restore removes it)")
    for f in ("tuning_off.txt", "tuning_profile_off.txt", "call_attrib_off.txt", "tactics_off.txt"):
        if os.path.exists(os.path.join(out, f)):
            print("WARNING: kill switch turbo_output\\%s is present: the moments will not write" % f)
    return 0


def restore(out):
    sw = os.path.join(out, TEST_SWITCH)
    if os.path.exists(sw):
        os.remove(sw)
        print("removed", sw)
    p = os.path.join(out, "tuning_profile.json")
    baks = sorted(x for x in os.listdir(out) if x.startswith("tuning_profile.json.bak-"))
    if baks:
        os.replace(os.path.join(out, baks[-1]), p)
        print("put back", baks[-1])
    elif os.path.exists(p):
        prof = json.load(open(p, encoding="utf-8"))
        prof["on"] = False
        open(p, "w", encoding="utf-8", newline="\n").write(json.dumps(prof, indent=1) + "\n")
        print("switched the profile off:", p)
    return 0


# ------------------------------------------------------------------------------------------------ self-test (synthetic logs)
SAMPLE_GOOD = """2026-10-14 09:00:00.000  hooks installed; press Num + in game to show Turbo
2026-10-14 09:00:01.000  game hooks: signature attrib_db_getter: found, match 0x141077070 -> 0x14C26B128
2026-10-14 09:01:10.000  gameplay tuning: knobs file C:\\FC 27 Live Editor\\turbo\\tuning\\knobs_6AC07E31.json for build 6AC07E31-2145C000: 23 knobs
2026-10-14 09:01:19.000  tuning: attribute manager: G 0x14C26B128, mgr 0x20000000, current database Fifa/Attribulator/Gameplay/gameplayattribdb (index 0), prematch database at mgr+0x8, 15 of 15 gameplay databases (8000 ms)
2026-10-14 09:01:19.100  tuning: chain attrib.attackinglinelengthwidth: base 0x10000000 (identity ok)
2026-10-14 09:01:44.000  tuning: chain cross-check with the name search (first run on this build): 2 agree, 0 refused (24.9 s)
2026-10-14 09:01:44.001  tuning: chain locate ok 2 groups 10 knobs 34001 ms
2026-10-14 09:01:45.000  tuning: profile applied (boot) 2 knobs (read back ok)
2026-10-14 09:01:45.001  gameplay tuning re-apply: Team width with the ball 1.00 -> 1.00, Deep: up to depth 30 -> 30 (read back ok) [job 12 us, 40 reads]
2026-10-14 09:03:10.000  tuning: profile applied (career_load) 2 knobs (read back ok)
2026-10-14 09:03:10.050  tuning: profile applied (hub) 2 knobs (read back ok)
"""


def self_test():
    ok = True

    def expect(name, text, phase, want_code, want_fail=None):
        nonlocal ok
        res = evaluate(session_lines(text), phase, 2)
        v, code = verdict(res)
        good = code == want_code and (want_fail is None or any(n == want_fail and vv == "FAIL" for n, vv, _ in res))
        ok = ok and good
        print("  %s %s: %s (exit %d)" % ("ok  " if good else "FAIL", name, v, code))
        if not good:
            for r in res:
                print("       ", r)

    old = "2026-10-13 21:00:00.000  hooks installed; press Num +\n2026-10-13 21:00:01.000  tuning: profile not applied (hub): G2: x\n"
    expect("a good session, hub phase", old + SAMPLE_GOOD, "hub", 0)
    expect("a good session, boot phase", SAMPLE_GOOD, "boot", 0)
    no_boot = SAMPLE_GOOD.replace("2026-10-14 09:01:45.000  tuning: profile applied (boot) 2 knobs (read back ok)\n", "")
    expect("no boot line, hub phase: inconclusive", no_boot, "hub", 3)
    res = evaluate(session_lines(no_boot), "hub", 2, boot=False)
    good = verdict(res)[1] == 0 and not any(n == "boot" for n, _, _ in res)
    ok = ok and good
    print("  %s no boot line, hub phase, --no-boot: PASS (exit %d)" % ("ok  " if good else "FAIL", verdict(res)[1]))
    expect("boot only so far, hub phase: inconclusive", SAMPLE_GOOD.split("2026-10-14 09:03:10.000")[0], "hub", 3)
    expect("no chain line yet: inconclusive", SAMPLE_GOOD.split("2026-10-14 09:01:10.000")[0], "boot", 3)
    expect("signature missing: FAIL", SAMPLE_GOOD.replace("signature attrib_db_getter: found, match 0x141077070 -> 0x14C26B128",
                                                          "signature attrib_db_getter: missing (no match)"), "boot", 1, "signature")
    expect("a refused group: FAIL", SAMPLE_GOOD.replace("base 0x10000000 (identity ok)", "REFUSED for this session: the instance 0x10000000 is a X, not a Y"),
           "boot", 1, "chain")
    expect("one group only: FAIL", SAMPLE_GOOD.replace("locate ok 2 groups", "locate ok 1 groups"), "boot", 1, "chain")
    expect("the name search fallback: FAIL", SAMPLE_GOOD.replace("tuning: chain locate ok 2 groups 10 knobs 34001 ms",
                                                                 "tuning: signature attrib_db_getter was not found on this game build: the attribulator is located by the name search (fallback)"),
           "boot", 1, "chain")
    expect("profile off at boot: FAIL", SAMPLE_GOOD.replace("tuning: profile applied (boot) 2 knobs (read back ok)",
                                                            "tuning: profile (boot): nothing to apply"), "boot", 1, "boot")
    expect("a refused hub moment: FAIL", SAMPLE_GOOD + "2026-10-14 09:04:00.000  tuning: profile not applied (hub): G2: a match was being set up 1 s ago\n",
           "hub", 1, "safety")
    expect("a read-back mismatch: FAIL", SAMPLE_GOOD + "2026-10-14 09:04:00.000  gameplay tuning re-apply: x READ-BACK MISMATCH (y), undone from the snapshot\n",
           "hub", 1, "safety")
    # setup / restore round trip in a temporary folder
    import tempfile
    d = tempfile.mkdtemp()
    open(os.path.join(d, "tuning_profile.json"), "w").write('{"on": false, "values": {"shape.width_in": 1.4}}')
    setup(d, dict(DEFAULT_VALUES))
    prof = json.load(open(os.path.join(d, "tuning_profile.json")))
    good = prof["on"] is True and prof["values"] == DEFAULT_VALUES and any(x.startswith("tuning_profile.json.bak-") for x in os.listdir(d))
    good = good and os.path.exists(os.path.join(d, TEST_SWITCH))
    restore(d)
    good = good and json.load(open(os.path.join(d, "tuning_profile.json")))["values"] == {"shape.width_in": 1.4}
    good = good and not os.path.exists(os.path.join(d, TEST_SWITCH))
    print("  %s setup / restore round trip" % ("ok  " if good else "FAIL"))
    ok = ok and good
    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="S-A1: the tuning profile's boot / career_load / hub moments and the chain locate, from Turbo's log")
    ap.add_argument("--phase", choices=["boot", "hub", "all"], default="all")
    ap.add_argument("--wait", type=float, default=0, help="poll the log up to this many seconds (at most 120) while a line is missing")
    ap.add_argument("--log", default=os.path.join(LE, "turbo_output", "turbo_gui.log"))
    ap.add_argument("--out", default=os.path.join(LE, "turbo_output"))
    ap.add_argument("--min-groups", type=int, default=2)
    ap.add_argument("--no-boot", action="store_true", help="skip the boot check (no main-menu report from Lua was made in this session)")
    ap.add_argument("--setup", action="store_true")
    ap.add_argument("--value", action="append", default=[], help="knob=value for --setup (default: FC 27's own values of two verified knobs)")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.setup:
        values = dict(DEFAULT_VALUES)
        if a.value:
            values = {}
            for kv in a.value:
                k, v = kv.split("=", 1)
                values[k.strip()] = float(v)
        return setup(a.out, values)
    if a.restore:
        return restore(a.out)
    return run_check(a.log, "all" if a.phase == "all" else a.phase, min(max(a.wait, 0), 120), a.min_groups, not a.no_boot)


if __name__ == "__main__":
    sys.exit(main())
