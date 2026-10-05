"""List instructions whose memory operand has a given disp32 (e.g. all [reg+0x2D38] users).

    bash scripts/re/py.sh scripts/re/dispscan_ref.py 0x2d38 [--lo 0x147000000 --hi 0x148000000] [--max 200]

Scans the image for the 4 little-endian bytes of the displacement, decodes candidate instructions that end the
disp field exactly there (capstone detail: disp_offset / disp) and prints them.  Part of the player status /
roles RE track (docs/re/player_status_roles.md).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402
import capstone  # noqa: E402

argv = sys.argv[1:]
disp = int(argv[0], 16)
lo = int(argv[argv.index("--lo") + 1], 16) if "--lo" in argv else rx.BASE
hi = int(argv[argv.index("--hi") + 1], 16) if "--hi" in argv else rx.BASE + rx.SIZE
mx = int(argv[argv.index("--max") + 1]) if "--max" in argv else 300
pat = struct.pack("<I", disp)
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
cs.detail = True
n = 0
pos = lo - rx.BASE
end = hi - rx.BASE
seen = set()
while n < mx:
    pos = rx._mm.find(pat, pos, end)
    if pos < 0:
        break
    va = rx.BASE + pos
    best = None
    for back in range(2, 9):  # opcode/modrm/sib (+ rex/prefix) before the disp
        start = va - back
        code = rx.read(start, 16)
        for ins in cs.disasm(code, start, 1):
            if ins.disp_offset == back and ins.disp == disp and ins.size >= back + 4:
                best = ins  # keep the longest-prefix decode (REX included)
    if best is not None and best.address not in seen:
        seen.add(best.address)
        print("%x  %-7s %s" % (best.address, best.mnemonic, best.op_str))
        n += 1
    pos += 1
print("%d hits" % n)
