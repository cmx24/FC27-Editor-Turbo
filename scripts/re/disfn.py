"""Disassemble n instructions at a VA, annotating rip-relative string operands and call targets.

    python scripts/re/disfn.py 0x147ed8688 250 [--strings-only]
"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import dis, cstr, u64, BASE, SIZE  # noqa: E402

va = int(sys.argv[1], 16)
n = int(sys.argv[2]) if len(sys.argv) > 2 else 100
only = "--strings-only" in sys.argv
for a, s, m, o in dis(va, n, raw=True):
    note = ""
    if "rip +" in o or "rip -" in o:
        try:
            expr = o[o.index("[rip") + 1:o.index("]")]
            disp = int(expr.split("+")[1].strip(), 16) if "+" in expr else -int(expr.split("-")[1].strip(), 16)
            t = a + s + disp
            if 0 <= t - BASE < SIZE:
                b = cstr(t, 80)
                if len(b) >= 3 and all(32 <= c < 127 for c in b):
                    note = '  ; "%s"' % b.decode()
                else:
                    note = "  ; -> %x (q=%x)" % (t, u64(t) if t - BASE + 8 <= SIZE else 0)
        except Exception:
            pass
    if only and not note:
        continue
    print("%x  %-7s %s%s" % (a, m, o, note))
    if m in ("ret",) and not only:
        pass
