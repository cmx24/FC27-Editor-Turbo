"""Checks docs/re/game_thread-signatures.json against the game image: every pattern must match exactly once over the
executable sections and resolve to the expected address. Also prints the first instructions of each target, so a title
update can be followed up by hand (docs/re/game_thread.md section 5).

Run: bash scripts/re/py.sh scripts/re/verify_game_thread.py [docs/re/game_thread-signatures.json]
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx  # noqa: E402
from rx import BASE, SECTIONS, dis, find_sig, i32, rb  # noqa: E402

CODE = [(BASE + va, BASE + va + size) for (n, va, size, ch) in SECTIONS if ch & 0x20000000]

p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "docs", "re", "game_thread-signatures.json")
doc = json.load(open(p))
print("image %s, build %s, executable sections: %s" % (rx.IMG, doc["build"], ", ".join(n for (n, _, _, ch) in SECTIONS if ch & 0x20000000)))


def resolve_rip(at):
    """Target of the rip-relative instruction at `at` (E8/E9 rel32, FF 15/25 [rip], REX? 8B/8D [rip+disp])."""
    b = rb(at, 8)
    if b[0] in (0xE8, 0xE9):
        return at + 5 + i32(at + 1)
    if b[0] == 0xFF and b[1] in (0x15, 0x25):
        return at + 6 + i32(at + 2)
    if b[0] in (0x48, 0x4C) and b[1] in (0x8B, 0x8D) and (b[2] & 0xC7) == 0x05:
        return at + 7 + i32(at + 3)
    if b[0] in (0x8B, 0x8D) and (b[1] & 0xC7) == 0x05:
        return at + 6 + i32(at + 2)
    raise ValueError("no rip-relative instruction at %#x: %s" % (at, b.hex(" ")))


bad = 0
for name, s in doc["signatures"].items():
    hits = find_sig(s["pattern"], limit=5, ranges=CODE)
    ok = len(hits) == 1
    addr = None
    if ok:
        at = hits[0] + int(s.get("offset", 0))
        addr = resolve_rip(at) if s.get("resolve") == "rip" else at
        ok = addr == int(s["expect"], 16)
    print("%-20s hits=%d %s -> %s expected %s  %s" % (name, len(hits), ["%#x" % h for h in hits], "%#x" % addr if addr else "-", s["expect"],
                                                  "OK" if ok else "MISMATCH"))
    if ok:
        print("    " + "\n    ".join(dis(addr, 4).splitlines()))
    else:
        bad += 1
sys.exit(1 if bad else 0)
