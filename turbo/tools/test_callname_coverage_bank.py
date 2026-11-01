#!/usr/bin/env python3
"""Synthetic test of callname_coverage.py --bank (fc27_commentary bank json + wav folder layout): the full set's rows and
a second set's rows (segment_offset 200000) become the master's ids and segments, ids of a set whose rows were not given
are listed without a Preview, labels come from the DB words / --labels-from / the database export. No game data: every
id and name is made up. Python 3 standard library only; runs in well under a second.

usage: python -I turbo/tools/test_callname_coverage_bank.py
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FAILS = []


def check(name, cond):
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAILS.append(name)


def row(seg, key, i, inten=2):
    return {"segment": seg, "variation": 1000 + seg, key: i, "intensity": [inten], "cm_sim": [0, 1]}


def main():
    t = tempfile.mkdtemp(prefix="cov_bank_")
    full = {"turbo_bank": 1, "lang": "xxx_yy", "game_build": "1.0.0.1", "primary_set": "xxx_yy_full",
            "generic": [row(0, "commentaryid", 900001), row(1, "commentaryid", 900002), row(2, "commentaryid", 980001)],
            "real": [row(0, "playerid", 11), row(1, "playerid", 11), row(2, "playerid", 12)],
            "real_link": [row(0, "playerid", 12), row(1, "playerid", 13)],
            "real_high": [row(0, "playerid", 11, 1), row(1, "playerid", 14, 1)], "generic_high": [],
            "sets": {"xxx_yy_full": {"team": "", "kind": "full", "ids": {"generic": [900001, 900002, 980001],
                                                                         "simple": [11, 12], "link": [12, 13], "high": [11, 14]}},
                     "xxx_yy_demo": {"team": "", "kind": "demo", "ids": {"generic": [], "simple": [11, 21, 22], "link": [],
                                                                         "high": [21]}}},
            "union": {"generic": [900001, 900002, 980001], "simple": [11, 12, 21, 22], "link": [12, 13], "high": [11, 14, 21]}}
    demo = dict(full, primary_set="xxx_yy_demo", segment_offset=200000, generic=[], real_link=[],
                real=[row(5, "playerid", 21)], real_high=[row(3, "playerid", 21, 1)])
    for n, d in (("full.json", full), ("demo.json", demo)):
        with open(os.path.join(t, n), "w", encoding="utf-8") as f:
            json.dump(d, f)
    wavs = ["generic/pSIMPLE_SURNAME_%d_%d.wav" % (s, s) for s in (0, 1, 2)]
    wavs += ["real/pPLAYER_NAMES_SIMPLE_%d_%d.wav" % (s, s) for s in (0, 1, 2, 200005)]
    wavs += ["real_link/pPLAYER_NAMES_LINK_%d_%d.wav" % (s, s) for s in (0, 1)]
    wavs += ["real_high/pPLAYER_NAMES_HIGH_%d_%d.wav" % (s, s) for s in (0, 1, 200003)]
    with open(os.path.join(t, "wavs.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(wavs) + "\n")
    old = {"turbo_masters": 1, "language": "xxx_yy", "game": "fc26", "real_players": [11, 99], "generic_ids": [900001],
           "names": {"11": "Player Eleven"}, "generic_names": {"900001": "Oldword"}, "wav_dir": "X"}
    other = {"names": {"12": "Player Twelve", "11": "Not this"}, "generic_names": {"980001": "Highword"}}
    for n, d in (("old.json", old), ("other.json", other)):
        with open(os.path.join(t, n), "w", encoding="utf-8") as f:
            json.dump(d, f)
    os.makedirs(os.path.join(t, "db"))
    with open(os.path.join(t, "db", "commentarynames.txt"), "w", encoding="utf-8") as f:
        f.write("commentarystring\tcommentaryid\tcommentarystartingletter\tcommentarypreview\nDbone\t900001\t1\t1\nDbtwo \t900002\t1\t1\n")
    with open(os.path.join(t, "db", "playernames.txt"), "w", encoding="utf-8") as f:
        f.write("name\tnameid\tcommentaryid\nDbone\t1\t900001\n")
    with open(os.path.join(t, "db", "playernamemap.txt"), "w", encoding="utf-8") as f:
        f.write("playerid\tcommentaryid\n")
    with open(os.path.join(t, "players.csv"), "w", encoding="utf-8") as f:
        f.write("playerid,firstnameid,lastnameid,commonnameid\n13,5,6,0\n22,0,0,7\n")
    with open(os.path.join(t, "names.txt"), "w", encoding="utf-8") as f:
        f.write("#turbo-names 0 3\n5\tFirst\n6\tLast\n7\tCommon\n")
    tool = os.path.join(HERE, "callname_coverage.py")
    base = [sys.executable, "-I", tool, os.path.join(t, "wavs.txt"), "--bank", os.path.join(t, "full.json"),
            os.path.join(t, "old.json"), "--db", os.path.join(t, "db"), "--labels-from", os.path.join(t, "other.json"),
            "--db-players", os.path.join(t, "players.csv"), "--db-names", os.path.join(t, "names.txt"),
            "--wav-dir-json", "C:\\w"]

    print("one --bank (the full set):")
    r = subprocess.run(base + ["--write-master", os.path.join(t, "m1.json"), "--write-spoken", os.path.join(t, "s1.txt")],
                       capture_output=True, text=True)
    check("runs (exit 1: the old master misses names)", r.returncode == 1 and "wrote" in r.stdout)
    m = json.load(open(os.path.join(t, "m1.json"), encoding="utf-8"))
    check("game fc27, every own player of every set", m["game"] == "fc27" and m["real_players"] == [11, 12, 13, 21, 22])
    check("demo-only players listed without a Preview", m["no_preview_players"] == [21, 22]
          and "21" not in m["segments"]["real"] and m["real_simple_players"] == [11, 12, 21, 22])
    check("segments of the full set", m["segments"]["real"] == {"11": [0, 1], "12": [2]}
          and m["segments"]["real_link"] == {"12": [0], "13": [1]} and m["segments"]["real_high"] == {"11": [0], "14": [1]})
    check("generic ids and segments", m["generic_ids"] == [900001, 900002, 980001]
          and m["segments"]["generic"] == {"900001": [0], "900002": [1], "980001": [2]})
    check("labels: DB words, trimmed; --labels-from for an id the DB lacks; previous label kept apart",
          m["generic_names"] == {"900001": "Dbone", "900002": "Dbtwo", "980001": "Highword"}
          and m["generic_previous_labels"] == {"900001": "Oldword"})
    check("names: input master first, then --labels-from, then the database export",
          m["names"].get("11") == "Player Eleven" and m["names"].get("12") == "Player Twelve"
          and m["names"].get("13") == "First Last" and m["names"].get("22") == "Common" and m["unnamed_players"] == [14, 21])
    s = open(os.path.join(t, "s1.txt"), encoding="utf-8").read().split()
    check("spoken list: header + the ids in 900001..965000", s[:3] == ["#turbo-spoken", "xxx_yy", "2"] and s[3:] == ["900001", "900002"])
    check("the report names the set the missing players are in", "only in xxx_yy_demo: 2 [21, 22]" in r.stdout)

    print("two --bank (full + demo set, segment_offset 200000):")
    r = subprocess.run(base + ["--bank", os.path.join(t, "demo.json"), "--write-master", os.path.join(t, "m2.json")],
                       capture_output=True, text=True)
    m = json.load(open(os.path.join(t, "m2.json"), encoding="utf-8"))
    check("the demo player gets the encoded segment", m["segments"]["real"].get("21") == [200005]
          and m["segments"]["real_high"].get("21") == [200003])
    check("only the player no set's rows give stays without a Preview", m["no_preview_players"] == [22])
    r = subprocess.run(base + ["--bank", os.path.join(t, "full.json")], capture_output=True, text=True)
    check("the same offset twice is refused", r.returncode == 2 and "given twice" in r.stderr)
    r = subprocess.run([sys.executable, "-I", tool, os.path.join(t, "wavs.txt"), "--bank", os.path.join(t, "full.json"),
                        "x", os.path.join(t, "old.json")], capture_output=True, text=True)
    check("three positionals with --bank are refused", r.returncode == 2)
    print("test_callname_coverage_bank: %s" % ("ok" if not FAILS else "%d failed" % len(FAILS)))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
