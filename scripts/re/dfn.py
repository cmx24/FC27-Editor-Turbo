"""Disassemble the whole function containing a VA (start found by the int3-padding heuristic), annotated.

    bash scripts/re/py.sh scripts/re/dfn.py 0x147ea10e8 [--from-start] [--max 400] [--at]

Default: start at the function start and stop at the first int3 after a ret/jmp; with --at only start at the VA.
Annotates rip-relative strings, call targets and [reg+disp] vtable calls.  Part of the player status / roles RE
track (docs/re/player_status_roles.md); a thin wrapper over rx_jobs.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

argv = sys.argv[1:]
va = int(argv[0], 16)
mx = int(argv[argv.index("--max") + 1]) if "--max" in argv else 600
start = va if "--at" in argv else (rx.func_start(va) or va)
print("; function start %x (containing %x)" % (start, va))
lines = rx.dis(start, mx, raw=True)
prev_ret = False
for a, s, m, o in lines:
    note = ""
    if "rip +" in o or "rip -" in o:
        try:
            expr = o[o.index("[rip") + 1:o.index("]")]
            disp = int(expr.split("+")[1].strip(), 16) if "+" in expr else -int(expr.split("-")[1].strip(), 16)
            t = a + s + disp
            if 0 <= t - rx.BASE < rx.SIZE:
                b = rx.cstr(t, 80)
                if len(b) >= 3 and all(32 <= c < 127 for c in b):
                    note = '  ; "%s"' % b.decode()
                else:
                    note = "  ; -> %x (q=%x)" % (t, rx.u64(t))
        except Exception:
            pass
    mark = "  <<<" if a == va else ""
    print("%x  %-7s %s%s%s" % (a, m, o, note, mark))
    if m == "int3" and prev_ret and a > va:
        break
    prev_ret = m in ("ret", "jmp") and not o.startswith("0x") or m == "ret"
