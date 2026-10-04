"""Static-analysis helpers for the FC27.exe memory image (scripts/re).

The image is mmapped once (never read into memory twice). file offset = VA - BASE.
Pure Python + capstone + pefile (no numpy): rip-relative reference scans use a regex over the code section.

Usage from a script:  sys.path.insert(0, 'scripts/re'); from rx import *
"""
import mmap
import os
import re
import struct
import sys

import capstone
import pefile

IMG = os.environ.get("FC27_IMAGE", r"C:\FC 27 Live Editor\turbo_output\fc27_image.bin")
BASE = 0x140000000

_f = open(IMG, "rb")
M = mmap.mmap(_f.fileno(), 0, access=mmap.ACCESS_READ)
SIZE = len(M)

_pe = pefile.PE(data=M[:0x2000], fast_load=True)
SECTIONS = []
for s in _pe.sections:
    SECTIONS.append((s.Name.rstrip(b"\0").decode(errors="replace"), s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData), s.Characteristics))
CODE = [(BASE + va, BASE + va + size) for (n, va, size, ch) in SECTIONS if ch & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE

_cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_cs.detail = True


def off(va):
    o = va - BASE
    if o < 0 or o >= SIZE:
        raise ValueError("VA outside image: %#x" % va)
    return o


def rb(va, n):
    return M[off(va):off(va) + n]


def u8(va):
    return M[off(va)]


def u16(va):
    return struct.unpack_from("<H", M, off(va))[0]


def u32(va):
    return struct.unpack_from("<I", M, off(va))[0]


def u64(va):
    return struct.unpack_from("<Q", M, off(va))[0]


def i32(va):
    return struct.unpack_from("<i", M, off(va))[0]


def cstr(va, maxlen=256):
    o = off(va)
    e = M.find(b"\0", o, o + maxlen)
    return M[o:e if e >= 0 else o + maxlen].decode("latin-1")


def find_str(s, nul=True, limit=50):
    """VAs of the (NUL-terminated) ASCII string s anywhere in the image."""
    pat = s.encode() + (b"\0" if nul else b"")
    out, pos = [], 0
    while len(out) < limit:
        pos = M.find(pat, pos)
        if pos < 0:
            break
        # require a NUL / non-printable before, so 'mHomeWins' does not match inside 'xmHomeWins'
        if pos == 0 or M[pos - 1] == 0 or not (32 <= M[pos - 1] < 127):
            out.append(BASE + pos)
        pos += 1
    return out


def find_bytes(pat, limit=50, ranges=None):
    """VAs of a raw byte string in the given VA ranges (default: code sections)."""
    out = []
    for (a, b) in (ranges or CODE):
        pos = off(a)
        end = min(off(b), SIZE)
        while len(out) < limit:
            pos = M.find(pat, pos, end)
            if pos < 0:
                break
            out.append(BASE + pos)
            pos += 1
    return out


def parse_sig(text):
    """'48 8B ?? 05' -> regex bytes pattern."""
    parts = text.split()
    rx = b""
    for p in parts:
        if p in ("??", "?"):
            rx += b"."
        elif "|" in p:
            rx += b"(?:" + b"|".join(re.escape(bytes([int(x, 16)])) for x in p.strip("()").split("|")) + b")"
        else:
            rx += re.escape(bytes([int(p, 16)]))
    return re.compile(rx, re.DOTALL)


def find_sig(text, limit=50, ranges=None):
    rx = parse_sig(text)
    out = []
    for (a, b) in (ranges or CODE):
        for m in rx.finditer(M, off(a), min(off(b), SIZE)):
            out.append(BASE + m.start())
            if len(out) >= limit:
                return out
    return out


# rip-relative forms: (opcode bytes before disp32, length of disp position from start, total instr length)
_RIP_FORMS = [
    (b"\x48\x8d\x05", 3, 7), (b"\x48\x8d\x0d", 3, 7), (b"\x48\x8d\x15", 3, 7), (b"\x48\x8d\x1d", 3, 7),
    (b"\x48\x8d\x25", 3, 7), (b"\x48\x8d\x2d", 3, 7), (b"\x48\x8d\x35", 3, 7), (b"\x48\x8d\x3d", 3, 7),
    (b"\x4c\x8d\x05", 3, 7), (b"\x4c\x8d\x0d", 3, 7), (b"\x4c\x8d\x15", 3, 7), (b"\x4c\x8d\x1d", 3, 7),
    (b"\x4c\x8d\x25", 3, 7), (b"\x4c\x8d\x2d", 3, 7), (b"\x4c\x8d\x35", 3, 7), (b"\x4c\x8d\x3d", 3, 7),
    (b"\x48\x8b\x05", 3, 7), (b"\x48\x8b\x0d", 3, 7), (b"\x48\x8b\x15", 3, 7), (b"\x48\x8b\x1d", 3, 7),
    (b"\x48\x8b\x35", 3, 7), (b"\x48\x8b\x3d", 3, 7), (b"\x4c\x8b\x05", 3, 7), (b"\x4c\x8b\x0d", 3, 7),
    (b"\x4c\x8b\x15", 3, 7), (b"\x4c\x8b\x1d", 3, 7), (b"\x4c\x8b\x35", 3, 7), (b"\x4c\x8b\x3d", 3, 7),
    (b"\x48\x89\x05", 3, 7), (b"\x48\x89\x0d", 3, 7), (b"\x48\x89\x15", 3, 7), (b"\x48\x89\x1d", 3, 7),
    (b"\x48\x89\x35", 3, 7), (b"\x48\x89\x3d", 3, 7), (b"\x4c\x89\x05", 3, 7), (b"\x4c\x89\x0d", 3, 7),
    (b"\x8b\x05", 2, 6), (b"\x8b\x0d", 2, 6), (b"\x8b\x15", 2, 6), (b"\x89\x05", 2, 6), (b"\x89\x0d", 2, 6),
    (b"\x48\x8b\x05", 3, 7),
]


def rip_refs(targets, ranges=None, limit=200, forms=None):
    """{target: [instr_va]} for rip-relative lea/mov references in the code sections."""
    targets = set(targets)
    res = {t: [] for t in targets}
    seen = set()
    for (pre, dpos, ln) in (forms or _RIP_FORMS):
        key = (pre, dpos, ln)
        if key in seen:
            continue
        seen.add(key)
        for (a, b) in (ranges or CODE):
            start, end = off(a), min(off(b), SIZE)
            pos = start
            while True:
                pos = M.find(pre, pos, end)
                if pos < 0:
                    break
                d = struct.unpack_from("<i", M, pos + dpos)[0]
                tgt = BASE + pos + ln + d
                if tgt in targets:
                    res[tgt].append(BASE + pos)
                pos += 1
    for t in res:
        res[t] = sorted(set(res[t]))[:limit]
    return res


def call_refs(target, ranges=None, limit=200):
    """VAs of E8 rel32 calls (and E9 jmps) to target."""
    out = []
    for (a, b) in (ranges or CODE):
        start, end = off(a), min(off(b), SIZE)
        for opc in (b"\xe8", b"\xe9"):
            pos = start
            while len(out) < limit:
                pos = M.find(opc, pos, end)
                if pos < 0:
                    break
                d = struct.unpack_from("<i", M, pos + 1)[0]
                if BASE + pos + 5 + d == target:
                    out.append(BASE + pos)
                pos += 1
    return sorted(set(out))


def dis(va, n=40, maxbytes=None):
    code = rb(va, maxbytes or n * 15)
    lines = []
    for i, ins in enumerate(_cs.disasm(code, va)):
        if i >= n:
            break
        lines.append("%#x  %-28s %s %s" % (ins.address, ins.bytes.hex(" "), ins.mnemonic, ins.op_str))
    return "\n".join(lines)


def disasm(va, n=40):
    return list(_cs.disasm(rb(va, n * 15), va))[:n]


def func_start(va, back=0x2000):
    """Heuristic: walk back to a typical MSVC prologue (after int3/ret padding or 'mov [rsp+8],rbx' etc.)."""
    o = off(va)
    for p in range(o, max(o - back, 0), -1):
        b = M[p:p + 4]
        if M[p - 1] in (0xCC, 0xC3) and b[:1] in (b"\x40", b"\x48", b"\x4c", b"\x53", b"\x55", b"\x56", b"\x57", b"\x41"):
            return BASE + p
    return None


def hexsig(va, n):
    return " ".join("%02X" % b for b in rb(va, n))


def unique(sig):
    hits = find_sig(sig, limit=3)
    return hits


if __name__ == "__main__":
    print("image", IMG, "size", SIZE)
    for s in SECTIONS:
        print("  %-10s va=%#x size=%#x ch=%#x" % s)
    print("code ranges", [("%#x" % a, "%#x" % b) for a, b in CODE])
