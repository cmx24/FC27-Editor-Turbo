"""Print rip-relative code references to the given string(s) or VAs, with the function start of each ref.

    python scripts/re/refs.py "JobMarketManager" "JobOfferSystem" 0x14b021200 ...
Args that parse as hex are VAs; otherwise the exact NUL-terminated string is looked up (all copies).
"""
import json
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
from rx_jobs import find_str, rip_refs, func_start, cstr, dis  # noqa: E402

targets = {}
for a in sys.argv[1:]:
    if a.startswith("0x"):
        targets[int(a, 16)] = "va"
    else:
        for va in find_str(a):
            targets[va] = a
refs = rip_refs(list(targets))
res = {}
for t, lst in refs.items():
    name = targets[t]
    print("== %x  %s  (%d refs)" % (t, name if name != "va" else cstr(t)[:60], len(lst)))
    for (ins, tail) in lst:
        fs = func_start(ins)
        line = "?"
        for back in (3, 2, 4, 5, 6, 7):
            d = dis(ins - back, 1, raw=True)
            if d and "rip" in d[0][3] and d[0][0] + d[0][1] == ins + 4 + tail:
                line = "%x  %-8s %s" % (d[0][0], d[0][2], d[0][3])
                break
        print("   disp@%x  %s   tail=%d  func~%s" % (ins, line, tail, ("%x" % fs) if fs else "?"))
    res["%x" % t] = [{"ins": "%x" % i, "tail": tl, "func": ("%x" % func_start(i)) if func_start(i) else None} for i, tl in lst]
if "--json" in sys.argv:
    print(json.dumps(res, indent=1))
