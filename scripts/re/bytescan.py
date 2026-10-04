"""bytescan.py <hex pattern with ?? wildcards> [max]: every match of a byte pattern in the executable sections,
with the enclosing function start (same pattern syntax as the signature table).

    python scripts/re/bytescan.py "C6 8? E0 01 00 00 01" 50
"""
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import _mm, BASE, CODE, SIZE, func_start, dis  # noqa: E402

toks = sys.argv[1].split()
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 60
pat = b""
for t in toks:
    if t == "??":
        pat += b"."
    elif len(t) == 2 and "?" in t:
        # nibble wildcard, e.g. 8? -> [\x80-\x8f]
        hi = t[0]
        if hi == "?":
            pat += b"."
        else:
            lo = int(hi, 16) * 16
            pat += b"[" + re.escape(bytes([lo])) + b"-" + re.escape(bytes([lo + 15])) + b"]"
    else:
        pat += re.escape(bytes([int(t, 16)]))
rx = re.compile(pat, re.S)
n = 0
for rva, size in CODE:
    start, end = rva, min(rva + size, SIZE)
    for m in rx.finditer(_mm, start, end):
        va = BASE + m.start()
        fs = func_start(va)
        print("%x  func~%s  %s" % (va, ("%x" % fs) if fs else "?", dis(va, 1)[0] if dis(va, 1) else ""))
        n += 1
        if n >= limit:
            print("... limit")
            sys.exit(0)
print("%d matches" % n)
