"""Make a wildcarded byte signature for a function start and check it is unique in the image.

    python scripts/re/sig_jobs.py 0x147dbbf64 [min_len]
Prints "AA BB ?? ?? ?? ?? CC ..." (rip-relative disp32, call/jmp rel32 and absolute imm32 wildcarded),
the number of matches in the whole image (1 = unique) and the length used.
Importable: make_sig(va, min_len) -> (sig_str, matches).
"""
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "scripts/re")
import rx_jobs as rx  # noqa: E402

_md = rx._md
_md.detail = True


def _wild_ranges(ins):
    """Byte ranges inside the instruction to wildcard: disp32 of rip-relative operands and rel32 of calls/jumps."""
    out = []
    b = ins.bytes
    if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
        if ins.op_str.startswith("0x") and ins.size >= 5 and b[0] in (0xE8, 0xE9) or (ins.size == 6 and b[0] == 0x0F):
            out.append((ins.size - 4, ins.size))
        return out
    disp_off = ins.disp_offset if hasattr(ins, "disp_offset") else 0
    disp_sz = ins.disp_size if hasattr(ins, "disp_size") else 0
    if "rip" in ins.op_str and disp_sz == 4:
        out.append((disp_off, disp_off + 4))
    return out


def make_sig(va, min_len=24, max_len=96):
    code = rx.read(va, max_len + 16)
    parts = []
    length = 0
    for ins in _md.disasm(code, va):
        wild = _wild_ranges(ins)
        for i, byte in enumerate(ins.bytes):
            if any(lo <= i < hi for lo, hi in wild):
                parts.append(None)
            else:
                parts.append(byte)
        length += ins.size
        if length >= min_len:
            sig = parts[:]
            n = count(sig)
            if n == 1:
                return fmt(sig), n
        if length >= max_len:
            break
    return fmt(parts), count(parts)


def fmt(parts):
    return " ".join("??" if p is None else "%02X" % p for p in parts)


def count(parts, limit=3):
    pat = b"".join(b"." if p is None else re.escape(bytes([p])) for p in parts)
    n = 0
    for _ in re.finditer(pat, rx._mm, re.DOTALL):
        n += 1
        if n >= limit:
            break
    return n


def find(sig_str):
    parts = [None if t == "??" else int(t, 16) for t in sig_str.split()]
    pat = b"".join(b"." if p is None else re.escape(bytes([p])) for p in parts)
    return [rx.BASE + m.start() for m in re.finditer(pat, rx._mm, re.DOTALL)]


if __name__ == "__main__":
    va = int(sys.argv[1], 16)
    ml = int(sys.argv[2]) if len(sys.argv) > 2 else 24
    s, n = make_sig(va, ml)
    print("%x  matches=%d  len=%d" % (va, n, len(s.split())))
    print(s)
