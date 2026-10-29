"""The shipped slider map must let Load write on the current game build (6AD922D1).

Fails on 1d023891: verified_by_build 6AD922D1 had no ids, so "Load into the game" wrote nothing on Cassio's build.
Usage: python -I tests/check_slider_map_verified.py [map.json]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "turbo", "package", "turbo", "settings", "slider_map_6AC07E31.json")
# Cassio's on-screen markers 42/41/43 read back at custom 0x24/0x50/0x51 on 6AD922D1 (marker_2041_status.txt)
WANT = {"cpu.sprint_speed", "user.sprint_speed", "user.acceleration"}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    m = json.load(open(path, encoding="utf-8"))
    vb = m.get("verified_by_build", {}).get("6AD922D1", {})
    ids = set(vb.get("ids", []))
    entries = {e["id"]: e for e in m["entries"]}
    bad = []
    if not WANT <= ids:
        bad.append("6AD922D1 ids miss %s" % sorted(WANT - ids))
    if ids and not vb.get("proof"):
        bad.append("6AD922D1 ids without proof")
    for i in ids:
        e = entries.get(i)
        if e is None or (e.get("custom_index") is None and e.get("index") is None):
            bad.append("%s has no index" % i)
    print(("FAIL " + "; ".join(bad)) if bad else "PASS slider map 6AD922D1: %d verified ids" % len(ids))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
