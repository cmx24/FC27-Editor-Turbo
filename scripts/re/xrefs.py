"""Callers + data references of one or more functions / addresses (numpy only; no ripscan.exe needed).

    bash scripts/re/py.sh scripts/re/xrefs.py 0x147c471ec 0x147d73d48 [--max 60]

For every target VA prints
  * direct call / jmp sites (E8 / E9 rel32) with the start of the enclosing function (heuristic),
  * other rip-relative references (lea / mov of the address),
  * absolute 8-byte pointers in the image (vtable slots).
Part of the player status / roles RE track (docs/re/player_status_roles.md).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

argv = sys.argv[1:]
mx = 60
if "--max" in argv:
    i = argv.index("--max")
    mx = int(argv[i + 1])
    del argv[i:i + 2]
targets = [int(a, 16) for a in argv if not a.startswith("--")]
refs = rx.rip_refs(targets)
ptrs = rx.imm64_refs(targets)
for t in targets:
    print("== %x" % t)
    shown = 0
    for (r, tail) in refs[t]:
        kind = "ref"
        at = r
        if tail == 0 and rx.u8(r - 1) in (0xE8, 0xE9):
            kind = "call" if rx.u8(r - 1) == 0xE8 else "jmp"
            at = r - 1
        elif tail == 0 and rx.u8(r - 2) == 0x0F and 0x80 <= rx.u8(r - 1) <= 0x8F:
            kind, at = "jcc", r - 2
        fs = rx.func_start(at)
        if shown < mx:
            print("   %-4s at %x   func %s" % (kind, at, "%x" % fs if fs else "?"))
        shown += 1
    if shown > mx:
        print("   ... %d more rip refs" % (shown - mx))
    for p in ptrs[t]:
        print("   qword ptr at %x" % p)
