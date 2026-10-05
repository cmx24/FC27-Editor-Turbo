"""Find `mov reg32, imm32` (B8+r imm32) instructions with a given immediate and print the following ~N lines for context,
plus any string operand in a 60-instruction window after it (the career events are posted as
  alloc(0x20, "EventName") ... mov r9d/edx, <event id> ... call PostEvent).

    bash scripts/re/py.sh scripts/re/imm_scan.py 0x5f [--lo 0x147000000 --hi 0x148000000] [--win 60]
Part of the player status / roles RE track (docs/re/player_status_roles.md).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

argv = sys.argv[1:]
imm = int(argv[0], 16)
lo = int(argv[argv.index("--lo") + 1], 16) if "--lo" in argv else 0x147000000
hi = int(argv[argv.index("--hi") + 1], 16) if "--hi" in argv else 0x148800000
win = int(argv[argv.index("--win") + 1]) if "--win" in argv else 60
pat = struct.pack("<I", imm)
pos = lo - rx.BASE
end = hi - rx.BASE
n = 0
while True:
    pos = rx._mm.find(pat, pos, end)
    if pos < 0:
        break
    va = rx.BASE + pos
    op = rx.u8(va - 1)
    if 0xB8 <= op <= 0xBF or (op == 0xC7):  # mov r32, imm32 / mov r/m, imm32
        ins = rx.dis(va - 1 if 0xB8 <= op <= 0xBF else va - 2, 1)
        names = []
        # backwards window (the name string is loaded before the event id is set)
        back = rx.dis(va - 1 - 90, 40, raw=True)
        for a, s, m, o in back:
            mm = re.search(r"rip \+ (0x[0-9a-f]+)\]", o)
            if m == "lea" and mm:
                t = a + s + int(mm.group(1), 16)
                if 0 <= t - rx.BASE < rx.SIZE:
                    b = rx.cstr(t, 80)
                    if len(b) >= 4 and all(32 <= c < 127 for c in b):
                        names.append(b.decode())
        print("%x  %s   names<-: %s" % (va - 1, ins[0].split("  ", 1)[1] if ins else "?", names[-3:]))
        n += 1
    pos += 1
print("%d hits" % n)
