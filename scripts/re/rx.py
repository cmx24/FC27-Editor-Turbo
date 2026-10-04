"""Static-analysis helpers for the FC27.exe memory image (scripts/re).

Usage (from the re_venv python):
    import sys; sys.path.insert(0, 'scripts/re'); from rx import *
The image is mmap'ed once (never copied); numpy views are built on the mmap.

    find_str(s)              -> list of VAs of the NUL-terminated string s
    rip_refs([va, ...])      -> {va: [(instr_va, tail)]}  rip-relative disp32 references
    imm64_refs([va, ...])    -> {va: [va_of_qword]}       absolute 8-byte pointers (vtables, data)
    dis(va, n)               -> capstone disassembly lines
    func_start(va)           -> heuristic start of the function containing va
    cstr(va)                 -> bytes of the C string at va
    read(va, n), u32(va), u64(va), i32(va)
"""
import mmap
import os
import struct

import numpy as np
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE = os.environ.get("FC27_IMAGE", r"C:\FC 27 Live Editor\turbo_output\fc27_image.bin")
BASE = 0x140000000

_f = open(IMAGE, "rb")
_mm = mmap.mmap(_f.fileno(), 0, access=mmap.ACCESS_READ)
_buf = np.frombuffer(_mm, dtype=np.uint8)
SIZE = len(_buf)

_pe = pefile.PE(data=_mm[:0x1000], fast_load=True)
SECTIONS = [(s.Name.rstrip(b"\0").decode(errors="ignore"), s.VirtualAddress, s.Misc_VirtualSize, s.Characteristics)
            for s in _pe.sections]
CODE = [(rva, size) for (_n, rva, size, ch) in SECTIONS if ch & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE

_md = Cs(CS_ARCH_X86, CS_MODE_64)
_md.detail = False


def off(va):
    return va - BASE


def va_of(o):
    return o + BASE


def read(va, n):
    o = off(va)
    return bytes(_buf[o:o + n])


def u8(va):
    return int(_buf[off(va)])


def u16(va):
    return struct.unpack_from("<H", _mm, off(va))[0]


def u32(va):
    return struct.unpack_from("<I", _mm, off(va))[0]


def i32(va):
    return struct.unpack_from("<i", _mm, off(va))[0]


def u64(va):
    return struct.unpack_from("<Q", _mm, off(va))[0]


def cstr(va, maxlen=256):
    o = off(va)
    end = _mm.find(b"\0", o, o + maxlen)
    if end < 0:
        end = o + maxlen
    return _mm[o:end]


def find_bytes(pat, start=0, limit=None):
    """All offsets (file) of the byte pattern."""
    out = []
    pos = start
    while True:
        pos = _mm.find(pat, pos)
        if pos < 0:
            break
        out.append(pos)
        pos += 1
        if limit and len(out) >= limit:
            break
    return out


def find_str(s, nul=True, aligned=False):
    if isinstance(s, str):
        s = s.encode()
    pat = s + (b"\0" if nul else b"")
    res = []
    for o in find_bytes(pat):
        # require the preceding byte to be NUL (string start) unless at 0
        if o > 0 and _buf[o - 1] != 0:
            continue
        res.append(va_of(o))
    return res


def find_wstr(s):
    pat = s.encode("utf-16-le") + b"\0\0"
    return [va_of(o) for o in find_bytes(pat) if o >= 2 and _buf[o - 2] == 0 and _buf[o - 1] == 0]


def _code_ranges():
    for rva, size in CODE:
        yield rva, min(size, SIZE - rva)


def rip_refs(targets, tails=(0, 1, 4)):
    """rip-relative references: instr ends at i+4+tail, target = BASE + i + 4 + tail + disp32."""
    targets = list(targets)
    out = {t: [] for t in targets}
    trv = np.array([t - BASE for t in targets], dtype=np.int64)
    CHUNK = 32 * 1024 * 1024
    for rva, size in _code_ranges():
        for start in range(rva, rva + size, CHUNK):
            end = min(start + CHUNK + 8, rva + size)
            seg = _buf[start:end]
            n = len(seg) - 8
            if n <= 0:
                continue
            v = seg[:n].astype(np.int64) | (seg[1:n + 1].astype(np.int64) << 8) | (
                seg[2:n + 2].astype(np.int64) << 16) | (seg[3:n + 3].astype(np.int64) << 24)
            v = np.where(v >= 2 ** 31, v - 2 ** 32, v)
            idx = np.arange(n, dtype=np.int64)
            for tail in tails:
                dest = start + idx + 4 + tail + v
                mask = np.isin(dest, trv)
                for h in np.nonzero(mask)[0]:
                    t = int(dest[h]) + BASE
                    out[t].append((BASE + start + int(h), tail))
    for t in targets:
        out[t].sort()
    return out


def imm64_refs(targets):
    out = {t: [] for t in targets}
    for t in targets:
        pat = struct.pack("<Q", t)
        for o in find_bytes(pat):
            if o % 8 == 0:
                out[t].append(va_of(o))
    return out


def dis(va, n=40, raw=False):
    code = read(va, n * 15)
    lines = []
    for ins in _md.disasm(code, va):
        lines.append((ins.address, ins.size, ins.mnemonic, ins.op_str))
        if len(lines) >= n:
            break
    if raw:
        return lines
    return ["%x  %-8s %s" % (a, m, o) for (a, _s, m, o) in lines]


def dis_range(va, end):
    code = read(va, end - va)
    return ["%x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str) for ins in _md.disasm(code, va)]


def func_start(va, back=0x4000):
    """Walk back to a likely function start: int3/ret padding followed by a typical prologue."""
    o = off(va)
    lo = max(0, o - back)
    i = o
    while i > lo:
        b = _buf[i]
        prev = _buf[i - 1]
        # padding (CC) or ret before, prologue-ish byte here
        if prev in (0xCC, 0xC3) and b in (0x40, 0x41, 0x44, 0x45, 0x48, 0x4C, 0x53, 0x55, 0x56, 0x57, 0x4D, 0x49):
            # check 16-byte alignment preference
            if (i % 16) == 0 or prev == 0xCC:
                return va_of(i)
        i -= 1
    return None


def func_end(va, limit=0x6000):
    """First `int3` padding after va reached by linear sweep (rough)."""
    o = off(va)
    code = _buf[o:o + limit].tobytes()
    last = va
    for ins in _md.disasm(code, va):
        last = ins.address + ins.size
        if ins.mnemonic == "int3":
            return ins.address
    return last


def rip_target(ins_va):
    """For an instruction at ins_va that has a rip-relative operand, compute the target."""
    for a, s, m, o in dis(ins_va, 1, raw=True):
        if "rip" in o:
            # find disp inside instruction bytes: assume last 4 (or 4 before imm)
            b = read(a, s)
            for tail in (0, 1, 4):
                if s - 4 - tail < 1:
                    continue
                d = struct.unpack_from("<i", b, s - 4 - tail)[0]
                t = a + s + d
                if "0x%x" % t in o or "0x%x" % (t & 0xffffffffffff) in o:
                    return t
            d = struct.unpack_from("<i", b, s - 4)[0]
            return a + s + d
    return None


def calls_in(va, end):
    out = []
    for a, s, m, o in dis(va, 100000, raw=True):
        if a >= end:
            break
        if m == "call" and o.startswith("0x"):
            out.append((a, int(o, 16)))
    return out


def hexdump(va, n=64, width=16):
    b = read(va, n)
    for i in range(0, n, width):
        chunk = b[i:i + width]
        print("%x  %s  %s" % (va + i, " ".join("%02x" % c for c in chunk),
                               "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)))


if __name__ == "__main__":
    print("image", IMAGE, "size", hex(SIZE))
    for s in SECTIONS:
        print(s[0], hex(s[1]), hex(s[2]), hex(s[3]))
