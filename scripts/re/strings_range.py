"""Dump NUL-terminated ASCII strings between two VAs.   python scripts/re/strings_range.py 0x14b021100 0x14b021b00"""
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import _mm, BASE  # noqa: E402

lo, hi = int(sys.argv[1], 16) - BASE, int(sys.argv[2], 16) - BASE
for m in re.finditer(rb"[\x20-\x7e]{3,200}\x00", _mm[lo:hi]):
    print("%x  %s" % (BASE + lo + m.start(), m.group()[:-1].decode("ascii", "replace")))
