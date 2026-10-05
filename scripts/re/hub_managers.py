"""List the career managers the hub builder allocates: name, size, constructor, vtable, hub slot (manager type id),
and the vtable's slot-1 handler (HandleEvent) -- recovered from the builder code, not guessed.

    bash scripts/re/py.sh scripts/re/hub_managers.py [--lo 0x147f17000 --hi 0x147f2b000]

Builder shape (docs/re/transfer_lists.md section 1): lea r8|r9,"Name" ; mov edx|ecx,SIZE ; call [alloc+0x10] ; call CTOR ;
movsxd rcx,[rbx+COUNTOFF] / mov rax,[rbx+COUNTOFF+8] ; mov [rax+rcx*8],obj  with COUNTOFF = type*0x20+0x10.
The constructor's first 'lea rax,[rip+vtable]' (within 0x60 bytes) is taken as the vtable.  Heuristic: every result is
printed with the raw numbers so it can be re-checked by hand.  Part of the player status / roles RE track.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

argv = sys.argv[1:]
lo = int(argv[argv.index("--lo") + 1], 16) if "--lo" in argv else 0x147F17000
hi = int(argv[argv.index("--hi") + 1], 16) if "--hi" in argv else 0x147F2B000


def ctor_vtable(ctor):
    for a, s, m, o in rx.dis(ctor, 30, raw=True):
        mm = re.search(r"rip \+ (0x[0-9a-f]+)\]", o)
        if m == "lea" and o.startswith(("rax,", "rcx,", "rdx,", "r8,")) and mm:
            t = a + s + int(mm.group(1), 16)
            if rx.BASE <= t < rx.BASE + rx.SIZE and rx.BASE <= rx.u64(t) < rx.BASE + rx.SIZE:
                return t
    return 0


out = []
pend_name = None
pend_size = None
ctor = None
for a, s, m, o in rx.dis(lo, (hi - lo) // 3, raw=True):
    if a >= hi:
        break
    mm = re.search(r"rip \+ (0x[0-9a-f]+)\]", o)
    if m == "lea" and o.split(",")[0] in ("r8", "r9") and mm:
        t = a + s + int(mm.group(1), 16)
        if 0 <= t - rx.BASE < rx.SIZE:
            b = rx.cstr(t, 64)
            if len(b) >= 4 and all(32 <= c < 127 for c in b):
                pend_name, pend_size, ctor = b.decode(), None, None
                continue
    if pend_name:
        mm = re.match(r"(edx|ecx), (0x[0-9a-f]+)$", o)
        if m == "mov" and mm and pend_size is None:
            pend_size = int(mm.group(2), 16)
        if m == "call" and o.startswith("0x") and pend_size is not None and ctor is None:
            ctor = int(o, 16)
        mm = re.search(r"rcx, dword ptr \[r.. \+ (0x[0-9a-f]+)\]", o)
        if m == "movsxd" and mm and ctor is not None:
            off = int(mm.group(1), 16)
            out.append((pend_name, pend_size, ctor, off))
            pend_name = pend_size = ctor = None
print("%-34s %-6s %-12s %-12s %-5s %s" % ("name", "size", "ctor", "vtable", "type", "slot1 (HandleEvent)"))
for name, size, ctor, off in out:
    vt = ctor_vtable(ctor)
    h = rx.u64(vt + 8) if vt else 0
    typ = (off - 0x10) // 0x20 if (off - 0x10) % 0x20 == 0 else None
    print("%-34s 0x%-4X %-12x %-12x %-5s %s" % (name, size or 0, ctor, vt, typ if typ is not None else "off=%#x" % off, ("%x" % h) if h else "-"))
