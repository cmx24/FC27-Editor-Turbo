"""E15 delivery round fix: the club clip pick and the decode cache (no game files needed).

    python -I test_club_pick.py [--module <path to a club_clips.py>]

Fail-old: point --module at the 4b56748c copy (git show 4b56748c:turbo/tools/fc27_commentary/club_clips.py), whose
pick_shortest takes the shortest row even when it is keyed by two clubs (home + away) and whose cache is write-once by id.
  P1  a row keyed by this club alone wins over a SHORTER two-club row (that one may say the opponent)
  P2  with only two-club rows, the shortest of them is still picked (no club loses its clip)
  P3  a cached <id>.wav whose sidecar names another segment (the index was rebuilt) does not count as the clip
The log prints the module path and md5 first, so a fail-old log names the copy it ran.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", default=os.path.join(HERE, "club_clips.py"))
    a = ap.parse_args(argv)
    with open(a.module, "rb") as f:
        print("module: %s md5 %s" % (os.path.abspath(a.module), hashlib.md5(f.read()).hexdigest()))
    sys.path.insert(0, HERE)  # fc27_commentary + club_ids of this folder
    spec = importlib.util.spec_from_file_location("club_clips_under_test", a.module)
    cc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cc)
    fails = []

    def check(cond, what):
        print(("PASS " if cond else "FAIL ") + what)
        if not cond:
            fails.append(what)

    # (id, set, family, segment, variation, param, samples, clubs)
    b = cc.pick_shortest([(7, "a", "f_two", 3, 0, "home_ID", 30000, 2),
                          (7, "a", "f_one", 1, 0, "home_ID", 90000, 1)])
    check(b[7][2] == "f_one", "P1 the row naming only club 7 wins over a shorter home+away row (picked %s)" % b[7][2])
    b = cc.pick_shortest([(9, "a", "f_long", 3, 0, "home_ID", 90000, 2),
                          (9, "a", "f_short", 1, 0, "away_ID", 30000, 2)])
    check(b[9][2] == "f_short", "P2 only two-club rows: the shortest is still picked (picked %s)" % b[9][2])

    d = tempfile.mkdtemp(prefix="e15pick_")
    try:
        wav = cc.clip_path(d, "eng_us", 95)
        os.makedirs(os.path.dirname(wav))
        with open(wav, "wb") as f:
            f.write(b"RIFF")
        with open(os.path.splitext(wav)[0] + ".json", "w", encoding="utf-8") as f:
            json.dump({"set": "a", "family": "f_old", "segment": 4}, f)
        row = {"set": "a", "family": "f_new", "segment": 1}
        fn = getattr(cc, "cache_matches", None)
        check(fn is not None and not fn(wav, row), "P3 a cached 95.wav from another segment is not taken as the clip (stale after an index rebuild)")
        check(fn is not None and fn(wav, {"set": "a", "family": "f_old", "segment": 4}), "P3b the same segment is kept (no needless decode)")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("RESULT %d FAIL" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
