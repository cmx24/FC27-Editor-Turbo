"""Static-RE helpers (player-capture track; ripscan.exe-based) for the FC27.exe memory image (mmap, never loads the file twice).

Usage (from the worktree root):  bash scripts/re/py.sh -i scripts/re/rx.py   or   import rx in a script.
  find_str(s)            -> [VA] of NUL-terminated string s (bytes or str)
  cstr(va, n=200)        -> the C string at va
  rip_refs([va...])      -> {va: [(ref_va, tail)]} rip-relative disp32 / call rel32 references (ripscan.exe over code sections)
  dis(va, n=20)          -> capstone listing (prints), dis_list(va, n) -> list of insns
  func_start(va)         -> heuristic start of the function containing va (walks back to int3/ret padding + prologue)
  calls_to(va)           -> same as rip_refs but only E8 (call) refs, returned as instruction addresses
  u32/u64(va), rd(va, n) -> raw reads
"""
import mmap
import os
import struct
import subprocess
import sys

import capstone
import pefile

IMAGE = os.environ.get("FC27_IMAGE", r"C:\FC 27 Live Editor\turbo_output\fc27_image.bin")
HERE = os.path.dirname(os.path.abspath(__file__))
RIPSCAN = os.path.join(HERE, "ripscan.exe")

_f = open(IMAGE, "rb")
mm = mmap.mmap(_f.fileno(), 0, access=mmap.ACCESS_READ)
pe = pefile.PE(IMAGE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase
SIZE = pe.OPTIONAL_HEADER.SizeOfImage
# the dump is a memory image: file offset == RVA
SECTIONS = [(s.Name.rstrip(b"\0").decode(errors="replace"), s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData), s.Characteristics)
            for s in pe.sections]
CODE = [(n, va, sz) for (n, va, sz, ch) in SECTIONS if n == ".text1"]  # the code section (others are data or protection stubs)

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False


def off(va):
    return va - BASE


def rd(va, n):
    return mm[off(va):off(va) + n]


def u8(va):
    return mm[off(va)]


def u16(va):
    return struct.unpack_from("<H", mm, off(va))[0]


def u32(va):
    return struct.unpack_from("<I", mm, off(va))[0]


def s32(va):
    return struct.unpack_from("<i", mm, off(va))[0]


def u64(va):
    return struct.unpack_from("<Q", mm, off(va))[0]


def cstr(va, n=200):
    o = off(va)
    e = mm.find(b"\0", o, o + n)
    if e < 0:
        e = o + n
    return mm[o:e].decode("latin-1")


def find_str(s, nul=True, limit=50):
    """Virtual addresses where the NUL-terminated (or raw) string s starts (whole-string matches, aligned to a
    preceding NUL or section start so 'Foo' does not match the tail of 'xFoo')."""
    if isinstance(s, str):
        s = s.encode("latin-1")
    pat = s + (b"\0" if nul else b"")
    out = []
    pos = 0
    while len(out) < limit:
        i = mm.find(pat, pos)
        if i < 0:
            break
        if i == 0 or mm[i - 1] == 0:
            out.append(BASE + i)
        pos = i + 1
    return out


def find_substr(s, limit=50):
    """Strings containing s (returns VAs of the start of each C string)."""
    if isinstance(s, str):
        s = s.encode("latin-1")
    out = []
    pos = 0
    while len(out) < limit:
        i = mm.find(s, pos)
        if i < 0:
            break
        st = mm.rfind(b"\0", max(0, i - 300), i) + 1
        out.append(BASE + st)
        pos = i + 1
    return out


def rip_refs(targets):
    """{target_va: [(disp_va, tail)]}: every rip-relative disp32 (lea/mov/cmp..., call/jmp rel32) in the executable
    sections that points at one of targets. The instruction starts a few bytes before disp_va."""
    targets = list(targets)
    res = {t: [] for t in targets}
    if not targets:
        return res
    for (n, va, sz) in CODE:
        for i in range(0, len(targets), 64):
            chunk = targets[i:i + 64]
            args = [RIPSCAN, IMAGE, "%x" % BASE, "%x" % va, "%x" % sz] + ["%x" % t for t in chunk]
            out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
            for line in out.splitlines():
                t, r, tail = line.split()
                res[int(t, 16)].append((int(r, 16), int(tail)))
    return res


def insn_at_ref(disp_va, tail):
    """Best-effort instruction start for a disp32 found at disp_va: tries 1..5 bytes before."""
    best = None
    for back in range(1, 8):
        start = disp_va - back
        for ins in md.disasm(rd(start, 16), start, 1):
            if ins.address + ins.size == disp_va + 4 + tail:
                best = ins
            break
        if best:
            return best
    return None


def calls_to(target):
    """Addresses of E8 call instructions whose target is `target`."""
    out = []
    for (r, tail) in rip_refs([target])[target]:
        if tail == 0 and u8(r - 1) == 0xE8:
            out.append(r - 1)
    return out


def dis_list(va, n=20):
    return list(md.disasm(rd(va, n * 15), va, n))


def dis(va, n=20, upto=None):
    for ins in md.disasm(rd(va, (n or 200) * 15), va, n or 0):
        print("%x  %-10s %s" % (ins.address, ins.mnemonic, ins.op_str))
        if upto and ins.address >= upto:
            break


_PROLOGUES = [b"\x48\x89\x5c\x24", b"\x48\x83\xec", b"\x48\x81\xec", b"\x40\x53", b"\x40\x55", b"\x40\x56", b"\x40\x57",
              b"\x48\x8b\xc4", b"\x48\x89\x4c\x24", b"\x48\x89\x54\x24", b"\x4c\x89\x44\x24", b"\x48\x8b\xc1",
              b"\x55", b"\x53", b"\x56", b"\x57", b"\x41\x54", b"\x41\x55", b"\x41\x56", b"\x41\x57", b"\x48\x8d\x6c\x24",
              b"\x48\x8d\xa8", b"\x48\x8b\xd1", b"\x48\x8b\xc2", b"\x33\xc0", b"\x48\x83\xe9", b"\x4c\x8b\xdc", b"\x8b\xc1",
              b"\x48\x8b\x01", b"\x48\x8b\x41", b"\x48\x8b\x81", b"\x48\x8b\x49", b"\x48\x8b\x89", b"\x0f\xb6", b"\xf2\x0f", b"\xf3\x0f",
              b"\x48\xc7", b"\xe9", b"\xeb", b"\x48\x8b\x05", b"\x48\x8b\x0d", b"\x48\x8d\x05", b"\x48\x8d\x0d", b"\x48\x85\xc9",
              b"\x48\x85\xd2", b"\x80\x79", b"\x80\xb9", b"\x83\x79", b"\x83\xb9", b"\x48\x83\x79", b"\x48\x83\xb9", b"\x48\x63",
              b"\xb8", b"\xb0", b"\x32\xc0", b"\xc3", b"\xc2", b"\x89\x54\x24", b"\x44\x89\x44\x24", b"\x89\x4c\x24", b"\x44\x88",
              b"\x88\x54\x24", b"\x4c\x8b\xc9", b"\x4c\x8b\xd1", b"\x0f\x29", b"\x66\x0f", b"\xff\x25"]


def func_start(va, maxback=0x4000):
    """Walk back to the nearest 'int3/nop padding or ret' boundary followed by a prologue-looking byte sequence."""
    o = off(va)
    i = o
    while o - i < maxback and i > 0:
        i -= 1
        b = mm[i]
        if b in (0xCC,) or (b == 0xC3 and mm[i + 1] != 0xCC) or (b == 0x90 and mm[i - 1] in (0xCC, 0x90, 0xC3)):
            # candidate start at i+1 (skip padding run)
            j = i + 1
            while mm[j] in (0xCC, 0x90) and j < o:
                j += 1
            head = mm[j:j + 4]
            if any(head.startswith(p) for p in _PROLOGUES):
                return BASE + j
    return None


def hexdump(va, n=64):
    b = rd(va, n)
    for i in range(0, n, 16):
        print("%x  %s" % (va + i, " ".join("%02x" % c for c in b[i:i + 16])))


def ptr_refs(va, limit=64):
    """File positions (as VAs) holding the 8-byte little-endian value va (pointer tables, vtables, descriptors)."""
    pat = struct.pack("<Q", va)
    out = []
    pos = 0
    while len(out) < limit:
        i = mm.find(pat, pos)
        if i < 0:
            break
        out.append(BASE + i)
        pos = i + 1
    return out


def show_refs(targets, label=None):
    """Print code refs (instruction + enclosing function) and data pointer refs for each target VA."""
    refs = rip_refs(targets)
    for t in targets:
        print("== %x %s" % (t, label(t) if label else ""))
        for (r, tail) in refs[t]:
            ins = insn_at_ref(r, tail)
            fs = func_start(ins.address) if ins else None
            print("   code %x  %-8s %-40s  func %s" % (ins.address if ins else r, ins.mnemonic if ins else "?",
                                                      ins.op_str if ins else "", "%x" % fs if fs else "?"))
        for p in ptr_refs(t, 16):
            print("   data ptr at %x" % p)


if __name__ == "__main__":
    print("image", IMAGE, "base %x size %x" % (BASE, SIZE))
    for s in SECTIONS:
        print("  %-8s rva %08x size %08x ch %08x" % s)
