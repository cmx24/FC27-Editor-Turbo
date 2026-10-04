"""List NUL-terminated ASCII strings in the image that match a regex (case-insensitive).

    python scripts/re/strings_grep.py "JobOffer|JOB_OFFER|JobMarket|JobSwitch" [max]
"""
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx import _mm, BASE  # noqa: E402

pat = re.compile(sys.argv[1].encode(), re.I)
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 400
seen = set()
n = 0
for m in re.finditer(rb"[\x20-\x7e]{4,200}\x00", _mm):
    s = m.group()[:-1]
    if not pat.search(s):
        continue
    if s in seen:
        continue
    seen.add(s)
    print("%x  %s" % (BASE + m.start(), s.decode("ascii", "replace")))
    n += 1
    if n >= limit:
        print("... limit")
        break
