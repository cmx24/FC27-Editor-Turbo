"""sig_capture.py <name=va_hex>... : make an IDA-style byte signature for the start of each function (rip-relative disp32 and
call/jmp rel32 operands masked with ??), check it is unique in the image, and print JSON lines.
  bash scripts/re/py.sh scripts/re/sig_capture.py GetController=1470e291c > out.json
"""
import json
import re
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_capture as rx
import capstone

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def make_sig(va, min_len=24, max_len=64):
    """Bytes + mask: every rip-relative displacement and every rel32 branch target is wildcarded."""
    out = []  # list of (byte or None)
    for ins in md.disasm(rx.rd(va, max_len + 16), va, 0):
        b = bytearray(ins.bytes)
        mask = [True] * len(b)
        # rel32 branches
        if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
            if ins.op_str.startswith("0x") and len(b) >= 5:
                for i in range(len(b) - 4, len(b)):
                    mask[i] = False
        # rip-relative memory operands: disp32 ends `disp_offset+4`, there may be an immediate after it
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                off = ins.disp_offset
                for i in range(off, off + 4):
                    mask[i] = False
        out.extend((b[i] if mask[i] else None) for i in range(len(b)))
        if len(out) >= min_len:
            break
    return out


def count(sig):
    pat = b"".join(re.escape(bytes([x])) if x is not None else b"." for x in sig)
    rgx = re.compile(pat, re.DOTALL)
    n = 0
    for m in rgx.finditer(rx.mm):
        n += 1
        if n > 5:
            break
    return n


for arg in sys.argv[1:]:
    name, va = arg.split("=")
    va = int(va, 16)
    sig = make_sig(va)
    n = count(sig)
    tries = 0
    while n != 1 and tries < 3 and len(sig) < 96:
        sig = make_sig(va, min_len=len(sig) + 16)
        n = count(sig)
        tries += 1
    text = " ".join("??" if x is None else "%02X" % x for x in sig)
    print(json.dumps({"name": name, "va": "0x%X" % va, "rva": "0x%X" % (va - rx.BASE), "signature": text,
                      "unique_in_dump": n == 1, "matches": n}))
