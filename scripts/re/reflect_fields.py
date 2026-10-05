"""Dump (name -> member offset) pairs of one of the game's reflection registrations ("TypeName" then per member:
lea rdx,"Member" ... mov qword ptr [rbp-0x28], <offset>).  The career code registers its UI / save structs this way.

    bash scripts/re/py.sh scripts/re/reflect_fields.py 0x147d49be4 [--max 2000]

Heuristic, read-only: prints each string operand and the next immediate stored to [rbp-0x28] (the member offset).
Part of the player status / roles RE track (docs/re/player_status_roles.md).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

va = int(sys.argv[1], 16)
mx = int(sys.argv[sys.argv.index("--max") + 1]) if "--max" in sys.argv else 2000
name = None
for a, s, m, o in rx.dis(va, mx, raw=True):
    mm = re.search(r"rip \+ (0x[0-9a-f]+)\]", o)
    if m == "lea" and mm:
        t = a + s + int(mm.group(1), 16)
        if 0 <= t - rx.BASE < rx.SIZE:
            b = rx.cstr(t, 80)
            if len(b) >= 2 and all(32 <= c < 127 for c in b):
                name = b.decode()
                continue
    mm = re.match(r"qword ptr \[rbp - 0x28\], (0x[0-9a-f]+|\d+)$", o)
    if m == "mov" and mm and name:
        print("%-34s +0x%X" % (name, int(mm.group(1), 0)))
        name = None
    elif m == "ret":
        break
