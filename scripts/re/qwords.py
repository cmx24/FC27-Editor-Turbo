"""Dump qwords: qwords.py <va_hex>[:count[:back]] ...  annotating pointers to C strings / sections. Default count 32."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx


def dump(va, n):
    for i in range(n):
        a = va + 8 * i
        q = rx.u64(a)
        note = ""
        if rx.BASE <= q < rx.BASE + rx.SIZE:
            s = rx.cstr(q, 60)
            if all(32 <= ord(c) < 127 for c in s) and len(s) >= 2:
                note = "\"%s\"" % s
            else:
                sec = next((nm for (nm, sva, sz, ch) in rx.SECTIONS if sva <= q - rx.BASE < sva + sz), "?")
                note = "-> %s" % sec
        print("%x  %016x  %08x %08x  %s" % (a, q, q & 0xFFFFFFFF, q >> 32, note))


for arg in sys.argv[1:]:
    parts = arg.split(":")
    va = int(parts[0], 16)
    n = int(parts[1]) if len(parts) > 1 else 32
    back = int(parts[2]) if len(parts) > 2 else 0
    print("--- %x" % va)
    dump(va - 8 * back, n)
