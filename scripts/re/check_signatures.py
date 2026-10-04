"""Checks every signature in docs/re/C3-signatures.json against the image: prints hits and uniqueness.

Run: python scripts/re/check_signatures.py [docs/re/C3-signatures.json]
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from rx import *  # noqa

p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "docs", "re", "C3-signatures.json")
doc = json.load(open(p))
bad = 0
for s in doc["signatures"]:
    hits = find_sig(s["sig"], limit=5)
    want = int(s["va"], 16)
    ok = (want in hits) and (len(hits) == 1 or not s.get("unique", True))
    print("%-30s hits=%-3d %s %s" % (s["name"], len(hits), ["%#x" % h for h in hits], "OK" if ok else "MISMATCH"))
    if not ok:
        bad += 1
for name, va in doc["vtables_rva"].items():
    print("vtable %-26s %#x slot0=%#x" % (name, BASE + int(va, 16), u64(BASE + int(va, 16))))
sys.exit(1 if bad else 0)
