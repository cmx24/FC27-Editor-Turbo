"""callers.py <va_hex>... : E8 call sites (and any other rip-relative reference) to each function, with the enclosing function start."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx

targets = [int(a, 16) for a in sys.argv[1:]]
refs = rx.rip_refs(targets)
for t in targets:
    print("== %x" % t)
    for (r, tail) in refs[t]:
        kind = "call" if (tail == 0 and rx.u8(r - 1) == 0xE8) else ("jmp" if (tail == 0 and rx.u8(r - 1) == 0xE9) else "ref")
        at = r - 1 if kind != "ref" else r
        fs = rx.func_start(at)
        print("   %s at %x   func %s" % (kind, at, "%x" % fs if fs else "?"))
