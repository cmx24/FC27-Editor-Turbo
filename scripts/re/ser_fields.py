"""Member names and offsets of one of the game's save-game (de)serializer functions.

    bash scripts/re/py.sh scripts/re/ser_fields.py 0x147edd1d0 [--max 1500] [--reg rdi]

The career serializers have the shape  lea rax,[rdi+OFF] ; mov [rbp-0x10],rax ; lea rax,"mName" ; mov [rbp-8],rax ; call.
Prints "mName  +OFF" in call order; "?" when the offset instruction was not the usual lea.  Read-only.
Part of the player status / roles RE track (docs/re/player_status_roles.md).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

argv = sys.argv[1:]
va = int(argv[0], 16)
mx = int(argv[argv.index("--max") + 1]) if "--max" in argv else 1500
reg = argv[argv.index("--reg") + 1] if "--reg" in argv else "rdi"
off = None
for a, s, m, o in rx.dis(va, mx, raw=True):
    mm = re.match(r"rax, \[%s(?: \+ (0x[0-9a-f]+|[0-9]+))?]$" % reg, o)
    if m == "lea" and mm:
        off = int(mm.group(1), 0) if mm.group(1) else 0
        continue
    if m == "mov" and o == "rax, %s" % reg:
        off = 0
        continue
    mm = re.search(r"rip \+ (0x[0-9a-f]+)\]", o)
    if m == "lea" and o.startswith("rax,") and mm:
        t = a + s + int(mm.group(1), 16)
        if 0 <= t - rx.BASE < rx.SIZE:
            b = rx.cstr(t, 80)
            if len(b) >= 2 and all(32 <= c < 127 for c in b):
                print("%-40s %s" % (b.decode(), ("+0x%X" % off) if off is not None else "?"))
                off = None
    if m == "ret" or (m == "jmp" and not o.startswith("0x14")):
        pass
    if m == "int3" and a > va + 16:
        break
