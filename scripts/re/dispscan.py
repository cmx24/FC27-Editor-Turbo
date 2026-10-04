"""Find instructions in a VA range whose memory operand uses a given displacement (e.g. a member offset).

    python scripts/re/dispscan.py 0x3f8 0x147d80000 0x147f30000 [regex-on-operand]
Prints instruction, and the heuristic function start, grouped by function.
"""
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import _mm, BASE, dis, func_start  # noqa: E402

disp = int(sys.argv[1], 16)
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
oprx = re.compile(sys.argv[4]) if len(sys.argv) > 4 else None
pat = struct.pack("<i", disp)
want = "0x%x" % disp
byfunc = defaultdict(list)
pos = lo - BASE
while True:
    pos = _mm.find(pat, pos, hi - BASE)
    if pos < 0:
        break
    va = BASE + pos
    for back in (2, 3, 4, 5, 6):
        d = dis(va - back, 1, raw=True)
        if not d:
            continue
        a, s, m, o = d[0]
        if a + s >= va + 4 and ("+ " + want + "]") in o and "rip" not in o:
            if oprx and not oprx.search(o):
                break
            fs = func_start(a)
            byfunc[fs].append("%x  %-7s %s" % (a, m, o))
            break
    pos += 1
for fs in sorted(byfunc, key=lambda x: x or 0):
    print("func~%s" % (("%x" % fs) if fs else "?"))
    for line in byfunc[fs]:
        print("   " + line)
