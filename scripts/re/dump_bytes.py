"""dump_bytes.py <va_hex> [n] ...: the image bytes at each VA as a C array initialiser (for native tests).

    python scripts/re/dump_bytes.py 0x147ddf900 48 0x147df42d0 48
"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import read  # noqa: E402

args = sys.argv[1:]
i = 0
while i < len(args):
    va = int(args[i], 16)
    n = int(args[i + 1]) if i + 1 < len(args) and not args[i + 1].startswith("0x") else 48
    i += 2 if n != 48 or (i + 1 < len(args) and not args[i + 1].startswith("0x")) else 1
    b = read(va, n)
    print("// %x (%d bytes)" % (va, n))
    for k in range(0, n, 16):
        print("    " + ", ".join("0x%02X" % x for x in b[k:k + 16]) + ",")
